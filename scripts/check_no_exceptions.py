#!/usr/bin/env python3
"""Offband #2287: every ESP32 env compiles without C++ exception tables.

Why this exists
---------------
The ESP32 frameworks compile with -fexceptions, so every object file carries
.eh_frame unwind tables: 28-59 KB per companion and 40-78 KB per repeater on
pioarduino 55.x, and 87 KB on today's C3 repeater. Nothing in src/, examples/,
arch/ or any library we build throws or catches, so the tables buy nothing; an
exception already ends the program.

`esp32_base` removes the flag (build_unflags = -fexceptions) and adds
-fno-exceptions. Two edits would undo that without any build failing:

* an env that sets its own `build_unflags` REPLACES the inherited value rather
  than adding to it, so unless it starts with ${esp32_base.build_unflags} the
  framework's -fexceptions comes straight back;
* an env adding -fexceptions to its build_flags.

The image just grows again, and on the C3 that is the difference between fitting
its app slot and not.

Checking the RESOLVED config (not grepping the .ini files) is deliberate, for the
same reason as check_esp32_ble_deps.py: values arrive through `extends` chains,
and only PlatformIO's own interpolation knows what an env really ends up with.

Run from the repo root:  python scripts/check_no_exceptions.py
Exit: 0 clean, 1 on any finding, 2 if the config could not be read.
"""

import json
import subprocess
import sys

UNFLAG = "-fexceptions"
FLAG = "-fno-exceptions"


def flatten(value):
    """Resolved options come back as either a string or a list of strings."""
    if value is None:
        return ""
    if isinstance(value, (list, tuple)):
        return "\n".join(str(v) for v in value)
    return str(value)


def tokens(value):
    """The whitespace-separated flags of an option, with ; comments removed."""
    out = []
    for line in flatten(value).splitlines():
        out.extend(line.split(";", 1)[0].split())
    return out


def evaluate(config):
    """Return (checked, findings) for a `pio project config --json-output` list."""
    findings, checked = [], 0
    for section, options in config:
        if not section.startswith("env:"):
            continue
        opts = dict(options)
        if "espressif32" not in flatten(opts.get("platform")):
            continue
        name = section[4:]
        checked += 1
        unflags = tokens(opts.get("build_unflags"))
        flags = tokens(opts.get("build_flags"))
        if UNFLAG not in unflags:
            findings.append(
                "%s: build_unflags does not remove %s, so the framework's exception tables "
                "are back. An env's own build_unflags replaces esp32_base's: start it with "
                "${esp32_base.build_unflags}." % (name, UNFLAG))
        if FLAG not in flags:
            findings.append("%s: build_flags lacks %s." % (name, FLAG))
        if UNFLAG in flags:
            findings.append("%s: build_flags adds %s back." % (name, UNFLAG))
    return checked, findings


def load_config():
    raw = subprocess.check_output(["pio", "project", "config", "--json-output"],
                                  stderr=subprocess.STDOUT)
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
    checked, findings = evaluate(config)
    if checked == 0:
        print("FAIL: no ESP32 env found, so nothing was checked.")
        return 1
    if findings:
        for f in findings:
            print("FAIL  " + f)
        print("%d finding(s) across %d ESP32 env(s)." % (len(findings), checked))
        return 1
    print("OK: all %d ESP32 env(s) compile with %s and remove %s." % (checked, FLAG, UNFLAG))
    return 0


if __name__ == "__main__":
    sys.exit(main())
