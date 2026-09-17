# qPocketPCR による環境DNA定量qPCR — ワークフロー

本リポジトリのファームウェア [qPocketPCR-wHRM](https://github.com/takakurausp/qPocketPCR-wHRM) （およびその元となっている[qPocketPCR](GaudiLabs, https://github.com/GaudiLabs/qPocketPCR)）から得られた生データ `DATA.TXT`（装置上では `DATAQPCR.TXT` としても公開）を処理し、**環境DNA (eDNA) の定量**を行う一連のワークフロー。

単色検出 (Cyber green / SYBR green) であるため、**標的領域と標準(standard)アンプロン
は別ウェル**に入れる。標的には「検出範囲よりも少しだけ広い範囲を増幅したもの」を使い、
毎回同じウェルで走らせる（後述）。

## ディレクトリ構成

```
ecDNA_qpcr/
├── scripts/
│   ├── qpcr_core.py            # コア処理ライブラリ (パース・ベースライン・Ct検出・回帰)
│   ├── make_synthetic_data.py  # synthetic DATA.TXT 生成器 (検証用)
│   ├── make_calibration.py     # 希釈系列から較定ファイル(JSON)を生成
│   └── run_qpcr.py             # ワークフロー統合スクリプト (実行エントリ)
├── config.json                 # チャネル配置・役割マッピング
├── config_sample.json          # 未知サンプル2ウェル用テンプレート
├── calibration_*.json          # 較定ファイル (make_calibration.py が生成)
├── data/
│   └── DATA.TXT                # qPocketPCR から取り出した生データ (または合成)
└── output/                     # 結果 (results.tsv, report.md, *.png)
```

## qPocketPCR の生データ形式

装置は USB Mass Storage ドライブとして `DATA.TXT`（`DATAQPCR.TXT`）を書き出す。
ファームウェア `qPocketPCR-wHRM v0.24`（および、同じく Temp 列を出力する互換ファームウェア）の形式は CSV（Temp 列なしの旧形式にも自動対応）:

```
Protocol name: <プロトコル名>       ← 先頭行 (任意)
Cycle, Time, Temp, Sensor1, Sensor2, ..., Sensor8
1, 12, 95.2, 1523.4, 1480.1, ...
```

- **Cycle**: PCRサイクル (整数)。1サイクル内に複数キャプチャがあれば同じ値が並ぶ
- **Time**: 測定開始からの秒数
- **Temp**: キャプチャ時点の実測ブロック温度 °C（`qPocketPCR-wHRM` で出力・融解/HRM解析に必須。Temp 列がない場合は qPCR 定量のみ）
- **Sensor1〜8**: 8ウェルの蛍光値 (wellFactor 補正済み)

### 増幅→融解 (HRM) の1ランと自動分離

増幅の後に `MELT FROM/TO/INC/HOLD` で融解ランプ (例: 65→95°C, 0.2°C刻み) を
実行すると、qPCR部の**後に融解行 (Cycle=1 のまま実測温度が単調上昇する末尾ブロック)**
が続きます。本スクリプトはこの末尾ブロックを自動検出して

- **qPCR部** → 従来の Ct / 標準カーブ定量
- **融解部** → 融解曲線と Tm (dF/dT ピーク)

に分離します (検出規則: サイクルが 2以上→1 へ戻る境界 + 単調上昇ランプ + 十分な
点数/温度幅)。**融解のみ**のファイルでは qPCR 解析をスキップし Tm のみを出力します。

装置はカメラで各ウェルの蛍光を側面から撮像し、ベースライン補正とウェル間補正
(wellFactor) を施した後にこの CSV に書き出します。単色検出のため multiplex は不可
で、標的・標準は別ウェルに入れます。

## ワークフローの概要

1. **DATA.TXT のパース** — チャネルごとに `Curve` (サイクル秒数蛍光値) に分解
2. **役割マッピング** — `config.json` で standard / sample / ntc を指定
3. **適応型ベースライン減算・正規化 (ΔRn)** — 増幅開始前に自動検出
4. **Ct/Cq の検出** — 固定しきい値法 (ΔRn方式)、フォールバックで微分最大法
5. **スタンダードカーブの回帰とPCR効率計算** — `log10(quantity)` vs Ct の線形回帰
6. **未知サンプルの標本量推定** — 回帰式に Ct を代入
7. **結果出力** — TSV / Markdown レポート + 増幅曲線図 & 標準カーブ図

### 注: 本解析ワークフローの位置づけ

このディレクトリは**装置とは独立したデスクトップ解析ワークフロー**です。装置ファームウェア
(`qPocketPCR-wHRM`) のビルド・書き込み動作そのものをテストするものではありません。
「装置能力」や「装置テスト」の基準資料として使う場合は、

- 装置側の値はファームウェアが書き出す `DATA.TXT` / `DATAQPCR.TXT` そのもの
- 解析側の値は、このスクリプト群がそこから算出した Ct・定量・Tm

であり、両者を混同しないでください。装置能力の評価に使う際は、測定条件・較定・プロトコルを
この README に合わせて実機で再現できるよう手順を明記してください。
## 使い方

### (A) 実機データで実行

`data/DATA.TXT` に装置から取り出したファイルを置き、`config.json` の役割マッピングを
実際のウェル配置に合わせて編集してから:

```bash
python3 scripts/run_qpcr.py --data data/DATA.TXT --config config.json --out-dir output/
```

### (B) 合成データで検証 (装置がなくても動作確認)

```bash
# synthetic DATA.TXT を生成 (標準希釈6段階 + サンプル2本)
python3 scripts/make_synthetic_data.py --out data/DATA.TXT --seed 1

# ワークフロー実行 (旧形式・増幅のみ)
python3 scripts/run_qpcr.py --data data/DATA.TXT --config config.json --out-dir output/

# ── 増幅 → 融解 (HRM) の検証 ──
# デバイス形式 (Temp列付き) で qPCR 45行 + 融解 151行 (65→95°C / 0.2°C刻み) を生成
#   各ウェルの真のTmは ch0=82.0°C から +0.5°C ずつ (--melt-tm-base で変更可)
python3 scripts/make_synthetic_data.py --with-melt --out data/DATA_HRM.txt --seed 1
python3 scripts/run_qpcr.py --data data/DATA_HRM.txt --config config.json --out-dir output_hrm/
# → results.tsv に qPCR定量 + 各ウェル Tm が書かれる (真値と ±0.5°C 程度で一致)

# ── 融解のみランの検証 (増幅0サイクル) ──
python3 scripts/make_synthetic_data.py --with-melt --n-cycles 0 --out data/DATA_MELT.txt --seed 2
python3 scripts/run_qpcr.py --data data/DATA_MELT.txt --config config_sample.json --out-dir output_melt/
```

## config.json の書き方

チャネル名と役割をマッピングする。`standard` は標本量 (任意の単位: copies / mol / ng)、
`sample` はラベル、`ntc` は未検出を期待するウェル。

```json
{
  "channel_names": ["ch0","ch1","ch2","ch3","ch4","ch5","ch6","ch7"],
  "frac_baseline": 0.5,
  "roles": {
    "standard": {"ch0": 1000000, "ch1": 100000, "ch2": 10000,
                 "ch3": 1000, "ch4": 100, "ch5": 10},
    "sample":   {"ch6": "eDNA_sample_1", "ch7": "eDNA_sample_2"},
    "ntc": []
  }
}
```

## 環境DNA定量の設計 (標的領域と標準について)

ご要望どおり、**標的領域は「検出範囲よりも少しだけ広い範囲を増幅したもの」**を用意し、
毎回同じウェルで標準として用います。単色検出のため multiplex 不可であること、および
qPCR の定量が「Ct値の比較」によって成立することを踏まえると、以下のように設計します。

- **標準アンプロン (固定ウェル)**: 既知コピー数の標的領域増幅産物を段階希釈 (例: 10^6 → 10 copies)。
  これにより `log10(標本量) vs Ct` の標準カーブを得る。
- **標的サンプル (任意のウェル)**: 環境DNAから増幅した標的領域を同じ条件で定量。
  標準カーブに Ct を当てはめて「反応あたりのコピー数」に戻す。
- **「少し広い範囲」の意味**: 検出範囲 (ダイナミックレンジ) の上限・下限をわずかに上回る
  増幅産物を標準として用意することで、希釈系列が検出範囲を確実にカバーし、外挿誤差を避ける。

## 出力

- `output/results.tsv` — PCR効率 / R² / 各ウェルの Ct / 標本量 (タブ区切り)
- `output/report.md` — 簡易レポート (Markdown)
- `output/amplification_curves.png` — 正規化増幅曲線 (ΔRn、matplotlib)
- `output/standard_curve.png` — 標準カーブ (回帰式・効率・R²付き、matplotlib)

融解 (HRM) セグメントがある場合、以下が追加されます:

- `output/results.tsv` / `report.md` — `# melt (HRM)` セクションに各ウェルの Tm
- `output/melt_curves.csv` — long format (`temp, channel, label, f, neg_dfdt`)
- `output/melt_curves.png` — 融解曲線 (F vs T) と -dF/dT (Tm ピーク) の2枚組

`--data` にはファイルのパスを直接指定できます (装置から取り出したままの
`DATAQPCR.TXT` 等、ファイル名が `DATA.TXT` でなくても可)。
グラフ不要なら `--no-plots` で matplotlib を省略できます。

### R / ggplot2 で作図する (オプション)

Python の描画ではなく **ggplot2** で論文向けに仕上げたい場合、`run_qpcr.py` は
R で読みやすい構造化 CSV を同時に出力します。

生成される CSV:

- `amplification.csv` — long format (`cycle, channel, role, raw, normalized_rn`)
- `standard_curve.csv` — 標準点 (`channel, log10_quantity, ct`)〔その回で標準を回帰した場合のみ〕
- `calibration_meta.csv` — 較定メタデータ (slope/intercept/R²/効率)〔外部較定使用時〕
- `samples.csv` — 標本量 (`label, channel, ct, copies`)

対応する R スクリプトで描画:

```bash
# 増幅曲線
Rscript scripts/plot_amplification.R output/sample_rtest/amplification.csv out.png "オイカワcytb 増幅曲線"
# 標準曲線 (その回で標準を回帰した場合)
Rscript scripts/plot_standard_curve.R output/std_rtest/standard_curve.csv out.png
# 標本量の比較 (点プロット + 数値ラベル、log10 Y軸)
Rscript scripts/plot_samples.R output/sample_rtest/samples.csv out.png
```

描画には `ggplot2` と `scales` のインストールが必要です。日本語タイトルはシステムに
Noto Sans CJK 等が導入されていれば正しく表示されます。

## 簡易定量（未知サンプル2ウェルだけ採取）

標準曲線は**毎回取る必要はありません**。反応条件（酵素・プライマー・温度プロトコル）を
変えない限り、一度取りました較定をそのまま再利用できます。未知サンプル2ウェルだけを
採取して定量したい場合は以下の手順で進めます。

### 1. 較定ファイルの生成（初回のみ）

標準希釈系列を含む `DATA.TXT` と、その役割指定 config で `make_calibration.py` を実行し、
較定ファイル (`calibration_*.json`) を生成します。これに標的の名前を付けます。

```bash
# 希釈系列の DATA.TXT と standard ロール指定 config で実行
python3 scripts/make_calibration.py \
    --data data/DATA.TXT \
    --config config_std.json \
    --target "オイカワcytb" \
    --out calibration_oikawa_cytb.json
```

生成される較定ファイルには以下が含まれます:

- 回帰式 (`slope`, `intercept`)、R²、PCR効率 (`efficiency`)
- 標的名・生成日・各標準ウェルの Ct 値

### 2. 未知サンプルの採取

8ウェルのうち**2ウェルだけ未知サンプルに入れます**。残りの6ウェルは空（または水）でOK。
例: ch0, ch1 に未知サンプル、ch2〜ch7 は無水。

### 3. ファイル配置と設定

採取したファイルを `data/` の下に **`DATA.TXT`** という名前で置きます。そして
`config_sample.json` を使います — ch0, ch1 が未知サンプルです（標準は省略可）。

```json
{
  "channel_names": ["ch0","ch1","ch2","ch3","ch4","ch5","ch6","ch7"],
  "frac_baseline": 0.5,
  "roles": {
    "sample": {"ch0": "unknown_1", "ch1": "unknown_2"},
    "standard": {},
    "ntc": []
  }
}
```

### 4. 解析実行（外部較定を使用）

`--calibration` で生成した較定ファイルを指定します。これでその回の標準を取り直さず、
保存しておいた較定を使って未知サンプルを定量できます。

```bash
python3 scripts/run_qpcr.py \
    --data data/DATA.TXT \
    --config config_sample.json \
    --calibration calibration_oikawa_cytb.json \
    --out-dir output/sample_run/
# → output/sample_run/results.tsv に ch0, ch1 の Ct と標本量 (コピー数相当) が書かれる
```

生成される `results.tsv` には較定標的名・較定日・回帰式・効率・R² も記録されます。

### 補足

- 較定ファイルは条件（酵素・プライマー・温度プロトコル）を変えなければ何度でも再利用。
- 反応条件を変えた時（酵素・プライマーのバッチ替えなど）は、再度 `make_calibration.py` で
  較定を取り直してください。
- `--calibration` を付けない場合は、その回の標準を回帰して定量する旧来の挙動です。

## 依存

- Python 3.9+
- numpy, matplotlib (描画のみ)
- scipy は使用しない (Savitzky-Golay 平滑化は自前実装)
