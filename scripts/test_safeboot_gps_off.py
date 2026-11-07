"""SafeBoot switches the GPS off before a low-battery hold (#2032).

Measured on the QCC badge: a refused boot held with the GPS powered, drawing 52-77 mA from
the cell for the whole hold. SafeBoot's refusal path now drives the GPS enable to its off
level before the SOS blink and the sleep. This guard fails if that call goes missing, moves
after the sleep, or is commented out, and if the pin stops resolving the way the runtime
GPS code resolves it.

Run: python scripts/test_safeboot_gps_off.py
Negative cases are the point: each rule is shown tripping on a broken copy of the source.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = (ROOT / "src" / "SafeBoot.cpp").read_text()
NMEA = (ROOT / "src" / "helpers" / "sensors" / "MicroNMEALocationProvider.h").read_text()
BADGE_INI = (ROOT / "variants" / "qcc_badge" / "platformio.ini").read_text()


def _strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def refusal_turns_gps_off(src):
    """True when the refusal path calls safeBootGpsOff() before the SOS and the sleep."""
    code = _strip_comments(src)
    below = code.rfind('"[SafeBoot] Vbat=%u mV below')
    if below < 0:
        return False
    gps = code.find("safeBootGpsOff();", below)
    sos = code.find("blinkSafeBootSos();", below)
    sleep = code.find("enterSafeBootSleep(sleep_secs, wake);", below)
    return 0 <= gps < sos < sleep


def pin_resolves_like_runtime(src):
    """GPS_EN before PIN_GPS_EN, GPS_EN_ACTIVE before PIN_GPS_EN_ACTIVE, default HIGH."""
    code = _strip_comments(src)
    pin = re.search(r"#if defined\(GPS_EN\)\s*#define SAFEBOOT_GPS_EN GPS_EN\s*"
                    r"#elif defined\(PIN_GPS_EN\)\s*#define SAFEBOOT_GPS_EN PIN_GPS_EN", code)
    act = re.search(r"#if defined\(GPS_EN_ACTIVE\)\s*#define SAFEBOOT_GPS_EN_ACTIVE GPS_EN_ACTIVE\s*"
                    r"#elif defined\(PIN_GPS_EN_ACTIVE\)\s*#define SAFEBOOT_GPS_EN_ACTIVE PIN_GPS_EN_ACTIVE\s*"
                    r"#else\s*#define SAFEBOOT_GPS_EN_ACTIVE HIGH", code)
    return bool(pin and act)


# ---- the real tree -------------------------------------------------------------------

def test_the_refusal_path_turns_the_gps_off_first():
    assert refusal_turns_gps_off(SRC), \
        "SafeBoot's refusal path must call safeBootGpsOff() before blinkSafeBootSos() and the sleep"


def test_safeboot_resolves_the_pin_like_the_runtime_gps_code():
    assert pin_resolves_like_runtime(SRC)
    # ...and the runtime still resolves it that way, with HIGH as the default.
    assert re.search(r"#ifdef PIN_GPS_EN_ACTIVE\s*#define GPS_EN_ACTIVE PIN_GPS_EN_ACTIVE\s*"
                     r"#else\s*#define GPS_EN_ACTIVE HIGH", NMEA), \
        "MicroNMEALocationProvider.h changed how it resolves GPS_EN_ACTIVE; match it in SafeBoot.cpp"


def test_the_badge_declares_its_gps_enable():
    assert re.search(r"^\s*-D PIN_GPS_EN=5\b", BADGE_INI, re.M), \
        "the QCC badge's GPS switch (P0.24, pin 5) is what this fix turns off"


# ---- negative cases ------------------------------------------------------------------

GOOD = '''
    mesh_log_print(MLOG_BOOT, "[SafeBoot] Vbat=%u mV below safe threshold", mv);
    safeBootGpsOff();
    blinkSafeBootSos();
    enterSafeBootSleep(sleep_secs, wake);
'''


def test_negative_a_missing_call_fails():
    assert refusal_turns_gps_off(GOOD)
    assert not refusal_turns_gps_off(GOOD.replace("    safeBootGpsOff();\n", ""))


def test_negative_a_call_after_the_sleep_fails():
    moved = GOOD.replace("    safeBootGpsOff();\n", "") + "    safeBootGpsOff();\n"
    assert not refusal_turns_gps_off(moved)


def test_negative_a_call_after_the_sos_fails():
    swapped = GOOD.replace("    safeBootGpsOff();\n    blinkSafeBootSos();\n",
                           "    blinkSafeBootSos();\n    safeBootGpsOff();\n")
    assert not refusal_turns_gps_off(swapped)


def test_negative_a_commented_out_call_fails():
    assert not refusal_turns_gps_off(GOOD.replace("safeBootGpsOff();", "// safeBootGpsOff();"))
    assert not refusal_turns_gps_off(GOOD.replace("safeBootGpsOff();", "/* safeBootGpsOff(); */"))


def test_negative_a_default_of_low_fails():
    flipped = SRC.replace("#define SAFEBOOT_GPS_EN_ACTIVE HIGH", "#define SAFEBOOT_GPS_EN_ACTIVE LOW")
    assert not pin_resolves_like_runtime(flipped)


def test_negative_pin_gps_en_taking_precedence_fails():
    swapped = SRC.replace("#if defined(GPS_EN)\n  #define SAFEBOOT_GPS_EN GPS_EN\n#elif defined(PIN_GPS_EN)\n  #define SAFEBOOT_GPS_EN PIN_GPS_EN",
                          "#if defined(PIN_GPS_EN)\n  #define SAFEBOOT_GPS_EN PIN_GPS_EN\n#elif defined(GPS_EN)\n  #define SAFEBOOT_GPS_EN GPS_EN")
    assert swapped != SRC, "fixture no longer matches SafeBoot.cpp's text"
    assert not pin_resolves_like_runtime(swapped)


if __name__ == "__main__":
    failures = 0
    for name, fn in list(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("  pass  %s" % name)
            except AssertionError as e:
                failures += 1
                print("  FAIL  %s: %s" % (name, e))
    sys.exit(1 if failures else 0)
