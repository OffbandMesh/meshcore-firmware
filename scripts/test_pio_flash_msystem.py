#!/usr/bin/env python3
"""Unit tests for pio-flash's MSYSTEM handling (#2275).

Self-contained (no pytest needed):  python scripts/test_pio_flash_msystem.py

ESP-IDF's idf_tools.py refuses to start while MSYSTEM is set, and pioarduino runs
it to install tools into a fresh package store. pio-flash removes MSYSTEM from its
own environment at startup so every PlatformIO child it starts inherits a clean
one. These tests check the helper, and check that a real child process started
after it no longer sees the variable, while one started without it still does.
"""
from __future__ import annotations

import importlib.util
import os
import pathlib
import subprocess
import sys

SCRIPT = pathlib.Path(__file__).resolve().parent / "pio-flash.py"
spec = importlib.util.spec_from_file_location("pio_flash", SCRIPT)
pio_flash = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pio_flash)

FAILURES = []
PROBE = "import os,sys; sys.stdout.write(os.environ.get('MSYSTEM','<absent>') + '|' + os.environ.get('OFFBAND_PROBE','<absent>'))"


def expect(condition, label):
    print("  %s  %s" % ("pass" if condition else "FAIL", label))
    if not condition:
        FAILURES.append(label)


def child_sees(env):
    """What a real child process sees for MSYSTEM and a marker variable."""
    out = subprocess.run([sys.executable, "-c", PROBE], env=env, capture_output=True, text=True)
    return out.stdout


def main():
    print("the helper")
    env = {"MSYSTEM": "UCRT64", "PATH": "x", "OFFBAND_PROBE": "kept"}
    was = pio_flash.drop_msys_marker(env)
    expect(was is True and "MSYSTEM" not in env, "removes MSYSTEM and reports it was there")
    expect(env == {"PATH": "x", "OFFBAND_PROBE": "kept"}, "leaves every other variable as it was (got %r)" % env)
    env2 = {"PATH": "x"}
    expect(pio_flash.drop_msys_marker(env2) is False and env2 == {"PATH": "x"},
           "is a no-op when MSYSTEM is absent")

    print("a real child process")
    base = dict(os.environ, MSYSTEM="UCRT64", OFFBAND_PROBE="kept")
    expect(child_sees(base) == "UCRT64|kept", "without the helper, the child sees MSYSTEM (negative control)")
    cleaned = dict(base)
    pio_flash.drop_msys_marker(cleaned)
    expect(child_sees(cleaned) == "<absent>|kept",
           "after the helper, the child has no MSYSTEM and keeps the rest (got %r)" % child_sees(cleaned))

    print("wired into main()")
    src = SCRIPT.read_text(encoding="utf-8")
    body = src.split("def main() -> int:", 1)[1].split("args = build_parser()", 1)[0]
    expect("drop_msys_marker(os.environ)" in body, "main() drops MSYSTEM before parsing or starting anything")

    if FAILURES:
        print("\n%d failure(s)." % len(FAILURES))
        return 1
    print("\nall cases passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
