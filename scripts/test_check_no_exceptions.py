#!/usr/bin/env python3
"""Offband #2287: tests for check_no_exceptions.py.

Run:  python scripts/test_check_no_exceptions.py
Exit: 0 on pass, nonzero with a clear failure message otherwise.
Stdlib only, no PlatformIO: each case feeds the REAL evaluate() a resolved config
in the shape `pio project config --json-output` returns.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_no_exceptions as chk  # noqa: E402

FAILURES = []
ESP = "platformio/espressif32@6.11.0"
GOOD_FLAGS = ["-w -DNDEBUG", "-D ESP32_PLATFORM", "-fno-exceptions"]


def expect(condition, label):
    if condition:
        print("  pass  %s" % label)
    else:
        print("  FAIL  %s" % label)
        FAILURES.append(label)


def env(name, platform=ESP, unflags="-fexceptions", flags=None):
    opts = [["platform", platform], ["build_flags", GOOD_FLAGS if flags is None else flags]]
    if unflags is not None:
        opts.append(["build_unflags", unflags])
    return ["env:" + name, opts]


def main():
    print("positive: the patterns that must pass")
    n, f = chk.evaluate([env("a"), ["esp32_base", [["build_unflags", "-fexceptions"]]]])
    expect(n == 1 and f == [], "an env with the base's flags is clean, non-env sections skipped")
    n, f = chk.evaluate([env("kiss", unflags=["-fexceptions", "-DARDUINO_USB_MODE=0"])])
    expect(f == [], "an env that chains the base unflag ahead of its own is clean")
    n, f = chk.evaluate([env("tlora", unflags="-fexceptions -Os")])
    expect(f == [], "the one-line chained form is clean")
    n, f = chk.evaluate([env("nrf", platform="nordicnrf52", unflags=None, flags=["-w"])])
    expect(n == 0 and f == [], "a non-ESP32 env is not checked")
    n, f = chk.evaluate([env("c6", platform="https://github.com/pioarduino/platform-espressif32/"
                                           "releases/download/53.03.13-1/platform-espressif32.zip")])
    expect(n == 1 and f == [], "a pioarduino URL platform counts as ESP32")

    print("negative: the defects that must fail")
    n, f = chk.evaluate([env("kiss", unflags="-DARDUINO_USB_MODE=0")])
    expect(len(f) == 1 and "${esp32_base.build_unflags}" in f[0],
           "an env whose own build_unflags dropped the base's is a finding")
    n, f = chk.evaluate([env("bare", unflags=None)])
    expect(len(f) == 1 and "build_unflags" in f[0], "no build_unflags at all is a finding")
    n, f = chk.evaluate([env("noflag", flags=["-w", "-D ESP32_PLATFORM"])])
    expect(len(f) == 1 and "-fno-exceptions" in f[0], "build_flags without -fno-exceptions is a finding")
    n, f = chk.evaluate([env("back", flags=GOOD_FLAGS + ["-fexceptions"])])
    expect(len(f) == 1 and "adds -fexceptions back" in f[0], "adding -fexceptions back is a finding")
    n, f = chk.evaluate([env("commented", unflags="; -fexceptions")])
    expect(len(f) == 1, "an unflag that is only in a ; comment does not count")
    n, f = chk.evaluate([env("x", unflags="-fexceptions-foo")])
    expect(len(f) == 1, "a flag that merely starts with -fexceptions does not count")

    if FAILURES:
        print("%d FAILED" % len(FAILURES))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
