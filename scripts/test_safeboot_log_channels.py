"""SafeBoot's log lines reach the bench rig (#1211).

SafeBoot prints its battery reading early in boot, when nothing is listening on a
native-USB board's USB port. Sent through mesh_log_print(), the line also reaches
the raw UART log mirror that the bench rig reads. This guard fails if a SafeBoot
line goes back to a bare Serial.printf, or if the low-battery path sleeps before
the mirror has sent its line.
Run: python scripts/test_safeboot_log_channels.py
  or python -m pytest scripts/test_safeboot_log_channels.py -q
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = (ROOT / "src" / "SafeBoot.cpp").read_text()


def test_no_safeboot_line_prints_straight_to_serial():
    assert not re.search(r"Serial\.print(f|ln)?\s*\(\s*\"\[SafeBoot\]", SRC), \
        "a [SafeBoot] line bypasses MeshLog; use mesh_log_print(MLOG_BOOT, ...)"


def test_both_battery_lines_go_through_mesh_log_print():
    lines = re.findall(r"mesh_log_print\(\s*MLOG_BOOT\s*,\s*\"\[SafeBoot\] Vbat=%u mV (\w+)", SRC)
    assert sorted(lines) == ["below", "stable"], lines


def test_low_battery_path_drains_the_mirror_before_sleeping():
    below = SRC.index('"[SafeBoot] Vbat=%u mV below')
    drain = SRC.find("meshLogDrainUart();", below)
    sleep = SRC.find("enterSafeBootSleep(sleep_secs);", below)
    assert below < drain < sleep, \
        "the low-battery path must call meshLogDrainUart() after its line and before enterSafeBootSleep()"


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as e:
                failures += 1
                print(f"FAIL {name}: {e}")
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
