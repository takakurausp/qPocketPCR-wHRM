# -*- coding: utf-8 -*-
"""
qpcr_core.py
============

qPocketPCR (GaudiLabs) の生データ DATA.TXT を処理するためのコアライブラリ。

DATA.TXT の形式（本機が書き出すCSV）::

    cycle, seconds, ch0, ch1, ch2, ch3, ch4, ch5, ch6, ch7

- 1列目: PCRサイクル (整数)
- 2列目: 測定開始からの秒数 (整数)
- 3〜9列目: 8チャネルの蛍光値 (wellFactor補正済み)

本ライブラリは単色検出(SYBR/Cyber-green)前提であり、標的領域と標準(standard)
アンプロンは別ウェルに入れる設計に対応している。主な機能:

- DATA.TXT のパース
- ベースライン減算・正規化
- 増幅曲線の平滑化 (Savitzky-Golay)
- Ct/Cq の検出 (固定しきい値法 と 微分最大法)
- スタンダードカーブの回帰と効率計算

外部依存: numpy, matplotlib(描画のみ)
"""

from __future__ import annotations

import csv
import os
from dataclasses import dataclass, field
from typing import Optional

import numpy as np


# ---------------------------------------------------------------------------
# データ構造
# ---------------------------------------------------------------------------
@dataclass
class Curve:
    """1チャネルの増幅曲線データ。"""
    channel: int
    cycles: np.ndarray          # shape (n,) 整数サイクル
    seconds: np.ndarray         # shape (n,) 秒数
    raw: np.ndarray             # shape (n,) 生蛍光値 (wellFactor補正済み)
    name: str = ""              # ウェル/サンプル名 (任意)

    @property
    def n(self) -> int:
        return len(self.cycles)


@dataclass
class CtResult:
    """Ct検出結果。"""
    method: str                 # 'threshold' または 'derivative'
    ct: Optional[float]         # Ct値 (しきい値を下回れば None)
    threshold: float            # 使用したしきい値
    normalized_at_ct: float     # Ctにおける正規化蛍光値


@dataclass
class StandardPoint:
    """スタンダードカーブの1点。"""
    name: str
    quantity: float             # 標本量 (コピー数 or mol or ng など任意の単位)
    logq: float                 # log10(quantity)
    ct: float


@dataclass
class StandardCurve:
    """線形回帰結果。"""
    slope: float
    intercept: float
    r2: float
    efficiency: float           # E = 10^(-1/slope) - 1
    points: list = field(default_factory=list)

    def quantity_from_ct(self, ct: float) -> float:
        """Ctから標本量を逆算。"""
        return 10.0 ** ((ct - self.intercept) / self.slope)


@dataclass
class MeltBlock:
    """融解 (HRM) セグメント。ブロック温度は全ウェル共通 (共通加熱ブロック)。"""
    temps: np.ndarray                 # (m,) 実測ブロック温度 °C (昇順)
    seconds: np.ndarray               # (m,)
    cycles: np.ndarray                # (m,)
    f: dict                          # {channel_name: np.ndarray (m,)} 蛍光
    qpcr_rows: int                   # 融解開始前のqPCR行数 (0なら融解のみラン)

    @property
    def n(self) -> int:
        return len(self.temps)


@dataclass
class MeltResult:
    """1チャネルの融解解析結果 (Tm)。"""
    channel: int
    name: str
    tm: Optional[float]              # 融解温度 °C (遷移なしなら None)
    method: str
    dfdt_min: Optional[float]        # dF/dT の最小値 (負のピーク)


@dataclass
class RunData:
    """DATA.TXT 1本分の解析済みデータ。qPCR部と融解部を分離して保持する。"""
    path: str
    channel_names: list
    curves: dict                     # {channel_name: Curve} qPCR部 (融解のみなら空)
    melt: Optional[MeltBlock]        # 融解セグメント (無ければ None)
    qpcr_rows: int                   # qPCR部の行数
    melt_rows: int                   # 融解部の行数 (検出時のみ)
    has_temp: bool                   # ファイルに温度列があるか


# ---------------------------------------------------------------------------
# DATA.TXT パース
# ---------------------------------------------------------------------------
def _is_number(s: str) -> bool:
    try:
        float(s)
        return True
    except (ValueError, TypeError):
        return False


def _resolve_layout(rows: list) -> tuple:
    """先頭行から列レイアウトを推定する。

    ファームは「Protocol name: ...」行 + ヘッダ (Cycle, Time, Temp, Sensor1..)
    を書く。旧形式・合成データはヘッダのみ、またはヘッダなし。

    Returns
    -------
    (data_start, col_idx, sensor_cols)
        data_start  : データ行の開始位置
        col_idx     : {"cycle": int, "time": int|None, "temp": int|None}
        sensor_cols : 8チャネル列の位置 (順序保持)
    """
    limit = min(len(rows), 10)
    for i in range(limit):
        row = rows[i]
        if not row:
            continue
        first = row[0].strip()
        if not first:
            continue
        low = first.lower()
        # 先頭の説明行 (Protocol name: ...) はスキップ
        if low.startswith("protocol name") or ("protocol" in low and "name" in low):
            continue
        if _is_number(first):
            # ヘッダなしデータ行 → 位置ベース (cycle, time, ch0..ch7)
            return i, {"cycle": 0, "time": 1, "temp": None}, list(range(2, 10))

        # ヘッダ行の候補: cycle列の名前を含む行
        names = [c.strip().lower() for c in row]
        ci = next((k for k, nm in enumerate(names)
                   if nm in ("cycle", "cycles") or nm.startswith("cycle")), None)
        if ci is not None:
            ti = next((k for k, nm in enumerate(names)
                       if nm in ("time", "seconds", "sec", "secs")
                       or nm.startswith("time") or nm.startswith("sec")), None)
            tmpi = next((k for k, nm in enumerate(names) if "temp" in nm), None)
            taken = {ci}
            if ti is not None:
                taken.add(ti)
            if tmpi is not None:
                taken.add(tmpi)
            sensor_cols = [k for k in range(len(names)) if k not in taken][:8]
            return i + 1, {"cycle": ci, "time": ti, "temp": tmpi}, sensor_cols

    # フォールバック: 先頭10行にヘッダなし → 最初の数値行から位置ベース
    for k, row in enumerate(rows):
        if row and _is_number(row[0].strip()):
            return k, {"cycle": 0, "time": 1, "temp": None}, list(range(2, 10))
    raise ValueError("有効なデータ行が見つかりません (列レイアウトを推定できません)")


def parse_data_txt(path: str, channel_names: Optional[list] = None) -> dict:
    """DATA.TXT を読み取り、qPCR部の Curve オブジェクト辞書を返す。

    HRM対応 (v1.2+): ファイルに Temp 列があり、末尾に融解ブロック (サイクルが
    1へ戻り、温度が単調上昇する連続キャプチャ) がある場合、qPCR部のみを返す。
    融解のみのランの場合は空の辞書を返す (run_qpcr 側で load_run_data を使う)。

    Parameters
    ----------
    path : str
        DATA.TXT (または同形式のCSV) のパス。
    channel_names : list[str], optional
        チャネル名。省略時は ch0..ch7。

    Returns
    -------
    dict
        {channel_name: Curve}  (qPCR部のみ)
    """
    run = load_run_data(path, channel_names=channel_names)
    return run.curves


def load_run_data(path: str, channel_names: Optional[list] = None) -> RunData:
    """DATA.TXT 1本を読み、qPCR部と融解部に分離した RunData を返す。"""
    if channel_names is None:
        channel_names = [f"ch{i}" for i in range(8)]

    with open(path, "r", newline="", encoding="utf-8", errors="replace") as f:
        rows = list(csv.reader(f))

    data_start, col_idx, sensor_cols = _resolve_layout(rows)
    ccol, tcol, tmpcol = col_idx["cycle"], col_idx["time"], col_idx["temp"]
    n_sensors = len(sensor_cols)
    if n_sensors < 8:
        # センサ列が8未満でも、channel_names の先頭 n 個として扱う
        sensor_cols = sensor_cols + [None] * (8 - n_sensors)

    cycles, seconds, temps = [], [], []
    channels = {name: [] for name in channel_names}

    for row in rows[data_start:]:
        if not row:
            continue
        def cell(idx):
            return row[idx].strip() if (idx is not None and idx < len(row)) else ""
        if not _is_number(cell(ccol)):
            continue
        try:
            cyc = int(float(cell(ccol)))
            sec = int(float(cell(tcol))) if tcol is not None else 0
            tmp = float(cell(tmpcol)) if tmpcol is not None else float("nan")
            vals = []
            for si in sensor_cols:
                if si is None or not _is_number(cell(si)):
                    vals.append(float("nan"))
                else:
                    vals.append(float(cell(si)))
        except (ValueError, IndexError):
            continue
        cycles.append(cyc)
        seconds.append(sec)
        if tmpcol is not None:
            temps.append(tmp)
        for name, v in zip(channel_names, vals):
            channels[name].append(v)

    if not cycles:
        raise ValueError(f"データが見つかりませんでした: {path}")

    has_temp = tmpcol is not None and len(temps) == len(cycles)
    temps_arr = np.asarray(temps, dtype=float) if has_temp else None

    # ---- 融解ブロックの分離 ----
    melt_start = None
    if has_temp:
        melt_start = detect_melt_start(np.asarray(cycles, dtype=int), temps_arr)

    if melt_start is not None and melt_start < len(cycles):
        # qPCR部と融解部に分ける
        melt = MeltBlock(
            temps=temps_arr[melt_start:],
            seconds=np.asarray(seconds[melt_start:], dtype=int),
            cycles=np.asarray(cycles[melt_start:], dtype=int),
            f={name: np.asarray(channels[name][melt_start:], dtype=float)
               for name in channel_names},
            qpcr_rows=melt_start,
        )
        qpcr_cycles = cycles[:melt_start]
        qpcr_seconds = seconds[:melt_start]
        qpcr_channels = {name: channels[name][:melt_start] for name in channel_names}
    else:
        melt = None
        qpcr_cycles = cycles
        qpcr_seconds = seconds
        qpcr_channels = channels

    curves = {}
    if qpcr_cycles:
        for name in channel_names:
            curves[name] = Curve(
                channel=channel_names.index(name),
                cycles=np.asarray(qpcr_cycles, dtype=int),
                seconds=np.asarray(qpcr_seconds, dtype=int),
                raw=np.asarray(qpcr_channels[name], dtype=float),
                name=name,
            )

    return RunData(
        path=path,
        channel_names=list(channel_names),
        curves=curves,
        melt=melt,
        qpcr_rows=len(qpcr_cycles),
        melt_rows=0 if melt is None else melt.n,
        has_temp=has_temp,
    )


def load_run(directory_or_path: str, filename: str = "DATA.TXT",
             channel_names: Optional[list] = None) -> dict:
    """ファイル (または directory 内の DATA.TXT) を読み込み qPCR部の Curve 辞書を返す。"""
    if os.path.isfile(directory_or_path):
        path = directory_or_path
    else:
        path = os.path.join(directory_or_path, filename)
        if not os.path.exists(path):
            # 大文字小文字を吸収
            found = None
            for f in os.listdir(directory_or_path):
                if f.lower() == "data.txt":
                    found = os.path.join(directory_or_path, f)
                    break
            if found is None:
                raise FileNotFoundError(f"{directory_or_path} に DATA.TXT が見つかりません")
            path = found
    return parse_data_txt(path, channel_names=channel_names)


# ---------------------------------------------------------------------------
# 平滑化 (Savitzky-Golay)
# ---------------------------------------------------------------------------
def savgol_window(n: int) -> int:
    """奇数で n 以下の適切な窓サイズを返す。"""
    w = n if n % 2 == 1 else n - 1
    return max(5, min(w, n - 1))


def savgol_smooth(y: np.ndarray, order: int = 2) -> np.ndarray:
    """移動多項式平滑化 (Savitzky-Golay)。scipy に依存しない実装。

    各点で重み付き最小二乗多項式フィッティングを行い、中心点を出力する。
    エッジ付近は窓を狭くして処理する。

    Note: 窓サイズは系列長から決まる (savgol_window)。系列が長い融解曲線
    (100点超) では窓が大きくなり遷移位置が歪むため、融解解析では
    savgol_fixed (固定窓) を使うこと。
    """
    y = np.asarray(y, dtype=float)
    n = len(y)
    if n <= order + 1:
        return y.copy()

    half = savgol_window(n) // 2
    return savgol_fixed(y, window=2 * half + 1, order=order)


def savgol_fixed(y: np.ndarray, window: int = 7, order: int = 2) -> np.ndarray:
    """固定窓の移動多項式平滑化 (Savitzky-Golay)。

    window は奇数の点幅 (既定 7)。融解曲線のような長い系列でも遷移位置を
    歪めずに平滑化できる。
    """
    y = np.asarray(y, dtype=float)
    n = len(y)
    if window % 2 == 0:
        window += 1
    half = window // 2
    if n <= order + 1 or n <= window:
        return y.copy()

    out = np.zeros(n)
    for i in range(n):
        lo = max(0, i - half)
        hi = min(n, i + half + 1)
        if hi - lo <= order:
            out[i] = y[i]
            continue
        xi = np.arange(lo, hi, dtype=float) - i
        di = np.vander(xi, order + 1, increasing=True)
        ata = di.T @ di
        try:
            ata_inv = np.linalg.pinv(ata)
        except np.linalg.LinAlgError:
            out[i] = y[i]
            continue
        coeffs = ata_inv @ di.T @ y[lo:hi]
        # 多項式を (j-i) で張っているので中心 (i) の値は定数項
        out[i] = coeffs[0]
    return out


# ---------------------------------------------------------------------------
# ベースライン減算・正規化
# ---------------------------------------------------------------------------
def detect_onset(curve: Curve, frac_baseline: float = 0.5) -> int:
    """増幅が明確に始まるサイクル位置 (インデックス) を自動検出する。

    手法:
      1. 前半 frac_baseline の平坦な区間からベースライン level とノイズ SD を推定。
      2. その level + N*SD (N=既定5) を初めて上回るサイクルを増幅開始とする。
      3. 増幅が検出されない場合は前半の半分を返す(後続処理で安全に動作)。

    これにより、高コピー数サンプルのように増幅が早期に始まる場合でも、
    ベースライン窓が増幅の立ち上がりを取り込む誤りを防ぐ。
    """
    n = curve.n
    frac_baseline = min(max(frac_baseline, 0.1), 0.9)

    # 候補ベースライン: 前半 frac_baseline の区間
    base_n = max(3, int(n * frac_baseline))
    level = float(np.mean(curve.raw[:base_n]))
    noise = float(np.std(curve.raw[:base_n]))

    threshold = level + 5.0 * noise if noise > 0 else level + 1e-9
    for i in range(base_n, n):
        if curve.raw[i] > threshold:
            return i

    # 増幅が検出されなければ、平坦と仮定して前半半分を返す
    return max(3, base_n // 2)


def baseline_window(curve: Curve, frac_baseline: float = 0.5,
                    max_cycles: int = 15) -> tuple:
    """ベースライン区間 (初期サイクル) を自動決定して返す。

    適応型: detect_onset で増幅開始を検出し、その前の平坦な区間をベースラインとする。
    ただし標準 qPCR では増幅が概ねサイクル 18 以降に始まるため、窓は最大
    max_cycles (既定 15) にキャップし、低コピー数サンプルでも十分な点数を保つ。

    frac_baseline は「増幅開始を待たずに平坦区間が短い場合のフォールバック」として
    用いる。
    """
    n = curve.n
    onset = detect_onset(curve, frac_baseline)
    base_n = max(3, min(onset, max_cycles))
    return np.arange(base_n)


def subtract_baseline(curve: Curve, frac_baseline: float = 0.5) -> np.ndarray:
    """ベースラインを減算した蛍光値を返す。"""
    idx = baseline_window(curve, frac_baseline)
    base = np.mean(curve.raw[idx])
    return curve.raw - base


def normalized_rn(curve: Curve, frac_baseline: float = 0.5) -> np.ndarray:
    """ΔRn 相当の正規化蛍光値を返す。

    (raw - baseline) / max(raw - baseline) で規格化し、0..1 の増幅曲線を得る。
    """
    sub = subtract_baseline(curve, frac_baseline)
    mx = np.max(sub)
    if mx <= 0:
        return sub
    return sub / mx


# ---------------------------------------------------------------------------
# Ct 検出
# ---------------------------------------------------------------------------
def detect_ct_threshold(curve: Curve, frac_baseline: float = 0.5,
                        r: float = 0.15) -> CtResult:
    """固定しきい値法 (ΔRn方式)。

    しきい値は「ベースラインレベル + 増幅振幅の r 倍 と ベースラインノイズの
    10倍 の大きい方」で決める。すべて正規化済み rn 空間 (0..1) で演算するため
    単位の一貫性が保たれる。
    """
    rn = normalized_rn(curve, frac_baseline)
    idx = baseline_window(curve, frac_baseline)
    base_level = float(np.mean(rn[idx]))
    noise = float(np.std(rn[idx]))
    plateau = float(np.max(rn))

    thr = base_level + max(10.0 * noise, r * (plateau - base_level))

    above = rn > thr
    if not np.any(above):
        return CtResult("threshold", None, thr, float(rn[-1]))

    first = np.argmax(above)
    # 線形補間で正確なCtを得る
    if first == 0:
        ct = float(curve.cycles[0])
    else:
        y0, y1 = rn[first - 1], rn[first]
        x0, x1 = float(curve.cycles[first - 1]), float(curve.cycles[first])
        if y1 == y0:
            ct = float(curve.cycles[first])
        else:
            ct = x0 + (thr - y0) * (x1 - x0) / (y1 - y0)

    return CtResult("threshold", ct, thr, float(rn[min(int(round(ct)), curve.n - 1)]))


def detect_ct_derivative(curve: Curve, frac_baseline: float = 0.5) -> CtResult:
    """微分最大法 (曲線の傾きが最大となるサイクルをCt)。

    しきい値設定が不要でノイズに強い。平滑化後に2階微分の極大点を検出する。
    """
    rn = normalized_rn(curve, frac_baseline)
    if np.max(rn) < 1e-6:
        return CtResult("derivative", None, float(np.max(rn)), float(rn[-1]))

    smooth = savgol_smooth(rn)
    # 2階微分 (サイクル間隔=1 assumed)
    d2 = np.gradient(np.gradient(smooth))
    # しきい値以上が増幅領域。その中で2階微分の極大(山)を探す
    thr = 0.05 * np.max(d2)
    amp = d2 > thr
    if not np.any(amp):
        return CtResult("derivative", None, float(thr), float(rn[-1]))

    peak = np.argmax(d2[amp])
    cyc = int(curve.cycles[np.where(amp)[0][peak]])
    return CtResult("derivative", float(cyc), float(thr), float(rn[min(cyc, curve.n - 1)]))


def detect_ct(curve: Curve, method: str = "auto", frac_baseline: float = 0.5) -> CtResult:
    """Ct を検出。method='auto' は固定しきい値法を優先し、増幅が認められなければ
    微分最大法にフォールバックする。"""
    if method == "derivative":
        return detect_ct_derivative(curve, frac_baseline)
    if method == "threshold":
        return detect_ct_threshold(curve, frac_baseline)

    # auto: 固定しきい値法を優先 (標準的)。増幅が認められなければ微分最大へ。
    r = detect_ct_threshold(curve, frac_baseline)
    if r.ct is not None:
        return r
    return detect_ct_derivative(curve, frac_baseline)


# ---------------------------------------------------------------------------
# スタンダードカーブ
# ---------------------------------------------------------------------------
def fit_standard_curve(points: list[StandardPoint]) -> StandardCurve:
    """標本量の log10 と Ct の線形回帰を行い、効率と共に返す。"""
    if len(points) < 2:
        raise ValueError("スタンダードは2点以上必要です")

    x = np.array([p.logq for p in points])
    y = np.array([p.ct for p in points])

    slope, intercept = np.polyfit(x, y, 1)
    pred = slope * x + intercept
    ss_res = np.sum((y - pred) ** 2)
    ss_tot = np.sum((y - np.mean(y)) ** 2)
    r2 = 1 - ss_res / ss_tot if ss_tot > 0 else float("nan")

    eff = 10.0 ** (-1.0 / slope) - 1.0
    return StandardCurve(slope, intercept, float(r2), float(eff), points)


def summarize(curve: Curve, frac: float = 0.5) -> dict:
    """1曲線の要約統計を返す。"""
    rn = normalized_rn(curve, frac)
    return {
        "channel": curve.channel,
        "name": curve.name,
        "n_cycles": curve.n,
        "max_raw": float(np.max(curve.raw)),
        "max_rn": float(np.max(rn)),
        "final_rn": float(rn[-1]),
    }


# ---------------------------------------------------------------------------
# 融解 (HRM) セグメントの分離
# ---------------------------------------------------------------------------
def detect_melt_start(cycles: np.ndarray, temps: np.ndarray,
                      min_pts: int = 15, min_span: float = 8.0,
                      min_inc_frac: float = 0.85) -> Optional[int]:
    """融解ブロックの開始行を返す (無ければ None)。

    ファームは融解を PCR サイクル完了後に実行し、その間 PCRcycle は 1 のまま
    各点を記録する。したがって融解行は

      1. サイクル値が 2 以上から 1 へ戻った後 (増幅→融解ラン)、または全行 cycle=1
      2. 実測温度がほぼ単調に上昇 (融解ランプ)
      3. 十分な点数 (>= min_pts) と温度幅 (>= min_span)

    を満たす末尾ブロックとして検出できる。
    """
    n = len(temps)
    if n < min_pts:
        return None

    def is_ramp(start: int) -> bool:
        """temps[start:] が融解ランプらしいか。"""
        seg = temps[start:]
        m = len(seg)
        if m < min_pts:
            return False
        if float(seg[-1]) - float(seg[0]) < min_span:
            return False
        inc_frac = float(np.mean(seg[1:] > seg[:-1]))
        return inc_frac >= min_inc_frac

    # 1) 末尾から遡って温度が単調増加する連続ブロックを探す (最も一般的)
    #    融解ステップはサイクル完了後に実行されるが、サイクルカウンタは最終値
    #    のまま記録されるため、サイクル値ではなく温度ランプで判定する。
    s = n
    while s > 1 and float(temps[s - 1]) >= float(temps[s - 2]):
        s -= 1
    # s=1 の場合、最終行を含めて全体がランプの可能性がある (融解のみラン)
    if s == 1:
        s = 0
    if is_ramp(s):
        return s

    # 2) サイクルが 2以上→1 に戻る最後の箇所 (旧ファーム互換)
    melt = None
    for i in range(n - 1, 0, -1):
        if int(cycles[i]) == 1 and int(cycles[i - 1]) >= 2:
            melt = i
            break
    if melt is not None and is_ramp(melt):
        return melt

    # 3) 末尾の cycle==1 の連続ブロック (増幅1サイクルなど少数ケース)
    s = n
    while s > 0 and int(cycles[s - 1]) == 1:
        s -= 1
    if s < n and is_ramp(s):
        return s

    # 4) 全行が cycle==1 かつ全体がランプ (融解のみラン)
    if is_ramp(0):
        return 0
    return None


# ---------------------------------------------------------------------------
# 融解 (HRM) 解析: Tm 検出
# ---------------------------------------------------------------------------
def analyze_melt(melt: MeltBlock, min_signal_sd: float = 3.0) -> dict:
    """各チャネルの融解曲線から Tm (融解温度) を検出する。

    Tm は蛍光 F(T) を平滑化した後、-dF/dT が最大 (dF/dT が最小) となる温度
    (= 変曲点) として求める。遷移 (シグナルの減少) がノイズに対して十分でない
    ウェル (NTC 等) は tm=None を返す。

    Returns
    -------
    dict
        {channel_name: MeltResult}
    """
    results = {}
    for name, f in melt.f.items():
        f = np.asarray(f, dtype=float)
        if len(f) < 8 or not np.all(np.isfinite(f)):
            results[name] = MeltResult(channel=0, name=name, tm=None,
                                       method="peak of -dF/dT", dfdt_min=None)
            continue

        n = len(f)
        smooth = savgol_fixed(f, window=7)
        temps = np.asarray(melt.temps, dtype=float)
        dfdt = np.gradient(smooth, temps)

        # 両端のエッジアーティファクトを除く
        lo, hi = 2, len(dfdt) - 2
        if hi - lo < 3:
            results[name] = MeltResult(channel=0, name=name, tm=None,
                                       method="peak of -dF/dT", dfdt_min=None)
            continue

        # S/N判定: 融解による蛍光ドロップ量 vs 平坦域の残差ノイズ
        k = max(3, n // 12)
        hi_f = float(np.median(f[:k]))
        lo_f = float(np.median(f[-k:]))
        drop = abs(hi_f - lo_f)
        resid = f - smooth
        noise = float(np.median(np.abs(resid))) * 1.4826  # MAD -> σ 換算
        noise = max(noise, 1e-9)

        interior = dfdt[lo:hi]
        peak_i = lo + int(np.argmin(interior))
        peak = float(dfdt[peak_i])

        # 遷移なし (フラット/NTC) はノイズと区別できないため tm=None
        if drop < max(min_signal_sd * noise, 1e-6) or peak >= 0:
            results[name] = MeltResult(channel=0, name=name, tm=None,
                                       method="peak of -dF/dT", dfdt_min=peak)
            continue

        results[name] = MeltResult(
            channel=0,
            name=name,
            tm=float(temps[peak_i]),
            method="peak of -dF/dT",
            dfdt_min=peak,
        )

    # channel 番号を channel_names の順で付与
    names = list(melt.f.keys())
    for i, name in enumerate(names):
        if name in results:
            results[name].channel = i
    return results


def melt_neg_derivative(melt: MeltBlock, channel: str) -> np.ndarray:
    """-dF/dT (Tm ピーク用の正値プロファイル) を返す。"""
    f = np.asarray(melt.f[channel], dtype=float)
    smooth = savgol_fixed(f, window=7)
    temps = np.asarray(melt.temps, dtype=float)
    dfdt = np.gradient(smooth, temps)
    return -dfdt
