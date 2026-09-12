# -*- coding: utf-8 -*-
"""
run_qpcr.py
===========

qPocketPCR による環境DNA定量 qPCR の一連のワークフロー。

装置から取り出した DATA.TXT を入力とし、以下の処理を経て未知サンプルの
標本量(コピー数相当)を定量する。

    1. DATA.TXT のパース (qPCR部 / 融解部の自動分離)
    2. 役割マッピング (standard / sample / ntc) の適用
    3. ベースライン減算・正規化 (ΔRn)
    4. 各ウェルの Ct/Cq 検出 (微分最大法、自動フォールバック)
    5. 標準(standard)アンプロンによるスタンダードカーブの回帰とPCR効率計算
    6. 未知サンプルをスタンダードカーブに当てはめて標本量を推定
    7. 融解(HRM)セグメントがある場合: 融解曲線と Tm の検出
    8. 結果表 (CSV/TSV) とグラフの出力

DATA.TXT は qPocketPCR ファーム v1.2+ の形式に対応:

    Protocol name: <名前>          (先頭行、任意)
    Cycle, Time, Temp, Sensor1, ..., Sensor8

    - qPCR部: Cycle が 1..N と進行する行 (1サイクル複数キャプチャ可)
    - 融解部: PCRサイクル完了後、Cycle=1 のまま実測温度が 65→95°C 等へ
      単調上昇する末尾ブロック (自動分離して Tm を計算)

    旧形式 (cycle, seconds, ch0..ch7 / 温度列なし) もそのまま読める。

役割マッピングは config.json で指定する。例::

    {
      "channel_names": ["ch0","ch1","ch2","ch3","ch4","ch5","ch6","ch7"],
      "roles": {
        "standard": {"ch0": 1000000, "ch1": 100000, "ch2": 10000,
                     "ch3": 1000, "ch4": 100, "ch5": 10},
        "sample":   {"ch6": "unknown_a", "ch7": "unknown_b"},
        "ntc":      []
      },
      "frac_baseline": 0.5
    }

使い方::

    python run_qpcr.py --data data/DATA.TXT --config config.json \
                       --out-dir output/
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qpcr_core as qc

# Windows コンソール (cp932) でも '·' 等を安全に表示する
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


# ---------------------------------------------------------------------------
# 設定読み込み
# ---------------------------------------------------------------------------
def load_config(path: str) -> dict:
    """config.json を読み込む。"""
    with open(path, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    # channel_names がなければ ch0..ch7 を生成
    names = cfg.get("channel_names") or [f"ch{i}" for i in range(8)]
    roles = cfg.get("roles", {})

    def expand(spec):
        """辞書 or リスト を一律の {name: value} 形式に。"""
        if isinstance(spec, dict):
            return spec
        if isinstance(spec, list):
            return {c: c for c in spec}
        raise ValueError(f"役割指定は辞書またはリストです: {spec}")

    standard = expand(roles.get("standard", {}))
    sample = expand(roles.get("sample", {}))
    ntc = roles.get("ntc", []) or []

    return {
        "channel_names": names,
        "standard": standard,
        "sample": sample,
        "ntc": list(ntc),
        "frac_baseline": float(cfg.get("frac_baseline", 0.5)),
    }


def role_labels(config: dict) -> dict:
    """チャネル名 → 表示ラベル (サンプル名/標準量/NTC) の辞書。"""
    labels = {}
    for ch, qty in config["standard"].items():
        labels[ch] = f"std {qty:g}"
    for ch, label in config["sample"].items():
        labels[ch] = label
    for ch in config["ntc"]:
        labels.setdefault(ch, f"{ch} (ntc)")
    return labels


# ---------------------------------------------------------------------------
# 解析
# ---------------------------------------------------------------------------
def analyze(data_path: str, config: dict) -> dict:
    """DATA.TXT を解析して結果辞書を返す。

    ファイルを qPCR部と融解部に分離し、qPCR部は従来どおり Ct/定量を、
    融解部は Tm を解析する。融解のみのファイルでは qPCR 解析は行わない。
    """
    run = qc.load_run_data(data_path, channel_names=config["channel_names"])
    curves = run.curves
    frac = config["frac_baseline"]
    labels = role_labels(config)

    mode = "qpcr"
    if not curves and run.melt is not None:
        mode = "melt"
    elif not curves:
        raise ValueError("qPCR部も融解部も見つかりません (データを確認してください)")

    results = {
        "run": run,
        "mode": mode,
        "standard": [],   # StandardPoint 用
        "samples": [],    # (label, ct, copies)
        "ntc": [],        # (name, ct_or_none)
        "all_curves": {},
        "melt_labels": labels,
    }

    # --- qPCR部: 標準アンプロン / NTC / 未知サンプルの Ct を記録 ---
    if mode == "qpcr":
        for ch, qty in config["standard"].items():
            if ch not in curves:
                print(f"  [警告] 標準チャネル {ch} がデータにありません")
                continue
            curve = curves[ch]
            r = qc.detect_ct(curve, method="auto", frac_baseline=frac)
            results["all_curves"][ch] = (curve, r)
            if r.ct is not None:
                results["standard"].append(
                    qc.StandardPoint(name=ch, quantity=float(qty),
                                     logq=np.log10(float(qty)), ct=r.ct))

        for ch in config["ntc"]:
            if ch not in curves:
                continue
            curve = curves[ch]
            r = qc.detect_ct(curve, method="auto", frac_baseline=frac)
            results["all_curves"][ch] = (curve, r)
            results["ntc"].append((ch, r.ct))

        for ch, label in config["sample"].items():
            if ch not in curves:
                print(f"  [警告] サンプルチャネル {ch} がデータにありません")
                continue
            curve = curves[ch]
            r = qc.detect_ct(curve, method="auto", frac_baseline=frac)
            results["all_curves"][ch] = (curve, r)
            if r.ct is not None:
                copies = float("nan")  # スタンダードカーブが成立してから埋める
                results["samples"].append((label, r.ct, ch, copies))

    # --- 融解部: Tm 検出 ---
    results["melt_results"] = {}
    if run.melt is not None:
        results["melt_results"] = qc.analyze_melt(run.melt)

    return results


def finalize(curves_results: dict, standard_points: list) -> tuple[dict, qc.StandardCurve]:
    """スタンダードカーブを回帰し、サンプルの標本量を推定する。"""
    if len(standard_points) < 2:
        raise ValueError("スタンダードが2点未満です (標準アンプロンを確認)")

    sc = qc.fit_standard_curve(standard_points)

    quantified = {}
    for label, ct, ch, _ in curves_results["samples"]:
        if ct is not None and np.isfinite(ct):
            copies = sc.quantity_from_ct(ct)
        else:
            copies = float("nan")
        quantified[ch] = (label, ct, copies)

    return quantified, sc


# ---------------------------------------------------------------------------
# 較定ファイルの読み込み (外部較定の再利用)
# ---------------------------------------------------------------------------
def load_calibration(path: str) -> tuple[qc.StandardCurve, dict]:
    """JSON 較定ファイルを読み込み、StandardCurve とメタデータを返す。

    make_calibration.py が生成したファイルを想定する。回帰式・効率・R² は
    そのまま使い、その回の標準を取り直す必要がない。
    """
    with open(path, "r", encoding="utf-8") as f:
        meta = json.load(f)

    sc = qc.StandardCurve(
        slope=float(meta["slope"]),
        intercept=float(meta["intercept"]),
        r2=float(meta.get("r2", float("nan"))),
        efficiency=float(meta.get("efficiency", float("nan"))),
    )
    return sc, meta


def quantify_samples(curves_results: dict, sc: qc.StandardCurve) -> dict:
    """外部較定 (StandardCurve) を用いて未知サンプルの標本量を推定する。"""
    quantified = {}
    for label, ct, ch, _ in curves_results["samples"]:
        if ct is not None and np.isfinite(ct):
            copies = sc.quantity_from_ct(ct)
        else:
            copies = float("nan")
        quantified[ch] = (label, ct, copies)
    return quantified


# ---------------------------------------------------------------------------
# 出力
# ---------------------------------------------------------------------------
def write_results(path: str, quantified: dict, standard_points: list,
                  sc: qc.StandardCurve, ntc: list, curves_results: dict,
                  calibration_meta: dict = None,
                  melt_results: dict = None, melt_labels: dict = None,
                  run_mode: str = "qpcr") -> None:
    """結果を TSV に書き込む。

    qPCR部の結果に加え、融解(HRM)があれば Tm のセクションを追記する。
    融解のみのラン (run_mode='melt') では Tm セクションのみを書く。
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    labels = melt_labels or {}

    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")

        if run_mode == "melt":
            w.writerow(["# qPocketPCR melt (HRM) results"])
            w.writerow(["run_mode", "melt-only"])
        else:
            w.writerow(["# qPocketPCR eDNA quantification results"])
            if calibration_meta is not None:
                w.writerow(["calibration_target", str(calibration_meta.get("target", ""))])
                w.writerow(["calibration_created", str(calibration_meta.get("created", ""))])
                w.writerow(["standard_curve_slope", f"{sc.slope:.5f}"])
                w.writerow(["standard_curve_intercept", f"{sc.intercept:.5f}"])
                w.writerow(["standard_curve_r2", f"{sc.r2:.5f}"])
                w.writerow(["pcr_efficiency_pct", f"{sc.efficiency * 100:.1f}%"])
            else:
                w.writerow(["standard_curve_slope", f"{sc.slope:.5f}"])
                w.writerow(["standard_curve_intercept", f"{sc.intercept:.5f}"])
                w.writerow(["standard_curve_r2", f"{sc.r2:.5f}"])
                w.writerow(["pcr_efficiency_pct", f"{sc.efficiency * 100:.1f}%"])
            w.writerow([])
            w.writerow(["role", "channel", "label_or_quantity", "ct", "copies_per_reaction"])

            for p in standard_points:
                w.writerow(["standard", p.name, f"{p.quantity:g}", f"{p.ct:.3f}", ""])
            for ch, (label, ct, copies) in quantified.items():
                w.writerow(["sample", ch, label,
                            f"{ct:.3f}" if ct is not None else "",
                            f"{copies:.4g}" if np.isfinite(copies) else ""])
            for ch, ct in ntc:
                w.writerow(["ntc", ch, ch,
                            f"{ct:.3f}" if ct is not None else "", ""])

        # --- 融解 (HRM) のセクション ---
        if melt_results:
            w.writerow([])
            w.writerow(["# melt (HRM)"])
            w.writerow(["role", "channel", "label", "tm_degc", "method"])
            for ch, mr in melt_results.items():
                tm_s = f"{mr.tm:.2f}" if mr.tm is not None else ""
                w.writerow(["melt", ch, labels.get(ch, ch), tm_s, mr.method])


def write_report_md(path: str, quantified: dict, sc: qc.StandardCurve,
                    standard_points: list, ntc: list,
                    calibration_meta: dict = None,
                    melt_results: dict = None, melt_labels: dict = None,
                    run_mode: str = "qpcr") -> None:
    """簡易レポート (Markdown) を書き込む。"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    labels = melt_labels or {}

    with open(path, "w", encoding="utf-8") as f:
        if run_mode == "melt":
            f.write("# qPocketPCR 融解 (HRM) レポート\n\n")
            f.write("このファイルは融解のみのランです (qPCR/Ct 解析なし)。\n\n")
        else:
            f.write("# qPocketPCR eDNA 定量レポート\n\n")
            if calibration_meta is not None:
                f.write(f"- 標的: {calibration_meta.get('target', '')}\n")
                f.write(f"- 較定日: {calibration_meta.get('created', '')}\n")
            f.write(f"- PCR効率: {sc.efficiency * 100:.1f}%\n")
            f.write(f"- 回帰式: Ct = {sc.slope:.4f} · log10(q) + {sc.intercept:.4f}\n")
            f.write(f"- R²: {sc.r2:.4f}\n\n")
            f.write("## 未知サンプルの標本量\n\n")
            f.write("| サンプル | Ct | 標本量 (copies/reaction) |\n")
            f.write("|---|---|---|\n")
            for ch, (label, ct, copies) in quantified.items():
                val = f"{copies:.4g}" if np.isfinite(copies) else "未検出"
                f.write(f"| {label} | {ct:.2f} | {val} |\n" if ct is not None
                        else f"| {label} | - | 未検出 |\n")

        if melt_results:
            f.write("\n## 融解 (HRM) の Tm\n\n")
            f.write("| ウェル | ラベル | Tm (°C) | 方法 |\n")
            f.write("|---|---|---|---|\n")
            for ch, mr in melt_results.items():
                tm_s = f"{mr.tm:.2f}" if mr.tm is not None else "- (遷移なし)"
                f.write(f"| {ch} | {labels.get(ch, ch)} | {tm_s} | {mr.method} |\n")


# ---------------------------------------------------------------------------
# R / ggplot2 向け構造化データ出力
# ---------------------------------------------------------------------------
def write_r_data(out_dir: str, curves_results: dict, standard_points: list,
                 quantified: dict, ntc: list, sc: qc.StandardCurve,
                 calibration_meta: dict = None,
                 melt_results: dict = None, melt_labels: dict = None,
                 run_mode: str = "qpcr") -> tuple:
    """R (ggplot2) で作図しやすいよう構造化 CSV を書き込む。

    返り値: (amplification_csv, standard_curve_csv, calibration_csv,
             samples_csv, melt_curves_csv)
    """
    os.makedirs(out_dir, exist_ok=True)
    labels = melt_labels or {}
    frac = 0.5

    # --- 1. 増幅曲線 (long format) ---
    amp_path = "(未生成)"
    if run_mode == "qpcr":
        amp_path = os.path.join(out_dir, "amplification.csv")
        with open(amp_path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["cycle", "channel", "role", "raw", "normalized_rn"])
            for ch, (curve, r) in curves_results["all_curves"].items():
                rn = qc.normalized_rn(curve, frac_baseline=frac)
                role = "standard" if ch in {p.name for p in standard_points} else "sample"
                for i in range(curve.n):
                    w.writerow([int(curve.cycles[i]), ch, role,
                                f"{curve.raw[i]:.6g}", f"{rn[i]:.6g}"])

    # --- 2a. 標準曲線 (その回で回帰した場合のみ) ---
    std_path = "(未生成)"
    if run_mode == "qpcr" and calibration_meta is None and standard_points:
        std_path = os.path.join(out_dir, "standard_curve.csv")
        with open(std_path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["channel", "log10_quantity", "ct"])
            for p in standard_points:
                w.writerow([p.name, f"{p.logq:.6g}", f"{p.ct:.6g}"])

    # --- 2b. 外部較定時はメタデータを出力 ---
    calib_path = "(未生成)"
    if calibration_meta is not None:
        calib_path = os.path.join(out_dir, "calibration_meta.csv")
        with open(calib_path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["key", "value"])
            for k in ["target", "created", "method"]:
                if k in calibration_meta:
                    w.writerow([k, str(calibration_meta[k])])
            w.writerow(["slope", f"{sc.slope:.6g}"])
            w.writerow(["intercept", f"{sc.intercept:.6g}"])
            w.writerow(["r2", f"{sc.r2:.6g}"])
            w.writerow(["efficiency_pct", f"{sc.efficiency * 100:.4f}%"])

    # --- 3. 未知サンプルの標本量 ---
    samples_path = "(未生成)"
    if run_mode == "qpcr":
        samples_path = os.path.join(out_dir, "samples.csv")
        with open(samples_path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["label", "channel", "ct", "copies"])
            for ch, (label, ct, copies) in quantified.items():
                disp_label = label if label else ch
                w.writerow([disp_label, ch,
                            f"{ct:.6g}" if ct is not None else "",
                            f"{copies:.6g}" if np.isfinite(copies) else ""])

    # --- 4. 融解曲線 (long format: temp, channel, label, f, neg_dfdt) ---
    melt_curves_path = "(未生成)"
    run = curves_results["run"]
    if run.melt is not None:
        melt_curves_path = os.path.join(out_dir, "melt_curves.csv")
        with open(melt_curves_path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["temp", "channel", "label", "f", "neg_dfdt"])
            temps = np.asarray(run.melt.temps, dtype=float)
            for ch, fv in run.melt.f.items():
                fv = np.asarray(fv, dtype=float)
                nd = qc.melt_neg_derivative(run.melt, ch)
                for i in range(len(temps)):
                    w.writerow([f"{temps[i]:.4f}", ch, labels.get(ch, ch),
                                f"{fv[i]:.6g}", f"{nd[i]:.6g}"])

    return amp_path, std_path, calib_path, samples_path, melt_curves_path


# ---------------------------------------------------------------------------
# 描画
# ---------------------------------------------------------------------------
def make_plots(out_dir: str, curves_results: dict, standard_points: list,
               sc: qc.StandardCurve, calibration_meta: dict = None) -> tuple:
    """増幅曲線とスタンダードカーブの2枚のグラフを描画 (qPCR部)。

    外部較定使用時 (calibration_meta が None でない) は標準点が存在しないため、
    増幅曲線の1枚のみを描画する。
    """
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    # 日本語フォント設定 (Noto Sans CJK JP があれば使用)
    try:
        from matplotlib import font_manager as fm
        fp = fm.FontProperties(family='Noto Sans CJK JP')
        if fm.findfont(fp, fallback_to_default=False):
            matplotlib.rcParams['font.family'] = 'Noto Sans CJK JP'
    except Exception:
        pass

    os.makedirs(out_dir, exist_ok=True)

    # --- 1. 増幅曲線 (正規化 ΔRn) ---
    fig, ax = plt.subplots(figsize=(8, 5))
    colors_std = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b",
                  "#17becf", "#bcbd22", "#e377c2", "#7f7f7f"]
    for ch, (curve, r) in curves_results["all_curves"].items():
        rn = qc.normalized_rn(curve, frac_baseline=0.5)
        role = "sample"
        if ch in {p.name for p in standard_points}:
            role = "standard"
        idx = next((i for i, p in enumerate(standard_points) if p.name == ch), None)
        color = colors_std[idx % len(colors_std)] if idx is not None else "#555555"
        ax.plot(curve.cycles, rn, lw=1.6, label=f"{ch} ({role})", color=color)

    ax.axhline(0.15, ls="--", lw=0.8, color="gray", label="threshold 0.15")
    ax.set_xlabel("PCR cycle")
    ax.set_ylabel("Normalized fluorescence (ΔRn)")
    if calibration_meta is not None:
        ax.set_title(f"qPocketPCR amplification curves ({calibration_meta.get('target','')})")
    else:
        ax.set_title("qPocketPCR amplification curves")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    p1 = os.path.join(out_dir, "amplification_curves.png")
    fig.savefig(p1, dpi=150)
    plt.close(fig)

    # --- 2. スタンダードカーブ (外部較定時は省略) ---
    if calibration_meta is not None:
        return p1, "(外部較定使用のため標準曲線図は省略)"

    fig, ax = plt.subplots(figsize=(6, 5))
    x = [p.logq for p in standard_points]
    y = [p.ct for p in standard_points]
    ax.scatter(x, y, s=60, color="#1f77b4", zorder=5)
    xs = np.linspace(min(x), max(x), 100)
    ax.plot(xs, sc.slope * xs + sc.intercept, "--", color="#d62728", lw=1.5,
            label=f"y={sc.slope:.3f}x+{sc.intercept:.3f}\nR²={sc.r2:.3f}, E={sc.efficiency*100:.0f}%")
    ax.set_xlabel("log10(quantity)")
    ax.set_ylabel("Ct")
    ax.set_title("Standard curve")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    p2 = os.path.join(out_dir, "standard_curve.png")
    fig.savefig(p2, dpi=150)
    plt.close(fig)

    return p1, p2


def make_melt_plot_from_run(out_dir: str, melt, melt_results: dict,
                            melt_labels: dict = None) -> str:
    """融解曲線 (F vs T と -dF/dT vs T) を描画し、Tm をマークする。"""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    try:
        from matplotlib import font_manager as fm
        fp = fm.FontProperties(family='Noto Sans CJK JP')
        if fm.findfont(fp, fallback_to_default=False):
            matplotlib.rcParams['font.family'] = 'Noto Sans CJK JP'
    except Exception:
        pass

    os.makedirs(out_dir, exist_ok=True)
    labels = melt_labels or {}
    temps = np.asarray(melt.temps, dtype=float)

    colors = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b",
              "#17becf", "#bcbd22", "#e377c2", "#7f7f7f"]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))

    for i, (ch, fv) in enumerate(melt.f.items()):
        fv = np.asarray(fv, dtype=float)
        color = colors[i % len(colors)]
        nd = qc.melt_neg_derivative(melt, ch)
        lab = labels.get(ch, ch)

        ax1.plot(temps, fv, lw=1.4, label=f"{ch} ({lab})", color=color)
        ax2.plot(temps, nd, lw=1.4, color=color)

        mr = melt_results.get(ch)
        if mr is not None and mr.tm is not None:
            ax2.axvline(mr.tm, ls=":", lw=1.0, color=color, alpha=0.8)
            ax1.axvline(mr.tm, ls=":", lw=1.0, color=color, alpha=0.8)

    ax1.set_xlabel("Temperature (°C)")
    ax1.set_ylabel("Fluorescence (RFU)")
    ax1.set_title("Melt curves (F vs T)")
    ax1.legend(fontsize=7)
    ax1.grid(True, alpha=0.3)

    ax2.set_xlabel("Temperature (°C)")
    ax2.set_ylabel("-dF/dT")
    ax2.set_title("Melt curves (-dF/dT, peak = Tm)")
    ax2.grid(True, alpha=0.3)

    fig.tight_layout()
    p = os.path.join(out_dir, "melt_curves.png")
    fig.savefig(p, dpi=150)
    plt.close(fig)
    return p


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main() -> None:
    ap = argparse.ArgumentParser(description="qPocketPCR eDNA qPCR / HRM workflow")
    ap.add_argument("--data", default="data/DATA.TXT", help="DATA.TXT のパス")
    ap.add_argument("--config", required=True, help="config.json のパス")
    ap.add_argument("--out-dir", default="output/", help="出力ディレクトリ")
    ap.add_argument("--calibration", default=None,
                    help="外部較定ファイル (JSON)。指定すると標準を回帰せずこれを使う")
    ap.add_argument("--no-plots", action="store_true",
                    help="グラフ描画をスキップ (matplotlib 不要)")
    args = ap.parse_args()

    if not os.path.exists(args.data):
        print(f"[エラー] データが見つかりません: {args.data}")
        sys.exit(1)

    cfg = load_config(args.config)
    print("設定を読み込みました:")
    print(f"  サンプル:       {cfg['sample']}")
    print(f"  NTC:            {cfg['ntc']}")

    res = analyze(args.data, cfg)
    run = res["run"]
    mode = res["mode"]
    melt_results = res["melt_results"] or {}
    melt_labels = res["melt_labels"]

    if mode == "melt":
        print("\n[情報] 融解 (HRM) のみのランです (qPCR/Ct 解析はスキップ)")
        quantified = {}
        sc = None
        calibration_meta = None
    elif args.calibration:
        sc, meta = load_calibration(args.calibration)
        print(f"\n[情報] 外部較定を使用: {args.calibration}")
        print(f"  標的   : {meta.get('target', '')}")
        print(f"  効率 E : {sc.efficiency * 100:.1f}%")
        print(f"  R²     : {sc.r2:.5f}")
        quantified = quantify_samples(res, sc)
        calibration_meta = meta
    else:
        quantified, sc = finalize(res, res["standard"])
        calibration_meta = None

    # 結果書き込み
    os.makedirs(args.out_dir, exist_ok=True)
    tsv_path = os.path.join(args.out_dir, "results.tsv")
    write_results(tsv_path, quantified, res["standard"], sc, res["ntc"], res,
                  calibration_meta=calibration_meta,
                  melt_results=melt_results, melt_labels=melt_labels,
                  run_mode=mode)
    md_path = os.path.join(args.out_dir, "report.md")
    write_report_md(md_path, quantified, sc, res["standard"], res["ntc"],
                    calibration_meta=calibration_meta,
                    melt_results=melt_results, melt_labels=melt_labels,
                    run_mode=mode)

    # R / ggplot2 向け構造化データ出力
    amp_csv, std_csv, calib_path, samples_csv, melt_curves_csv = write_r_data(
        args.out_dir, res, res["standard"], quantified, res["ntc"], sc,
        calibration_meta=calibration_meta,
        melt_results=melt_results, melt_labels=melt_labels, run_mode=mode)

    # グラフ
    p1 = p2 = melt_png = "(未生成)"
    if not args.no_plots:
        try:
            if mode == "qpcr":
                p1, p2 = make_plots(args.out_dir, res, res["standard"], sc,
                                    calibration_meta)
            if run.melt is not None:
                melt_png = make_melt_plot_from_run(args.out_dir, run.melt,
                                                   melt_results, melt_labels)
        except Exception as e:
            print(f"[警告] グラフ描画に失敗しました: {e}")
            if p1 == "(未生成)":
                p1 = p2 = "(未生成)"

    # 結果表示
    print("\n" + "=" * 60)
    if mode == "qpcr":
        if calibration_meta is not None:
            print("外部較定による標本量推定")
        else:
            print("スタンダードカーブ")
        print("=" * 60)
        print(f"  PCR効率 E      : {sc.efficiency * 100:.1f}%")
        print(f"  回帰式         : Ct = {sc.slope:.4f}·log10(q) + {sc.intercept:.4f}")
        print(f"  R²             : {sc.r2:.5f}")
        if calibration_meta is not None:
            print(f"  較定標的       : {calibration_meta.get('target', '')}")
            print(f"  較定生成日     : {calibration_meta.get('created', '')}")
        print()
        print("未知サンプルの標本量:")
        for ch, (label, ct, copies) in quantified.items():
            disp = label if label else f"{ch}"
            if ct is not None and np.isfinite(ct):
                print(f"  {disp:<16} Ct={ct:6.2f}   ->   {copies:.4g} copies/reaction")
            else:
                print(f"  {disp:<16} 未検出")

        if res["ntc"]:
            detected = [(ch, ct) for ch, ct in res["ntc"] if ct is not None]
            if detected:
                print(f"\n[注意] NTCで検出: {detected}")

    if run.melt is not None:
        print("\n融解 (HRM) の Tm:")
        for ch, mr in melt_results.items():
            lab = melt_labels.get(ch, ch)
            if mr.tm is not None:
                print(f"  {ch} ({lab}): Tm = {mr.tm:.2f} °C")
            else:
                print(f"  {ch} ({lab}): 遷移なし")

    print("\n出力:")
    print(f"  {tsv_path}")
    print(f"  {md_path}")
    if mode == "qpcr":
        print(f"  {p1}")
        print(f"  {p2}")
    if melt_png != "(未生成)":
        print(f"  {melt_png}")
    print("  R/ggplot2 用データ (CSV):")
    if amp_csv != "(未生成)":
        print(f"    増幅曲線: {amp_csv}")
    if std_csv != "(未生成)":
        print(f"    標準曲線: {std_csv}")
    if calib_path != "(未生成)":
        print(f"    較定メタ: {calib_path}")
    if samples_csv != "(未生成)":
        print(f"    標本量:   {samples_csv}")
    if melt_curves_csv != "(未生成)":
        print(f"    融解曲線: {melt_curves_csv}")
    print(f"  qPCR行: {run.qpcr_rows}, 融解行: {run.melt_rows}")


if __name__ == "__main__":
    main()
