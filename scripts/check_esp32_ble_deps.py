#!/usr/bin/env python3
"""Assert the ESP32 BLE dependency invariant across every env (Offband #199).

The defect this guards against: companion envs pull the whole helpers/esp32
directory in with a greedy wildcard (`+<helpers/esp32/*.cpp>`), and
SerialBLEInterface.h includes <NimBLEDevice.h> unconditionally -- there is no
#ifdef guard. So an env compiles the BLE interface whether or not the board uses
BLE, and then fails to build for want of a Bluetooth stack.

That left 64 ESP32 envs unbuildable, two of them in the shipped release matrix.
Because release.yml runs `fail-fast: false`, those boards' binaries were simply
absent from releases rather than failing the build -- silent, and it recurred four
times board-by-board before anyone chased it.

An env compiling SerialBLEInterface.cpp must therefore resolve to exactly one of:
  * it genuinely uses BLE      -> declare the dep via ${esp32_ble.lib_deps}
  * it does not use BLE        -> drop the file via ${esp32_no_ble.build_src_filter}

Checking the RESOLVED config (not grepping the .ini files) is deliberate: the
values are inherited through `extends` chains, so only PlatformIO's own
interpolation knows what an env really ends up with.

Exits non-zero and names every offender. Run from the repo root:
    python scripts/check_esp32_ble_deps.py
"""

import json
import subprocess
import sys

BLE_SRC = "helpers/esp32/SerialBLEInterface.cpp"
BLE_GLOB = "helpers/esp32/*.cpp"
BLE_DEP = "NimBLE-Arduino"


def flatten(value):
    """Resolved options come back as either a string or a list of strings."""
    if value is None:
        return ""
    if isinstance(value, (list, tuple)):
        return "\n".join(str(v) for v in value)
    return str(value)


def main():
    try:
        raw = subprocess.check_output(
            ["pio", "project", "config", "--json-output"],
            stderr=subprocess.STDOUT,
        )
    except (subprocess.CalledProcessError, OSError) as exc:
        out = getattr(exc, "output", b"")
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        print("ERROR: could not read PlatformIO config:\n%s" % (out or exc))
        return 2

    config = json.loads(raw.decode("utf-8", "replace"))

    offenders = []
    checked = 0
    for section, options in config:
        if not section.startswith("env:"):
            continue
        name = section[4:]
        opts = dict(options)
        if "espressif32" not in flatten(opts.get("platform")):
            continue

        src = flatten(opts.get("build_src_filter"))
        compiles_ble = (BLE_GLOB in src or ("+<%s>" % BLE_SRC) in src) and (
            ("-<%s>" % BLE_SRC) not in src
        )
        if not compiles_ble:
            continue

        checked += 1
        if BLE_DEP not in flatten(opts.get("lib_deps")):
            offenders.append(name)

    if offenders:
        print("FAIL: %d ESP32 env(s) compile %s without %s.\n" % (len(offenders), BLE_SRC, BLE_DEP))
        for name in sorted(offenders):
            print("  %s" % name)
        print(
            "\nFix each by adding EITHER:\n"
            "  lib_deps          =  ${esp32_ble.lib_deps}          (env genuinely uses BLE)\n"
            "  build_src_filter  =  ${esp32_no_ble.build_src_filter}  (env does not -- list it LAST)\n"
        )
        return 1

    print("OK: %d ESP32 env(s) compile %s, all declare %s." % (checked, BLE_SRC, BLE_DEP))
    return 0


if __name__ == "__main__":
    sys.exit(main())
