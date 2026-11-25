#!/usr/bin/env python3
"""Offband #2288: tests for check_esp32_helper_files.py.

Run:  python scripts/test_check_esp32_helper_files.py
Exit: 0 on pass, nonzero with a clear failure message otherwise.
Stdlib only, no PlatformIO: each case feeds the REAL evaluate() a resolved config
in the shape `pio project config --json-output` returns, plus the real
variant_uses_espnow() over fixture variant directories.
"""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_esp32_helper_files as chk  # noqa: E402

FAILURES = []
ESP = "platformio/espressif32@6.11.0"
BASE = "+<*.cpp>\n+<helpers/*.cpp>\n"
COMP = "+<../examples/companion_radio/*.cpp>\n"


def expect(condition, label):
    if condition:
        print("  pass  %s" % label)
    else:
        print("  FAIL  %s" % label)
        FAILURES.append(label)


def env(name, src, flags="-I variants/board_x", platform=ESP):
    return ["env:" + name, [["platform", platform], ["build_src_filter", src],
                            ["build_flags", flags]]]


def main():
    root = tempfile.mkdtemp(prefix="esp32-helpers-")
    try:
        os.makedirs(os.path.join(root, "variants", "board_x"))
        os.makedirs(os.path.join(root, "variants", "espnow_x"))
        with open(os.path.join(root, "variants", "board_x", "target.h"), "w") as fh:
            fh.write("#include <helpers/radiolib/CustomSX1262Wrapper.h>\n")
        with open(os.path.join(root, "variants", "espnow_x", "target.h"), "w") as fh:
            fh.write("#include <helpers/esp32/ESPNOWRadio.h>\nextern ESPNOWRadio radio_driver;\n")
        uses = lambda v: chk.variant_uses_espnow(root, v)  # noqa: E731

        def run(*envs):
            return chk.evaluate(list(envs), uses)

        ble = BASE + "+<helpers/esp32/SerialBLEInterface.cpp>\n" + COMP
        wifi_src = ble + "+<helpers/esp32/SerialWifiInterface.cpp>\n"
        wifi_flags = "-I variants/board_x\n-D WIFI_SSID='\"x\"'"

        print("positive: the patterns that must pass")
        n, f = run(env("ble", ble), ["esp32_base", [["build_src_filter", "+<helpers/esp32/*.cpp>"]]])
        expect(n == 1 and f == [], "a BLE companion naming its file is clean, non-env sections skipped")
        n, f = run(env("wifi", wifi_src, wifi_flags))
        expect(f == [], "a Wi-Fi companion with SerialWifiInterface.cpp and WIFI_SSID is clean")
        n, f = run(env("now", BASE + "+<helpers/esp32/ESPNOWRadio.cpp>\n" + COMP,
                       "-I variants/espnow_x"))
        expect(f == [], "an env whose variant's radio is ESP-NOW may compile ESPNOWRadio.cpp")
        n, f = run(env("usb", ble + "-<helpers/esp32/SerialBLEInterface.cpp>\n"))
        expect(f == [], "a USB companion dropping the BLE file is clean")
        n, f = run(env("nrf", "+<helpers/esp32/*.cpp>\n", platform="nordicnrf52"))
        expect(n == 0 and f == [], "a non-ESP32 env is not checked")
        n, f = run(env("rep", BASE + "+<../examples/simple_repeater>\n", wifi_flags))
        expect(f == [], "a repeater with WIFI_SSID is not required to compile the companion transport")

        print("negative: the defects that must fail")
        n, f = run(env("wild", BASE + "+<helpers/esp32/*.cpp>\n" + COMP))
        expect(any("wildcard" in x for x in f), "the wildcard is a finding")
        n, f = run(env("wild2", BASE + "+<helpers/esp32/*.cpp>\n" + COMP, "-I variants/board_x"))
        expect(any("ESPNOWRadio" in x for x in f) and any("SerialWifiInterface" in x for x in f),
               "the wildcard's ESP-NOW and Wi-Fi files are each reported too")
        n, f = run(env("blewifi", wifi_src))
        expect(len(f) == 1 and "without WIFI_SSID" in f[0],
               "SerialWifiInterface.cpp in a companion without WIFI_SSID is a finding")
        n, f = run(env("nofile", ble, wifi_flags))
        expect(len(f) == 1 and "Wi-Fi companion" in f[0],
               "a WIFI_SSID companion lacking SerialWifiInterface.cpp is a finding")
        n, f = run(env("stray", ble + "+<helpers/esp32/ESPNOWRadio.cpp>\n"))
        expect(len(f) == 1 and "ESPNOWRadio" in f[0] and "board_x" in f[0],
               "ESPNOWRadio.cpp on a LoRa variant is a finding")
        n, f = run(env("ghost", ble + "+<helpers/esp32/ESPNOWRadio.cpp>\n", "-I variants/missing"))
        expect(len(f) == 1 and "ESPNOWRadio" in f[0], "a variant directory that does not exist fails closed")
    finally:
        shutil.rmtree(root)

    if FAILURES:
        print("%d FAILED" % len(FAILURES))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
