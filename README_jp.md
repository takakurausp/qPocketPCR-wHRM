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
## WiFi クライアントモード（アクセポイントに接続）

このファームウェアは、アクセポイントモードに加えて**クライアントモード**にも対応しています。USBドライブ上の `WIFI.TXT` にSSIDとパスワードを記入しておくと、起動時にそのアクセポイントに接続できます（接続できればクライアントモード、できなければ従来どおりアクセポイントモード）。

### 使い方
1. 装置をPCに接続し、USBドライブを開く
2. `WIFI.TXT` を開く（まだ無ければ空のテンプレートが自動生成される）
3. 次のように記入して保存する

```ini
NAME: My WiFi Config
SSID=あなたのアクセポイント名
PASSWORD=あなたのパスワード
```

4. 装置の電源を再起動する

### 動作の仕組み
- `WIFI.TXT` に**SSIDとPASSWORDが両方とも空でなく**、そのアクセポイントに接続できれば → **クライアントモード**。
- SSID/PASSWORDが空、または接続に失敗した場合は → **アクセポイントモード**（SSID: `qPocketPCR`）に自動切り替え。
- 起動時に `WIFI.TXT` が存在しない場合は、SSIDとパスワードを空にした**テンプレートファイル**を作成する。
- すでに `WIFI.TXT` があれば、その内容は維持される（上書きされない）。

### WiFi を無効にする場合
クライアントモードでもアクセポイントモードでもWiFiは電波を発射します。日本国内で使う場合は、ビルド時に `-DWIFI_ENABLED=0` を指定してWiFiそのものを無効にしてください（上記参照）。WIFI_ENABLED=0 の場合、`WIFI.TXT` の読み込み・テンプレート作成・モード切り替えは一切行われません。

> **⚠️ 日本の電波法に関する注意**
> 本ファームウェアのWiFi（アクセスポイント）機能は、日本の電波法の認証を受けていません。
> 日本国内でWiFiを使用する場合、またはRF発射を避ける場合は、ビルド時にWiFiを無効にして書き込んでください。

> ### WiFi を無効にしてビルドする
> ソースコードのデフォルトは `WIFI_ENABLED=1`（WiFi有効・現状と同じ動作）です。
> 日本国内向けには `-DWIFI_ENABLED=0` でビルドすると、setup()で `WiFi.softAP()` が呼ばれずWiFi/Webサーバーが無効になります（RAM/Flash使用量も減少します）。

> ```bash
> PLATFORMIO_BUILD_FLAGS="-DWIFI_ENABLED=0" pio run -e esp32_s2_usb_native
> ```

> PlatformIOの `platformio.ini` に `-DWIFI_ENABLED=0` を追加する方法と同様です。
> 無効化済みファームウェア（v0.1）は本リポジトリのReleaseからダウンロードできます。

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
pio run                 # firmware.bin をビルド（WiFi有効）
pio run -t upload       # USB接続で装置へフラッシュ
pio monitor             # 115200 ボーのシリアルコンソール
```

リリース用バイナリは、`WIFI_ENABLED` だけが異なる次の2つの環境でビルドします：

| 環境 | 出力ディレクトリ | バイナリ |
|------|------------------|----------|
| `esp32_s2_usb_native_wifi` | `.pio/build/esp32_s2_usb_native_wifi/` | `dist/v0.24/*_wifi_enabled.bin` |
| `esp32_s2_usb_native_nofw` | `.pio/build/esp32_s2_usb_native_nofw/` | `dist/v0.24/*_wifi_disabled.bin` |

```bash
pio run -e esp32_s2_usb_native_wifi -e esp32_s2_usb_native_nofw
```

`TLC59108` ライブラリは原版 qPocketPCR のチェックアウトを兄弟ディレクトリ（`../qPocketPCR-main/Software/Libraries/TLC59108-main`）として参照します。チェックアウト位置が違う場合は `lib_deps` の該当行を書き換えてください。

### Adafruit WebSerial ESP Tool での書き込み

`pio upload` の代わりにブラウザから書き込む場合は、[Adafruit WebSerial ESP Tool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) を使います。完全に書き込むには**4つ**のスロットを使います：

| スロット | ファイル | Offset（0x形式） |
|------|------|------|
| 1番目 | `bootloader.bin` | `0x1000` |
| 2番目 | `partitions.bin` | `0x8000` |
| 3番目 | `boot_app0.bin` | `0xE000` |
| 4番目 | `firmware.bin` | `0x10000` |

最後のスロットは空のまま（Offset は `0` のまま）でかまいません。`bootloader.bin` / `partitions.bin` / `firmware.bin` は `.pio/build/esp32_s2_usb_native/` にあります：

- `bootloader.bin`（約 14.8 KB）
- `partitions.bin`（約 3 KB）
- `firmware.bin`（約 997 KB ← メイン本体）

`boot_app0.bin` はプロジェクトごとには生成されないため、フレームワークのパッケージから取得します：
`~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin`
（Arduino IDE の場合は `%LOCALAPPDATA%/Arduino15/packages/esp32/hardware/esp32/<バージョン>/tools/partitions/boot_app0.bin`）。

または `tools/make_webflash_bin.py` を使うと、これらのコピーと**単一の結合イメージ（0x0 に書き込むだけ）**をまとめて作成できます：

```bash
python tools/make_webflash_bin.py \
  --app        .pio/build/esp32_s2_usb_native/firmware.bin \
  --bootloader .pio/build/esp32_s2_usb_native/bootloader.bin \
  --partitions .pio/build/esp32_s2_usb_native/partitions.bin \
  -o dist
```

Arduino IDE の場合は、先に **スケッチ → コンパイル済みバイナリをエクスポート** を実行し、`pPocketPCR_Main.ino.bin` / `pPocketPCR_Main.ino.bootloader.bin` / `pPocketPCR_Main.ino.partitions.bin` を渡してください。

**手順：**

1. ブラウザで [https://adafruit.github.io/Adafruit_WebSerial_ESPTool/](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) を開く
2. **Baud** を選択（推奨 **460800**、なければ 115200）
3. ESP32-S2 をUSB接続した状態で **Connect** をクリック
4. 各スロットで「Choose a file…」から上の bin を選び、Offset 欄に上の値を入力（結合イメージを作った場合は**1番目のスロットに `0x0`** で指定し、他は空にする）
5. **Program** ボタンを押す（Erase は自動で行われる）

### オフセットがこの値である理由

- `bootloader.bin` は ESP32-S2 では **0x1000** に配置される固定位置
- `partitions.bin` は ESP32 の仕様で常に **0x8000** に配置される固定位置
- `boot_app0.bin` は OTA データ領域の初期化用で **0xE000**
- `firmware.bin` はアプリ本体で **0x10000**（標準的なファームウェア領域の始まり）
- 実行アドレスは `0x40026b84` で、0x10000 配下にロードされる構成

---

> ⚠️ **注意点**
> - 書き込み前に ESP32-S2 をUSBケーブルでPCに接続し、ブラウザの Connect が成功していることを確認してください。
> - `firmware.bin` が約 997 KB と大きいため、460800 baud でも数分かかることがあります。
> - 1回で全部入らない場合は、**firmware.bin 単体（0x10000）だけ**書き込めば動作します（bootloader/partitions は既存のまま）。

## クレジット

本ファirmwareは、原版の[qPocketPCR](https://github.com/GaudiLabs/qPocketPCR)（GaudiLabs、Urs Gaudenz）をベースにした独立したフォーク **qPocketPCR-wHRM v0.1** です。原版はそのまま **V1.1** を維持し、本フォークで高分解能融解曲線（HRM）・128 KB USB Mass Storage・WiFi によるWeb制御・名前付きプロトコル保存を追加しました。

- 元プロジェクト: qPocketPCR — GaudiLabs / Urs Gaudenz（GPL-3.0）
- フーク・拡張: © 高倉耕一（GPL-3.0）

## ライセンス

このプロジェクトは **GNU General Public License v3.0 (GPL-3.0)** でライセンスされています。これはオリジナルの [qPocketPCR](https://github.com/GaudiLabs/qPocketPCR) リポジトリと同じライセンスです。

詳細は [LICENSE](LICENSE) または <https://www.gnu.org/licenses/gpl-3.0.html> を参照してください。
