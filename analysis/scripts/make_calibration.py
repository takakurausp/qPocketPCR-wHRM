# -*- coding: utf-8 -*-
"""
make_calibration.py
====================

希釈系列 (dilution series) の DATA.TXT から標準曲線を回帰し、較定パラメータ
(slope / intercept / R² / PCR効率) を JSON ファイルとして書き出す。

生成した較定ファイルは run_qpcr.py の --calibration オプションで再利用でき、
次回以降の未知サンプル解析で毎回標準を取り直す必要がなくなる。

使い方::

    # 1. 希釈系列を含む DATA.TXT とその config (standard ロール) で実行
    python make_calibration.py --data data/DATA.TXT \\
        --config config_std.json \\
        --target "オイカワcytb" \\
        --out calibration_oikawa_cytb.json

生成される JSON の形式::

    {
      "target": "オイカワcytb",
      "created": "2026-09-04",
      "method": "threshold",
      "slope": -3.441,
      "intercept": 11.78,
      "r2": 0.982,
      "efficiency": 0.953,
      "n_points": 8,
      "dilutions": {"ch0": 0.0001, "ch6": 1.0, ...},
      "ct_values": {"ch0": 24.9, "ch6": 11.6, ...},
      "notes": ""
    }
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys
from datetime import date

import numpy as np

# Windows コンソール (cp932) でも '·' 等を安全に表示する
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qpcr_core as qc


# ---------------------------------------------------------------------------
# 設定読み込み (make_calibration 専用: standard のみ)
# ---------------------------------------------------------------------------
def load_config(path: str) -> dict:
    """config.json を読み込み、standard ロールのみ抽出する。"""
    with open(path, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    names = cfg.get("channel_names") or [f"ch{i}" for i in range(8)]
    roles = cfg.get("roles", {})

    standard_spec = roles.get("standard", {})
    if isinstance(standard_spec, dict):
        standard = {c: float(v) for c, v in standard_spec.items() if v is not None}
    elif isinstance(standard_spec, list):
        standard = {c: float(c) for c in standard_spec}
    else:
        raise ValueError(f"standard は辞書またはリストです: {standard_spec}")

    return {
        "channel_names": names,
        "standard": standard,
        "frac_baseline": float(cfg.get("frac_baseline", 0.5)),
    }


# ---------------------------------------------------------------------------
# 較定ファイル生成
# ---------------------------------------------------------------------------
def build_calibration(data_path: str, config: dict, target: str,
                      method: str = "threshold", notes: str = "") -> dict:
    """希釈系列の DATA.TXT を解析し、較定パラメータ辞書を返す。

    data_path はファイルのパス (load_run がディレクトリ/ファイル両対応)。
    """
    curves = qc.load_run(data_path, channel_names=config["channel_names"])
    frac = config["frac_baseline"]

    points = []
    ct_values = {}
    for ch, qty in config["standard"].items():
        if ch not in curves:
            print(f"  [警告] 標準チャネル {ch} がデータにありません")
            continue
        curve = curves[ch]
        r = qc.detect_ct(curve, method=method, frac_baseline=frac)
        ct_values[ch] = None if r.ct is None else round(float(r.ct), 3)
        if r.ct is not None:
            points.append(qc.StandardPoint(
                name=ch, quantity=float(qty),
                logq=np.log10(float(qty)), ct=r.ct))

    if len(points) < 2:
        raise ValueError("標準が2点未満です (standard ロールを確認)")

    sc = qc.fit_standard_curve(points)

    return {
        "target": target,
        "created": date.today().isoformat(),
        "method": method,
        "slope": round(float(sc.slope), 6),
        "intercept": round(float(sc.intercept), 6),
        "r2": round(float(sc.r2), 6),
        "efficiency": round(float(sc.efficiency), 6),
        "n_points": len(points),
        "dilutions": {ch: float(qty) for ch, qty in config["standard"].items()},
        "ct_values": ct_values,
        "notes": notes,
    }


def write_calibration(path: str, calib: dict) -> None:
    """較定ファイルを JSON として書き込む。"""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(calib, f, ensure_ascii=False, indent=2)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main() -> None:
    ap = argparse.ArgumentParser(
        description="希釈系列から標準曲線を回帰し、較定ファイルを生成する")
    ap.add_argument("--data", default="data/DATA.TXT", help="DATA.TXT のパス")
    ap.add_argument("--config", required=True, help="standard ロール指定の config.json")
    ap.add_argument("--target", required=True, help="標的の名前 (例: オイカワcytb)")
    ap.add_argument("--out", default=None,
                    help="出力較定ファイル (省略時は --target から自動生成)")
    ap.add_argument("--method", default="threshold",
                    choices=["threshold", "derivative"],
                    help="Ct 検出方法 (既定: threshold)")
    ap.add_argument("--notes", default="", help="任意のメモ")
    args = ap.parse_args()

    if not os.path.exists(args.data):
        print(f"[エラー] データが見つかりません: {args.data}")
        sys.exit(1)

    cfg = load_config(args.config)
    print("設定を読み込みました:")
    print(f"  標準アンプロン: {cfg['standard']}")

    calib = build_calibration(args.data, cfg,
                              target=args.target, method=args.method,
                              notes=args.notes)

    out_path = args.out or f"calibration_{args.target}.json"

    # 上書き防止
    if os.path.exists(out_path) and args.out is None:
        print(f"[警告] 出力先が既に存在します: {out_path}")
        confirm = input("  上書きしますか? [y/N] ")
        if confirm.strip().lower() not in ("y", "yes"):
            out_path = f"calibration_{args.target}_{date.today().isoformat()}.json"
            print(f"  -> {out_path} に保存します")

    write_calibration(out_path, calib)

    print("\n" + "=" * 60)
    print("較定パラメータ (標準曲線)")
    print("=" * 60)
    print(f"  標的   : {calib['target']}")
    print(f"  生成日 : {calib['created']}")
    print(f"  回帰式 : Ct = {calib['slope']:.4f} · log10(q) + {calib['intercept']:.4f}")
    print(f"  R²     : {calib['r2']:.5f}")
    print(f"  PCR効率: {calib['efficiency'] * 100:.1f}%")
    print(f"  標準点数: {calib['n_points']}")
    print()
    print("各標準ウェルの Ct:")
    for ch, ct in calib["ct_values"].items():
        print(f"  {ch:<6}  {ct}")

    print("\n出力:")
    print(f"  {out_path}")


if __name__ == "__main__":
    main()
