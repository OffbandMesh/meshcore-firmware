"""Badge env invariants: the values a one-line edit would silently revert (#1638).

Every invariant here is a flag or number in variants/qcc_badge/platformio.ini,
variants/qcc_badge/variant.h or src/helpers/NRF52Board.cpp that has no coverage today.
Deleting any of them leaves CI green, the matrix green and the badge wrong.

    LED_BLUE == (-1)                     the Arduino core takes P0.15 back and the three
                                         bright front LEDs strobe again (#1594/#1597)
    OFFBAND_HEARTBEAT_OFF_MS == 4890     the badge returns to the 1 Hz metronome the owner
                                         called "obnoxious"
    ON/OFF defaults stay 90/890          every OTHER nRF52 board's heartbeat moves,
                                         including the solar car companion it was sized for
    OFFBAND_NAME_FROM_BLE_MAC == "QCC-"  fresh badges fall back to pubkey-hex names
    ..._CHARS == 4 (= the header)        the name stops matching the badge labels (#1807)
    OFFBAND_TUNE_TRIGGER badge-only      sixty other variants compile the trigger
    OFFBAND_NAME_MAC_VERIFY diag-only    the SHIPPING image prints the BLE address (#1635)

WHAT THIS IS, SAID PLAINLY. These are source assertions, not behavioural tests. They
catch a deleted flag or a changed number. They pass any refactor that preserves the text
while changing behaviour, and they false-positive on a rename. Tripwires. They are not
evidence the badge works -- see #1641 for the only task in this epic that runs firmware.

THE TRAP THESE EXIST TO CLOSE, which is more specific than "these values are unasserted".
test/test_heartbeat_cadence has 12 passing cases and hardcodes its own copies:

    constexpr uint32_t kOn = 90;
    constexpr uint32_t kCarOff = 890;
    constexpr uint32_t kBadgeOff = 4890;

Those are COPIES, not links. Delete OFFBAND_HEARTBEAT_OFF_MS from the badge env, or change
the define in NRF52Board.cpp, and all 12 cases still pass -- they are testing three
integers that happen to be spelled the same as the real ones. A test that cannot see the
value it protects is decoration. So this file reads both sides and asserts they AGREE,
which is what turns those 12 cases into actual coverage of the shipped numbers.

Run: python scripts/test_qcc_badge_env.py
  or python -m pytest scripts/test_qcc_badge_env.py -q
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QCC = ROOT / "variants" / "qcc_badge"
INI = QCC / "platformio.ini"
VARIANT_H = QCC / "variant.h"
NRF52 = ROOT / "src" / "helpers" / "NRF52Board.cpp"
HB_TEST = ROOT / "test" / "test_heartbeat_cadence" / "test_heartbeat_cadence.cpp"
NAME_HEADER = ROOT / "src" / "helpers" / "NodeNameFromMac.h"


def _header_constant(pattern):
    m = re.search(pattern, NAME_HEADER.read_text())
    assert m, "cannot read %s from %s" % (pattern, NAME_HEADER)
    return m.group(1)


# The fresh-badge name format, read from the header the host tests exercise, so the env
# and the tested constants cannot drift apart (#1807).
NAME_PREFIX = _header_constant(r'kQccNamePrefix\s*=\s*"([^"]*)"')
NAME_MAC_CHARS = int(_header_constant(r"kQccNameMacChars\s*=\s*(\d+)"))

SHIPPING_ENV = "env:QCC_Badge_companion_radio_ble"
DIAG_SECTION = "qcc_badge_diag"


# --- reading the sources ----------------------------------------------------------------

def ini_sections(text):
    """platformio.ini split into {section_name: body}, bodies not interpolated.

    Deliberately textual. The point is to know which SECTION a flag is written in, and
    `pio project config` would resolve `extends` and report the diag env's flags as the
    shipping env's too -- erasing the exact distinction #1635 is about.
    """
    out, name, buf = {}, None, []
    for line in text.splitlines():
        m = re.match(r"\s*\[([^\]]+)\]\s*$", line)
        if m:
            if name is not None:
                out[name] = "\n".join(buf)
            name, buf = m.group(1), []
        elif name is not None:
            buf.append(line)
    if name is not None:
        out[name] = "\n".join(buf)
    return out


def strip_ini_comments(body):
    """Drop `;` comments so a flag named in prose is not mistaken for a set flag.

    This matters here: the shipping env's comment block now mentions
    OFFBAND_NAME_MAC_VERIFY by name, explaining why it was moved out. A naive substring
    search would read that explanation as the flag still being set and the #1635 check
    would pass forever on its own documentation.
    """
    return "\n".join(l.split(";", 1)[0] for l in body.splitlines())


def flag(body, name):
    """The value of `-D NAME=value` in one section body, or None. '' means bare -D NAME.

    A quoted value is matched WHOLE, including its spaces. `\\S+` alone stops at the first
    space, so OFFBAND_BLE_MORSE_TEXT='"I AM NOT WORTHY"' came back as `'"I` -- which then
    passed the letters-only check vacuously, because `I` is a perfectly good letter. The
    check looked present and tested nothing. Found by its own negative case.
    """
    m = re.search(r"-D\s+%s(?:=('[^']*'|\"[^\"]*\"|\S+))?(?:\s|$)" % re.escape(name),
                  strip_ini_comments(body))
    if not m:
        return None
    return m.group(1) if m.group(1) is not None else ""


def define(text, name):
    """The value of `#define NAME value` from a header, as written."""
    m = re.search(r"^#define\s+%s\s+(\S+)" % re.escape(name), text, re.M)
    return m.group(1) if m else None


def ifndef_default(text, name):
    """The fallback in `#ifndef NAME / #define NAME value / #endif`."""
    m = re.search(r"#ifndef\s+%s\s*\n\s*#define\s+%s\s+(\S+)" % (re.escape(name),
                                                                 re.escape(name)), text)
    return m.group(1) if m else None


def cpp_constexpr(text, name):
    """A `constexpr <type> name = <int>;` value from a test file."""
    m = re.search(r"constexpr\s+\w+\s+%s\s*=\s*(\d+)" % re.escape(name), text)
    return m.group(1) if m else None


def read_other_inis():
    """{label: text} for every platformio.ini that is NOT the badge's.

    Split out from the check below so the check is reachable with synthetic input. The
    first version scanned the real tree inline, which made the leak case untestable by
    construction -- the only way to exercise it would have been to actually plant the flag
    in a sibling variant. A check that cannot be made to fail is not a check yet.
    """
    out = {}
    root = ROOT / "platformio.ini"
    if root.exists():
        out["platformio.ini (root -- reaches EVERY variant)"] = root.read_text()
    for p in sorted((ROOT / "variants").glob("*/platformio.ini")):
        if p.parent.name != "qcc_badge":
            out["variants/%s" % p.parent.name] = p.read_text()
    return out


def trigger_stray_failures(others):
    """OFFBAND_TUNE_TRIGGER must reach the badge env and nothing else.

    Sixty-odd other variants build this same companion role. If the flag reaches them they
    compile the arriving-message rule and still link, so the matrix stays green and nothing
    anywhere reports it.
    """
    strays = sorted(label for label, text in others.items()
                    if flag(text, "OFFBAND_TUNE_TRIGGER") is not None)
    if not strays:
        return []
    return ["OFFBAND_TUNE_TRIGGER is set outside the badge env: %s. Those builds compile "
            "the trigger and stay green, so nothing would report it." % ", ".join(strays)]


# --- the invariants, as a pure function so the negatives can mutate the inputs ----------

def env_failures(ini_text, variant_text, nrf52_text, hb_test_text):
    """Every badge env invariant, checked. Returns failure messages; empty means healthy.

    Pure over its four inputs on purpose: the negative cases below hand it mutated copies
    and assert each check actually fires. A tripwire nobody has watched trip is a tripwire
    nobody knows is connected.
    """
    bad = []
    secs = ini_sections(ini_text)

    for needed in (SHIPPING_ENV, DIAG_SECTION):
        if needed not in secs:
            bad.append("variants/qcc_badge/platformio.ini has no [%s] section -- this "
                       "guard cannot see what it is protecting" % needed)
    if bad:
        return bad
    ship, diag = secs[SHIPPING_ENV], secs[DIAG_SECTION]

    # 1. LED_BLUE frees P0.15 from the Arduino core's advertising blink.
    led_blue = define(variant_text, "LED_BLUE")
    if led_blue != "(-1)":
        bad.append(
            "LED_BLUE is %s, expected (-1). The Arduino core toggles LED_BLUE from a "
            "FreeRTOS timer while BLE advertises; anything but -1 hands it P0.15 and the "
            "badge's three bright front LEDs strobe again (#1594/#1597)." % led_blue)

    # 2/3. The heartbeat numbers, and the link between the shipped values and the test
    #      that claims to cover them.
    badge_off = flag(ship, "OFFBAND_HEARTBEAT_OFF_MS")
    if badge_off != "4890":
        bad.append(
            "the badge env sets OFFBAND_HEARTBEAT_OFF_MS=%s, expected 4890. Without it "
            "the badge falls back to the ~1 Hz fleet default -- the metronome the owner "
            "called obnoxious. 4890 and not 4900 because it must sit just UNDER a 100 ms "
            "RTC wake boundary or the observed gap depends on jitter." % badge_off)

    on_default = ifndef_default(nrf52_text, "OFFBAND_HEARTBEAT_ON_MS")
    off_default = ifndef_default(nrf52_text, "OFFBAND_HEARTBEAT_OFF_MS")
    if on_default != "90" or off_default != "890":
        bad.append(
            "NRF52Board.cpp heartbeat defaults are ON=%s OFF=%s, expected 90/890. These "
            "are every OTHER nRF52 board's cadence, sized for the solar car companion "
            "where the blip is the only liveness signal and power binds."
            % (on_default, off_default))

    # The link. This is the check that gives test_heartbeat_cadence's 12 cases something
    # to actually protect.
    for const, actual, what in (("kOn", on_default, "the NRF52Board.cpp ON default"),
                                ("kCarOff", off_default, "the NRF52Board.cpp OFF default"),
                                ("kBadgeOff", badge_off, "the badge env's OFF value")):
        in_test = cpp_constexpr(hb_test_text, const)
        if in_test is None:
            bad.append(
                "test_heartbeat_cadence no longer defines %s. That file hardcodes the "
                "heartbeat numbers, so this guard is the only thing tying its 12 cases to "
                "the values the badge actually ships." % const)
        elif in_test != actual:
            bad.append(
                "test_heartbeat_cadence has %s = %s but %s is %s. The test is passing "
                "against a number the firmware does not use -- which is exactly the state "
                "this check exists to make impossible." % (const, in_test, what, actual))

    # 4. The naming format: the env's two flags, and the header's two constants, agree.
    #    #1807: "QCC-" + the last 4 of the MAC, matching the badge labels.
    prefix = flag(ship, "OFFBAND_NAME_FROM_BLE_MAC")
    want_prefix = "'\"%s\"'" % NAME_PREFIX
    if prefix != want_prefix:
        bad.append(
            "OFFBAND_NAME_FROM_BLE_MAC is %s, expected %s (NodeNameFromMac.h). Without it a "
            "fresh badge falls back to a pubkey-hex name, which is not the name in the "
            "owner's phone pairing list (#1598)." % (prefix, want_prefix))
    chars = flag(ship, "OFFBAND_NAME_FROM_BLE_MAC_CHARS")
    if chars != str(NAME_MAC_CHARS):
        bad.append(
            "OFFBAND_NAME_FROM_BLE_MAC_CHARS is %s, expected %d (NodeNameFromMac.h). The "
            "badge labels carry the last %d hex characters of the MAC, so a different count "
            "makes a badge harder to find against its label (#1807)."
            % (chars, NAME_MAC_CHARS, NAME_MAC_CHARS))

    # 5. The trigger is badge-scoped. Sixty other variants build this companion role.
    if flag(ship, "OFFBAND_TUNE_TRIGGER") != "1":
        bad.append("the badge env must set -D OFFBAND_TUNE_TRIGGER=1; without it the "
                   "arriving-message rule is compiled out and no badge can be triggered.")
    bad.extend(trigger_stray_failures(read_other_inis()))

    # 6. #1635: the BLE-address print is diagnostic-only. Asserting "absent everywhere"
    #    would just delete a useful check; asserting "diag yes, shipping no" is what makes
    #    the issue self-enforcing instead of a line somebody has to remember.
    if flag(diag, "OFFBAND_NAME_MAC_VERIFY") != "1":
        bad.append(
            "[%s] must set -D OFFBAND_NAME_MAC_VERIFY=1. It is the #1598 FICR-vs-"
            "SoftDevice equivalence check and it belongs with the diag surface."
            % DIAG_SECTION)
    if flag(ship, "OFFBAND_NAME_MAC_VERIFY") is not None:
        bad.append(
            "OFFBAND_NAME_MAC_VERIFY is set in [%s], the SHIPPING env, which the _diag env "
            "extends. It prints the device's BLE address and this repo is public, so the "
            "release image must not carry it (#1635). Move it to [%s]."
            % (SHIPPING_ENV, DIAG_SECTION))

    # 7. The Morse blink, whose text reaches morseFor() character by character.
    if flag(ship, "OFFBAND_BLE_MORSE") != "1":
        bad.append("the badge env must set -D OFFBAND_BLE_MORSE=1; without it P0.15 is "
                   "freed from the core and then driven by nothing (#1597).")
    text = flag(ship, "OFFBAND_BLE_MORSE_TEXT")
    if text is None:
        bad.append("the badge env must set OFFBAND_BLE_MORSE_TEXT; the blink has no "
                   "message without it.")
    else:
        inner = text.strip("'").strip('"')
        if not inner.strip():
            bad.append("OFFBAND_BLE_MORSE_TEXT is empty, so the LED never lights and a "
                       "wedged loop looks identical to a healthy one.")
        elif not re.fullmatch(r"[A-Za-z0-9 ]+", inner):
            bad.append(
                "OFFBAND_BLE_MORSE_TEXT is %r. morseFor() encodes letters and digits; "
                "anything else contributes NOTHING to the pattern -- no element, no gap -- "
                "so the badge silently blinks a shorter message than the string reads "
                "(#1640)." % inner)

    return bad


# --- positive ---------------------------------------------------------------------------

def _real():
    return (INI.read_text(), VARIANT_H.read_text(), NRF52.read_text(), HB_TEST.read_text())


def test_badge_env_invariants_hold():
    bad = env_failures(*_real())
    assert not bad, "badge env invariants broken:\n  - " + "\n  - ".join(bad)


# --- negative: every check must actually fire -------------------------------------------

def _fails_with(phrase, ini=None, variant=None, nrf52=None, hb=None):
    r, v, n, h = _real()
    bad = env_failures(ini or r, variant or v, nrf52 or n, hb or h)
    assert any(phrase in b for b in bad), (
        "expected a failure mentioning %r; got %s" % (phrase, bad or "NO FAILURES AT ALL"))


def _sanity():
    assert not env_failures(*_real()), "sanity: the real files must pass"


def test_negative_led_blue_reclaimed_by_the_core_is_caught():
    _sanity()
    _fails_with("strobe again", variant=_real()[1].replace("#define LED_BLUE             (-1)",
                                                           "#define LED_BLUE             PIN_LED"))


def test_negative_a_deleted_badge_heartbeat_value_is_caught():
    _sanity()
    _fails_with("obnoxious", ini=_real()[0].replace("  -D OFFBAND_HEARTBEAT_OFF_MS=4890\n", ""))


def test_negative_the_badge_heartbeat_back_on_a_wake_boundary_is_caught():
    _sanity()
    _fails_with("expected 4890", ini=_real()[0].replace("OFFBAND_HEARTBEAT_OFF_MS=4890",
                                                        "OFFBAND_HEARTBEAT_OFF_MS=4900"))


def test_negative_moving_the_fleet_heartbeat_default_is_caught():
    _sanity()
    _fails_with("every OTHER nRF52 board",
                nrf52=_real()[2].replace("#define OFFBAND_HEARTBEAT_OFF_MS 890",
                                         "#define OFFBAND_HEARTBEAT_OFF_MS 1890"))


def test_negative_the_test_drifting_from_the_shipped_value_is_caught():
    # THE one that matters most here. The env keeps 4890, the test keeps passing, and the
    # number it is passing against is no longer the number the badge ships.
    _sanity()
    _fails_with("passing against a number the firmware does not use",
                hb=_real()[3].replace("kBadgeOff = 4890", "kBadgeOff = 4900"))


def test_negative_a_vanished_test_constant_is_caught():
    _sanity()
    _fails_with("no longer defines kBadgeOff",
                hb=_real()[3].replace("kBadgeOff = 4890", "kSomethingElse = 4890"))


def test_negative_a_dropped_naming_prefix_is_caught():
    _sanity()
    _fails_with("pubkey-hex name",
                ini=_real()[0].replace("  -D OFFBAND_NAME_FROM_BLE_MAC='\"QCC-\"'\n", ""))


def test_negative_the_old_five_character_name_is_caught():
    # #1807: the labels carry the last 4 of the MAC. Reverting to 5 must not pass quietly.
    _sanity()
    _fails_with("harder to find against its label",
                ini=_real()[0].replace("  -D OFFBAND_NAME_FROM_BLE_MAC_CHARS=4\n",
                                       "  -D OFFBAND_NAME_FROM_BLE_MAC_CHARS=5\n"))


def test_negative_a_dropped_name_length_is_caught():
    # Without the flag the firmware falls back to its own default of 5 (MyMesh.cpp).
    _sanity()
    _fails_with("harder to find against its label",
                ini=_real()[0].replace("  -D OFFBAND_NAME_FROM_BLE_MAC_CHARS=4\n", ""))


def test_negative_the_old_prefix_without_the_hyphen_is_caught():
    _sanity()
    _fails_with("pubkey-hex name",
                ini=_real()[0].replace("OFFBAND_NAME_FROM_BLE_MAC='\"QCC-\"'",
                                       "OFFBAND_NAME_FROM_BLE_MAC='\"QCC\"'"))


def test_negative_a_badge_that_cannot_be_triggered_at_all_is_caught():
    _sanity()
    _fails_with("no badge can be triggered",
                ini=_real()[0].replace("  -D OFFBAND_TUNE_TRIGGER=1\n", ""))


def test_negative_the_trigger_leaking_to_the_fleet_is_caught():
    # Synthetic input, because the real tree is clean and the leak has to be observable
    # anyway. Both directions: the root ini, which reaches all 92 variants, and one
    # sibling variant.
    assert not trigger_stray_failures(read_other_inis()), \
        "sanity: no non-badge platformio.ini may set OFFBAND_TUNE_TRIGGER today"
    for label in ("platformio.ini (root -- reaches EVERY variant)", "variants/promicro"):
        bad = trigger_stray_failures({label: "build_flags =\n  -D OFFBAND_TUNE_TRIGGER=1\n"})
        assert bad and label in bad[0], \
            "a stray OFFBAND_TUNE_TRIGGER in %s must be reported; got %s" % (label, bad)
    # And a flag merely NAMED in a comment there is not a leak.
    assert not trigger_stray_failures(
        {"variants/promicro": "build_flags =\n  ; not -D OFFBAND_TUNE_TRIGGER=1 here\n"}), \
        "a commented-out flag must not count as set"


def test_negative_the_ble_address_print_returning_to_the_shipping_env_is_caught():
    # #1635, made self-enforcing. This is the mutation that proves it.
    _sanity()
    _fails_with("the SHIPPING env",
                ini=_real()[0].replace("  -D OFFBAND_NAME_FROM_BLE_MAC='\"QCC-\"'",
                                       "  -D OFFBAND_NAME_FROM_BLE_MAC='\"QCC-\"'\n"
                                       "  -D OFFBAND_NAME_MAC_VERIFY=1"))


def test_negative_losing_the_verify_flag_from_diag_is_caught():
    _sanity()
    _fails_with("belongs with the diag surface",
                ini=_real()[0].replace("  -D OFFBAND_NAME_MAC_VERIFY=1\n", ""))


def test_negative_a_mentioned_but_unset_flag_does_not_pass_as_set():
    # The comment block in the shipping env now names OFFBAND_NAME_MAC_VERIFY to explain
    # why it moved. A substring search would read that prose as the flag being set, and
    # the #1635 check would pass forever on its own documentation.
    _sanity()
    assert flag("  ; we moved -D OFFBAND_NAME_MAC_VERIFY=1 out of here\n  -D OTHER=1",
                "OFFBAND_NAME_MAC_VERIFY") is None, \
        "a flag named inside a comment must not read as set"


def _shipped_morse_literal():
    """The blink text exactly as the ini writes it, quotes included.

    Read from the ini rather than restated here: when the text changes (#1887), a copy in
    this file would stop matching, every replace() below would quietly do nothing, and the
    negatives would be checking an unmodified ini.
    """
    m = re.search(r"-D\s+OFFBAND_BLE_MORSE_TEXT=('[^']*')", _real()[0])
    assert m, "OFFBAND_BLE_MORSE_TEXT not found in the badge ini"
    return m.group(1)


def test_negative_morse_text_with_punctuation_is_caught():
    _sanity()
    lit = _shipped_morse_literal()
    _fails_with("contributes NOTHING",
                ini=_real()[0].replace(lit, lit[:-2] + "!" + lit[-2:]))


def test_negative_empty_morse_text_is_caught():
    _sanity()
    _fails_with("wedged loop looks identical",
                ini=_real()[0].replace(_shipped_morse_literal(), "'\"\"'"))


def test_negative_a_renamed_section_is_not_a_silent_pass():
    _sanity()
    _fails_with("cannot see what it is protecting",
                ini=_real()[0].replace("[qcc_badge_diag]", "[qcc_badge_diagnostics]"))


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("PASS %s" % name)
            except AssertionError as e:
                failures += 1
                print("FAIL %s: %s" % (name, e))
    print("\n%d failure(s)" % failures)
    sys.exit(1 if failures else 0)
