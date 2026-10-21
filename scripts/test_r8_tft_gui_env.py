"""R8 TFT GUI env invariants: the pins Heltec's schematic fixes (#1675, #1676).

The Heltec V4 R8 with the Expansion Kit V2 runs the companion GUI from
`[env:heltec_v4_r8_tft_companion_radio_ble_gui]`. Its pins come from the Expansion
Board V2.03 schematic, and four of them are one-line edits away from a board that
builds clean and misbehaves:

    PIN_TFT_RST == 21, PIN_TOUCH_RST == -1
        GPIO21 is ONE net, LCD_RST TP_RST. Exactly one reset pulse, from the panel
        driver. A touch reset of 21 after the panel is up blanks the panel; no reset at
        all leaves both chips unreset.
    PIN_TOUCH_SDA/SCL == the board's I2C pins, and UITask then uses Wire
        The touch chip shares the sensors' bus. A second I2C controller on the same
        pins breaks the touch chip and the sensors.
    P_LORA_TX_LED undefined, PIN_USER_BTN_2 == 46, active HIGH
        GPIO46 is the TX LED and the kit's KEY button, which closes to 3.3 V. Read as
        active-low with a pull-up, the button never registers.
    Every P_LORA_TX_LED use in HeltecV4R8Board.cpp is guarded
        Otherwise the GUI env, which undefines it, doesn't build.

These are source assertions, not behaviour: they catch a changed or deleted flag. The
bench run on the owner's R8 (#1679) is what shows the board works.

Run: python scripts/test_r8_tft_gui_env.py
  or python -m pytest scripts/test_r8_tft_gui_env.py -q
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INI = ROOT / "variants" / "heltec_v4_r8" / "platformio.ini"
BOARD = ROOT / "variants" / "heltec_v4_r8" / "HeltecV4R8Board.cpp"
UITASK = ROOT / "examples" / "companion_radio" / "ui-gui" / "UITask.cpp"
CI = ROOT / ".github" / "workflows" / "ci.yml"
SDCARD = ROOT / "src" / "helpers" / "gui" / "hw" / "SdCard.cpp"
PANEL = ROOT / "src" / "helpers" / "gui" / "hw" / "GuiPanel.cpp"
GUI_TREES = (ROOT / "src" / "helpers" / "gui", ROOT / "examples" / "companion_radio" / "ui-gui")
ROOT_INI = ROOT / "platformio.ini"
NATIVE = "env:native_heltec_v4_r8_gui"

BASE = "heltec_v4_r8_tft"
GUI = "env:heltec_v4_r8_tft_companion_radio_ble_gui"
DIAG = "env:heltec_v4_r8_tft_companion_radio_ble_gui_diag"


def ini_sections(text):
    """platformio.ini split into {section_name: body}, textual, not interpolated."""
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


def strip_comments(body, mark):
    """Drop comments so a flag named in prose doesn't read as set."""
    return "\n".join(l.split(mark, 1)[0] for l in body.splitlines())


def flag(body, name):
    """The LAST `-D NAME=value` in a section (a later -D wins), or None."""
    hits = re.findall(r"-D\s+%s(?:=(\S+))?(?:\s|$)" % re.escape(name), strip_comments(body, ";"))
    return hits[-1] if hits else None


def undefines(body, name):
    """True if the section undefines NAME with -U."""
    return re.search(r"-U\s*%s(?:\s|$)" % re.escape(name), strip_comments(body, ";")) is not None


def unguarded_tx_led_uses(cpp):
    """Lines using P_LORA_TX_LED outside an `#ifdef P_LORA_TX_LED` block."""
    bad, depth, guard = [], 0, []
    for n, raw in enumerate(cpp.splitlines(), 1):
        line = raw.split("//", 1)[0]
        s = line.strip()
        if s.startswith("#if"):
            guard.append("P_LORA_TX_LED" in s and not s.startswith("#ifndef"))
        elif s.startswith("#endif"):
            if guard:
                guard.pop()
        elif s.startswith("#else") and guard:
            guard[-1] = False
        elif "P_LORA_TX_LED" in line and not any(guard):
            bad.append(n)
    return bad


def sd_bus_failures(gui, sdcard, panel, gui_sources):
    """The SD card shares the panel's SPI bus (#1677): CS GPIO3, MISO GPIO45.

    ESP-IDF's bus lock keeps the card and the panel's DMA flushes apart, but only
    if both are devices on the SAME host, the bus came up with MISO, and nothing
    starts a second driver on those pins. Arduino's SPI.begin() would do exactly
    that: it writes SPI2's registers directly.
    """
    out = []
    if flag(gui, "PIN_SD_CS") != "3":
        out.append("PIN_SD_CS must be 3, the kit's SD_SPI_CS (got %r)" % flag(gui, "PIN_SD_CS"))
    if flag(gui, "PIN_TFT_MISO") != "45":
        out.append("PIN_TFT_MISO must be 45: the card reads through it (got %r)" % flag(gui, "PIN_TFT_MISO"))
    code = strip_comments(sdcard, "//")
    if not re.search(r"host\.slot\s*=\s*SPI2_HOST", code) or not re.search(r"dev\.host_id\s*=\s*SPI2_HOST", code):
        out.append("SdCard must put the card on SPI2_HOST, the panel's bus; another host means "
                   "two drivers on the same pins with no lock between them")
    panel_code = strip_comments(panel, "//")
    if "_panel.begin(SPI2_HOST" not in panel_code:
        out.append("GuiPanel must bring the panel up on SPI2_HOST, the bus SdCard joins")
    if not re.search(r"pins\.miso\s*=\s*PIN_TFT_MISO", panel_code):
        out.append("GuiPanel must bring the bus up with MISO (pins.miso = PIN_TFT_MISO), "
                   "or the card can't answer")
    for name, text in gui_sources.items():
        if re.search(r"\bSPI\.begin\s*\(", strip_comments(text, "//")):
            out.append("%s calls SPI.begin(): Arduino's driver writes SPI2's registers directly "
                       "and breaks the panel and card's shared bus" % name)
    return out


def read_gui_sources():
    found = {}
    for tree in GUI_TREES:
        for p in tree.rglob("*"):
            if p.suffix in (".cpp", ".h", ".c"):
                found[str(p.relative_to(ROOT))] = p.read_text(encoding="utf-8", errors="replace")
    return found


def failures(ini, board, uitask, ci, root_ini, sdcard="", panel="", gui_sources=None):
    if gui_sources is None:
        gui_sources = {}
    out = []
    secs = ini_sections(ini)
    if GUI not in secs or BASE not in secs:
        return ["%s or [%s] is missing: this check cannot see what it is protecting" % (GUI, BASE)]
    gui, base = secs[GUI], secs[BASE]

    if "${%s.build_flags}" % BASE in strip_comments(gui, ";"):
        out.append("the GUI env must not inherit [%s]'s build_flags: their reset pins differ, and "
                   "PlatformIO applies -U after every -D, so an override can't be expressed" % BASE)

    if flag(gui, "PIN_TFT_RST") != "21":
        out.append("PIN_TFT_RST must be 21 in the GUI env: the panel driver owns the shared "
                   "LCD_RST TP_RST pulse (got %r)" % flag(gui, "PIN_TFT_RST"))
    if flag(gui, "PIN_TOUCH_RST") != "-1":
        out.append("PIN_TOUCH_RST must be -1 in the GUI env: a touch reset of GPIO21 after the "
                   "panel is up blanks the panel (got %r)" % flag(gui, "PIN_TOUCH_RST"))

    bsda, bscl = flag(base, "PIN_BOARD_SDA"), flag(base, "PIN_BOARD_SCL")
    tsda, tscl = flag(gui, "PIN_TOUCH_SDA"), flag(gui, "PIN_TOUCH_SCL")
    if (tsda, tscl) != ("17", "18") or (tsda, tscl) != (bsda, bscl):
        out.append("touch must be on the board I2C bus 17/18 (TP_SDA/TP_SCL = I2C_SDA/I2C_SCL): "
                   "touch %s/%s, board %s/%s" % (tsda, tscl, bsda, bscl))
    if not re.search(r"PIN_TOUCH_SDA\s*==\s*PIN_BOARD_SDA\s*\n\s*Chsc6xTouch\s+s_touch\(Wire\);",
                     strip_comments(uitask, "//")):
        out.append("UITask must construct the touch driver on Wire when touch shares the board "
                   "bus; a second controller on the same pins breaks touch and the sensors")

    if not undefines(gui, "P_LORA_TX_LED"):
        out.append("the GUI env must undefine P_LORA_TX_LED: GPIO46 is the kit's KEY button")
    if flag(gui, "P_LORA_TX_LED") is not None:
        out.append("the GUI env must not define P_LORA_TX_LED at all: GPIO46 is the kit's KEY "
                   "button, and an -U/-D pair in one env is resolved by PlatformIO, not by order")
    if flag(gui, "PIN_USER_BTN_2") != "46":
        out.append("PIN_USER_BTN_2 must be 46, the kit's KEY (got %r)" % flag(gui, "PIN_USER_BTN_2"))
    if flag(gui, "PIN_USER_BTN_2_ACTIVE") != "HIGH":
        out.append("PIN_USER_BTN_2_ACTIVE must be HIGH: KEY closes to 3.3 V, so read active-low "
                   "it never registers")
    if "digitalRead(PIN_USER_BTN_2) == PIN_USER_BTN_2_ACTIVE" not in uitask:
        out.append("UITask must read AUX against PIN_USER_BTN_2_ACTIVE, not a fixed LOW")

    bad = unguarded_tx_led_uses(board)
    if bad:
        out.append("HeltecV4R8Board.cpp uses P_LORA_TX_LED unguarded at line(s) %s; the GUI env "
                   "undefines it" % bad)
    if not re.search(r"#if\s+defined\(PIN_TOUCH_RST\)\s*&&\s*PIN_TOUCH_RST\s*>=\s*0", board):
        out.append("HeltecV4R8Board::begin() must skip the touch-reset pulse when PIN_TOUCH_RST "
                   "is -1, or it drives pin -1")

    native = ini_sections(root_ini).get(NATIVE)
    if native is None:
        out.append("[%s] is missing: the board's GUI configuration has no host test" % NATIVE)
    else:
        if flag(native, "OFFBAND_TEST_R8_GUI_CONFIG") is None:
            out.append("[%s] must set OFFBAND_TEST_R8_GUI_CONFIG, or a drifted copy runs the LED "
                       "tests instead and passes" % NATIVE)
        if flag(native, "PIN_TOUCH_RST") != flag(gui, "PIN_TOUCH_RST") or \
                flag(native, "P_LORA_TX_LED") is not None:
            out.append("[%s] is out of step with the GUI env: touch reset %r vs %r, TX LED %r" % (
                NATIVE, flag(native, "PIN_TOUCH_RST"), flag(gui, "PIN_TOUCH_RST"),
                flag(native, "P_LORA_TX_LED")))

    out.extend(sd_bus_failures(gui, sdcard, panel, gui_sources))

    if DIAG not in secs or "extends = %s" % GUI not in secs[DIAG]:
        out.append("the _diag twin must extend the GUI env")
    for env in (GUI, DIAG):
        name = env.split(":", 1)[1]
        if not re.search(r"^\s*-\s*%s\s*$" % re.escape(name), ci, re.M):
            out.append("%s is not in the CI build matrix" % name)
    return out


def _real():
    return (INI.read_text(encoding="utf-8"), BOARD.read_text(encoding="utf-8"),
            UITASK.read_text(encoding="utf-8"), CI.read_text(encoding="utf-8"),
            ROOT_INI.read_text(encoding="utf-8"), SDCARD.read_text(encoding="utf-8"),
            PANEL.read_text(encoding="utf-8"), read_gui_sources())


def _fails_with(fragment, **swap):
    parts = dict(zip(("ini", "board", "uitask", "ci", "root_ini", "sdcard", "panel", "gui_sources"),
                     _real()))
    parts.update(swap)
    got = failures(**parts)
    assert any(fragment in f for f in got), "expected a failure naming %r, got %s" % (fragment, got)


def _gui_section_replace(old, new):
    """The real ini with one edit made inside the GUI env only."""
    ini = _real()[0]
    start = ini.index("[%s]" % GUI)
    end = ini.index("[%s]" % DIAG)
    body = ini[start:end]
    assert old in body, "sanity: %r must be in the GUI env" % old
    return ini[:start] + body.replace(old, new) + ini[end:]


def test_the_real_tree_passes():
    got = failures(*_real())
    assert not got, "\n".join(got)


def test_negative_touch_resetting_the_shared_line_is_caught():
    _fails_with("blanks the panel", ini=_gui_section_replace("-D PIN_TOUCH_RST=-1", "-D PIN_TOUCH_RST=21"))


def test_negative_no_panel_reset_is_caught():
    _fails_with("owns the shared", ini=_gui_section_replace("-D PIN_TFT_RST=21", "-D PIN_TFT_RST=-1"))


def test_negative_touch_on_its_own_bus_is_caught():
    _fails_with("board I2C bus", ini=_gui_section_replace("-D PIN_TOUCH_SDA=17", "-D PIN_TOUCH_SDA=47"))


def test_negative_touch_on_a_second_controller_is_caught():
    _fails_with("construct the touch driver on Wire",
                uitask=_real()[2].replace("Chsc6xTouch s_touch(Wire);", "Chsc6xTouch s_touch;"))


def test_negative_keeping_the_tx_led_is_caught():
    _fails_with("undefine P_LORA_TX_LED", ini=_gui_section_replace("  -UP_LORA_TX_LED\n", ""))


def test_negative_redefining_the_tx_led_after_undefining_it_is_caught():
    _fails_with("must not define P_LORA_TX_LED", ini=_gui_section_replace(
        "  -UP_LORA_TX_LED\n", "  -UP_LORA_TX_LED\n  -D P_LORA_TX_LED=46\n"))


def test_negative_an_active_low_key_is_caught():
    _fails_with("closes to 3.3 V", ini=_gui_section_replace("-D PIN_USER_BTN_2_ACTIVE=HIGH", ""))


def test_negative_a_fixed_low_aux_read_is_caught():
    _fails_with("not a fixed LOW", uitask=_real()[2].replace(
        "digitalRead(PIN_USER_BTN_2) == PIN_USER_BTN_2_ACTIVE", "digitalRead(PIN_USER_BTN_2) == LOW"))


def test_negative_an_unguarded_tx_led_write_is_caught():
    board = _real()[1] + "\nvoid x() { digitalWrite(P_LORA_TX_LED, LOW); }\n"
    _fails_with("unguarded", board=board)


def test_negative_an_unguarded_touch_reset_pulse_is_caught():
    _fails_with("drives pin -1", board=_real()[1].replace(
        "#if defined(PIN_TOUCH_RST) && PIN_TOUCH_RST >= 0", "#ifdef PIN_TOUCH_RST"))


def test_negative_dropping_the_env_from_ci_is_caught():
    _fails_with("not in the CI build matrix", ci=_real()[3].replace(
        "          - heltec_v4_r8_tft_companion_radio_ble_gui\n", ""))


def test_negative_a_renamed_env_is_not_a_silent_pass():
    _fails_with("cannot see what it is protecting", ini=_real()[0].replace(
        "[%s]" % GUI, "[env:heltec_v4_r8_tft_gui]"))


def test_negative_inheriting_the_ui_new_tft_flags_is_caught():
    _fails_with("must not inherit", ini=_gui_section_replace(
        "  ${Heltec_v4_r8.build_flags}\n", "  ${heltec_v4_r8_tft.build_flags}\n"))


def test_negative_a_native_env_without_the_marker_is_caught():
    _fails_with("runs the LED tests", root_ini=_real()[4].replace(
        "  -D OFFBAND_TEST_R8_GUI_CONFIG\n", ""))


def test_negative_a_native_env_out_of_step_is_caught():
    _fails_with("out of step", root_ini=_real()[4].replace(
        "  -D OFFBAND_TEST_R8_GUI_CONFIG\n  -D PIN_TOUCH_RST=-1",
        "  -D OFFBAND_TEST_R8_GUI_CONFIG\n  -D PIN_TOUCH_RST=21"))


def test_negative_the_card_on_the_wrong_cs_is_caught():
    _fails_with("PIN_SD_CS must be 3", ini=_gui_section_replace("-D PIN_SD_CS=3", "-D PIN_SD_CS=39"))


def test_negative_the_card_on_another_host_is_caught():
    _fails_with("SPI2_HOST, the panel's bus",
                sdcard=_real()[5].replace("host.slot = SPI2_HOST", "host.slot = SPI3_HOST"))


def test_negative_a_bus_without_miso_is_caught():
    _fails_with("with MISO", panel=_real()[6].replace("pins.miso = PIN_TFT_MISO", "pins.miso = -1"))


def test_negative_an_arduino_spi_begin_in_the_gui_is_caught():
    srcs = dict(_real()[7])
    srcs["src/helpers/gui/hw/Extra.cpp"] = "void f() { SPI.begin(16, 45, 15); }\n"
    _fails_with("calls SPI.begin()", gui_sources=srcs)


def test_negative_spi_begin_named_in_a_comment_is_not_a_call():
    srcs = {"x.cpp": "// nothing here may call SPI.begin()\n"}
    assert not [f for f in sd_bus_failures("  -D PIN_SD_CS=3\n  -D PIN_TFT_MISO=45\n",
                                           _real()[5], _real()[6], srcs) if "SPI.begin" in f]


def test_negative_a_flag_named_in_a_comment_does_not_read_as_set():
    assert flag("  ; was -D PIN_TOUCH_RST=21 before #1676\n  -D OTHER=1", "PIN_TOUCH_RST") is None


def test_negative_the_guard_scanner_sees_a_guarded_use_as_guarded():
    assert unguarded_tx_led_uses("#ifdef P_LORA_TX_LED\n  digitalWrite(P_LORA_TX_LED, LOW);\n#endif\n") == []
    assert unguarded_tx_led_uses("#ifdef X\n#else\n  digitalWrite(P_LORA_TX_LED, LOW);\n#endif\n") == [3]


if __name__ == "__main__":
    n_fail = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("PASS %s" % name)
            except AssertionError as e:
                n_fail += 1
                print("FAIL %s: %s" % (name, e))
    print("\n%d failure(s)" % n_fail)
    sys.exit(1 if n_fail else 0)
