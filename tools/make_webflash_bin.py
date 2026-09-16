#!/usr/bin/env python3
"""Create web-flash .bin files for the ESP32-S2 (Adafruit WebSerial ESP Tool).

Given the individual images produced by Arduino IDE / PlatformIO, this writes,
into the output directory:

  bootloader.bin   -> flash at 0x1000
  partitions.bin   -> flash at 0x8000
  boot_app0.bin    -> flash at 0xE000  (optional; auto-detected if omitted)
  firmware.bin     -> flash at 0x10000
  qPocketPCR-wHRM_merged.bin -> single image, flash at 0x0

Arduino IDE (Sketch > Export Compiled Binary):
  python tools/make_webflash_bin.py \
    --app       build/pPocketPCR_Main.ino.bin \
    --bootloader build/pPocketPCR_Main.ino.bootloader.bin \
    --partitions build/pPocketPCR_Main.ino.partitions.bin \
    -o dist

PlatformIO:
  python tools/make_webflash_bin.py \
    --app       .pio/build/esp32_s2_usb_native/firmware.bin \
    --bootloader .pio/build/esp32_s2_usb_native/bootloader.bin \
    --partitions .pio/build/esp32_s2_usb_native/partitions.bin \
    -o dist
"""
import argparse
import glob
import os
import shutil
import sys

BOOTLOADER_OFFSET = 0x1000
PARTITIONS_OFFSET = 0x8000
BOOT_APP0_OFFSET = 0xE000
APP_OFFSET = 0x10000


def find_boot_app0(explicit=None):
    """Return a path to boot_app0.bin, or None if it cannot be found."""
    if explicit:
        return explicit
    candidates = []
    localappdata = os.environ.get("LOCALAPPDATA")
    if localappdata:
        candidates += glob.glob(os.path.join(
            localappdata, "Arduino15", "packages", "esp32", "hardware",
            "esp32", "*", "tools", "partitions", "boot_app0.bin"))
    candidates += glob.glob(os.path.join(
        os.path.expanduser("~"), ".platformio", "packages",
        "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin"))
    candidates += glob.glob(
        "/mnt/c/Users/*/AppData/Local/Arduino15/packages/esp32/hardware/"
        "esp32/*/tools/partitions/boot_app0.bin")
    candidates = [c for c in candidates if os.path.isfile(c)]
    if not candidates:
        return None
    # Prefer the newest installed core/tool version.
    candidates.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    return candidates[0]


def read_bytes(path):
    with open(path, "rb") as handle:
        return handle.read()


def merge(parts, out_path):
    """parts: list of (offset, bytes). Returns the merged size."""
    end = max(off + len(data) for off, data in parts)
    buf = bytearray(b"\xff" * end)
    for off, data in parts:
        buf[off:off + len(data)] = data
    with open(out_path, "wb") as handle:
        handle.write(buf)
    return end


def main():
    parser = argparse.ArgumentParser(
        description="Create ESP32-S2 web-flash binaries and a merged image.")
    parser.add_argument("--app", required=True,
                        help="app image: sketch .ino.bin or PlatformIO firmware.bin")
    parser.add_argument("--bootloader", required=True)
    parser.add_argument("--partitions", required=True)
    parser.add_argument("--boot-app0", default=None,
                        help="boot_app0.bin (auto-detected if omitted)")
    parser.add_argument("-o", "--outdir", default="dist")
    args = parser.parse_args()

    for path in (args.app, args.bootloader, args.partitions):
        if not os.path.isfile(path):
            sys.exit("file not found: %s" % path)

    boot_app0 = find_boot_app0(args.boot_app0)
    if boot_app0:
        print("boot_app0.bin: %s" % boot_app0)
    else:
        print("WARNING: boot_app0.bin not found; merged image will omit 0xE000")

    os.makedirs(args.outdir, exist_ok=True)

    outputs = {
        "bootloader.bin": args.bootloader,
        "partitions.bin": args.partitions,
        "firmware.bin": args.app,
    }
    if boot_app0:
        outputs["boot_app0.bin"] = boot_app0
    for name, src in outputs.items():
        shutil.copyfile(src, os.path.join(args.outdir, name))
        print("wrote %-26s %8d bytes" % (name, os.path.getsize(src)))

    parts = [
        (BOOTLOADER_OFFSET, read_bytes(args.bootloader)),
        (PARTITIONS_OFFSET, read_bytes(args.partitions)),
        (APP_OFFSET, read_bytes(args.app)),
    ]
    if boot_app0:
        parts.append((BOOT_APP0_OFFSET, read_bytes(boot_app0)))

    merged_name = "qPocketPCR-wHRM_merged.bin"
    merged_size = merge(parts, os.path.join(args.outdir, merged_name))
    print("wrote %-26s %8d bytes" % (merged_name, merged_size))

    print("\nAdafruit WebSerial ESP Tool - flash each file at its offset:")
    print("  0x1000    bootloader.bin")
    print("  0x8000    partitions.bin")
    if boot_app0:
        print("  0xE000    boot_app0.bin")
    print("  0x10000   firmware.bin")
    print("\nOr flash the single merged image at:")
    print("  0x0       %s" % merged_name)


if __name__ == "__main__":
    main()
