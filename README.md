# qPocketPCR — HRM Analysis & USB Storage Extension

This repository is derived from the [qPocketPCR](https://github.com/GaudiLabs/qPocketPCR) project by GaudiLabs (Urs Gaudenz, 2025).

We extracted only the `Software/pPocketPCR_Main` firmware directory and added the following features:

## Added Features

### 1. High Resolution Melting (HRM) Analysis

The firmware now supports a **MELT** block in `PROTOCOL.TXT` that runs a fine temperature ramp with fluorescence capture after (or instead of) the PCR amplification. This enables:

- **Melt curve acquisition** — fluorescence vs. temperature at user-defined increments (e.g. 0.2 °C)
- **Tm detection** — the analysis scripts (`analysis/`) automatically separate the qPCR and melt segments, then compute the melting temperature (Tm) as the peak of −dF/dT
- **Amplification-then-melt** or **melt-only** runs (set `CYCLES: 0` for melt-only)

The melt steps are expanded into the internal `steps[]` array after the repeat region, so the existing state machine runs them exactly once when the PCR cycles end.

### 2. USB Storage Capacity Increase

The virtual USB Mass Storage disk size has been increased to **128 KB** (256 sectors × 512 bytes), with the FAT12 layout dynamically rebuilt to allocate all remaining clusters to `DATAQPCR.TXT`. This allows storing larger datasets — a full 45-cycle qPCR run plus a 151-point HRM ramp fits comfortably.

## PROTOCOL.TXT Format

Place a `PROTOCOL.TXT` file on the USB drive (the device presents itself as a FAT12 mass-storage device). The protocol is parsed at startup.

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

## Data Output

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

The `analysis/` directory contains Python scripts for downstream processing:

- `scripts/run_qpcr.py` — full qPCR + HRM workflow
- `scripts/make_synthetic_data.py` — synthetic data generator for validation
- `scripts/qpcr_core.py` — core library (baseline, Ct, standard curve, melt analysis)

See `analysis/README.md` for details.

## USB Drive Layout

The device presents a 128 KB FAT12 filesystem with two files:

| File | Purpose |
|------|---------|
| `PROTOCOL.TXT` | Protocol uploaded from the host (read/write) |
| `DATAQPCR.TXT` | Measurement data written by the device |

The FAT12 table is rebuilt on every file update so that all remaining clusters are chained into `DATAQPCR.TXT`, maximizing available space for data.

## License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**, matching the license of the original [qPocketPCR](https://github.com/GaudiLabs/qPocketPCR) repository.

See [LICENSE](LICENSE) or <https://www.gnu.org/licenses/gpl-3.0.html> for the full text.
