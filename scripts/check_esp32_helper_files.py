#!/usr/bin/env python3
"""Offband #2288: ESP32 envs name the src/helpers/esp32/ files they use.

Why this exists
---------------
Companion envs used to compile the whole directory through
`+<helpers/esp32/*.cpp>`. That pulled in two files that most of them never use:

* ESPNOWRadio.cpp calls WiFi.mode();
* SerialWifiInterface.cpp uses the network client and server.

On pioarduino 55.x those two link the whole Wi-Fi and TCP/IP stack into BLE and
USB companions that never start it: 67-80 KB per S3, C3 and classic ESP32 image.
Nothing fails; the image is just bigger, and on the C3 that decides whether it
fits its slot. It is the same pattern #199 fixed for the BLE file, and a new file
dropped into the directory would come back the same way.

So, on the RESOLVED config of every ESP32 env (values arrive through `extends`
chains, so only PlatformIO knows what an env really ends up with):

1. No env uses the `+<helpers/esp32/*.cpp>` wildcard.
2. SerialWifiInterface.cpp is compiled only where WIFI_SSID is defined (the
   companion's Wi-Fi transport is chosen by WIFI_SSID), and every companion that
   defines WIFI_SSID compiles it.
3. ESPNOWRadio.cpp is compiled only where one of the env's variant directories
   includes ESPNOWRadio.h, which is to say it is the env's radio.

A missing file the env needs is a link error, so rules 2 and 3 catch the silent
direction: a file compiled that the env does not use.

Run from the repo root:  python scripts/check_esp32_helper_files.py
Exit: 0 clean, 1 on any finding, 2 if the config could not be read.
"""

import json
import os
import re
import subprocess
import sys

WILDCARD = "+<helpers/esp32/*.cpp>"
WIFI_SRC = "helpers/esp32/SerialWifiInterface.cpp"
ESPNOW_SRC = "helpers/esp32/ESPNOWRadio.cpp"
ESPNOW_HDR = re.compile(r'#include\s*[<"]helpers/esp32/ESPNOWRadio\.h[>"]')
WIFI_SSID = re.compile(r"-D\s*WIFI_SSID\b")
VARIANT_DIR = re.compile(r"variants/([A-Za-z0-9_\-]+)")
COMPANION = "examples/companion_radio"


def flatten(value):
    """Resolved options come back as either a string or a list of strings."""
    if value is None:
        return ""
    if isinstance(value, (list, tuple)):
        return "\n".join(str(v) for v in value)
    return str(value)


def compiles(src, rel):
    """True when the filter adds the file (by name or wildcard) and does not drop it."""
    added = WILDCARD in src or ("+<%s>" % rel) in src
    return added and ("-<%s>" % rel) not in src


def variant_uses_espnow(repo_root, variant):
    vdir = os.path.join(repo_root, "variants", variant)
    if not os.path.isdir(vdir):
        return False
    for fname in os.listdir(vdir):
        if fname.endswith((".h", ".cpp")):
            with open(os.path.join(vdir, fname), encoding="utf-8", errors="replace") as fh:
                if ESPNOW_HDR.search(fh.read()):
                    return True
    return False


def evaluate(config, uses_espnow):
    """Return (checked, findings). uses_espnow(variant) says if a variant's radio is ESP-NOW."""
    findings, checked = [], 0
    for section, options in config:
        if not section.startswith("env:"):
            continue
        opts = dict(options)
        if "espressif32" not in flatten(opts.get("platform")):
            continue
        name = section[4:]
        checked += 1
        src = flatten(opts.get("build_src_filter"))
        flags = flatten(opts.get("build_flags"))
        if WILDCARD in src:
            findings.append(
                "%s: compiles %s. Name the files the env uses instead; the wildcard links "
                "ESP-NOW and Wi-Fi into envs that never start them." % (name, WILDCARD))
        has_ssid = bool(WIFI_SSID.search(flags))
        if compiles(src, WIFI_SRC) and not has_ssid:
            findings.append(
                "%s: compiles SerialWifiInterface.cpp without WIFI_SSID, so it links the "
                "TCP/IP stack for a transport it never starts." % name)
        if has_ssid and COMPANION in src and not compiles(src, WIFI_SRC):
            findings.append(
                "%s: a Wi-Fi companion (WIFI_SSID) that does not compile "
                "SerialWifiInterface.cpp." % name)
        if compiles(src, ESPNOW_SRC):
            variants = set(VARIANT_DIR.findall(flags + "\n" + src))
            if not any(uses_espnow(v) for v in variants):
                findings.append(
                    "%s: compiles ESPNOWRadio.cpp, but none of its variants (%s) uses "
                    "ESPNOWRadio as its radio." % (name, ", ".join(sorted(variants)) or "none"))
    return checked, findings


def load_config():
    raw = subprocess.check_output(["pio", "project", "config", "--json-output"],
                                  stderr=subprocess.STDOUT)
    return json.loads(raw.decode("utf-8", "replace"))


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    try:
        config = load_config()
    except (subprocess.CalledProcessError, OSError, ValueError) as exc:
        out = getattr(exc, "output", b"")
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        print("ERROR: could not read PlatformIO config:\n%s" % (out or exc))
        return 2
    checked, findings = evaluate(config, lambda v: variant_uses_espnow(repo_root, v))
    if checked == 0:
        print("FAIL: no ESP32 env found, so nothing was checked.")
        return 1
    if findings:
        for f in findings:
            print("FAIL  " + f)
        print("%d finding(s) across %d ESP32 env(s)." % (len(findings), checked))
        return 1
    print("OK: all %d ESP32 env(s) name their helpers/esp32 files; Wi-Fi and ESP-NOW code "
          "only where it is used." % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
