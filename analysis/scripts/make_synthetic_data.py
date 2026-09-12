# -*- coding: utf-8 -*-
"""
make_synthetic_data.py
======================

qPocketPCR が書き出す DATA.TXT の合成データ生成器。

実際の装置がなくてもワークフロー全体(qpcr_core を通した処理・グラフ描画)を
検証できるように、現実的な増幅曲線モデルから synthetic DATA.TXT を作る。

モデル (シグモイド):

    F(n) = baseline + (Fmax - baseline) / (1 + exp(-k * (n - Ct)))

Ct は初期コピー数 N0 から:

    Ct = C0 - log2(N0) / log2(1 + E)

(E: PCR効率, 既定0.9 ≈ 効率が90%)

--with-melt を付けると、qPCR 増幅の後に融解 (HRM) ブロック (65→95°C 等の
温度ランプ + 各ウェルの融解シグモイド) を追記し、ファーム v1.2+ の形式
(Protocol name: 行 + Cycle, Time, Temp, Sensor1.. のヘッダ) で書き出す。

使い方::

    python make_synthetic_data.py                          # 旧形式: データのみ
    python make_synthetic_data.py --with-melt --out data/DATA_HRM.txt
"""

from __future__ import annotations

import argparse
import csv
import os
import random
import sys

import numpy as np

# Windows コンソール (cp932) でも UTF-8 で表示する
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


# ---------------------------------------------------------------------------
# qPCR 増幅曲線モデル (旧来)
# ---------------------------------------------------------------------------
def sigmoid_curve(n: np.ndarray, baseline: float, fmax: float,
                  ct: float, k: float) -> np.ndarray:
    """シグモイド増幅曲線。"""
    return baseline + (fmax - baseline) / (1.0 + np.exp(-k * (n - ct)))


def ct_from_copies(n0: float, c0: float, eff: float) -> float:
    """初期コピー数から Ct を計算。eff は効率(例 1.9 = 90%)。"""
    growth = np.log2(1.0 + eff)
    if n0 <= 0:
        return float("inf")
    return c0 - np.log2(n0) / growth


def make_run(seed: int = 0,
             efficiency: float = 0.9,   # PCR効率 (0.9 = 90%)
             c0: float = 38.0,           # 1コピーのCt換算定数(概算)
             baseline: float = 600.0,    # ベースライン蛍光(RFU)
             fmax: float = 250000.0,     # プレートau蛍光
             k: float = 0.7,             # 増幅勾配(急峻さ)
             n_cycles: int = 45,
             noise: float = 15.0,        # ベースラインノイズSD
             layout: dict | None = None) -> tuple[list[int], list[str], list[tuple]]:
    """DATA.TXT の qPCR部の行列表を生成。

    Returns
    -------
    (cycles, channel_names, rows)
        rows : [(cycles, col, name), ...]
    """
    if layout is None:
        # デフォルト配置: ch0-5 が標準希釈系列、ch6/7 が未知サンプル2本
        layout = {
            "standard": [1_000_000.0, 100_000.0, 10_000.0, 1_000.0, 100.0, 10.0],
            "sample_a": 500.0,
            "sample_b": 2500.0,
        }

    np_rng = np.random.default_rng(seed)

    names = [f"ch{i}" for i in range(8)]
    rows = []
    cycles = list(range(1, n_cycles + 1))

    # --- 標準希釈系列 (固定ウェル ch0-5) ---
    std_values = layout["standard"]
    for wi, n0 in enumerate(std_values):
        ct = ct_from_copies(n0, c0, efficiency)
        col = []
        for n in cycles:
            if n0 <= 0:
                val = baseline + np_rng.normal(0, noise)
            else:
                f = sigmoid_curve(np.array([n]), baseline, fmax, ct, k)[0]
                val = f + np_rng.normal(0, noise * (1 + n / n_cycles))
            col.append(max(0.0, val))
        rows.append((cycles, col, names[wi]))

    # --- 未知サンプルA (ch6) と B (ch7) ---
    for wi, key in enumerate(["sample_a", "sample_b"]):
        n0 = layout.get(key, 0.0)
        ct = ct_from_copies(n0, c0, efficiency)
        col = [max(0.0, sigmoid_curve(np.array([n]), baseline, fmax, ct, k)[0]
                + np_rng.normal(0, noise * (1 + n / n_cycles))) for n in cycles]
        rows.append((cycles, col, names[6 + wi]))

    return cycles, names, rows


# ---------------------------------------------------------------------------
# 融解 (HRM) ブロック
# ---------------------------------------------------------------------------
def make_melt_temps(melt_from: float = 65.0, melt_to: float = 95.0,
                    melt_inc: float = 0.2) -> np.ndarray:
    """融解温度ランプ (65.0, 65.2, ..., 95.0)。整数安全な点数の生成。"""
    n = int(round((melt_to - melt_from) / melt_inc)) + 1
    if n < 2:
        raise ValueError("融解範囲が小さすぎます")
    return melt_from + np.arange(n) * melt_inc


def melt_transition(temps: np.ndarray, tm: float, f_hi: float, f_lo: float,
                    sharp: float = 0.5) -> np.ndarray:
    """融解シグモイド (低温で高蛍光 → Tm 付近で急減 → 高温で低蛍光)。

    F(T) = f_lo + (f_hi - f_lo) / (1 + exp(sharp * (T - Tm)))
    """
    return f_lo + (f_hi - f_lo) / (1.0 + np.exp(sharp * (temps - tm)))


def make_melt_block(names: list[str], temps: np.ndarray,
                    seed: int = 0, tm_base: float = 82.0,
                    f_hi: float = 40000.0, f_lo: float = 1200.0,
                    sharp: float = 0.5, noise: float = 40.0) -> dict:
    """各ウェルの融解蛍光プロファイルを生成。{channel_name: np.ndarray}"""
    np_rng = np.random.default_rng(seed + 1000)
    out = {}
    for i, name in enumerate(names):
        tm = tm_base + 0.5 * i          # ウェルごとに Tm を 0.5°C ずらす
        f = melt_transition(temps, tm, f_hi, f_lo, sharp)
        f = f + np_rng.normal(0, noise, size=len(temps))
        out[name] = np.maximum(0.0, f)
    return out


# ---------------------------------------------------------------------------
# 書き出し
# ---------------------------------------------------------------------------
def write_data_txt(path: str, cycles: list[int], names: list[str],
                   rows: list[tuple]) -> None:
    """DATA.TXT (旧形式CSV) に書き込む。ヘッダ付き::

        cycle, seconds, ch0, ch1, ..., ch7
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["cycle", "seconds"] + names)
        for i, cyc in enumerate(cycles):
            sec = int(i * 8)  # 概算: 1サイクル約8秒
            line = [cyc, sec]
            for _, col, _ in rows:
                line.append(f"{col[i]:.2f}")
            w.writerow(line)


def write_device_data_txt(path: str, cycles: list[int], names: list[str],
                          rows: list[tuple],
                          qpcr_temps: list[float],
                          melt_temps: np.ndarray | None = None,
                          melt_rows: dict | None = None,
                          melt_hold_sec: int = 3) -> None:
    """ファーム v1.2+ 形式 (温度列付き) で書き出す。

    形式::

        Protocol name: synthetic
        Cycle, Time, Temp, Sensor1, ..., Sensor8

    - qPCR部: 各行に実測ブロック温度 (qpcr_temps[i])
    - 融解部 (melt_temps/melt_rows 指定時): 続けて Cycle=1 の温度ランプ行
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sensor_names = [f"Sensor{i + 1}" for i in range(len(names))]

    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Protocol name: synthetic"])
        w.writerow(["Cycle", "Time", "Temp"] + sensor_names)

        # qPCR部
        for i, cyc in enumerate(cycles):
            sec = int(i * 8)
            line = [cyc, sec, f"{qpcr_temps[i]:.1f}"]
            for _, col, _ in rows:
                line.append(f"{col[i]:.2f}")
            w.writerow(line)

        # 融解部
        if melt_temps is not None and melt_rows is not None:
            sec = int(len(cycles) * 8)
            for j, t in enumerate(melt_temps):
                line = [1, sec + j * melt_hold_sec, f"{t:.1f}"]
                for name in names:
                    line.append(f"{melt_rows[name][j]:.2f}")
                w.writerow(line)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main() -> None:
    ap = argparse.ArgumentParser(description="qPocketPCR synthetic DATA.TXT generator")
    ap.add_argument("--out", default=os.path.join("data", "DATA.TXT"),
                    help="出力先 (既定: data/DATA.TXT)")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--efficiency", type=float, default=0.9,
                    help="PCR効率 0<e<=2 (既定0.9 = 90%)")
    ap.add_argument("--n-cycles", type=int, default=45)
    # 融解 (HRM) ブロック
    ap.add_argument("--with-melt", action="store_true",
                    help="増幅の後に融解ブロックを追記 (デバイス形式で出力)")
    ap.add_argument("--melt-from", type=float, default=65.0)
    ap.add_argument("--melt-to", type=float, default=95.0)
    ap.add_argument("--melt-inc", type=float, default=0.2)
    ap.add_argument("--melt-hold-sec", type=int, default=3,
                    help="融解1点あたりの秒数 (Time列の間隔)")
    ap.add_argument("--melt-tm-base", type=float, default=82.0,
                    help="ch0 の Tm (°C)。以降 ch ごとに +0.5°C ずつ")
    args = ap.parse_args()

    cycles, names, rows = make_run(seed=args.seed, efficiency=args.efficiency,
                                   n_cycles=args.n_cycles)

    if not args.with_melt:
        write_data_txt(args.out, cycles, names, rows)
        print(f"書き込みました: {args.out}  (qPCRのみ・旧形式)")
        print("チャネル:", ", ".join(names))
        return

    # 融解ブロック付きデバイス形式
    temps = make_melt_temps(args.melt_from, args.melt_to, args.melt_inc)
    melt = make_melt_block(names, temps, seed=args.seed,
                           tm_base=args.melt_tm_base)

    # qPCR部の「実測ブロック温度」: キャプチャごとに 95/60/72°C を行き来
    qpcr_temps = [float([95.0, 60.0, 72.0][i % 3]) for i in range(len(cycles))]

    write_device_data_txt(args.out, cycles, names, rows,
                          qpcr_temps=qpcr_temps,
                          melt_temps=temps, melt_rows=melt,
                          melt_hold_sec=args.melt_hold_sec)

    n_melt = len(temps)
    print(f"書き込みました: {args.out}  (デバイス形式 / qPCR {len(cycles)}行 + 融解 {n_melt}行)")
    print(f"融解: {args.melt_from:.1f}→{args.melt_to:.1f}°C / {args.melt_inc}°C刻み")
    print(f"ウェルTm (設定値): " +
          ", ".join(f"{name}={args.melt_tm_base + 0.5 * i:.1f}"
                    for i, name in enumerate(names)))


if __name__ == "__main__":
    main()
