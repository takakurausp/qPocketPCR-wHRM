# qPocketPCR-wHRM — ESP32-S2 eDNA qPCR + HRM Device

A compact, self-contained quantitative PCR (qPCR) device with **High-Resolution Melting (HRM)** support, built on the [qPocketPCR](https://github.com/GaudiLabs/qPocketPCR) project by GaudiLabs (Urs Gaudenz, 2025).

This repository extracts only the `Software/pPocketPCR_Main` firmware directory from the original project and adds:

1. **High-Resolution Melting (HRM)** — a fine temperature ramp with fluorescence capture after (or instead of) PCR amplification
2. **USB Mass Storage** — a 128 KB FAT12 virtual drive exposing `PROTOCOL.TXT` and `DATAQPCR.TXT`
3. **Web control over WiFi** — build protocols, start/stop runs, upload/download files from any browser
4. **Named protocol library** — save/load multiple protocols directly on the device

## Hardware Overview

| Component | Part / Detail |
|-----------|---------------|
| MCU | ESP32-S2 (native USB: CDC serial + MSC mass storage) |
| Display | TFT touchscreen (TFT_eSPI) |
| Temperature | TLA202x voltage amplifier reading thermistors/heaters |
| Fluorescence | 8-channel photodiode array, corrected by camera baseline/wellFactor |
| Camera | OV2640-style sensor for fluorescence imaging (used during measurement only) |
| LEDs | LED driver (TLC59108) for excitation illumination |
| Storage | SPIFFS on flash + virtual 128 KB FAT12 USB drive |

## Features

### HRM (High-Resolution Melting)

The firmware supports a **MELT** block in `PROTOCOL.TXT` that runs a fine temperature ramp with fluorescence capture after (or instead of) the PCR amplification. This enables:

- **Melt curve acquisition** — fluorescence vs. temperature at user-defined increments (e.g. 0.2 °C)
- **Tm detection** — analysis scripts automatically separate the qPCR and melt segments, then compute the melting temperature (Tm) as the peak of −dF/dT
- **Amplification-then-melt** or **melt-only** runs (`CYCLES: 0` for melt-only)

The melt steps are expanded into the internal `steps[]` array after the repeat region, so the existing state machine runs them exactly once when the PCR cycles end.

### USB Mass Storage (128 KB FAT12)

The virtual USB Mass Storage disk size is **128 KB** (256 sectors × 512 bytes), with a FAT12 layout dynamically rebuilt to allocate all remaining clusters to `DATAQPCR.TXT`. This stores larger datasets — a full 45-cycle qPCR run plus a 151-point HRM ramp fits comfortably.

The FAT table is rebuilt on every file update so that all remaining clusters are chained into the data file, maximizing available space.

### Web Control over WiFi (Access Point)

On startup the device broadcasts an access point:

- **SSID:** `qPocketPCR`
- **Password:** `12345678`

Connect a computer or phone to this network, then open a browser to the device IP (default `192.168.4.1`). Available endpoints:

| URL | Purpose |
|-----|---------|
| `/` | Status page — mode, progress, step, temperature; links to control and results |
| `/builder` | Interactive **Protocol Builder** (see below) |
| `/status` | JSON status (`mode`, `progress`, `step`, `temp`, `cycle`) for polling / dashboards |
| `/start` | Start the current protocol (loads `PROTOCOL.TXT`, runs buffer-limit check) |
| `/stop` | Request a stop of the current run |
| `/download` | Download `DATAQPCR.TXT` as a file attachment |
| `/upload` (POST, multipart) | Upload a new `PROTOCOL.TXT` |

### Protocol Builder (`/builder`)

A single-page web app embedded in the firmware that lets you:

- Build a temperature profile step-by-step with a live preview chart
- Configure the **Melt (HRM)** block (`MELT FROM / TO / INC / HOLD`)
- Save the current protocol as `PROTOCOL.TXT` and upload it to the device
- **Save named protocols** — store multiple protocols on the device and reload any of them by name

Named protocols are managed through these endpoints:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/listproto` | GET | Returns newline-separated `id\|name` lines for saved protocols |
| `/loadproto?id=N` | GET | Loads protocol N into the running protocol and returns its raw text |
| `/saveproto` | POST | Saves a named protocol (`name`, `protocol` fields); returns the updated list |

Up to **99 named protocols** can be stored (as `PROTO_0N.txt` files on SPIFFS).

### Data Output

The device writes `DATAQPCR.TXT` (also accessible as `DATA.TXT`) to the USB drive in CSV format:

```
Protocol name: My Protocol
Cycle, Time, Temp, Sensor1, Sensor2, Sensor3, Sensor4, Sensor5, Sensor6, Sensor7, Sensor8
1, 12, 95.2, 1523.4, 1480.1, ...
```

- **Cycle** — PCR cycle number (1 during melt)
- **Time** — seconds since run start
- **Temp** — measured block temperature at capture (required for HRM)
- **Sensor1–8** — fluorescence values for 8 wells (wellFactor-corrected)

## PROTOCOL.TXT Format

Place a `PROTOCOL.TXT` file on the USB drive (the device presents itself as a FAT12 mass-storage device). The protocol is parsed at startup. You can also create it with the web **Protocol Builder** (`/builder`).

### Basic format (amplification only)

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

### Adding a melt (HRM) block

Append the following lines **after** the last STEP to run a melt ramp after amplification:

```
MELT FROM: 65.0
MELT TO: 95.0
MELT INC: 0.2
MELT HOLD: 2
```

- `MELT FROM` — starting temperature (°C)
- `MELT TO` — ending temperature (°C)
- `MELT INC` — temperature increment per point (°C). Default: 0.2
- `MELT HOLD` — hold time per point (seconds). Default: 1

Each melt point becomes a capture step. For the example above, this produces 151 capture steps (65.0, 65.2, …, 95.0 °C).

### Melt-only run

Set `CYCLES: 0` and include only the MELT block:

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

### Field reference

| Field | Description |
|-------|-------------|
| `NAME:` | Protocol name (logged to DATA.TXT) |
| `DATE:` | Date string (free form) |
| `REPEAT: start-end` | Cycle range that repeats (1-based) |
| `CYCLES:` | Number of PCR cycles (0 for melt-only) |
| `STEP n: name` | Step definition (1-based, up to 200) |
| `TEMPERATURE:` | Target temperature (°C) |
| `DURATION:` | Hold time. Units: `min`, `s`, or bare number (seconds) |
| `CAPTURE: yes` | Capture fluorescence during this step |
| `MELT FROM:` | Melt ramp start temperature |
| `MELT TO:` | Melt ramp end temperature |
| `MELT INC:` | Temperature step (default 0.2 °C) |
| `MELT HOLD:` | Seconds per melt point (default 1 s) |

## Storage Layout

The device uses two storage areas:

### Virtual USB drive (RAM-backed FAT12 image)

The "USB drive" is a **128 KB FAT12 disk image held in PSRAM**, exposed to the host as a removable mass-storage device. It contains only two files:

| File | Purpose |
|------|---------|
| `PROTOCOL.TXT` | Protocol uploaded from the host (read/write) |
| `DATAQPCR.TXT` | Measurement data written by the device |

The image lives in RAM and is **volatile** — it is persisted to SPIFFS (`/my_array.bin`) whenever a write or eject event occurs. On eject ("Remove device"), the disk image is now written to SPIFFS immediately, so an incomplete transfer can no longer lose data even if the cable is pulled right away.

### SPIFFS (flash)

SPIFFS holds non-volatile files:

| File | Purpose |
|------|---------|
| `/my_array.bin` | Persisted copy of the 128 KB USB disk image (~128 KB) |
| `/PROTO_0N.txt` | Named protocol text files (up to 99) |
| `/PROTOLIST.TXT` | Management file listing `id:name` for saved protocols |
| `/base.bin` | Camera baseline correction (~75 KB) |
| `/mask.bin` | Pixel mask for fluorescence ROI selection, packed as ~9.4 KB (was 75 KB) |

The pixel mask (`/mask.bin`) is stored **packed** (one bit per pixel, MSB-first, with a magic byte header). The in-RAM mask stays a plain boolean array; only the on-disk form is packed, saving roughly 66 KB of flash. Legacy byte-packed masks are detected via the missing magic byte and re-initialized automatically.

## Analysis

The `analysis/` directory contains Python scripts for downstream processing:

- `scripts/run_qpcr.py` — full qPCR + HRM workflow (entry point)
- `scripts/make_synthetic_data.py` — synthetic data generator for validation
- `scripts/make_calibration.py` — generate calibration files from a dilution series
- `scripts/qpcr_core.py` — core library (baseline, Ct, standard curve, melt analysis)

See [`analysis/README.md`](analysis/README.md) for the full eDNA qPCR workflow, config format, and HRM/Tm details.

## Building

The firmware is built with [PlatformIO](https://platformio.org/) targeting `esp32-s2-usb-native`:

```bash
pio run                 # build firmware.bin
pio run -t upload       # flash to device (connect via USB)
pio monitor             # serial console at 115200 baud
```

### Flashing with the Adafruit WebSerial ESP Tool

If you prefer to flash over a browser instead of `pio upload`, use the [Adafruit WebSerial ESP Tool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/). It has five slots, but only **four** are used for a complete flash:

| Slot | File | Offset (hex) |
|------|------|--------------|
| 1st | `bootloader.bin` | `0x1000` |
| 2nd | `partitions.bin` | `0x8000` |
| 3rd | `boot_app0.bin` | `0xE000` |
| 4th | `firmware.bin` | `0x10000` |

Leave the last slot empty (offset stays `0`). The `bootloader.bin`, `partitions.bin` and `firmware.bin` are in `.pio/build/esp32_s2_usb_native/`:

- `bootloader.bin` (~14.8 KB)
- `partitions.bin` (~3 KB)
- `firmware.bin` (~997 KB — the main image)

`boot_app0.bin` is not generated per project; take it from the framework package:
`~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin`
(or, for Arduino IDE, `%LOCALAPPDATA%/Arduino15/packages/esp32/hardware/esp32/<version>/tools/partitions/boot_app0.bin`).

Alternatively, use `tools/make_webflash_bin.py` to copy these files into `dist/` and also build a **single** merged image to flash at `0x0`:

```bash
python tools/make_webflash_bin.py \
  --app        .pio/build/esp32_s2_usb_native/firmware.bin \
  --bootloader .pio/build/esp32_s2_usb_native/bootloader.bin \
  --partitions .pio/build/esp32_s2_usb_native/partitions.bin \
  -o dist
```

For Arduino IDE, first use **Sketch → Export Compiled Binary**, then pass `pPocketPCR_Main.ino.bin`, `pPocketPCR_Main.ino.bootloader.bin` and `pPocketPCR_Main.ino.partitions.bin`.

**Steps:**

1. Open [https://adafruit.github.io/Adafruit_WebSerial_ESPTool/](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) in a browser.
2. Select **Baud** (recommended **460800**, or 115200 if unavailable).
3. Click **Connect** with the ESP32-S2 connected via USB.
4. For each slot, use **"Choose a file…"** to pick the bin above and enter its offset in the Offset field. (If you built the merged image, put it in the **first** slot at `0x0` and leave the rest empty.)
5. Press **Program** (Erase is done automatically).

**Why these offsets:** `bootloader.bin` is at `0x1000` on the ESP32-S2, the `partitions.bin` partition table is fixed at `0x8000`, the OTA data seed `boot_app0.bin` goes at `0xE000`, and `firmware.bin` is the app image loaded at `0x10000` (the start of the standard firmware region). The entry address of this firmware is `0x40026b84`.

> ⚠️ **Notes**
> - Connect the ESP32-S2 to the PC via USB and confirm that **Connect** succeeds before flashing.
> - Because `firmware.bin` is large (~997 KB), it can take a few minutes even at 460800 baud.
> - If it does not all fit in one go, flashing just `firmware.bin` at `0x10000` is enough to run (the bootloader and partitions stay as they are).

## Credits

This firmware is **qPocketPCR-wHRM v0.1**, an independent fork of the original [qPocketPCR](https://github.com/GaudiLabs/qPocketPCR) (GaudiLabs, Urs Gaudenz), which remains at version **V1.1**. The fork adds High-Resolution Melting (HRM), 128 KB USB Mass Storage, WiFi web control, and named protocol storage.

- Original project: qPocketPCR — GaudiLabs / Urs Gaudenz (GPL-3.0)
- Fork & extensions: © Takakura Kōichi (GPL-3.0)

## License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**, matching the license of the original [qPocketPCR](https://github.com/GaudiLabs/qPocketPCR) repository.

See [LICENSE](LICENSE) or <https://www.gnu.org/licenses/gpl-3.0.html> for the full text.
