"""OKIMesh Solar env invariants: the values a one-line edit would silently revert (#1626).

These are source assertions, not behavioral tests. They catch a deleted flag, a changed
number or a dropped call. They are not evidence the board works: no OKIMesh board has run
this firmware. The build matrix compiles the two non-diag envs; this checks what compiling
can't.

    Serial1 on P0.20/P0.22, GPS power on P0.24    GPS is dead (stock ProMicro's pins)
    1.68 divider                                  every battery line reads ~8% wrong
    3100 mV, one line                             the owner's figure, for unprotected cells
    LPCOMP on AIN7 above WAKE_MV                  a refused node never comes back by itself
    handoff flag and both call sites              a running node runs its cell flat
    no badge-only flags                           no display, buzzer or con channels here

Run: python scripts/test_okimesh_solar_env.py
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_qcc_badge_env import flag, ini_sections  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
INI = ROOT / "variants" / "okimesh_solar" / "platformio.ini"
VARIANT_H = ROOT / "variants" / "okimesh_solar" / "variant.h"
REPEATER = ROOT / "examples" / "simple_repeater" / "main.cpp"
COMPANION = ROOT / "examples" / "companion_radio" / "main.cpp"

BOARD = "OKIMesh_Solar"
DIAG = "okimesh_solar_diag"
ENVS = ("env:OKIMesh_Solar_repeater", "env:OKIMesh_Solar_companion_radio_ble",
        "env:OKIMesh_Solar_repeater_diag", "env:OKIMesh_Solar_companion_radio_ble_diag")
DIVIDER_RATIO = 1.68
NOMINAL_VDD_MV = 3300
THE_LINE = "3100"
BADGE_ONLY = ("PIN_BUZZER", "PIN_QCC_ACTIVITY_LED", "OFFBAND_LED_ACTIVITY", "UI_HAS_CARDKB",
              "OFFBAND_EVENT_SPLASH", "OFFBAND_PREFLASH_CHANNELS", "OFFBAND_TUNE_TRIGGER",
              "OFFBAND_BLE_MORSE", "QCC_BADGE", "DISPLAY_CLASS", "DEFAULT_TONE_SENT")
CALL = re.compile(r"#if defined\(OFFBAND_LOW_BATT_HANDOFF\)\s*\n\s*offband::lowBattHandoffTick\(board\);")


def env_failures(ini_text, variant_text, repeater_text, companion_text):
    """Every invariant, checked. Pure over its inputs so the negative cases can mutate them."""
    bad = []
    secs = ini_sections(ini_text)
    for needed in (BOARD, DIAG) + ENVS:
        if needed not in secs:
            bad.append("variants/okimesh_solar/platformio.ini has no [%s] section" % needed)
    if bad:
        return bad
    base, diag = secs[BOARD], secs[DIAG]

    for name, want in (("PIN_GPS_RX", "3"), ("PIN_GPS_TX", "4"), ("PIN_GPS_EN", "5"),
                       ("SAFEBOOT_PIN_VBAT_READ", "17"), ("SAFEBOOT_ADC_MULTIPLIER", "1.68f"),
                       ("SAFEBOOT_ADC_SAMPLE_US", "40")):
        if flag(base, name) != want:
            bad.append("%s is %s, expected %s, per the board layout" % (name, flag(base, name), want))
    for name, want in (("PIN_SERIAL1_TX", "(3)"), ("PIN_SERIAL1_RX", "(4)")):
        m = re.search(r"#define\s+%s\s+(\(\d+\))" % name, variant_text)
        if not m or m.group(1) != want:
            bad.append("%s must be %s: the GPS header is on P0.20/P0.22, not the ProMicro default" % (name, want))
    if not re.search(r"#define\s+LED_BLUE\s+\(-1\)", variant_text):
        bad.append("LED_BLUE must be (-1), or the Arduino core takes P0.15 back while advertising")

    for name in ("DEFAULT_SAFE_BOOT_SLEEP_MV", "AUTO_SHUTDOWN_MILLIVOLTS", "BATT_MIN_MILLIVOLTS"):
        if flag(base, name) != THE_LINE:
            bad.append("%s is %s, expected %s: the owner's one line for this board"
                       % (name, flag(base, name), THE_LINE))

    if flag(base, "BATTERY_LPCOMP_INPUT") != "NRF_LPCOMP_INPUT_7":
        bad.append("BATTERY_LPCOMP_INPUT must be NRF_LPCOMP_INPUT_7: the divider is on P0.31, AIN7")
    ref = re.fullmatch(r"NRF_LPCOMP_REF_SUPPLY_(\d+)_(8|16)", flag(base, "BATTERY_LPCOMP_THRESHOLD") or "")
    wake, full = flag(base, "DEFAULT_SAFE_BOOT_WAKE_MV"), flag(base, "BATT_MAX_MILLIVOLTS")
    if ref is None or wake is None or full is None:
        bad.append("the LPCOMP wake line cannot be checked without its threshold, WAKE_MV and BATT_MAX")
    else:
        line = DIVIDER_RATIO * NOMINAL_VDD_MV * int(ref.group(1)) / int(ref.group(2))
        if line <= int(wake):
            bad.append("the wake line is %.0f mV, at or under WAKE_MV %s: the node would wake, "
                       "refuse, and sleep above the line with no crossing left" % (line, wake))
        if line >= int(full):
            bad.append("the wake line is %.0f mV, at or over %s mV: a charge might never reach it" % (line, full))

    if flag(base, "OFFBAND_LOW_BATT_HANDOFF") != "1":
        bad.append("OFFBAND_LOW_BATT_HANDOFF must be set: with no display, nothing else acts on the cutoff")
    for label, text in (("simple_repeater", repeater_text), ("companion_radio", companion_text)):
        if not CALL.search(text):
            bad.append("%s/main.cpp must call lowBattHandoffTick under OFFBAND_LOW_BATT_HANDOFF" % label)

    for name in BADGE_ONLY:
        for sec in (BOARD, DIAG) + ENVS:
            if flag(secs[sec], name) is not None:
                bad.append("[%s] sets %s, which is badge-only: this board has none of that" % (sec, name))

    if flag(diag, "SAFEBOOT_LOG_VDD") is None:
        bad.append("[%s] must set SAFEBOOT_LOG_VDD: the wake line is a fraction of VDD" % DIAG)
    for sec in (BOARD, ENVS[0], ENVS[1]):
        if flag(secs[sec], "SAFEBOOT_LOG_VDD") is not None or flag(secs[sec], "OFFBAND_FORCE_CAPLOG") is not None:
            bad.append("[%s] carries a diagnostic flag; those belong in [%s] only" % (sec, DIAG))
    return bad


def _real():
    return (INI.read_text(), VARIANT_H.read_text(), REPEATER.read_text(), COMPANION.read_text())


def test_env_invariants_hold():
    bad = env_failures(*_real())
    assert not bad, "invariants broken:\n  - " + "\n  - ".join(bad)


def _fails_with(phrase, old=None, new="", which=0):
    files = list(_real())
    assert not env_failures(*files), "sanity: the real files must pass"
    assert old in files[which], "mutation target %r is gone" % old
    files[which] = files[which].replace(old, new)
    bad = env_failures(*files)
    assert any(phrase in b for b in bad), "expected %r; got %s" % (phrase, bad or "NO FAILURES")


def test_negative_stock_promicro_uart_pins():
    _fails_with("not the ProMicro default", "#define PIN_SERIAL1_TX       (3)", "#define PIN_SERIAL1_TX       (1)", 1)


def test_negative_a_changed_gps_pin():
    _fails_with("PIN_GPS_EN", "-D PIN_GPS_EN=5", "-D PIN_GPS_EN=7")


def test_negative_the_stock_promicro_divider():
    _fails_with("SAFEBOOT_ADC_MULTIPLIER", "SAFEBOOT_ADC_MULTIPLIER=1.68f", "SAFEBOOT_ADC_MULTIPLIER=1.815f")


def test_negative_the_core_reclaiming_the_led():
    _fails_with("LED_BLUE", "#define LED_BLUE             (-1)", "#define LED_BLUE             PIN_LED", 1)


def test_negative_a_moved_refuse_line():
    _fails_with("DEFAULT_SAFE_BOOT_SLEEP_MV", "DEFAULT_SAFE_BOOT_SLEEP_MV=3100", "DEFAULT_SAFE_BOOT_SLEEP_MV=3500")


def test_negative_a_moved_cutoff():
    _fails_with("AUTO_SHUTDOWN_MILLIVOLTS", "AUTO_SHUTDOWN_MILLIVOLTS=3100", "AUTO_SHUTDOWN_MILLIVOLTS=3000")


def test_negative_a_wake_line_under_wake_mv():
    _fails_with("no crossing left", "NRF_LPCOMP_REF_SUPPLY_11_16", "NRF_LPCOMP_REF_SUPPLY_5_8")


def test_negative_a_wake_line_too_high():
    _fails_with("might never reach", "NRF_LPCOMP_REF_SUPPLY_11_16", "NRF_LPCOMP_REF_SUPPLY_6_8")


def test_negative_the_comparator_on_the_wrong_input():
    _fails_with("AIN7", "BATTERY_LPCOMP_INPUT=NRF_LPCOMP_INPUT_7", "BATTERY_LPCOMP_INPUT=NRF_LPCOMP_INPUT_3")


def test_negative_no_handoff_flag():
    _fails_with("nothing else acts", "  -D OFFBAND_LOW_BATT_HANDOFF=1\n", "")


def test_negative_a_repeater_that_never_checks():
    _fails_with("simple_repeater/main.cpp", "offband::lowBattHandoffTick(board);", "", 2)


def test_negative_a_companion_that_never_checks():
    _fails_with("companion_radio/main.cpp", "offband::lowBattHandoffTick(board);", "", 3)


def test_negative_a_badge_flag_creeping_in():
    _fails_with("badge-only", "  -D ENV_INCLUDE_GPS=1\n", "  -D ENV_INCLUDE_GPS=1\n  -D PIN_BUZZER=1\n")


def test_negative_losing_the_vdd_log():
    _fails_with("SAFEBOOT_LOG_VDD", "  -D SAFEBOOT_LOG_VDD\n", "")


def test_negative_a_diag_flag_in_the_shipping_image():
    _fails_with("diagnostic flag", "  -D OFFBAND_LOW_BATT_HANDOFF=1\n",
                "  -D OFFBAND_LOW_BATT_HANDOFF=1\n  -D OFFBAND_FORCE_CAPLOG\n")


def test_negative_a_renamed_section():
    _fails_with("has no [OKIMesh_Solar] section", "[OKIMesh_Solar]\n", "[OKI]\n")


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn) and fn.__module__ == __name__:
            try:
                fn()
                print("PASS", name)
            except AssertionError as e:
                failures += 1
                print("FAIL", name, e)
    print("\n%d failure(s)" % failures)
    sys.exit(1 if failures else 0)
