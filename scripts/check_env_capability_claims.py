#!/usr/bin/env python3
"""Assert that an env's NAME agrees with its resolved CONFIG (Offband #649).

An env name is a claim. `Heltec_t096_companion_radio_ble` claims the companion
link is BLE; `..._companion_radio_usb` claims it is not. This script asserts the
claim and the resolved PlatformIO config agree, and fails when they do not --
when they disagree, one of the two is wrong and a human has to decide which.

What actually gates BLE
-----------------------
`BLE_PIN_CODE`, not the build_src_filter. examples/companion_radio/main.cpp
selects the transport like this:

    ESP32 :  #ifdef WIFI_SSID        -> SerialWifiInterface   (WiFi WINS)
             #elif defined(BLE_PIN_CODE) -> SerialBLEInterface
             #else                   -> ArduinoSerialInterface
    nRF52 :  #ifdef BLE_PIN_CODE     -> SerialBLEInterface
             #else                   -> ArduinoSerialInterface

So "the env compiles SerialBLEInterface.cpp" is NOT the capability. nRF52
companion envs pull `+<helpers/nrf52/*.cpp>` with a greedy wildcard and compile
the BLE interface even when `BLE_PIN_CODE` is commented out -- they build it and
never instantiate it. Detecting on the source filter reports every one of those
as BLE-capable, which is wrong. Detect on the flag that the code branches on.

Relationship to scripts/check_esp32_ble_deps.py (#199)
-----------------------------------------------------
They ask DIFFERENT questions and neither subsumes the other:

  #199  "is this env internally COHERENT -- does it declare exactly one of the
         two escape hatches, so it can LINK?"
  #649  "does the config match what the env SAYS IT IS?"

#199 is satisfied by an env that is coherently configured but coherently WRONG:
a `*_ble` env with no `BLE_PIN_CODE` links cleanly and ships with no BLE.
Nothing catches that but the name.

Why this exists (#647)
----------------------
Upstream base updates add BOARDS, and new boards conflict with nothing -- they
are pure additions, so every file-oriented merge review passes them silently.
The 1.17.0 update landed 21 upstream-new ESP32 envs already violating #199
(#645). Offband diverges from upstream on capability-bearing config (NimBLE vs
Bluedroid, #288), and upstream has no reason to satisfy an Offband invariant, so
this recurs on every base update.

Reading the RESOLVED config rather than grepping .ini files is deliberate:
values inherit through `extends` chains, so only PlatformIO's own interpolation
knows what an env really ends up with -- and it strips commented-out flags,
which a grep would happily count as present.

Usage
-----
    python scripts/check_env_capability_claims.py                 # human table
    python scripts/check_env_capability_claims.py --format json   # machine
    python scripts/check_env_capability_claims.py --json-out r.json

Exit codes: 0 = every claim honoured, 1 = at least one mismatch, 2 = could not
read the PlatformIO config.
"""

import argparse
import json
import re
import subprocess
import sys


def defines(flags, macro):
    """Is `macro` defined in the resolved build_flags?

    Word-anchored so `BLE_PIN_CODE` does not match a hypothetical
    `NO_BLE_PIN_CODE`. Comments are already stripped by PlatformIO's own
    parser, so a plain presence test is sound here -- verified against
    Heltec_t114_companion_radio_usb, whose `; -D BLE_PIN_CODE=...` line does
    not survive resolution.
    """
    return re.search(r"(?<![A-Za-z0-9_])%s(?![A-Za-z0-9_])" % macro, flags) is not None


# --------------------------------------------------------------------------
# Capability table.
#
# A capability is DATA, not a code path, so WiFi / GPS / Ethernet can be added
# without restructuring anything (#649).
#
#   claim_re  : regex over the env name; group 1 is the transport token
#   yes / no  : which transport tokens assert presence / absence
#   detectors : platform substring -> predicate(flags, src_filter, lib_deps)
# --------------------------------------------------------------------------


def _esp32_ble_enabled(flags, src, deps):
    # WIFI_SSID takes precedence in the #if chain, so an env with both is a
    # WiFi companion, not a BLE one.
    return defines(flags, "BLE_PIN_CODE") and not defines(flags, "WIFI_SSID")


def _nrf52_ble_enabled(flags, src, deps):
    return defines(flags, "BLE_PIN_CODE")


CAPABILITIES = [
    {
        "key": "ble",
        # Anchored to the COMPANION TRANSPORT specifically.
        #
        # `heltec_v4_companion_observer_wifi` is a BLE companion that ships
        # observer telemetry over WiFi -- the `_wifi` qualifies the OBSERVER,
        # not the companion link, and the env genuinely defines BLE_PIN_CODE.
        # A check anchored on the trailing token alone reads that as "claims no
        # BLE" and reports every observer env as a violation forever. Only
        # `companion_radio_<transport>` states the companion transport.
        "claim_re": re.compile(r"_companion_radio_(ble|usb|wifi|ethernet)$"),
        "yes": ("ble",),
        "no": ("usb", "wifi", "ethernet"),
        "detectors": {
            "espressif32": _esp32_ble_enabled,
            "nordicnrf52": _nrf52_ble_enabled,
        },
        "remedy_missing": (
            "env name claims a BLE companion but BLE_PIN_CODE is not in effect "
            "(on ESP32, defining WIFI_SSID also disables it -- WiFi wins the "
            "#if chain). Either define BLE_PIN_CODE, or rename the env."
        ),
        "remedy_unexpected": (
            "env name claims a non-BLE companion transport, but BLE_PIN_CODE is "
            "in effect, so main.cpp will select SerialBLEInterface. Either drop "
            "BLE_PIN_CODE, or rename the env."
        ),
    },
]

OK = "ok"
MISSING = "MISSING"
UNEXPECTED = "UNEXPECTED"
UNCLAIMED = "unclaimed"
UNSUPPORTED = "unsupported-platform"

FAILING = (MISSING, UNEXPECTED)


def flatten(value):
    """Resolved options come back as either a string or a list of strings."""
    if value is None:
        return ""
    if isinstance(value, (list, tuple)):
        return "\n".join(str(v) for v in value)
    return str(value)


def name_claim(env_name, cap):
    """What does the NAME assert about this capability? True / False / None.

    Trailing underscores are stripped first -- several envs are named
    `..._companion_radio_ble_` to keep them out of upstream's automatic build
    globs.
    """
    m = cap["claim_re"].search(env_name.rstrip("_").lower())
    if not m:
        return None
    token = m.group(1)
    if token in cap["yes"]:
        return True
    if token in cap["no"]:
        return False
    return None


def classify(env_name, platform, flags, src_filter, lib_deps, cap):
    """Pure decision function -- no PlatformIO, no I/O, no globals.

    Kept pure so the negative tests can feed it synthetic envs. A checker only
    ever exercised against a clean tree proves nothing about what it CATCHES.
    """
    claimed = name_claim(env_name, cap)
    record = {
        "env": env_name,
        "capability": cap["key"],
        "platform": platform,
        "claimed": claimed,
        "actual": None,
        "verdict": UNCLAIMED,
        "reason": "env name makes no claim about %s" % cap["key"],
    }

    detector = None
    for token, fn in cap["detectors"].items():
        if token in platform:
            detector = fn
            break

    if detector is None:
        record["verdict"] = UNSUPPORTED
        record["reason"] = "no %s detector for this platform" % cap["key"]
        return record

    actual = bool(detector(flags, src_filter, lib_deps))
    record["actual"] = actual

    if claimed is None:
        return record
    if claimed and not actual:
        record["verdict"] = MISSING
        record["reason"] = cap["remedy_missing"]
    elif actual and not claimed:
        record["verdict"] = UNEXPECTED
        record["reason"] = cap["remedy_unexpected"]
    else:
        record["verdict"] = OK
        record["reason"] = "name and config agree"
    return record


def read_config():
    """Read the resolved PlatformIO config.

    On failure this deliberately does NOT echo the subprocess output. That
    output is the fully-interpolated project config, which includes the
    gitignored secrets sections -- printing it on an error path is how a live
    credential reached a transcript once already (#638). Report the failure and
    let a human re-run it themselves.
    """
    try:
        raw = subprocess.check_output(
            ["pio", "project", "config", "--json-output"],
            stderr=subprocess.DEVNULL,
        )
    except (subprocess.CalledProcessError, OSError) as exc:
        print(
            "ERROR: could not read the PlatformIO config (%s: %s).\n"
            "Output withheld -- it contains interpolated secrets (#638).\n"
            "Re-run `pio project config --json-output` yourself to see why."
            % (type(exc).__name__, getattr(exc, "returncode", exc)),
            file=sys.stderr,
        )
        return None
    return json.loads(raw.decode("utf-8", "replace"))


def evaluate(config):
    records = []
    for section, options in config:
        if not section.startswith("env:"):
            continue
        name = section[4:]
        opts = dict(options)
        platform = flatten(opts.get("platform"))
        flags = flatten(opts.get("build_flags"))
        src = flatten(opts.get("build_src_filter"))
        deps = flatten(opts.get("lib_deps"))
        for cap in CAPABILITIES:
            records.append(classify(name, platform, flags, src, deps, cap))
    return records


def render_human(records, stream=sys.stdout):
    bad = [r for r in records if r["verdict"] in FAILING]
    considered = [r for r in records if r["verdict"] in FAILING + (OK,)]

    def yn(v):
        return "-" if v is None else ("yes" if v else "no")

    if bad:
        print("FAIL: %d env/capability claim(s) contradict the resolved "
              "config.\n" % len(bad), file=stream)
        width = max(len(r["env"]) for r in bad)
        header = "  %-*s  %-4s  %-10s %-8s %s" % (
            width, "ENV", "CAP", "VERDICT", "CLAIMED", "ACTUAL")
        print(header, file=stream)
        print("  " + "-" * (len(header) - 2), file=stream)
        for r in sorted(bad, key=lambda r: (r["verdict"], r["env"])):
            print("  %-*s  %-4s  %-10s %-8s %s" % (
                width, r["env"], r["capability"], r["verdict"],
                yn(r["claimed"]), yn(r["actual"])), file=stream)
        print("", file=stream)
        for reason in sorted({r["reason"] for r in bad}):
            print("  * %s" % reason, file=stream)
        print("", file=stream)
    else:
        print("OK: %d env/capability claim(s) checked, all honoured by the "
              "resolved config." % len(considered), file=stream)

    skipped = len(records) - len(considered)
    if skipped:
        print("  (%d env/capability pair(s) made no claim, or had no detector "
              "for their platform)" % skipped, file=stream)
    return 1 if bad else 0


def build_report(records):
    bad = [r for r in records if r["verdict"] in FAILING]
    return {
        "tool": "check_env_capability_claims",
        "issue": 649,
        "summary": {
            "records": len(records),
            "failing": len(bad),
            "by_verdict": {
                v: sum(1 for r in records if r["verdict"] == v)
                for v in (OK, MISSING, UNEXPECTED, UNCLAIMED, UNSUPPORTED)
            },
        },
        "records": records,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Assert env names agree with their resolved config.")
    ap.add_argument("--format", choices=("human", "json"), default="human",
                    help="stdout format (default: human)")
    ap.add_argument("--json-out", metavar="PATH",
                    help="also write the machine-readable report to PATH")
    args = ap.parse_args(argv)

    config = read_config()
    if config is None:
        return 2

    records = evaluate(config)
    report = build_report(records)

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as fh:
            json.dump(report, fh, indent=2, sort_keys=True)
            fh.write("\n")

    if args.format == "json":
        json.dump(report, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
        return 1 if report["summary"]["failing"] else 0

    return render_human(records)


if __name__ == "__main__":
    sys.exit(main())
