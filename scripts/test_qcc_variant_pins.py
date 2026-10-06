"""Pin-map guard for variants/qcc_badge (#1172).

The badge drives its buzzer from P0.06 and its LED from P0.08 -- the ProMicro
variant's default Serial1 TX/RX. Opening Serial1 on those defaults would hold the
buzzer on. This test fails if a badge role, or Serial1, lands on the wrong GPIO.
Run: python scripts/test_qcc_variant_pins.py
  or python -m pytest scripts/test_qcc_variant_pins.py -q
"""
import base64
import hashlib
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QCC = ROOT / "variants" / "qcc_badge"


def pin_map(path):
    src = path.read_text()
    body = src[src.index("g_ADigitalPinMap"):]
    body = body[body.index("{") + 1: body.index("}")]
    return [int(t) for t in re.findall(r"\d+", body)]


def define(name):
    m = re.search(r"#define\s+%s\s+\((\d+)\)" % name, (QCC / "variant.h").read_text())
    assert m, f"{name} must be defined as (N) in variant.h"
    return int(m.group(1))


def gpio(name):
    return pin_map(QCC / "variant.cpp")[define(name)]


def test_pin_map_is_the_promicro_map():
    assert pin_map(QCC / "variant.cpp") == pin_map(ROOT / "variants" / "promicro" / "variant.cpp")


def test_badge_roles_land_on_their_schematic_gpios():
    assert gpio("PIN_QCC_MSG_LED") == 8         # P0.08 -> Q2 -> MSG_LED, connector Q2D
    assert gpio("PIN_QCC_BUZZER") == 6          # P0.06 -> Q3 -> MUTE -> BZ1
    assert gpio("PIN_QCC_GPS_POWER") == 24      # P0.24 -> Q1 -> GPS header ground
    assert gpio("PIN_QCC_SPARE_GPIO33") == 33   # P1.01 -> "GPIO33" pad


def test_serial1_stays_off_the_buzzer_and_led():
    assert gpio("PIN_SERIAL1_TX") == 20   # P0.20 -> GPS RX
    assert gpio("PIN_SERIAL1_RX") == 22   # P0.22 <- GPS TX


def test_default_buses_are_the_badge_buses():
    # A library that calls SPI.begin() or Wire.begin() without setting pins must land
    # on the badge's real buses, not on RXEN, the GPS pins or the user button.
    assert gpio("PIN_SPI_SCK") == 43     # P1.11, LoRa SCK
    assert gpio("PIN_SPI_MISO") == 2     # P0.02, LoRa MISO
    assert gpio("PIN_SPI_MOSI") == 47    # P1.15, LoRa MOSI
    assert gpio("PIN_SPI_NSS") == 45     # P1.13, LoRa NSS
    assert gpio("PIN_WIRE_SDA") == 36    # P1.04, OLED / keyboard / SAO
    assert gpio("PIN_WIRE_SCL") == 11    # P0.11


def test_unused_second_buses_do_not_exist():
    # SPI1 would share P1.01 with the GPIO33 pad; Wire1 would share the LoRa NSS/MOSI.
    src = (QCC / "variant.h").read_text()
    assert re.search(r"#define\s+SPI_INTERFACES_COUNT\s+1\b", src)
    assert re.search(r"#define\s+WIRE_INTERFACES_COUNT\s+1\b", src)
    assert "PIN_SPI1_" not in src
    assert "PIN_WIRE1_" not in src


def test_badge_outputs_share_no_pin_with_any_bus():
    outputs = {gpio(n) for n in ("PIN_QCC_MSG_LED", "PIN_QCC_BUZZER",
                                 "PIN_QCC_GPS_POWER", "PIN_QCC_SPARE_GPIO33")}
    buses = {gpio(n) for n in ("PIN_SERIAL1_TX", "PIN_SERIAL1_RX", "PIN_SPI_SCK", "PIN_SPI_MISO",
                               "PIN_SPI_MOSI", "PIN_SPI_NSS", "PIN_WIRE_SDA", "PIN_WIRE_SCL")}
    assert outputs.isdisjoint(buses)
    assert 17 not in buses   # P0.17 is the radio's RXEN
    assert 32 not in buses   # P1.00 is the user button


def ini_flag(name):
    m = re.search(r"-D\s+%s=(\d+)" % name, (QCC / "platformio.ini").read_text())
    assert m, f"-D {name}=N missing from variants/qcc_badge/platformio.ini"
    return int(m.group(1))


def test_env_pin_flags_name_the_badge_pins():
    # Shared code reads these as bare numbers; they must equal the variant's names.
    assert ini_flag("PIN_STATUS_LED") == define("PIN_QCC_MSG_LED")
    assert ini_flag("PIN_BUZZER") == define("PIN_QCC_BUZZER")
    assert ini_flag("PIN_GPS_EN") == define("PIN_QCC_GPS_POWER")


def test_safeboot_and_board_share_one_divider_ratio():
    m = re.search(r"-D\s+SAFEBOOT_ADC_MULTIPLIER=([\d.]+)f", (QCC / "platformio.ini").read_text())
    assert m and float(m.group(1)) == 1.68


def test_safeboot_reads_the_battery_pin():
    # SafeBoot.cpp can't see PromicroBoard.h's PIN_VBAT_READ. Without this flag SafeBoot
    # compiles out and the D2 boot gate does nothing (#1176). It must name the pin the
    # board itself reads, and that pin must be the divider's P0.31.
    board_h = (ROOT / "variants" / "promicro" / "PromicroBoard.h").read_text()
    board_pin = int(re.search(r"#define\s+PIN_VBAT_READ\s+(\d+)", board_h).group(1))
    assert ini_flag("SAFEBOOT_PIN_VBAT_READ") == board_pin
    assert pin_map(QCC / "variant.cpp")[board_pin] == 31   # P0.31 <- R4/R5 divider


def test_diag_log_mirror_transmits_on_the_spare_pad():
    # The mirror is a UART transmitter: pointed at P0.06/P0.08 it would drive the
    # buzzer and LED MOSFETs, the same hazard as the ProMicro Serial1 default. It sits
    # on GPIO38, the one spare pad the pad ID beacon proved reaches the sniffer (#1210).
    m = re.search(r"-D\s+OFFBAND_LOG_MIRROR_TX_PIN=(\w+)", (QCC / "platformio.ini").read_text())
    assert m and m.group(1) == "PIN_QCC_SPARE_GPIO38"
    assert gpio("PIN_QCC_SPARE_GPIO38") == 38   # P1.06, not a badge output
    taken = {gpio(n) for n in ("PIN_QCC_MSG_LED", "PIN_QCC_BUZZER", "PIN_QCC_GPS_POWER",
                               "PIN_SERIAL1_TX", "PIN_SERIAL1_RX", "PIN_WIRE_SDA", "PIN_WIRE_SCL",
                               "PIN_SPI_SCK", "PIN_SPI_MISO", "PIN_SPI_MOSI", "PIN_SPI_NSS")}
    assert 38 not in taken


def beacon_pads():
    src = (QCC / "variant.h").read_text()
    m = re.search(r"#define\s+OFFBAND_PAD_BEACON_PADS\b(.*?)(?:\n\s*\n|\n#)", src, re.S)
    assert m, "OFFBAND_PAD_BEACON_PADS is missing from variant.h"
    return [(int(pin), name) for pin, name in re.findall(r'\{\s*(\d+)\s*,\s*"([^"]+)"\s*\}', m.group(1))]


def test_pad_beacon_drives_only_the_four_spare_pads():
    # #1210: the diag ID beacon sets these pins as outputs and transmits on them. Every
    # one must be a spare pad, never the buzzer (P0.06), the LED (P0.08), the GPS power
    # switch or a bus pin.
    pads = beacon_pads()
    assert [pin for pin, _ in pads] == [33, 34, 38, 39]
    for pin, name in pads:
        port, bit = divmod(pin, 32)
        assert name == "GPIO%d P%d.%02d" % (pin, port, bit), name
    taken = {gpio(n) for n in ("PIN_QCC_MSG_LED", "PIN_QCC_BUZZER", "PIN_QCC_GPS_POWER",
                               "PIN_SERIAL1_TX", "PIN_SERIAL1_RX", "PIN_WIRE_SDA", "PIN_WIRE_SCL")}
    assert not taken & {pin for pin, _ in pads}


def test_pad_beacon_is_diag_only():
    ini = (QCC / "platformio.ini").read_text()
    diag = ini.split("[qcc_badge_diag]", 1)[1].split("\n[", 1)[0]
    assert re.search(r"-D\s+OFFBAND_PAD_BEACON\b", diag), "the diag env must enable the beacon"
    assert len(re.findall(r"-D\s+OFFBAND_PAD_BEACON\b", ini)) == 1, "only the diag env may"


def test_sw1_is_the_active_low_user_button():
    # SW1 is P1.00, pulled up to VCC by 10 k and switched to GND (badge schematic), so
    # it reads LOW while pressed. The UI's button must be built active-low with the
    # pull-up on; otherwise every idle read would look like a press (#1206).
    assert ini_flag("PIN_USER_BTN") == 6
    assert pin_map(QCC / "variant.cpp")[6] == 32   # P1.00
    # Anchored to a line start, so a commented-out copy of the line cannot satisfy it.
    target = (QCC / "target.cpp").read_text()
    assert re.search(r"^\s*MomentaryButton\s+user_btn\(\s*PIN_USER_BTN\s*,\s*\d+\s*,\s*true\s*,\s*true\s*\)",
                     target, re.MULTILINE), "user_btn must be (PIN_USER_BTN, <long-press ms>, true, true)"


def test_badge_env_enables_the_keyboard():
    # The badge's CardKB-compatible keyboard (#1204/#1205): the UI only polls it when the
    # flag is set, and the driver must be compiled into the env for that to link.
    ini = (QCC / "platformio.ini").read_text()
    assert re.search(r"-D\s+UI_HAS_CARDKB=1\b", ini)
    assert "+<helpers/ui/CardKbInput.cpp>" in ini


def ini_value(name):
    """A -D flag's value as a float. ini_flag() only matches integers; the radio
    parameters are decimal, so a float-aware reader is needed."""
    m = re.search(r"-D\s+%s=([\d.]+)" % name, (QCC / "platformio.ini").read_text())
    assert m, f"-D {name}=<value> missing from variants/qcc_badge/platformio.ini"
    return float(m.group(1))


def test_badge_radio_defaults_are_the_qcc_conference_channel():
    # #1363: the badge ships on its own frequency for the conference; attendees retune
    # afterwards. 919.5 and 500.0 are both exactly representable in binary32, so they
    # survive the float round-trip through prefs and the CLI.
    assert ini_value("LORA_FREQ") == 919.5
    assert ini_value("LORA_BW") == 500.0
    assert ini_value("LORA_SF") == 7
    assert ini_value("LORA_CR") == 5        # CR 4:5


def badge_screens():
    return (ROOT / "examples" / "companion_radio" / "ui-new" / "BadgeScreens.cpp").read_text()


def strip_c_comments(text):
    """`text` with // and /* */ comments blanked, newlines preserved.

    Any check that looks for code by substring or counts braces has to run on code: a
    comment reading `/*}*/`, or prose naming the thing being checked for, otherwise
    satisfies or defeats the check. Newlines are kept so positions stay comparable and
    error messages still point at the right place.
    """
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            if j < 0:
                break
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if c == "\n" else " " for c in text[i:j]))
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def badge_screens_code():
    """BadgeScreens.cpp with `//` comments stripped.

    Checks that look for a call by substring must not match prose: a comment saying a
    screen "must not go through uiListRows()" is documentation, not a call site, and
    tripping on it is a false positive that teaches people to reword comments to appease
    a test. Block comments are left alone -- nothing here needs them stripped, and a
    naive strip would break a `/* */` inside a string literal.
    """
    out = []
    for line in badge_screens().splitlines():
        i = line.find("//")
        out.append(line if i < 0 else line[:i])
    return "\n".join(out)


def node_prefs():
    return (ROOT / "examples" / "companion_radio" / "NodePrefs.h").read_text()


def test_badge_text_sizes_are_nav_large_and_messages_medium():
    # #1362: Jeremy's ask -- large reads well as a menu row and badly as a message, where
    # characters per line is what matters on a 128x64 panel. Navigation keeps the shipped
    # large (BadgeLayout's kDefaultTextSize, which has its own static_assert); the badge's
    # env sets messages to medium. The enum is 0 large / 1 medium / 2 small.
    assert ini_flag("DEFAULT_UI_MSG_TEXT_SIZE") == 1, "the badge's message text must default to medium"
    # Navigation is NOT overridden in the badge env, so it takes NodePrefs' fallback.
    ini = (QCC / "platformio.ini").read_text()
    assert not re.search(r"-D\s+DEFAULT_UI_TEXT_SIZE=", ini), (
        "the badge does not override the navigation size; it keeps the shipped large default")
    m = re.search(r"#define\s+DEFAULT_UI_TEXT_SIZE\s+(\d+)", node_prefs())
    assert m and int(m.group(1)) == 0, "the navigation fallback must stay large"


def test_the_message_size_default_is_scoped_to_the_badge():
    # Sixty-one variants compile these screens and only the badge was asked to change, so
    # the fallback has to leave every other board exactly as it was. If someone replaces
    # this with a literal, 60 boards silently get a new message font -- the #973 class of
    # blast radius.
    m = re.search(r"#define\s+DEFAULT_UI_MSG_TEXT_SIZE\s+(\S+)", node_prefs())
    assert m, "NodePrefs.h must provide a DEFAULT_UI_MSG_TEXT_SIZE fallback"
    assert m.group(1) == "DEFAULT_UI_TEXT_SIZE", (
        "the message-size fallback must follow the navigation default, so boards that were "
        f"not asked to change do not; found {m.group(1)!r}")


def test_both_text_sizes_persist_under_distinct_keys():
    # Two prefs serialized through one key, or one pref serialized twice, would lose a
    # setting silently on reboot -- and a copy-paste of the def() line is exactly how that
    # happens. Keys are also load-bearing: /prefs.json is keyed, so renaming one orphans
    # every badge's saved choice.
    prefs = node_prefs()
    for field, key in (("ui_text_size", "txt"), ("ui_msg_text_size", "mtxt")):
        assert re.search(r"uint8_t\s+%s\s*=" % field, prefs), f"{field} must be a NodePrefs field"
        assert re.search(r'def\("%s",\s*_parent->%s\)' % (key, field), prefs), \
            f'{field} must be serialized as def("{key}", ...)'
    # distinct fields, distinct keys -- no aliasing in either direction
    assert prefs.count('def("txt"') == 1 and prefs.count('def("mtxt"') == 1
    assert prefs.count("_parent->ui_text_size)") == 1
    assert prefs.count("_parent->ui_msg_text_size)") == 1


_SIG = re.compile(r'^[A-Za-z_][\w:<>,&*\s]*?\b(\w+::\w+|\w+)\s*\([^;]*\)\s*\{')


def face_sites(src):
    """Every body-face read, attributed to the function it sits in.

    Returns {"nav": [fn, ...], "msg": [fn, ...]}. The accessor definitions themselves
    are excluded; everything else is a call site.
    """
    out = {"nav": [], "msg": []}
    current = "(file scope)"
    for line in src.splitlines():
        m = _SIG.match(line)
        if m:
            current = m.group(1)
        for key, fn_name in (("nav", "uiNavBody"), ("msg", "uiMsgBody")):
            if f"{fn_name}()" in line and f"const Face& {fn_name}" not in line:
                out[key].append(current)
    return out


# #1362: the routing IS the feature, so the inventory is pinned rather than inferred.
# An adversarial review called a pattern-matching version of this test "theatre" -- a
# regex cannot follow indirection. Pinning the exact set answers that differently: a new
# screen, a removed one, or a rerouted one all fail here until someone states which
# surface it belongs to. The test cannot be fooled by drift; it can only be updated
# deliberately.
# Counts, not just names: a SECOND call site inside a function that is already on the
# list would leave a set unchanged, so the inventory would miss exactly the drift it
# exists to catch. Each function takes its face once, at the top of its draw.
EXPECTED_NAV_SITES = {
    "listRow": 1,                   # the shared settings-style row
    "ContactsScreen::render": 1,
    "NearbyScreen::render": 1,
    "StatusScreen::drawAs": 1,
    "SettingsScreen::drawGate": 1,
    "SettingsScreen::render": 1,
    "BatteryScreen::render": 1,
    "ZonePickerScreen::render": 1,
    "GpsScreen::render": 1,
}
EXPECTED_MSG_SITES = {
    "InboxScreen::drawItem": 1,
    "InboxScreen::render": 1,
    "ThreadScreen::drawRow": 1,
    "ThreadScreen::drawCompose": 1,
    "ThreadScreen::drawEditor": 1,
    "ThreadScreen::render": 1,
}


def test_the_body_face_inventory_is_exactly_what_was_agreed():
    sites = face_sites(badge_screens())
    nav, msg = Counter(sites["nav"]), Counter(sites["msg"])
    for label, got, want in (("MESSAGE", msg, EXPECTED_MSG_SITES),
                             ("NAVIGATION", nav, EXPECTED_NAV_SITES)):
        if dict(got) != want:
            extra = {k: v for k, v in got.items() if want.get(k) != v}
            missing = {k: v for k, v in want.items() if got.get(k) != v}
            raise AssertionError(
                f"the {label}-face inventory changed; classify the difference "
                f"deliberately.\n  unexpected/changed: {sorted(extra.items())}\n"
                f"  missing/changed:    {sorted(missing.items())}")
    # No function may read both faces -- that is a screen drawing itself two sizes.
    both = set(nav) & set(msg)
    assert not both, f"these read BOTH faces: {sorted(both)}"


# Draw helpers in BadgeUi.h, and which argument carries the row or the y.
#   (d, face, row, ...)        -> index 2
#   textAt(d, face, x, y, ...) -> index 3
_ROW_ARG = {"line": 2, "lineRight": 2, "fillRow": 2, "bar": 2, "textAt": 3, "breadcrumb": 3}


def _split_args(text, start):
    """Split the argument list of a call whose '(' is at `text[start]`."""
    depth, arg, args = 0, [], []
    for ch in text[start:]:
        if ch in "([":
            depth += 1
            if depth == 1:
                continue
        elif ch in ")]":
            depth -= 1
            if depth == 0:
                args.append("".join(arg).strip())
                return args
        if depth == 1 and ch == ",":
            args.append("".join(arg).strip())
            arg = []
        else:
            arg.append(ch)
    return None


def title_row_draws(src):
    """Every draw on row/y 0 inside a message screen, as (function, helper, face)."""
    found = []
    current = "(file scope)"
    MESSAGE_SCREENS = ("InboxScreen", "ThreadScreen")
    for line in src.splitlines():
        m = _SIG.match(line)
        if m:
            current = m.group(1)
            continue
        if not current.startswith(MESSAGE_SCREENS):
            continue
        for helper, idx in _ROW_ARG.items():
            for m2 in re.finditer(r"\b%s\s*\(" % helper, line):
                args = _split_args(line, m2.end() - 1)
                if not args or len(args) <= idx or len(args) < 2:
                    continue
                if args[idx] == "0":          # the title row
                    found.append((current, helper, args[1]))
    return found


def test_everything_on_the_title_row_uses_the_pinned_title_face():
    # #1370: the title bar is chrome, so EVERYTHING in it is chrome -- the title text, the
    # unread count, the breadcrumb. This exists because the first cut of the change drew
    # " Messages" in the pinned face and the unread count in the message face, two fonts in
    # one bar, with textPx measuring the wrong one so the right edge moved too. The
    # arithmetic tests all passed; nothing looked at the screens. Adversarial review found
    # it, so it is a test now.
    draws = title_row_draws(badge_screens_code())
    assert draws, "no title-row draws found on the message screens; recheck this test"
    ALLOWED = {"kTitle", "uiTitleFace()"}
    for fn, helper, face in draws:
        assert face in ALLOWED, (
            f"{fn} draws on the title row via {helper}() with `{face}` -- the title bar is "
            f"chrome and must use the pinned title face ({' or '.join(sorted(ALLOWED))})")


def test_message_screens_are_message_screens():
    # The classification itself, stated independently of the pinned list above, so that
    # updating the inventory cannot quietly also change what "message screen" means.
    sites = face_sites(badge_screens())
    MESSAGE_SCREENS = ("InboxScreen", "ThreadScreen")
    for fn in sites["msg"]:
        assert fn.startswith(MESSAGE_SCREENS), \
            f"{fn} reads the MESSAGE face but is not an Inbox or Thread screen"
    for fn in sites["nav"]:
        assert not fn.startswith(MESSAGE_SCREENS), \
            f"{fn} is a message screen but reads the NAVIGATION face"


def test_a_face_is_only_ever_taken_at_the_top_of_a_draw():
    # Closes the indirection hole the review named: the accessors are usable only as
    # `const Face& kBody = ui*Body();`. Any other use -- an inline call inside an
    # expression, a face stashed in a member, a helper that reads the pref for itself --
    # would put a face somewhere the inventory above cannot see.
    for line in badge_screens().splitlines():
        for fn_name in ("uiNavBody", "uiMsgBody"):
            if f"{fn_name}()" not in line or f"const Face& {fn_name}" in line:
                continue
            assert re.match(r"^\s*const Face& kBody = %s\(\);" % fn_name, line), \
                f"a body face must be taken as `const Face& kBody = {fn_name}();` -- got: {line.strip()}"


def test_helpers_that_read_a_face_are_called_only_from_that_surface():
    # listRow reads the navigation face internally, so calling it from a message screen
    # would draw a nav-sized row on a message surface -- the one real way to defeat the
    # inventory above. Any future helper that reads a face gets the same treatment,
    # because the helper list is derived from the source rather than hardcoded.
    src = badge_screens()
    sites = face_sites(src)
    helpers = {fn: "nav" for fn in sites["nav"] if "::" not in fn and fn != "(file scope)"}
    helpers.update({fn: "msg" for fn in sites["msg"] if "::" not in fn and fn != "(file scope)"})
    assert "listRow" in helpers, "listRow should still be a face-reading helper; recheck this test"

    MESSAGE_SCREENS = ("InboxScreen", "ThreadScreen")
    current = "(file scope)"
    for line in src.splitlines():
        m = _SIG.match(line)
        if m:
            current = m.group(1)
            continue
        for helper, surface in helpers.items():
            if not re.search(r"\b%s\s*\(" % helper, line):
                continue
            caller_is_msg = current.startswith(MESSAGE_SCREENS)
            if surface == "nav":
                assert not caller_is_msg, (
                    f"{current} is a message screen but calls {helper}(), which reads the "
                    "navigation face")
            else:
                assert caller_is_msg, (
                    f"{current} is not a message screen but calls {helper}(), which reads "
                    "the message face")


def test_list_rows_cannot_silently_pick_the_wrong_face():
    # uiListRows takes the face rather than reading the pref, and has NO no-argument
    # overload, so a screen cannot size its rows with one face and draw them with the
    # other. Restoring a no-arg version would make that mistake possible again and
    # compile cleanly, which is why this is a test and not a comment.
    src = badge_screens_code()   # comments stripped: prose must not trip a call check
    assert re.search(r"int\s+uiListRows\(const\s+Face&\s*\w*\)", src), \
        "uiListRows must take the body face"
    assert not re.search(r"int\s+uiListRows\(\s*\)", src), \
        "uiListRows must NOT have a no-argument overload"
    assert "uiListRows()" not in src, "every uiListRows call must name its face"
    assert not re.search(r"\buiBody\(\)", src), \
        "uiBody() is gone; call sites must say uiNavBody() or uiMsgBody()"


def test_both_text_size_rows_exist_and_cycle():
    # The settings screen is the only way to change these on a badge (a companion has no
    # CLI), so a row that exists but does not cycle, or cycles without being listed, is a
    # setting the owner cannot reach.
    hdr = (ROOT / "examples" / "companion_radio" / "ui-new" / "BadgeScreens.h").read_text()
    assert re.search(r"NavTextSize\s*,\s*MsgTextSize", hdr), \
        "both rows must exist, adjacent, in the Row enum"
    assert "TextSize," not in hdr.replace("NavTextSize,", "").replace("MsgTextSize,", ""), \
        "the old single TextSize row must be gone"
    src = badge_screens()
    for row in ("NavTextSize", "MsgTextSize"):
        assert re.search(r"case %s:" % row, src), f"{row} must be handled in act()"
        assert re.search(r"row == %s" % row, src), f"{row} must be registered in cycles()"
    assert 'label = "Nav text"' in src and 'label = "Message text"' in src, \
        "both rows must be labelled in the settings list"


# ---- #1364: the channels a fresh badge ships holding -------------------------------

EXPECTED_PREFLASH_CHANNELS = ["#queencitycon", "#okimesh", "#test", "#echo", "#offband"]


def preflash_table():
    """[(name, psk_base64), ...] as QccChannels.h declares them, in order."""
    src = (QCC / "QccChannels.h").read_text()
    body = src[src.index("kQccPreflashChannels"):]
    body = body[body.index("{") + 1: body.index("};")]
    return re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}', body)


def test_the_preflashed_channels_are_the_five_agreed():
    names = [n for n, _ in preflash_table()]
    assert names == EXPECTED_PREFLASH_CHANNELS, (
        f"the pre-flashed channel list changed.\n  got:      {names}\n"
        f"  expected: {EXPECTED_PREFLASH_CHANNELS}")
    # Stored with the '#', as the client stores them -- the '#' is part of the name AND
    # part of what the key is derived from, so dropping it changes both.
    for n in names:
        assert n.startswith("#"), f"{n} must be stored with its leading '#'"


def test_every_preflashed_key_is_the_derived_hashtag_psk():
    # The point of this test: derive each key HERE, independently, rather than comparing
    # the header against a copy of itself. A transcribed or stale key would give a badge a
    # channel with the right name and the wrong secret -- it would look configured, join
    # nothing, and the failure would show up as "the badge cannot hear #queencitycon".
    #
    # Derivation, from the client as source of record (lib/models/channel.dart,
    # derivePskFromHashtag): the first 16 bytes of SHA-256 over the name INCLUDING '#'.
    for name, psk_b64 in preflash_table():
        want = base64.b64encode(hashlib.sha256(name.encode()).digest()[:16]).decode()
        assert psk_b64 == want, (
            f"{name}: key does not match its derivation\n  in header: {psk_b64}\n"
            f"  derived:   {want}")
        # addChannel() decodes and requires 16 or 32 bytes.
        assert len(base64.b64decode(psk_b64)) == 16, f"{name}: key must decode to 16 bytes"


def test_public_is_not_in_the_preflash_table():
    # Public is added for every board from PUBLIC_GROUP_PSK. Repeating it here would give a
    # fresh badge two Publics, and with a different key the second would be a dead channel
    # that looks real.
    names = [n.lower() for n, _ in preflash_table()]
    assert not any("public" in n for n in names), "Public is added by MyMesh, not seeded here"


def test_preflashed_channels_are_seeded_on_a_fresh_badge_only():
    # The guard is correctness, not caution. loadChannels() writes slots through
    # setChannel() and never advances `num_channels`, so on a configured node seeding
    # before the load leaves seeds in the slots past the stored ones (a list that grows at
    # every flash), and seeding after it appends at a stale `num_channels` of 1, on top of
    # a user's channel. Both orderings corrupt; only the guard is safe.
    # Comments stripped before anything is located or counted. The brace check below is
    # defeated by a comment containing a brace -- `/*}*/` -- which the review pointed out,
    # and prose about the guard would otherwise count as the guard.
    src = strip_c_comments((ROOT / "examples" / "companion_radio" / "MyMesh.cpp").read_text())

    # Presence is asserted before anything is located. str.index() raises ValueError, not
    # AssertionError, so a landmark that has been removed -- which is exactly the
    # regression this test exists to catch -- would abort the run with a traceback instead
    # of naming the problem, and take the rest of the suite with it.
    LANDMARKS = {
        "probe": "_store->hasChannels()",
        "load": "_store->loadChannels(this)",
        "guard": "if (!had_stored_channels)",
        "seed": "kQccPreflashChannels",
    }
    for what, needle in LANDMARKS.items():
        assert needle in src, (
            f"MyMesh.cpp no longer contains the {what} (`{needle}`) -- pre-flashed channels "
            "must be seeded on a fresh badge only, and that guard is gone")
    at = {what: src.index(needle) for what, needle in LANDMARKS.items()}

    assert at["probe"] < at["load"], (
        "hasChannels() must be read BEFORE loadChannels(), or it reports the state the "
        "load just created rather than the state the badge arrived in")
    assert at["seed"] > at["load"], \
        "channels must be seeded after the load, into the real slot count"
    assert at["guard"] < at["seed"], \
        "the seeding must be inside the `!had_stored_channels` guard"
    tail = src[at["guard"]:at["seed"]]
    assert "}" not in tail.split("{", 1)[-1], (
        "the guard's block closes before the seeding -- the seeding is not actually guarded")
    seed = at["seed"]

    # And it persists what it added, or the next boot does it all again.
    assert "saveChannels();" in src[seed:seed + 1200], \
        "seeded channels must be saved, or they are re-seeded on every boot"


def test_the_preflash_table_is_badge_scoped():
    # Sixty other variants build this companion role. The table reaches them only if the
    # flag does, so the flag must be set by the badge's env and nowhere else.
    assert ini_flag("OFFBAND_PREFLASH_CHANNELS") == 1, (
        "the badge env must set -D OFFBAND_PREFLASH_CHANNELS=1; without it a fresh badge "
        "ships with Public alone and the conference channels are silently absent")
    root_ini = (ROOT / "platformio.ini").read_text()
    assert "OFFBAND_PREFLASH_CHANNELS" not in root_ini, \
        "the flag must not be set globally -- it would reach every variant"
    others = [p for p in (ROOT / "variants").glob("*/platformio.ini")
              if p.parent.name != "qcc_badge"
              and "OFFBAND_PREFLASH_CHANNELS" in p.read_text()]
    assert not others, f"only the badge may set the flag; also set by: {[p.parent.name for p in others]}"
    # And the include is guarded, so a board without the flag does not even compile it in.
    mymesh = (ROOT / "examples" / "companion_radio" / "MyMesh.cpp").read_text()
    inc = mymesh.index('#include "QccChannels.h"')
    before = mymesh[:inc]
    assert before.rstrip().endswith("#endif") is False, "sanity: expected a guard directly above"
    assert "#ifdef OFFBAND_PREFLASH_CHANNELS" in before, "the include must be flag-guarded"
    assert before.rindex("#ifdef OFFBAND_PREFLASH_CHANNELS") > before.rindex("#include \"OffbandConfigProtocol.h\""), \
        "the guard must be the one immediately governing the QccChannels include"


def test_badge_bandwidth_survives_the_prefs_sanitiser():
    # MyMesh clamps a loaded bw into a fixed range. If someone tightens that ceiling
    # below the badge's bandwidth, the badge would come up on a bandwidth this file does
    # not declare -- silently, because constrain() clamps rather than rejecting. Read the
    # real bound out of the source instead of assuming it.
    mymesh = (ROOT / "examples" / "companion_radio" / "MyMesh.cpp").read_text()
    m = re.search(r"_prefs\.bw\s*=\s*constrain\(_prefs\.bw,\s*([\d.]+)f,\s*([\d.]+)f\)", mymesh)
    assert m, "MyMesh.cpp must clamp _prefs.bw with constrain(_prefs.bw, lo, hi)"
    lo, hi = float(m.group(1)), float(m.group(2))
    assert lo <= ini_value("LORA_BW") <= hi, f"LORA_BW is outside the prefs clamp [{lo}, {hi}]"


def test_compiled_radio_defaults_are_first_boot_seeds_only():
    # Owner constraint (#1363): "it should only be a first-flash default." The compiled
    # LORA_* values are seeds -- MyMesh assigns them to _prefs, and loadPrefs() runs
    # AFTERWARDS, so a stored prefs file wins and DFU preserves that file. A badge that
    # already has a frequency therefore keeps it across a flash.
    #
    # A lexical ordering check alone is weak (adversarial review, #1363): index() finds
    # only the FIRST occurrence, so a second call site could configure the radio from the
    # seeds before loadPrefs ever runs, and an #ifdef could compile loadPrefs out for one
    # build while the source text still reads in the right order. So this asserts four
    # things, not one: the landmarks are unique, every radio-apply site is after the load,
    # none of them is nested in conditional compilation relative to the others, and
    # nothing returns between the seed and the load.
    src = (ROOT / "examples" / "companion_radio" / "MyMesh.cpp").read_text()
    SEED, LOAD, APPLY = ("_prefs.freq = LORA_FREQ", "_store->loadPrefs(_prefs)",
                         "radio_driver.setParams(_prefs.freq")

    # 1. unique landmarks -- a duplicate seed or load would make index() meaningless
    assert src.count(SEED) == 1, "LORA_* must be seeded in exactly one place"
    assert src.count(LOAD) == 1, "loadPrefs must be called in exactly one place"

    seed, load = src.index(SEED), src.index(LOAD)
    assert seed < load, ("LORA_* must be seeded BEFORE loadPrefs(), or a flash would "
                         "overwrite a configured badge's radio settings")

    # 2. EVERY apply site, not just the first, must read post-load prefs
    applies = [i for i in range(len(src)) if src.startswith(APPLY, i)]
    assert applies, "MyMesh.cpp must configure the radio from _prefs"
    assert min(applies) > load, ("the radio is configured from the seeds before "
                                 "loadPrefs() runs -- a flash would retune the badge")

    # 3. none of the landmarks may sit at a different #if nesting depth from the others,
    #    which is what would let a build variant drop loadPrefs and keep the seeds
    def if_depth(upto):
        d = 0
        for line in src[:upto].splitlines():
            s = line.strip()
            if s.startswith("#if"):
                d += 1
            elif s.startswith("#endif"):
                d -= 1
        return d

    depths = {if_depth(seed), if_depth(load)} | {if_depth(a) for a in applies}
    assert depths == {0}, (f"seed/load/apply must all be unconditional; #if depths {depths}")

    # 4. no early exit between the seed and the load
    between = src[seed:load]
    for word in ("return", "goto"):
        assert word not in between, f"'{word}' between the seed and loadPrefs() could skip the load"


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as e:
                failures += 1
                print(f"FAIL {name}: {e}")
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
