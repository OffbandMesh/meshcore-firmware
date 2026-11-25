#!/usr/bin/env python3
"""Offband #2298: every shipped or CI-built 4 MB ESP32 env uses offband_4MB.csv.

Why this exists
---------------
The 4 MB boards (Xiao ESP32-C3, Xiao ESP32-C6, MeshSmith Photon C6) moved to one
partition table in #2295: two 1,728 KB app slots and 512 KB of SPIFFS. Either
table they used before reopens a defect:

* the board default leaves a 1,280 KB app slot, which the C3 repeater and room
  server overflow on pioarduino 55.x;
* min_spiffs.csv leaves 128 KB of SPIFFS, which holds about 107 of the 350
  contacts those builds allow. That one is silent: contacts just stop saving.

A board on two different tables is also a hazard on its own: SPIFFS sits at a
different offset in each, so a USB flash between them erases the node.

What it checks
--------------
Every env named in .github/release-envs.txt or in ci.yml, on an ESP32 platform,
whose flash is 4 MB, must resolve board_build.partitions = offband_4MB.csv and
define OFFBAND_4MB_TABLE, which tells the firmware the layout it should find at
boot (#2297). The firmware's copy of that layout, src/helpers/esp32/
Offband4MBTable.h, must match offband_4MB.csv.

The flash size comes from, in order: the env's own board_upload.flash_size; the
repo's boards/<board>.json; PLATFORM_BOARD_FLASH below, for boards that come from
the platform package (config-lint doesn't install platforms, so their JSON isn't
there). An env on a board found in none of them FAILS, naming the board. A new
board can't slip past this check by being unknown.

Checking the RESOLVED config (not grepping the .ini files) is deliberate: the
table arrives through `extends` chains, and only PlatformIO's own interpolation
knows what an env really ends up with.

Run from the repo root:  python scripts/check_4mb_partitions.py
Exit: 0 clean, 1 on any finding, 2 if the config could not be read.
"""

import json
import os
import re
import subprocess
import sys

TABLE = "offband_4MB.csv"
FOUR_MB = "4MB"

# upload.flash_size from each board's JSON in the platform package, for the
# platform-supplied boards a shipped or CI ESP32 env uses. Read from the
# platform's own boards/*.json, not assumed.
PLATFORM_BOARD_FLASH = {
    "esp32-c6-devkitm-1": "4MB",
    "esp32-s3-devkitc-1": "8MB",
    "heltec_wifi_lora_32_V2": "8MB",
    "seeed_xiao_esp32c3": "4MB",
    "seeed_xiao_esp32s3": "8MB",
}

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def flatten(value):
    if value is None:
        return ""
    if isinstance(value, (list, tuple)):
        return "\n".join(str(v) for v in value)
    return str(value)


def option(value):
    """A single-valued option, with any ; comment removed."""
    return flatten(value).split(";", 1)[0].strip()


def size_bytes(text):
    """A size as PlatformIO or a partition CSV writes it: 4MB, 4M, 1728K, 0x1B0000, 4194304."""
    if text is None:
        return None
    t = str(text).strip().upper()
    if not t:
        return None
    for suffix, mult in (("MB", 1 << 20), ("M", 1 << 20), ("KB", 1 << 10), ("K", 1 << 10)):
        if t.endswith(suffix) and not t.startswith("0X"):
            return int(t[: -len(suffix)], 0) * mult
    return int(t, 0)


FOUR_MB_BYTES = 4 << 20


def is_esp32(platform):
    p = option(platform)
    return "espressif32" in p or "pioarduino" in p


def shipped_envs(release_text):
    out = set()
    for line in release_text.splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.add(line.split()[0])
    return out


def ci_envs(ci_text, names):
    """Env names that appear in ci.yml as whole words."""
    return {n for n in names
            if re.search(r"(?<![\w-])" + re.escape(n) + r"(?![\w-])", ci_text)}


def repo_board_flash(board, boards_dir):
    path = os.path.join(boards_dir, board + ".json")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as fh:
        return json.load(fh).get("upload", {}).get("flash_size")


def flash_size(opts, boards_dir, platform_map):
    own = option(opts.get("board_upload.flash_size"))
    if own:
        return own
    board = option(opts.get("board"))
    return repo_board_flash(board, boards_dir) or platform_map.get(board)


def evaluate(config, wanted, boards_dir, platform_map=None):
    """Return (checked_4mb, findings) for a `pio project config --json-output` list.

    wanted: the env names to check (shipped or built by CI).
    """
    platform_map = PLATFORM_BOARD_FLASH if platform_map is None else platform_map
    envs = {s[4:]: dict(o) for s, o in config if s.startswith("env:")}
    findings, checked = [], 0
    for name in sorted(wanted):
        opts = envs.get(name)
        if opts is None or not is_esp32(opts.get("platform")):
            continue
        size = flash_size(opts, boards_dir, platform_map)
        if size is None:
            findings.append(
                "%s: flash size of board '%s' is unknown. Add it to PLATFORM_BOARD_FLASH "
                "in this script, from the platform's boards/%s.json." % (
                    name, option(opts.get("board")), option(opts.get("board"))))
            continue
        try:
            nbytes = size_bytes(size)
        except ValueError:
            findings.append("%s: flash size '%s' could not be read." % (name, size))
            continue
        if nbytes != FOUR_MB_BYTES:
            continue
        checked += 1
        table = os.path.basename(option(opts.get("board_build.partitions")) or "(board default)")
        if table != TABLE:
            findings.append(
                "%s: a 4 MB board on %s. Shipped 4 MB ESP32 boards use %s (#2295)." % (
                    name, table, TABLE))
        if MACRO not in build_flag_tokens([flatten(opts.get("build_flags")),
                                           flatten(opts.get("build_src_flags"))]):
            findings.append(
                "%s: build_flags lacks -D %s, so the firmware can't tell it is on the old "
                "table after an OTA (#2297)." % (name, MACRO))
    return checked, findings


MACRO = "OFFBAND_4MB_TABLE"


def build_flag_tokens(value):
    """The macro names a build_flags value defines, from -D NAME, -DNAME and -D NAME=x."""
    names, pending = set(), False
    for line in flatten(value).splitlines():
        for tok in line.split(";", 1)[0].split():
            if pending:
                names.add(tok.split("=", 1)[0])
                pending = False
            elif tok == "-D":
                pending = True
            elif tok.startswith("-D"):
                names.add(tok[2:].split("=", 1)[0])
    return names


def csv_layout(csv_text):
    """(app0 size, spiffs offset, spiffs size) from a partition CSV."""
    rows = {}
    for line in csv_text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) >= 5:
            rows[cols[0]] = (size_bytes(cols[3]), size_bytes(cols[4]))
    return rows["app0"][1], rows["spiffs"][0], rows["spiffs"][1]


def header_layout(header_text):
    """The three constants from src/helpers/esp32/Offband4MBTable.h."""
    def grab(name):
        m = re.search(r"\b%s\s*=\s*(0x[0-9A-Fa-f]+|\d+)" % name, header_text)
        if not m:
            raise ValueError("%s not found in Offband4MBTable.h" % name)
        return int(m.group(1), 0)
    return (grab("kOffband4MBAppSize"), grab("kOffband4MBSpiffsOffset"),
            grab("kOffband4MBSpiffsSize"))


def compare_layouts(csv_text, header_text):
    """Findings when the firmware's copy of the layout differs from the CSV."""
    try:
        c, h = csv_layout(csv_text), header_layout(header_text)
    except (KeyError, ValueError, IndexError) as exc:
        return ["could not read the layout: %s" % exc]
    names = ("app0 size", "spiffs offset", "spiffs size")
    return ["%s: %s says %#x, Offband4MBTable.h says %#x. They must match (#2297)." % (
        n, TABLE, a, b) for n, a, b in zip(names, c, h) if a != b]


def read_text(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as fh:
        return fh.read()


def load_config():
    raw = subprocess.check_output(["pio", "project", "config", "--json-output"],
                                  stderr=subprocess.STDOUT, cwd=ROOT)
    return json.loads(raw.decode("utf-8", "replace"))


def main():
    try:
        config = load_config()
    except (subprocess.CalledProcessError, OSError, ValueError) as exc:
        out = getattr(exc, "output", b"")
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        print("ERROR: could not read PlatformIO config:\n%s" % (out or exc))
        return 2
    try:
        release_text = read_text(".github", "release-envs.txt")
        ci_text = read_text(".github", "workflows", "ci.yml")
        csv_text = read_text(TABLE)
        header_text = read_text("src", "helpers", "esp32", "Offband4MBTable.h")
    except OSError as exc:
        print("ERROR: %s" % exc)
        return 2
    names = {s[4:] for s, _ in config if s.startswith("env:")}
    wanted = shipped_envs(release_text) | ci_envs(ci_text, names)
    checked, findings = evaluate(config, wanted, os.path.join(ROOT, "boards"))
    findings += compare_layouts(csv_text, header_text)
    if findings:
        for f in findings:
            print("FAIL  " + f)
        print("%d finding(s)." % len(findings))
        return 1
    if checked == 0:
        print("FAIL: no shipped or CI 4 MB ESP32 env found, so nothing was checked.")
        return 1
    print("OK: all %d shipped or CI 4 MB ESP32 env(s) use %s." % (checked, TABLE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
