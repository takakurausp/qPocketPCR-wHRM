# qPocketPCR-wHRM — ESP32-S2 用 eDNA 定量qPCR＋融解曲線(HRM)装置

[qPocketPCR](https://github.com/GaudiLabs/qPocketPCR)（GaudiLabs, Urs Gaudenz, 2025）をベースにした、**高分解能融解曲線 (HRM)** 対応のコンパクトな自作定量PCR (qPCR) 装置です。

元プロジェクトの `Software/pPocketPCR_Main` ファームウェアディレクトリのみを取り出し、以下の機能を追加しています：

1. **高分解能融解曲線 (HRM)** — PCR増幅の後（または代わりに）細かな温度ランプを行い蛍光を取得
2. **USB Mass Storage** — `PROTOCOL.TXT` と `DATAQPCR.TXT` を公開する 128 KB の FAT12 バーチャルドライブ
3. **WiFi によるWeb制御** — ブラウザからプロトコルの作成・走査の開始/停止・ファイルの送受信
4. **名前付きプロトコルライブラリ** — 複数のプロトコルをデバイス上に保存・読み込み

## ハードウェア概要

| 部品 | 内容 / 詳細 |
|------|-------------|
| MCU | ESP32-S2（NATIVE USB：CDC シリアル + MSC マスストレージ） |
| ディスプレイ | TFT タッチスクリーン（TFT_eSPI） |
| 温度 | TLA202x 電圧増幅器（サーミスタ・ヒーター読み取り） |
| 蛍光 | 8ch 光电ダイオードアレイ、カメラによるベースライン/ウェル間補正 (wellFactor) 済み |
| カメラ | OV2640 系センサー（計測時の蛍光撮像に使用） |
| LED | LED ドライバー（TLC59108）、励起照明用 |
| 記憶装置 | SPIFFS（フラッシュ）＋ バーチャル 128 KB FAT12 USBドライブ |

## 機能

### HRM（高分解能融解曲線）

ファームウェアは `PROTOCOL.TXT` の **MELT** ブロックで、PCR増幅の後に細かな温度ランプを蛍光取得とともに実行できます。これにより：

- **融解曲線の取得** — 任意の間隔（例: 0.2 °C）で温度対蛍光を取得
- **Tm の検出** — 解析スクリプトが qPCR セグメントと融解セグメントを自動分離し、−dF/dT のピークから融解温度 (Tm) を算出
- **増幅→融解** または **融解のみ**（`CYCLES: 0` で融解のみ実行）

MELT ステップはリピート領域の後に内部 `steps[]` 配列に展開されるため、既存の状態機械がPCRサイクル終了時にちょうど1回だけ実行します。

### USB Mass Storage（128 KB FAT12）

仮想USB Mass Storageドライブのサイズは **128 KB**（256 セクタ × 512 バイト）で、FAT12 レイアウトを動的に組み直し、残りのクラスターをすべて `DATAQPCR.TXT` に割り当てます。これにより、フル45サイクルのqPCR走査＋151点のHRMランプなど、大容量データも保存できます。

ファイル更新のたびにFATテーブルを再構築し、すべての残リソースをデータファイルにチェーンすることで利用可能な容量を最大化します。

### WiFi によるWeb制御（アクセスポイント）

起動時、装置は以下のようなアクセポイントを出力します：

- **SSID:** `qPocketPCR`
- **パスワード:** `12345678`

このネットワークにPCやスマホから接続し、ブラウザで装置IP（既定 `192.168.4.1`）を開きます。利用可能なエンドポイント：

| URL | 用途 |
|-----|------|
| `/` | ステータスページ — モード、進捗、ステップ、温度；制御と結果へのリンク |
| `/builder` | インタラクティブな**プロトコルビルダー**（下記参照） |
| `/status` | JSONステータス（ポーリング・ダッシュボード用：`mode`, `progress`, `step`, `temp`, `cycle`） |
| `/start` | 現在のプロトコルを開始（`PROTOCOL.TXT` を読み込み、バッファ上限チェックを行う） |
| `/stop` | 現在走査の停止を要求 |
| `/download` | `DATAQPCR.TXT` をファイル添付としてダウンロード |
| `/upload`（POST, multipart） | 新しい `PROTOCOL.TXT` をアップロード |

### プロトコルビルダー（`/builder`）

ファームウェアに組み込まれたシングルページのWebアプリで、以下ができます：

- ステップごとに温度プロファイルを構築し、ライブプレビューチャートを表示
- **Melt (HRM)** ブロック（`MELT FROM / TO / INC / HOLD`）を設定
- 現在のプロトコルを `PROTOCOL.TXT` として保存・アップロード
- **名前付きプロトコルを保存** — 複数のプロトコルをデバイス上に保存し、名前で読み込み

名前付きプロトコルは次のエンドポイントで管理されます：

| エンドポイント | メソッド | 説明 |
|----------------|----------|------|
| `/listproto` | GET | 保存済みプロトコルの `id\|name` を改行区切りで返す |
| `/loadproto?id=N` | GET | プロトコル N を読み込んで実行中プロトコルに設定し、生テキストを返す |
| `/saveproto` | POST | 名前付きプロトコルを保存（`name`, `protocol` フィールド）；更新後の一覧を返す |

最大 **99個** の名前付きプロトコルを保存できます（SPIFFS の `PROTO_0N.txt` ファイル）。

### データ出力

装置は `DATAQPCR.TXT`（`DATA.TXT` としてもアクセス可）をUSBドライブのCSV形式で書き出します：

```
Protocol name: My Protocol
Cycle, Time, Temp, Sensor1, Sensor2, Sensor3, Sensor4, Sensor5, Sensor6, Sensor7, Sensor8
1, 12, 95.2, 1523.4, 1480.1, ...
```

- **Cycle** — PCRサイクル数（融解中は1）
- **Time** — 走査開始からの秒数
- **Temp** — キャプチャ時点の実測ブロック温度 °C（HRM解析に必須）
- **Sensor1–8** — 8ウェルの蛍光値（wellFactor補正済み）

## PROTOCOL.TXT の形式

USBドライブ上に `PROTOCOL.TXT` ファイルを置きます（装置はFAT12マスストレージデバイスとして振る舞います）。起動時にプロトコルがパースされます。Webの**プロトコルビルダー**（`/builder`）で作成することもできます。

### 基本形式（増幅のみ）

```
NAME: My Protocol
DATE: 9.9.2026

PROTOCOL:

REPEAT: 2-4
CYCLES: 35

STEP 1: Initial step
  TEMPERATURE: 95C
  DURATION: 12 min

STEP 2: Denaturation
  TEMPERATURE: 94C
  DURATION: 20 sec

STEP 3: Annealing
  TEMPERATURE: 65C
  DURATION: 15s

STEP 4: Extension
  TEMPERATURE: 72C
  DURATION: 45s
  CAPTURE: yes

STEP 5: Final Step
  TEMPERATURE: 20C
  DURATION: 10 min
```

### 融解（HRM）ブロックの追加

最後の STEP の**後**に以下の行を追加すると、増幅後に融解ランプを実行できます：

```
MELT FROM: 65.0
MELT TO: 95.0
MELT INC: 0.2
MELT HOLD: 2
```

- `MELT FROM` — 開始温度（°C）
- `MELT TO` — 終了温度（°C）
- `MELT INC` — 各ポイントの温度刻み（°C）。既定: 0.2
- `MELT HOLD` — 各ポイントの保持時間（秒）。既定: 1

各融解ポイントはキャプチャステップになります。上記例では151点のキャプチャステップが生成されます（65.0, 65.2, …, 95.0 °C）。

### 融解のみ走査

`CYCLES: 0` を設定し、MELT ブロックのみを含めます：

```
NAME: Melt Only
DATE: 9.9.2026

PROTOCOL:

REPEAT: 1-1
CYCLES: 0

MELT FROM: 65.0
MELT TO: 95.0
MELT INC: 0.2
MELT HOLD: 2
```

### フィールド一覧

| フィールド | 説明 |
|-----------|------|
| `NAME:` | プロトコル名（DATA.TXT にログ出力） |
| `DATE:` | 日付文字列（自由形式） |
| `REPEAT: start-end` | リピートするサイクル範囲（1-based） |
| `CYCLES:` | PCRサイクル数（融解のみなら0） |
| `STEP n: name` | ステップ定義（1-based、最大200） |
| `TEMPERATURE:` | 目標温度（°C） |
| `DURATION:` | 保持時間。単位: `min`、`s`、または数値のみ（秒） |
| `CAPTURE: yes` | このステップで蛍光を取得 |
| `MELT FROM:` | 融解ランプの開始温度 |
| `MELT TO:` | 融解ランプの終了温度 |
| `MELT INC:` | 温度刻み（既定 0.2 °C） |
| `MELT HOLD:` | 各融解ポイントの秒数（既定 1 s） |

## 記憶レイアウト

装置は2つの記憶領域を使用します：

### バーチャルUSBドライブ（RAMバックファットのFAT12画像）

「USBドライブ」とは、**PSRAM上に保持される128 KB の FAT12 ディスク画像**で、取り外し可能なマスストレージデバイスとしてホストに公開されます。2つのファイルのみを含みます：

| ファイル | 用途 |
|---------|------|
| `PROTOCOL.TXT` | ホストからアップロードされたプロトコル（読み書き可） |
| `DATAQPCR.TXT` | 装置が書き出す測定データ |

画像はRAM上にあり**揮発性**です。書き込みまたは取り出しイベントが発生するたびに SPIFFS（`/my_array.bin`）に永続化されます。**「デバイスの取り外し」時にはディスク画像が即座にSPIFFSに書き込まれるため**、ケーブルを直ちに抜いてもデータ消失が起こりません。

### SPIFFS（フラッシュ）

SPIFFS は不揮発性ファイルを持ちます：

| ファイル | 用途 |
|---------|------|
| `/my_array.bin` | 128 KB USBドライブ画像の永続化コピー（約128 KB） |
| `/PROTO_0N.txt` | 名前付きプロトコルテキストファイル（最大99個） |
| `/PROTOLIST.TXT` | 保存済みプロトコルの `id:name` を一覧管理するファイル |
| `/base.bin` | カメラのベースライン補正（約75 KB） |
| `/mask.bin` | 蛍光ROI選択用ピクセルマスク、パック済み（約9.4 KB。旧75 KB） |

ピクセルマスク（`/mask.bin`）は**パック形式**で保存されます（1ピクセル1bit、MSB-first、マジックバイトヘッダ付き）。RAM上のマスクは通常のboolean配列のまま、ディスク上の表現のみをパックするため、フラッシュ容量を約66 KB節約します。旧来のバイトパック形式のマスクはマジックバイトの欠落で検知され、自動的に再初期化されます。

## 解析

`analysis/` ディレクトリには下流処理用のPythonスクリプトが含まれます：

- `scripts/run_qpcr.py` — 完全なqPCR＋HRMワークフロー（エントリポイント）
- `scripts/make_synthetic_data.py` — 検証用合成データ生成器
- `scripts/make_calibration.py` — 希釈系列から較定ファイルを生成
- `scripts/qpcr_core.py` — コアライブラリ（ベースライン、Ct、標準カーブ、融解解析）

eDNA qPCR のワークフロー全体・設定形式・HRM/Tm の詳細は [`analysis/README_jp.md`](analysis/README_jp.md) を参照してください。

## ビルド

ファームウェアは [PlatformIO](https://platformio.org/) で `esp32-s2-usb-native` 対象にビルドします：

```bash
pio run                 # firmware.bin をビルド
pio run -t upload       # USB接続で装置へフラッシュ
pio monitor             # 115200 ボーのシリアルコンソール
```

## クレジット

本ファirmwareは、原版の[qPocketPCR](https://github.com/GaudiLabs/qPocketPCR)（GaudiLabs、Urs Gaudenz）をベースにした独立したフォーク **qPocketPCR-wHRM v0.1** です。原版はそのまま **V1.1** を維持し、本フォークで高分解能融解曲線（HRM）・128 KB USB Mass Storage・WiFi によるWeb制御・名前付きプロトコル保存を追加しました。

- 元プロジェクト: qPocketPCR — GaudiLabs / Urs Gaudenz（GPL-3.0）
- フーク・拡張: © 高倉耕一（GPL-3.0）

## ライセンス

このプロジェクトは **GNU General Public License v3.0 (GPL-3.0)** でライセンスされています。これはオリジナルの [qPocketPCR](https://github.com/GaudiLabs/qPocketPCR) リポジトリと同じライセンスです。

詳細は [LICENSE](LICENSE) または <https://www.gnu.org/licenses/gpl-3.0.html> を参照してください。
