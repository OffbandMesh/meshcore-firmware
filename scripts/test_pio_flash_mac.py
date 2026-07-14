#!/usr/bin/env python3
"""Unit tests for pio-flash's esptool MAC parsing (#290).

Self-contained (no pytest needed):  python scripts/test_pio_flash_mac.py

Regression guard for #290: `esptool read_mac` on IEEE 802.15.4 chips
(ESP32-C6 / ESP32-H2) prints an 8-byte EUI-64 as the FIRST "MAC:" line; its
first 6 bytes carry the ff:fe EUI-64 fill and are NOT the device base MAC. The
real address is on the "BASE MAC:" line. The old naive regex mis-parsed this.

Fixtures use synthetic, locally-administered (02:..) MACs on purpose -- never a
real device MAC (SAFELANE redaction discipline; real MACs live only in the
gitignored hardware-devices.yaml). The output *shape* mirrors esptool v5.2.0 as
observed on an ESP32-C6 (2026-07-14).
"""
from __future__ import annotations

import importlib.util
import pathlib
import re
import sys

SCRIPT = pathlib.Path(__file__).resolve().parent / "pio-flash.py"
spec = importlib.util.spec_from_file_location("pio_flash", SCRIPT)
pio_flash = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pio_flash)
parse_base_mac = pio_flash.parse_base_mac

# --- Fixtures: esptool v5.2.0 output shapes (synthetic MACs) ------------------

# ESP32-C6 (802.15.4): 8-byte EUI-64 first, then the true BASE MAC.
C6 = """\
esptool v5.2.0
Connected to ESP32-C6 on COM23:
Chip type:          ESP32-C6FH4 (QFN32) (revision v0.2)
Features:           Wi-Fi 6, BT 5 (LE), IEEE802.15.4
Crystal frequency:  40MHz
USB mode:           USB-Serial/JTAG
MAC:                02:71:bc:ff:fe:12:34:56
BASE MAC:           02:71:bc:12:34:56
MAC_EXT:            ff:fe
"""

# ESP32-S3: plain 6-byte MAC, no BASE MAC line.
S3_PLAIN = """\
esptool v5.2.0
Connected to ESP32-S3 on COM10:
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
USB mode:           USB-Serial/JTAG
MAC:                02:cc:a8:12:34:56
"""

# Some esptool builds also print BASE MAC for non-802.15.4 chips; must agree.
S3_WITH_BASE = """\
Connected to ESP32-S3 on COM10:
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
MAC:                02:cc:a8:12:34:56
BASE MAC:           02:cc:a8:12:34:56
"""

# Uppercase in the wild -> normalized lowercase.
UPPER = "MAC:  AA:BB:CC:DD:EE:FF\n"

NO_MAC = "esptool v5.2.0\nConnected to ESP32-S3\nStub flasher running.\n"

# Noisy mid-line "BASE MAC:" must NOT be mistaken for the real field; only the
# line-anchored MAC line counts (guards the #292 anchoring hardening).
NOISY = (
    "Note: check BASE MAC: aa:bb:cc:dd:ee:ff on the sticker\n"
    "MAC:  02:dd:ee:11:22:33\n"
)

CASES = [
    ("C6 EUI-64 -> BASE MAC", C6, "02:71:bc:12:34:56"),
    ("S3 plain MAC", S3_PLAIN, "02:cc:a8:12:34:56"),
    ("S3 with BASE MAC", S3_WITH_BASE, "02:cc:a8:12:34:56"),
    ("uppercase normalized", UPPER, "aa:bb:cc:dd:ee:ff"),
    ("noisy mid-line BASE MAC ignored", NOISY, "02:dd:ee:11:22:33"),
    ("no MAC -> None", NO_MAC, None),
]


def main() -> int:
    failures = []

    # Guard: the OLD naive regex must be demonstrably wrong on C6 (documents #290).
    old = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", C6)
    assert old and old.group(1).lower() == "02:71:bc:ff:fe:12", (
        "fixture drift: old regex should reproduce the #290 bug"
    )

    for name, stdout, expected in CASES:
        got = parse_base_mac(stdout)
        ok = got == expected
        print(f"[{'PASS' if ok else 'FAIL'}] {name}: got={got!r} expected={expected!r}")
        if not ok:
            failures.append(name)

    if failures:
        print(f"\n{len(failures)} FAILED: {failures}")
        return 1
    print(f"\nAll {len(CASES)} cases passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
