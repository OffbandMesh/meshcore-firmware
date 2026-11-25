#!/usr/bin/env python
"""Offband #2278: measure ESP32 image size per chip and role, across two platforms.

Every platform bump runs this and attaches the grid to its PR, so growth is seen and
explained rather than discovered. It is the tool behind
docs/initiatives/standalone/2198-esp32-framework/SIZE-ANALYSIS.md.

Subcommands
-----------
  build   --tree <worktree> --out <dir> [--workers N] [env ...]
          Build envs (default: the matrix below) with a linker map. Each env's
          result, real image size, slot and RAM go to <dir>/results.jsonl, and its
          map is copied to <dir>/<env>.map, so a later config change that wipes
          .pio/build cannot lose the measurement.
  grid    --old <dir> --new <dir>
          A markdown table: image today vs new, growth, slot fill, left, static RAM.
  diff    --old <map> --new <map> [--sections .flash.text,...] [--skip-strings] [--top N]
          Bytes per library in the given sections, old vs new, largest growth first.
  pulled  --map <map> --pattern <regex>
          Which object pulled each matching archive member into the link, and why.
  spiffs  --mkspiffs <exe> --size <bytes> [--channels N] [--max-contacts N]
          The most companion contacts whose worst-case data set fits a SPIFFS
          partition of that size, with room to rewrite the contact file.

Notes
-----
- The image is firmware.bin, what an OTA writes. PlatformIO's own size figure leaves
  out .eh_frame. Where a build overflows and writes no .bin, the image is the sum of
  the loaded sections in the map, marked "~" (it matched the .bin within a few
  hundred bytes wherever both existed).
- Merged string pools (.rodata.*.str1.*) are listed under the first object the linker
  merged them into, with pre-merge sizes for the rest, so per-library attribution
  skips them under --skip-strings and section totals stay the source of truth.
- PlatformIO is started without MSYSTEM (#2275). It is never run verbose: that prints
  build flags, including secrets.
"""

import argparse
import concurrent.futures as cf
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections import defaultdict

# One board per chip, every shipped role (the analysis matrix).
MATRIX = [
    "Heltec_v3_companion_radio_ble", "Heltec_v3_companion_radio_usb", "Heltec_v3_repeater",
    "Heltec_v3_room_server", "Heltec_v3_companion_observer_wifi",
    "Xiao_C3_companion_radio_ble", "Xiao_C3_companion_radio_usb", "Xiao_C3_repeater", "Xiao_C3_room_server",
    "meshsmith_photon_esp32c6_e22p_30dbm_companion_radio_ble", "meshsmith_photon_esp32c6_e22p_30dbm_companion_radio_usb",
    "meshsmith_photon_esp32c6_e22p_30dbm_repeater", "meshsmith_photon_esp32c6_e22p_30dbm_room_server",
    "Heltec_v2_companion_radio_ble", "Heltec_v2_companion_radio_usb", "Heltec_v2_repeater", "Heltec_v2_room_server",
]

LOADED = {".iram0.text", ".iram0.vectors", ".dram0.data", ".flash.text", ".flash.rodata", ".eh_frame",
          ".eh_frame_hdr", ".flash.appdesc", ".rtc.text", ".rtc.data", ".rtc.force_fast"}
CODE = {".flash.text", ".iram0.text"}

_OUT_HDR = re.compile(r"^(\.[\w.]+)\s+0x[0-9a-f]+\s+0x([0-9a-f]+)")
_OUT_NAME = re.compile(r"^(\.[\w.]+)\s*$")
_IN_FULL = re.compile(r"^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")
_IN_CONT = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")
_IN_NAME = re.compile(r"^ (\.\S+)\s*$")
_STOP = ("Discarded input sections", "Allocating common", "Memory Configuration")
_ERR = re.compile(r"(error:|Error \d|fatal error|undefined reference|was not declared|no matching function|"
                  r"overflowed|Exception:|Traceback|\*\*\* \[)", re.I)


# ── map parsing ───────────────────────────────────────────────────────────────

def section_totals(map_text):
    """{output section: size} for every output section with an address and size."""
    out = {}
    for line in map_text.splitlines():
        m = _OUT_HDR.match(line)
        if m:
            out.setdefault(m.group(1), int(m.group(2), 16))
    return out


def loaded_bytes(map_text):
    """The image size as the sum of the loaded sections."""
    return sum(v for k, v in section_totals(map_text).items() if k in LOADED)


def owner(path):
    """Library that an input path belongs to: an archive name, a PlatformIO library
    folder, '[our src]', or the object's basename."""
    p = path.strip().replace("\\", "/")
    m = re.match(r"(.*?)\(([^)]*)\)$", p)
    if m:
        a = m.group(1)
        lm = re.search(r"\.pio/build/[^/]+/lib\w+/(?:lib)?([^/]+?)(?:\.a)?$", a)
        if lm:
            return "[lib] " + lm.group(1)
        return os.path.basename(a)
    m = re.search(r"\.pio/build/[^/]+/(.*)$", p) or re.match(r"(src/.*|lib\w*/.*)$", p)
    if m:
        rest = m.group(1)
        if rest.startswith("src/"):
            return "[our src]"
        parts = rest.split("/")
        return "[lib] " + (parts[1] if len(parts) > 2 else parts[0])
    return os.path.basename(p)


def attribute(map_text, sections, skip_strings=False):
    """{library: bytes} for input sections inside the given output sections."""
    sizes = defaultdict(int)
    out, pending = None, False
    started = False
    for line in map_text.splitlines():
        if not started:
            started = line.startswith("Linker script and memory map")
            continue
        m = _OUT_HDR.match(line) or _OUT_NAME.match(line)
        if m and not line.startswith(" "):
            out, pending = m.group(1), False
            continue
        if out not in sections:
            continue
        m = _IN_FULL.match(line)
        if m and not m.group(4).startswith("0x"):
            if not (skip_strings and ".str1." in m.group(1)):
                sz = int(m.group(3), 16)
                if sz and int(m.group(2), 16):
                    sizes[owner(m.group(4))] += sz
            pending = False
            continue
        n = _IN_NAME.match(line)
        if n:
            pending = not (skip_strings and ".str1." in n.group(1))
            continue
        if pending:
            c = _IN_CONT.match(line)
            if c:
                sz = int(c.group(2), 16)
                if sz and int(c.group(1), 16):
                    sizes[owner(c.group(3))] += sz
            pending = False
    return dict(sizes)


def pulled_by(map_text, pattern):
    """[(member, by, symbol)] from the archive-member section, for members matching pattern."""
    rx = re.compile(pattern)
    lines = []
    for line in map_text.splitlines():
        if line.startswith(_STOP):
            break
        lines.append(line)
    found = []
    for i in range(len(lines) - 1):
        a = lines[i]
        if a and not a.startswith(" ") and not a.startswith("Archive member") and rx.search(a):
            m = re.match(r"^\s+(\S+)\s+\((.*)\)\s*$", lines[i + 1])
            if m:
                found.append((a.strip(), m.group(1), m.group(2)))
    return found


# ── build ─────────────────────────────────────────────────────────────────────

def build_one(tree, out, env):
    log = os.path.join(out, env + ".log")
    child = dict(os.environ)
    child.pop("MSYSTEM", None)
    child["SCONSFLAGS"] = "-k"
    child["PLATFORMIO_BUILD_FLAGS"] = "-Wl,-Map,${BUILD_DIR}/firmware.map"
    t0 = time.time()
    with open(log, "w", encoding="utf-8", errors="replace") as fh:
        p = subprocess.run(["pio", "run", "-e", env], cwd=tree, stdout=fh, stderr=subprocess.STDOUT, env=child)
    text = open(log, encoding="utf-8", errors="replace").read()
    bdir = os.path.join(tree, ".pio", "build", env)
    image, estimated = None, False
    if os.path.isfile(os.path.join(bdir, "firmware.bin")):
        image = os.path.getsize(os.path.join(bdir, "firmware.bin"))
    mp = os.path.join(bdir, "firmware.map")
    if os.path.isfile(mp):
        shutil.copyfile(mp, os.path.join(out, env + ".map"))
        if image is None:
            image, estimated = loaded_bytes(open(mp, encoding="utf-8", errors="replace").read()), True
    ram = re.findall(r"RAM:\s+\[.*?\]\s+[\d.]+% \(used (\d+) bytes from (\d+) bytes\)", text)
    flash = re.findall(r"Flash:\s+\[.*?\]\s+[\d.]+% \(used (\d+) bytes from (\d+) bytes\)", text)
    errors = []
    for s in (x.strip() for x in text.splitlines()):
        if _ERR.search(s) and s not in errors:
            errors.append(s[:400])
    return {"env": env, "ok": p.returncode == 0, "rc": p.returncode, "secs": round(time.time() - t0),
            "image": image, "image_estimated": estimated,
            "slot": int(flash[-1][1]) if flash else None,
            "ram": int(ram[-1][0]) if ram else None, "ram_max": int(ram[-1][1]) if ram else None,
            "errors": errors[:8]}


def cmd_build(a):
    os.makedirs(a.out, exist_ok=True)
    envs = a.envs or MATRIX
    with open(os.path.join(a.out, "results.jsonl"), "a", encoding="utf-8") as res:
        with cf.ThreadPoolExecutor(max_workers=max(1, a.workers)) as ex:
            for r in ex.map(lambda e: build_one(a.tree, a.out, e), envs):
                res.write(json.dumps(r) + "\n")
                res.flush()
                print("%-58s %s %4ds  image=%s%s" % (r["env"], "OK  " if r["ok"] else "FAIL", r["secs"],
                                                     "~" if r["image_estimated"] else "", r["image"]), flush=True)
    return 0


# ── grid ──────────────────────────────────────────────────────────────────────

def load_results(path):
    out = {}
    with open(os.path.join(path, "results.jsonl"), encoding="utf-8") as fh:
        for line in fh:
            if line.strip():
                r = json.loads(line)
                out[r["env"]] = r
    return out


def grid(old, new):
    """Markdown rows comparing two result sets, envs in the old set's order."""
    kb = lambda x: "-" if x is None else "{:,.0f}".format(x / 1024)  # noqa: E731
    rows = ["| Env | Today KB | New KB | Growth | Slot KB | Fill | Left KB | Static RAM KB |",
            "|---|---|---|---|---|---|---|---|"]
    for env, o in old.items():
        n = new.get(env)
        if not n or not o.get("image") or not n.get("image"):
            rows.append("| `%s` | %s | - | - | - | - | - | - |" % (env, kb(o.get("image"))))
            continue
        slot = n.get("slot") or o.get("slot")
        g = n["image"] - o["image"]
        mark = lambda r: "~" if r.get("image_estimated") else ""  # noqa: E731
        rows.append("| `%s` | %s%s | %s%s | %+.0f (%+.1f%%) | %s | %s | %s | %s -> %s |" % (
            env, mark(o), kb(o["image"]), mark(n), kb(n["image"]), g / 1024, 100.0 * g / o["image"], kb(slot),
            ("%.0f%%" % (100.0 * n["image"] / slot)) if slot else "-",
            kb(slot - n["image"]) if slot else "-", kb(o.get("ram")), kb(n.get("ram"))))
    return "\n".join(rows)


def cmd_grid(a):
    print(grid(load_results(a.old), load_results(a.new)))
    return 0


def cmd_diff(a):
    sections = set(a.sections.split(","))
    o = attribute(open(a.old, encoding="utf-8", errors="replace").read(), sections, a.skip_strings)
    n = attribute(open(a.new, encoding="utf-8", errors="replace").read(), sections, a.skip_strings)
    keys = sorted(set(o) | set(n), key=lambda k: n.get(k, 0) - o.get(k, 0), reverse=True)
    print("counted: old {:,}  new {:,}  delta {:+,}".format(sum(o.values()), sum(n.values()),
                                                           sum(n.values()) - sum(o.values())))
    for k in keys[:a.top] + (["..."] if len(keys) > a.top else []) + keys[-min(8, len(keys)):]:
        if k == "...":
            print("  ...")
            continue
        print("  {:42} {:>10,} {:>10,} {:>+10,}".format(k[:42], o.get(k, 0), n.get(k, 0), n.get(k, 0) - o.get(k, 0)))
    return 0


def cmd_pulled(a):
    for member, by, sym in pulled_by(open(a.map, encoding="utf-8", errors="replace").read(), a.pattern):
        print("%s  <-  %s  [%s]" % (member, by, sym))
    return 0


# ── spiffs ────────────────────────────────────────────────────────────────────

def companion_files(contacts, channels, headroom):
    """The companion's worst-case SPIFFS data set, from the write code's record sizes
    (DataStore.cpp, IdentityStore.cpp, MyMesh.cpp): 152 B per contact, one advert
    file of at most 166 B per contact, 68 B per channel, 32 blocked keys, and a 4 KB
    allowance per prefs file. headroom adds a second copy of the contact file."""
    files = {"identity/_main.id": 128, "contacts3": 152 * contacts, "channels2": 68 * channels,
             "blocks": 1 + 32 * 32, "prefs.json": 4096, "new_prefs": 4096}
    for i in range(contacts):
        files["bl/%016x" % i] = 166
    if headroom:
        files["spare"] = 152 * contacts
    return files


def spiffs_fits(mkspiffs, size, files):
    root = tempfile.mkdtemp(prefix="sizematrix_")
    try:
        for name, n in files.items():
            p = os.path.join(root, "img", *name.split("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as fh:
                fh.write(os.urandom(n))
        r = subprocess.run([mkspiffs, "-c", os.path.join(root, "img"), "-b", "4096", "-p", "256", "-s", str(size),
                            os.path.join(root, "img.bin")], capture_output=True, text=True)
        return r.returncode == 0
    finally:
        shutil.rmtree(root, ignore_errors=True)


def cmd_spiffs(a):
    lo, hi = 0, a.max_contacts
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if spiffs_fits(a.mkspiffs, a.size, companion_files(mid, a.channels, True)):
            lo = mid
        else:
            hi = mid - 1
    print("SPIFFS %d bytes (%#x): %d of %d contacts fit, with room to rewrite the contact file"
          % (a.size, a.size, lo, a.max_contacts))
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--tree", required=True)
    b.add_argument("--out", required=True)
    b.add_argument("--workers", type=int, default=2)
    b.add_argument("envs", nargs="*")
    g = sub.add_parser("grid")
    g.add_argument("--old", required=True)
    g.add_argument("--new", required=True)
    d = sub.add_parser("diff")
    d.add_argument("--old", required=True)
    d.add_argument("--new", required=True)
    d.add_argument("--sections", default=",".join(sorted(CODE)))
    d.add_argument("--skip-strings", action="store_true")
    d.add_argument("--top", type=int, default=20)
    w = sub.add_parser("pulled")
    w.add_argument("--map", required=True)
    w.add_argument("--pattern", required=True)
    s = sub.add_parser("spiffs")
    s.add_argument("--mkspiffs", required=True)
    s.add_argument("--size", type=lambda v: int(v, 0), required=True)
    s.add_argument("--channels", type=int, default=40)
    s.add_argument("--max-contacts", type=int, default=350)
    a = p.parse_args(argv)
    return {"build": cmd_build, "grid": cmd_grid, "diff": cmd_diff, "pulled": cmd_pulled, "spiffs": cmd_spiffs}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
