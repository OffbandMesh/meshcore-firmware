#!/usr/bin/env python
"""Offband #1692: tests for check_safeboot_wake_source.py.

Run:  python scripts/test_check_safeboot_wake_source.py
Exit: 0 on pass, nonzero with a clear failure message otherwise.
Stdlib only, matching the other scripts/test_*.py.

Negative cases are the point. A guard watched only against a clean tree proves it stays
quiet, not what it catches -- so every rule below is shown tripping, and the legitimate
patterns it must NOT flag are shown passing.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_safeboot_wake_source as chk  # noqa: E402

FAILURES = []


def expect(condition, label):
    if condition:
        print("  pass  %s" % label)
    else:
        print("  FAIL  %s" % label)
        FAILURES.append(label)


NRF_INI = "[env:x]\nextends = nrf52_base\nbuild_flags =\n  -D PIN_VBAT_READ=5\n"
ESP_INI = "[env:x]\nplatform = espressif32\nbuild_flags =\n  -D PIN_VBAT_READ=5\n"


def run(ini, headers, name="testboard"):
    return chk.check_variant(name, ini, headers)


def test_variant_rule():
    print("variant rule")

    applies, probs = run(NRF_INI, "")
    expect(applies and len(probs) == 1, "nRF52 SafeBoot with no wake source FAILS")

    _, probs = run(NRF_INI, "#define PIN_USER_BTN (-1)\n")
    expect(len(probs) == 1, "a button defined as (-1) is not a wake source")

    _, probs = run(NRF_INI, "#define PIN_USER_BTN RADIOLIB_NC\n")
    expect(len(probs) == 1, "a button defined as RADIOLIB_NC is not a wake source")

    _, probs = run(NRF_INI, "// #define PIN_USER_BTN 9\n/* #define BATTERY_LPCOMP_INPUT 3 */\n")
    expect(len(probs) == 1, "commented-out declarations do not count")

    _, probs = run(NRF_INI + "  ; -D BATTERY_LPCOMP_INPUT=NRF_LPCOMP_INPUT_7\n", "")
    expect(len(probs) == 1, "an ini comment does not count")

    _, probs = run(NRF_INI, "#define PIN_USER_BTN (9)\n")
    expect(probs == [], "a connected button passes")

    _, probs = run(NRF_INI + "  -D BATTERY_LPCOMP_INPUT=NRF_LPCOMP_INPUT_7\n", "")
    expect(probs == [], "an LPCOMP input passes")

    # gat562_mesh_watch13 / rak_wismesh_tag: target.cpp's (-1) fallback sits beside the
    # real definition, and sorts first. Taking the first #define missed both boards.
    _, probs = run(NRF_INI, "#ifndef PIN_USER_BTN\n#define PIN_USER_BTN (-1)\n#endif\n"
                            "#define PIN_USER_BTN PIN_BUTTON1\n")
    expect(probs == [], "a (-1) fallback beside a real definition passes")

    # An alias that ends at -1 is still no button, however many names it passes through.
    _, probs = run(NRF_INI, "#define PIN_USER_BTN BUTTON_PIN\n#define BUTTON_PIN PIN_BUTTON1\n"
                            "#define PIN_BUTTON1 (-1)\n")
    expect(len(probs) == 1, "an alias chain ending at (-1) FAILS")

    _, probs = run(NRF_INI, "#define PIN_USER_BTN PIN_BUTTON1\n#define PIN_BUTTON1 (32 + 0)\n")
    expect(probs == [], "an alias chain ending at a real pin passes")

    applies, probs = run(ESP_INI, "")
    expect(not applies and probs == [], "an ESP32 variant is out of scope")

    applies, probs = run("[env:x]\nextends = nrf52_base\n", "")
    expect(not applies and probs == [], "nRF52 without a battery pin has no SafeBoot")

    applies, probs = run(NRF_INI + "  -D SAFE_BOOT_DISABLED\n", "")
    expect(not applies and probs == [], "SAFE_BOOT_DISABLED takes it out of scope")


def test_exemptions_cannot_rot():
    print("exemptions")
    name = sorted(chk.EXEMPT)[0]

    _, probs = run(NRF_INI, "", name)
    expect(probs == [], "an exempt board with no source passes")

    _, probs = run(NRF_INI, "#define PIN_USER_BTN 9\n", name)
    expect(len(probs) == 1, "an exempt board that gains a source FAILS")

    _, probs = run("[env:x]\nextends = nrf52_base\n", "", name)
    expect(len(probs) == 1, "an exempt board without SafeBoot FAILS")

    for n, why in chk.EXEMPT.items():
        expect(bool(why.strip()), "exemption %s carries a reason" % n)


GOOD_SRC = """
#define USER_BTN_PRESSED LOW
static uint32_t safeBootButtonChipPin() { return g_ADigitalPinMap[PIN_USER_BTN]; }
static bool safeBootButtonInMap() { const int p = (int)(PIN_USER_BTN); return p >= 0; }
if (safeBootButtonInMap()) { }
const bool pressed_high = (USER_BTN_PRESSED == HIGH);
if (wake != offband::SafeBootWake::TimedRestart) { sd_power_system_off(); }
"""


def test_source_rules():
    print("SafeBoot.cpp rules")

    expect(chk.check_safeboot_source(GOOD_SRC) == [], "the fixed shape passes")

    # The #1687 defect itself: the Arduino pin handed to the HAL as a chip pin.
    bad = GOOD_SRC + "nrf_gpio_cfg_sense_input(PIN_USER_BTN, NRF_GPIO_PIN_PULLUP, x);\n"
    expect(len(chk.check_safeboot_source(bad)) == 1, "raw PIN_USER_BTN into nrf_gpio_ FAILS")

    expect(len(chk.check_safeboot_source(GOOD_SRC.replace("g_ADigitalPinMap", "pinMap"))) == 1,
           "dropping the pin-map lookup FAILS")

    expect(len(chk.check_safeboot_source(GOOD_SRC.replace("USER_BTN_PRESSED ==", "LOW =="))) == 1,
           "ignoring the button's polarity FAILS, even with the default #define present")

    expect(len(chk.check_safeboot_source(GOOD_SRC.replace("if (safeBootButtonInMap()) ", ""))) == 1,
           "reading the pin map without the -1 guard FAILS")

    ungated = GOOD_SRC.replace("if (wake != offband::SafeBootWake::TimedRestart) ", "")
    expect(len(chk.check_safeboot_source(ungated)) == 1, "an ungated SYSTEM_OFF FAILS")

    commented = GOOD_SRC.replace("const bool pressed_high = (USER_BTN_PRESSED == HIGH);",
                                 "// USER_BTN_PRESSED == HIGH")
    expect(len(chk.check_safeboot_source(commented)) == 1,
           "a mention in a comment does not satisfy a rule")


def test_live_tree():
    print("live tree")
    expect(chk.main([]) == 0, "the checked-in tree passes")


if __name__ == "__main__":
    test_variant_rule()
    test_exemptions_cannot_rot()
    test_source_rules()
    test_live_tree()
    print("\n%d failure(s)" % len(FAILURES))
    sys.exit(1 if FAILURES else 0)
