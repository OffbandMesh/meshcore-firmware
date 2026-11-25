#!/usr/bin/env python3
"""Offband #2298: tests for check_4mb_partitions.py.

Run:  python scripts/test_check_4mb_partitions.py
Exit: 0 on pass, nonzero with a clear failure message otherwise.
Stdlib only, no PlatformIO: each case feeds the REAL evaluate() a resolved config
in the shape `pio project config --json-output` returns.
"""

import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_4mb_partitions as chk  # noqa: E402

FAILURES = []
ESP = "platformio/espressif32@6.11.0"
C6 = "https://github.com/pioarduino/platform-espressif32/releases/download/53.03.13-1/platform-espressif32.zip"
PMAP = {"seeed_xiao_esp32c3": "4MB", "esp32-c6-devkitm-1": "4MB", "seeed_xiao_esp32s3": "8MB"}


def expect(condition, label):
    if condition:
        print("  pass  %s" % label)
    else:
        print("  FAIL  %s" % label)
        FAILURES.append(label)


def env(name, board="seeed_xiao_esp32c3", table="offband_4MB.csv", platform=ESP, flash=None,
        flags=("-w", "-D OFFBAND_4MB_TABLE")):
    opts = [["platform", platform], ["board", board], ["build_flags", list(flags)]]
    if table is not None:
        opts.append(["board_build.partitions", table])
    if flash is not None:
        opts.append(["board_upload.flash_size", flash])
    return ["env:" + name, opts]


def run(config, wanted, boards_dir):
    return chk.evaluate(config, set(wanted), boards_dir, PMAP)


def main():
    boards = tempfile.mkdtemp()
    with open(os.path.join(boards, "big_board.json"), "w", encoding="utf-8") as fh:
        json.dump({"upload": {"flash_size": "16MB"}}, fh)
    with open(os.path.join(boards, "small_board.json"), "w", encoding="utf-8") as fh:
        json.dump({"upload": {"flash_size": "4MB"}}, fh)

    print("positive: the patterns that must pass")
    n, f = run([env("c3"), env("c6", board="esp32-c6-devkitm-1", platform=C6)], ["c3", "c6"], boards)
    expect(n == 2 and f == [], "C3 and C6 on offband_4MB.csv are clean")
    n, f = run([env("c3", table="offband_4MB.csv ; comment")], ["c3"], boards)
    expect(n == 1 and f == [], "a trailing ; comment on the table is ignored")
    n, f = run([env("s3", board="seeed_xiao_esp32s3", table="default_8MB.csv")], ["s3"], boards)
    expect(n == 0 and f == [], "an 8 MB board on another table is out of scope")
    n, f = run([env("rcc6", board="esp32-c6-devkitm-1", table="default_16MB.csv", platform=C6,
                    flash="16MB")], ["rcc6"], boards)
    expect(n == 0 and f == [], "the env's own board_upload.flash_size beats the board default")
    n, f = run([env("v4", board="big_board", table="default_16MB.csv")], ["v4"], boards)
    expect(n == 0 and f == [], "a repo board JSON gives the flash size")
    n, f = run([env("c3_lab", table="min_spiffs.csv")], [], boards)
    expect(n == 0 and f == [], "an env neither shipped nor in CI is not checked")
    n, f = run([env("rak", board="wiscore_rak4631", table=None, platform="nordicnrf52")], ["rak"], boards)
    expect(n == 0 and f == [], "a non-ESP32 env is skipped, unknown board or not")

    print("negative: the defects this guards against")
    n, f = run([env("c3", table=None)], ["c3"], boards)
    expect(n == 1 and len(f) == 1 and "(board default)" in f[0],
           "a 4 MB C3 on the board default fails (the 55.x app-slot overflow)")
    n, f = run([env("c6", board="esp32-c6-devkitm-1", table="min_spiffs.csv", platform=C6)], ["c6"], boards)
    expect(n == 1 and len(f) == 1 and "min_spiffs.csv" in f[0],
           "a 4 MB C6 on min_spiffs.csv fails (the 128 KB SPIFFS contact limit)")
    n, f = run([env("x", board="small_board", table="default.csv")], ["x"], boards)
    expect(n == 1 and len(f) == 1, "a 4 MB repo board on another table fails")
    n, f = run([env("y", board="esp32-s3-devkitc-1", table="default_8MB.csv", flash="4MB")], ["y"], boards)
    expect(n == 1 and len(f) == 1, "an env that sets 4 MB itself is checked")
    n, f = run([env("z", board="mystery_board", table="whatever.csv")], ["z"], boards)
    expect(n == 0 and len(f) == 1 and "unknown" in f[0] and "mystery_board" in f[0],
           "a board of unknown flash size fails closed, naming the board")

    n, f = run([env("c3", flags=["-w"])], ["c3"], boards)
    expect(n == 1 and len(f) == 1 and "OFFBAND_4MB_TABLE" in f[0],
           "a 4 MB env without -D OFFBAND_4MB_TABLE fails (no old-table warning)")
    n, f = run([env("c3", flags=["-DOFFBAND_4MB_TABLE=1"])], ["c3"], boards)
    expect(n == 1 and f == [], "-DNAME=value is read as defining NAME")
    n, f = run([env("c3", flags=["-D OFFBAND_4MB_TABLE_X"])], ["c3"], boards)
    expect(len(f) == 1, "a macro that merely starts with the name doesn't count")

    print("layout: the firmware's copy against the CSV")
    csv_text = ("# c\nnvs, data, nvs, 0x9000, 0x5000,\n"
                "app0, app, ota_0, 0x10000, 0x1B0000,\n"
                "spiffs, data, spiffs, 0x370000, 0x80000,\n")
    hdr = ("constexpr uint32_t kOffband4MBAppSize = 0x1B0000;\n"
           "constexpr uint32_t kOffband4MBSpiffsOffset = 0x370000;\n"
           "constexpr uint32_t kOffband4MBSpiffsSize = 0x80000;\n")
    expect(chk.compare_layouts(csv_text, hdr) == [], "matching CSV and header are clean")
    f = chk.compare_layouts(csv_text.replace("0x370000", "0x360000"), hdr)
    expect(len(f) == 1 and "spiffs offset" in f[0], "a moved SPIFFS in the CSV alone fails")
    f = chk.compare_layouts(csv_text, hdr.replace("0x1B0000", "0x1A0000"))
    expect(len(f) == 1 and "app0 size" in f[0], "a changed slot in the header alone fails")
    f = chk.compare_layouts(csv_text, hdr.replace("kOffband4MBSpiffsSize", "kOther"))
    expect(len(f) == 1 and "could not read" in f[0], "a missing constant fails, not passes")
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(root, "offband_4MB.csv"), encoding="utf-8") as fh:
        real_csv = fh.read()
    with open(os.path.join(root, "src", "helpers", "esp32", "Offband4MBTable.h"), encoding="utf-8") as fh:
        real_hdr = fh.read()
    expect(chk.compare_layouts(real_csv, real_hdr) == [], "the repo's CSV and header match")

    print("parsing")
    s = chk.shipped_envs("# comment\nXiao_C3_repeater\n\n  heltec_v4_repeater   # trailing\n")
    expect(s == {"Xiao_C3_repeater", "heltec_v4_repeater"}, "release-envs.txt: comments and blanks skipped")
    c = chk.ci_envs("env: [Xiao_C3_repeater, Xiao_C3_repeater_diag]", {"Xiao_C3_repeater", "Xiao_C3", "x"})
    expect(c == {"Xiao_C3_repeater"}, "ci.yml: whole env names only, not prefixes")

    if FAILURES:
        print("\n%d FAILED" % len(FAILURES))
        return 1
    print("\nall passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
