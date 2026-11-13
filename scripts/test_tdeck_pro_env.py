"""T-Deck Pro env invariants: V1.1 pins, one shared SPI bus, a full diag surface (#2033, #2035).

The T-Deck Pro's e-paper panel (CS 34), SX1262 (CS 3) and micro-SD (CS 48) share one
SPI bus, SCK 36 / MOSI 33 / MISO 47. A pin routes to one SPI peripheral at a time, so:

    every pin matches LilyGo's V1.1 schematic and pin header (#2015)
    the radio uses the panel driver's own SPIClass (SPI1) and begins no second one
    TDeckProBoard::begin() drives every chip-select high, and the radio's regulator on
    the radio carries LilyGo's V1.1 settings: TCXO 2.4 V, DIO2 as RF switch, 140 mA
    every env in the variant is a _diag env carrying the fleet diag surface (bring-up
        directive 2026-08-22), and each one is in the CI build matrix
    the GUI env (#2009) draws through the e-paper GuiPanel, and no env turns on the
        GUI's card driver (PIN_SD_CS), which would run the radio's bus through the IDF
    the GUI env builds the 1-bit font set (fonts_eink/), which holds the same fonts as
        fonts/, every one at 1 bpp
    the GUI env selects the cst66xx touch driver (#2110, #2111) with INT 12 and RST 38,
        on the board's own I2C pins, and no second touch driver; UITask builds it on the
        board's Wire, and the driver never starts a bus (a second begin on the same pins
        breaks every device on them)
    the GUI env selects the TCA8418 keyboard (#2149, #2152), never the T-Deck's (its
        address, 0x55, is this board's fuel gauge); UITask builds it on the board's Wire
        with INT 15 and the key backlight, drains it in the panel's BUSY wait, and never
        lets a Backspace repeat become Back; the driver never starts a bus; and the
        native suite runs the key-map tests

Source assertions: the bench unit (C5, #2037) is what shows the panel and radio work.

Run: python scripts/test_tdeck_pro_env.py
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_r8_tft_gui_env import flag, ini_sections, strip_comments  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
VARIANT = ROOT / "variants" / "lilygo_tdeck_pro"
INI = VARIANT / "platformio.ini"
TARGET = VARIANT / "target.cpp"
BOARD = VARIANT / "TDeckProBoard.cpp"
DISPLAY = ROOT / "src" / "helpers" / "ui" / "GxEPDDisplay.cpp"
EINK_PANEL = ROOT / "src" / "helpers" / "gui" / "hw" / "EinkPanel.cpp"
UITASK = ROOT / "examples" / "companion_radio" / "ui-gui" / "UITask.cpp"
TOUCH_DRIVER = ROOT / "src" / "helpers" / "gui" / "hw" / "Cst66xxTouch.cpp"
KB_DRIVER = ROOT / "src" / "helpers" / "gui" / "hw" / "Tca8418Keyboard.cpp"
ROOT_INI = ROOT / "platformio.ini"
KEYS_TEST = "test_tdeck_pro_keys"
FONTS = ROOT / "src" / "helpers" / "gui" / "fonts"
FONTS_EINK = ROOT / "src" / "helpers" / "gui" / "fonts_eink"
CI = ROOT / ".github" / "workflows" / "ci.yml"

HW = "LilyGo_TDeck_Pro_hw"
DIAG = "tdeck_pro_diag"
GUI_ENV = "env:LilyGo_TDeck_Pro_companion_radio_ble_gui_diag"
TEXT_ENV = "env:LilyGo_TDeck_Pro_companion_radio_ble_diag"
# What each env must set to draw on the right panel class.
ENV_FLAGS = {
    GUI_ENV: {"DISPLAY_CLASS": "GuiPanel", "GUI_PANEL_EINK": "1", "OFFBAND_GUI": "1"},
    TEXT_ENV: {"DISPLAY_CLASS": "GxEPDDisplay", "DISPLAY_ROTATION": "0"},
}

# LilyGo's V1.1 schematic and examples/factory2/utilities.h, branch HD-V2-250915 (#2015).
HW_PINS = {
    "P_LORA_SCLK": "36", "P_LORA_MISO": "47", "P_LORA_MOSI": "33", "P_LORA_NSS": "3",
    "P_LORA_RESET": "4", "P_LORA_DIO_1": "5", "P_LORA_BUSY": "6", "P_LORA_EN": "46",
    "PIN_DISPLAY_SCLK": "36", "PIN_DISPLAY_MISO": "47", "PIN_DISPLAY_MOSI": "33",
    "PIN_DISPLAY_CS": "34", "PIN_DISPLAY_DC": "35", "PIN_DISPLAY_BUSY": "37",
    "PIN_DISPLAY_RST": "16", "PIN_DISPLAY_FRONTLIGHT": "45", "TDECK_PRO_SD_CS": "48",
    "PIN_GPS_EN": "39", "PIN_MODEM_EN": "41", "PIN_KEYBOARD_LED": "42", "PIN_KEYBOARD_INT": "15",
    "PIN_BOARD_SDA": "13", "PIN_BOARD_SCL": "14", "PIN_USER_BTN": "0",
}
HW_FLAGS = {
    "EINK_DISPLAY_MODEL": "GxEPD2_310_GDEQ031T10",
    # LilyGo's V1.1 peri_lora.cpp: setTCXO(2.4), setDio2AsRfSwitch(), setCurrentLimit(140).
    "SX126X_DIO3_TCXO_VOLTAGE": "2.4f", "SX126X_DIO2_AS_RF_SWITCH": "true",
    "SX126X_CURRENT_LIMIT": "140",
}
# Touch (#2110): LilyGo examples/factory2/utilities.h, plan row 4. The chip found on
# tdeck-pro-1 (#2108) is the cst66xx family, and only that driver is supported.
TOUCH_PINS = {"PIN_TOUCH_INT": "12", "PIN_TOUCH_RST": "38"}
TOUCH_ON_BOARD_BUS = (("PIN_TOUCH_SDA", "PIN_BOARD_SDA"), ("PIN_TOUCH_SCL", "PIN_BOARD_SCL"))
OTHER_TOUCH_DRIVERS = ("TOUCH_GT911",)
SHARED_BUS = (("PIN_DISPLAY_SCLK", "P_LORA_SCLK"), ("PIN_DISPLAY_MOSI", "P_LORA_MOSI"),
              ("PIN_DISPLAY_MISO", "P_LORA_MISO"))
# The fleet diag surface, plus the panel's refresh timing that C5 (#2037) reads.
DIAG_SURFACE = ("OFFBAND_BOOT_BEACON", "OFFBAND_LOG_MIRROR_UART", "OFFBAND_FORCE_CAPLOG", "OFFBAND_EINK_TIMING")


def has_flag(body, name):
    """True if `-D NAME` (with or without a value) is set in the section, comments removed."""
    return re.search(r"-D\s+%s(?:=\S+)?(?:\s|$)" % re.escape(name), strip_comments(body, ";")) is not None


def matrix_envs(ci):
    """The env names in ci.yml's build matrix: the `- name` lines under `matrix: env:`.

    The list is interleaved with comment lines, so it runs until the first line that is
    neither a list item, a comment nor blank.
    """
    m = re.search(r"^\s*matrix:\s*\n\s*env:\s*\n", ci, re.M)
    if not m:
        return set()
    out = set()
    for line in ci[m.end():].splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        item = re.fullmatch(r"-\s*([\w.-]+)\s*(?:#.*)?", s)
        if not item:
            break
        out.add(item.group(1))
    return out


def target_failures(target):
    """The radio shares the panel driver's SPIClass and begins nothing else."""
    code = strip_comments(target, "//")
    out = []
    if not re.search(r"extern\s+SPIClass\s+SPI1\s*;", code):
        out.append("target.cpp must use the panel's SPIClass (extern SPIClass SPI1): one bus, one SPIClass")
    if not re.search(r"new\s+Module\([^)]*,\s*SPI1\s*\)", code):
        out.append("the radio's Module must be built on SPI1, the panel's SPIClass")
    if re.search(r"\bSPIClass\s+(?!SPI1\b)\w+", code):
        out.append("target.cpp declares a second SPIClass: a second peripheral would take the panel's pins")
    if not re.search(r"radio\.std_init\(\s*&SPI1\s*\)", code):
        out.append("radio_init must call std_init(&SPI1): any other SPIClass takes the shared pins")
    return out


def hw_cs_failures(*sources):
    """Nothing on the shared bus turns on the SPI peripheral's hardware chip-select.

    SPIClass::begin() only records the SS pin; setHwCs(true) would hand it to the
    peripheral, which then asserts the panel's CS on every transfer, the radio's too.
    """
    for code in sources:
        if re.search(r"\bsetHwCs\s*\(\s*true\s*\)|\bspiAttachSS\s*\(", strip_comments(code, "//")):
            return ["hardware chip-select is enabled on the shared bus: the panel's CS would assert "
                    "during radio transfers"]
    return []


def eink_log_failures(display):
    """Under OFFBAND_EINK_TIMING the panel's init, refreshes and BUSY timeouts reach MeshLog.

    GxEPD2 reports a BUSY timeout only on Serial, which the capture never records.
    """
    code = strip_comments(display, "//")
    out = []
    for what, pat in (("the init time", r'MESH_DEBUG_PRINTLN\("eink: init \+ full refresh'),
                      ("each refresh time", r'MESH_DEBUG_PRINTLN\("eink: partial refresh'),
                      ("a BUSY timeout", r'MESH_DEBUG_PRINTLN\("eink: BUSY timeout')):
        if not re.search(pat, code):
            out.append("GxEPDDisplay must log %s through MeshLog under OFFBAND_EINK_TIMING" % what)
    return out


def eink_panel_failures(panel):
    """The GUI's e-paper panel: SPI1 on FSPI for the radio, and its refreshes in the log.

    Its refresh lines go through one helper, so the check looks for the helper's MeshLog
    line, each refresh's call to it, and the BUSY line outside the timing flag.
    """
    code = strip_comments(panel, "//")
    out = []
    if not re.search(r"SPIClass\s+SPI1\s*=\s*SPIClass\(\s*FSPI\s*\)\s*;", code):
        out.append("EinkPanel.cpp must define SPI1 on FSPI: the radio's Module is built on it")
    if not re.search(r'mesh_log_line\(\s*MLOG_BOOT\s*,\s*"\[gui\] eink: %s %lu ms', code):
        out.append("EinkPanel.cpp must log each refresh time through MeshLog")
    if not re.search(r'mesh_log_line\(\s*MLOG_BOOT\s*,\s*"\[gui\] eink: BUSY timeout', code):
        out.append("EinkPanel.cpp must log a BUSY timeout through MeshLog: GxEPD2 says it only on Serial")
    for what in ("init + full refresh", "full refresh", "partial refresh"):
        if not re.search(r'noteRefresh\(\s*"%s"' % re.escape(what), code):
            out.append("EinkPanel.cpp must time the %s" % what)
    if not (re.search(r"_init_ms\s*=\s*millis\(\)\s*-\s*t0\s*;", code)
            and re.search(r'noteRefresh\(\s*"init \+ full refresh"\s*,\s*_init_ms\s*\)', code)):
        out.append("EinkPanel.cpp must keep the init time in _init_ms for replayInitTiming()")
    return out


def init_replay_failures(uitask):
    """The panel's init line is logged in display.begin(), before log capture is on, so
    it reaches UART0 only; the GUI repeats it once capture is on (#2037)."""
    if not re.search(r"panel->replayInitTiming\(\)", strip_comments(uitask, "//")):
        return ["UITask.cpp must call panel->replayInitTiming() after LVGL starts: the panel's "
                "init line is written before log capture is on"]
    return []


def board_failures(board):
    """TDeckProBoard::begin() parks every chip-select high and powers the radio."""
    code = strip_comments(board, "//")
    out = []
    m = re.search(r"for\s*\(\s*int\s+cs\s*:\s*\{([^}]*)\}\s*\)\s*\{\s*pinMode\(\s*cs\s*,\s*OUTPUT\s*\)\s*;"
                  r"\s*digitalWrite\(\s*cs\s*,\s*HIGH\s*\)", code)
    pins = {p.strip() for p in m.group(1).split(",")} if m else set()
    for need in ("P_LORA_NSS", "PIN_DISPLAY_CS", "TDECK_PRO_SD_CS"):
        if need not in pins:
            out.append("TDeckProBoard::begin() must drive %s high: the three chips share one SPI bus" % need)
    if not re.search(r"digitalWrite\(\s*P_LORA_EN\s*,\s*HIGH\s*\)", code):
        out.append("TDeckProBoard::begin() must drive P_LORA_EN high: the radio's regulator is off otherwise")
    return out


def _real_fonts():
    color = {p.name for p in FONTS.glob("*.c")}
    eink = {p.name: p.read_text(errors="replace")[:4096] for p in FONTS_EINK.glob("*.c")}
    return color, eink


def eink_font_failures(gui_env, color, eink):
    """The e-paper GUI builds the 1-bit font set, and that set matches the color one (#2009).
    `color` is the set of file names in fonts/; `eink` maps each fonts_eink/ file name to its head."""
    out = []
    body = strip_comments(gui_env, ";")
    if "+<helpers/gui/fonts_eink/*.c>" not in body:
        out.append("[%s] must build +<helpers/gui/fonts_eink/*.c>: the 1-bit fonts" % GUI_ENV)
    if "+<helpers/gui/fonts/*.c>" in body:
        out.append("[%s] builds +<helpers/gui/fonts/*.c>: the anti-aliased fonts lose strokes on a "
                   "panel with no grey" % GUI_ENV)
    if set(eink) != color:
        out.append("fonts_eink/ must hold the same fonts as fonts/: missing %s, extra %s"
                   % (sorted(color - set(eink)), sorted(set(eink) - color)))
    for name, head in sorted(eink.items()):
        if not re.search(r"^ \* Bpp: 1$", head, re.M):
            out.append("fonts_eink/%s is not 1 bpp" % name)
    # The caps label: at 1 bit the 9 px condensed face drops N's diagonal (owner's pick, #2037).
    label = eink.get("ob_font_label_9.c", "")
    if label and not ("BarlowSemiCondensed-Medium.ttf" in label and re.search(r"^ \* Size: 10 px$", label, re.M)):
        out.append("fonts_eink/ob_font_label_9.c must be Barlow Semi Condensed Medium at 10 px: the 9 px "
                   "condensed face reads N as H at 1 bit")
    return out


def touch_failures(gui_env, hw, uitask, driver):
    """The GUI env's touch (#2110): the cst66xx driver, its pins, the board's bus and Wire.

    The chip shares the board bus with the keyboard, fuel gauge and charger, so it must
    use the board's SDA/SCL and its Wire, and the driver must never start the bus again.
    """
    out = []
    if not has_flag(gui_env, "TOUCH_CST66XX"):
        out.append("[%s] must set TOUCH_CST66XX: tdeck-pro-1's chip is the cst66xx family (#2108)" % GUI_ENV)
    for name in OTHER_TOUCH_DRIVERS:
        if has_flag(gui_env, name):
            out.append("[%s] sets %s as well as TOUCH_CST66XX: one chip, one driver" % (GUI_ENV, name))
    for name, want in TOUCH_PINS.items():
        if flag(gui_env, name) != want:
            out.append("[%s] must set %s=%s (LilyGo utilities.h); got %r" % (GUI_ENV, name, want, flag(gui_env, name)))
    for touch, board in TOUCH_ON_BOARD_BUS:
        if flag(gui_env, touch) is None or flag(gui_env, touch) != flag(hw, board):
            out.append("[%s] must set %s to %s (%r): touch is on the board bus; got %r"
                       % (GUI_ENV, touch, board, flag(hw, board), flag(gui_env, touch)))
    code = strip_comments(uitask, "//")
    if not re.search(r"defined\(\s*TOUCH_CST66XX\s*\)\s*\n\s*Cst66xxTouch\s+s_touch\(\s*Wire\s*\)\s*;", code):
        out.append("UITask.cpp must build the cst66xx driver on the board's Wire "
                   "(Cst66xxTouch s_touch(Wire) under TOUCH_CST66XX)")
    if re.search(r"\b(?:_wire|Wire)\s*\.\s*begin\s*\(", strip_comments(driver, "//")):
        out.append("Cst66xxTouch.cpp starts the I2C bus: the board has started it, and a second "
                   "begin on the same pins breaks every device on them")
    return out


def native_ignored(root_ini):
    """The tests [env:native]'s test_ignore names: its value runs over continuation lines."""
    m = re.search(r"^\[env:native\]\s*\n(.*?)(?=^\[)", root_ini, re.M | re.S)
    if not m:
        return None
    t = re.search(r"^test_ignore\s*=\s*(.*?)(?=^\S)", strip_comments(m.group(1), ";") + "\nend", re.M | re.S)
    return {n.strip() for n in re.split(r"[,\s]+", t.group(1)) if n.strip()} if t else set()


def keyboard_failures(gui_env, uitask, kb_driver, root_ini):
    """The GUI env's keyboard (#2149-#2152): the TCA8418 driver on the board's Wire.

    TDECK_KEYBOARD is the T-Deck's own keyboard, an ESP32-C3 polled at 0x55. On this board
    0x55 is the BQ27220 fuel gauge, so that driver must never be selected here.
    """
    out = []
    if not has_flag(gui_env, "TDECK_PRO_KEYBOARD"):
        out.append("[%s] must set TDECK_PRO_KEYBOARD: the keyboard is a TCA8418 (#2149)" % GUI_ENV)
    if has_flag(gui_env, "TDECK_KEYBOARD"):
        out.append("[%s] sets TDECK_KEYBOARD: that is the T-Deck's keyboard, and its address 0x55 is "
                   "this board's fuel gauge" % GUI_ENV)
    code = strip_comments(uitask, "//")
    if not re.search(r"Tca8418Keyboard\s+s_keyboard\(\s*Wire\s*,\s*PIN_KEYBOARD_INT\s*,\s*PIN_KEYBOARD_LED\s*\)\s*;",
                     code):
        out.append("UITask.cpp must build the keyboard on the board's Wire with its INT and backlight pins "
                   "(Tca8418Keyboard s_keyboard(Wire, PIN_KEYBOARD_INT, PIN_KEYBOARD_LED))")
    if not re.search(r"lvglSetBusyClient\(\s*\[\]\s*\(\s*\)\s*\{\s*s_keyboard\.service\(\)\s*;\s*\}\s*\)", code):
        out.append("UITask.cpp must drain the keyboard in the panel's BUSY wait (lvglSetBusyClient): its "
                   "FIFO holds 10 events, and a refresh holds the loop for up to 2.3 s")
    if not re.search(r"s_keyboard\.read\(\s*&repeat\s*\)", code) or not re.search(r"if\s*\(\s*repeat\s*\)\s*continue\s*;",
                                                                             code):
        out.append("UITask.cpp must drop a Backspace repeat the field didn't take: as Back, holding "
                   "Backspace would walk out of the screen")
    if re.search(r"\b(?:_wire|Wire)\s*\.\s*begin\s*\(", strip_comments(kb_driver, "//")):
        out.append("Tca8418Keyboard.cpp starts the I2C bus: the board has started it, and a second begin "
                   "on the same pins breaks every device on them")
    ignored = native_ignored(root_ini)
    if ignored is None:
        out.append("platformio.ini has no [env:native]: the key-map tests can't run")
    elif KEYS_TEST in ignored:
        out.append("[env:native] ignores %s: CI would never run the key-map tests" % KEYS_TEST)
    if not (ROOT / "test" / KEYS_TEST).is_dir():
        out.append("test/%s is missing" % KEYS_TEST)
    return out


def failures(ini, target, board, ci, display=None, panel=None, fonts=None, uitask=None, driver=None,
             kb_driver=None, root_ini=None):
    out = target_failures(target) + board_failures(board)
    uitask = uitask if uitask is not None else UITASK.read_text()
    driver = driver if driver is not None else TOUCH_DRIVER.read_text()
    kb_driver = kb_driver if kb_driver is not None else KB_DRIVER.read_text()
    root_ini = root_ini if root_ini is not None else ROOT_INI.read_text()
    out += init_replay_failures(uitask)
    display = display if display is not None else DISPLAY.read_text()
    panel = panel if panel is not None else EINK_PANEL.read_text()
    out += hw_cs_failures(target, board, display, panel) + eink_log_failures(display)
    out += eink_panel_failures(panel)
    secs = ini_sections(ini)
    for s in (HW, DIAG, GUI_ENV, TEXT_ENV):
        if s not in secs:
            return ["[%s] is missing: this check cannot see what it is protecting" % s]
    hw, diag = secs[HW], secs[DIAG]
    out += eink_font_failures(secs[GUI_ENV], *(fonts if fonts is not None else _real_fonts()))
    out += touch_failures(secs[GUI_ENV], hw, uitask, driver)
    out += keyboard_failures(secs[GUI_ENV], uitask, kb_driver, root_ini)
    # Octal PSRAM needs GPIO33-37, which carry this board's SPI bus and panel.
    m = re.search(r"^\s*board_build\.arduino\.memory_type\s*=\s*(\S+)", strip_comments(hw, ";"), re.M)
    if not m or m.group(1) != "qio_qspi":
        out.append("[%s] must set board_build.arduino.memory_type = qio_qspi: the PSRAM is quad, and "
                   "GPIO33-37 are the SPI bus; got %r" % (HW, m.group(1) if m else None))
    for s, want in ENV_FLAGS.items():
        for name, value in want.items():
            if flag(secs[s], name) != value:
                out.append("[%s] must set %s=%s; got %r" % (s, name, value, flag(secs[s], name)))
    for s, body in secs.items():
        if has_flag(body, "PIN_SD_CS"):
            out.append("[%s] sets PIN_SD_CS: the GUI's card driver would run the shared bus through the "
                       "IDF and take it from the radio" % s)

    for name, want in HW_PINS.items():
        if flag(hw, name) != want:
            out.append("%s must be %s (LilyGo's V1.1 schematic, #2015); got %r" % (name, want, flag(hw, name)))
    for name, want in HW_FLAGS.items():
        if flag(hw, name) != want:
            out.append("%s must be %s; got %r" % (name, want, flag(hw, name)))
    for a, b in SHARED_BUS:
        if flag(hw, a) != flag(hw, b):
            out.append("%s and %s must match: the panel and the radio are one bus" % (a, b))
    for name in DIAG_SURFACE:
        if not has_flag(diag, name):
            out.append("[%s] must set %s: it is part of the fleet diag surface" % (DIAG, name))

    envs = {s: body for s, body in secs.items() if s.startswith("env:")}
    if not envs:
        out.append("the variant defines no env: this check cannot see what it is protecting")
    in_ci = matrix_envs(ci)
    for s, body in sorted(envs.items()):
        name = s.split(":", 1)[1]
        if not name.endswith("_diag"):
            out.append("%s is not a _diag env: bring-up ships only _diag envs" % name)
        if "${%s.build_flags}" % DIAG not in strip_comments(body, ";"):
            out.append("%s must carry ${%s.build_flags}: the fleet diag surface" % (name, DIAG))
        if name not in in_ci:
            out.append("%s is not in ci.yml's build matrix" % name)
    return out


def _real():
    return INI.read_text(), TARGET.read_text(), BOARD.read_text(), CI.read_text()


def _fails_with(fragment, ini=None, target=None, board=None, ci=None):
    r = _real()
    got = failures(ini if ini is not None else r[0], target if target is not None else r[1],
                   board if board is not None else r[2], ci if ci is not None else r[3])
    assert any(fragment in f for f in got), "expected a failure naming %r, got %s" % (fragment, got)


def _sub(text, old, new):
    assert old in text, "sanity: %r must be in the file" % old
    return text.replace(old, new, 1)


def test_the_real_tree_passes():
    got = failures(*_real())
    assert not got, "\n".join(got)


def test_negative_a_wrong_schematic_pin_is_caught():
    _fails_with("PIN_DISPLAY_RST must be 16", ini=_sub(_real()[0], "-D PIN_DISPLAY_RST=16", "-D PIN_DISPLAY_RST=-1"))


def test_negative_the_panel_off_the_radio_bus_is_caught():
    _fails_with("one bus", ini=_sub(_real()[0], "-D PIN_DISPLAY_MOSI=33", "-D PIN_DISPLAY_MOSI=11"))


def test_negative_the_wrong_tcxo_voltage_is_caught():
    _fails_with("SX126X_DIO3_TCXO_VOLTAGE must be 2.4f",
                ini=_sub(_real()[0], "-D SX126X_DIO3_TCXO_VOLTAGE=2.4f", "-D SX126X_DIO3_TCXO_VOLTAGE=1.8f"))


def test_negative_a_diag_flag_missing_is_caught():
    _fails_with("must set OFFBAND_FORCE_CAPLOG", ini=_sub(_real()[0], "-D OFFBAND_FORCE_CAPLOG", ""))


def test_negative_the_refresh_timing_missing_is_caught():
    _fails_with("must set OFFBAND_EINK_TIMING", ini=_sub(_real()[0], "  -D OFFBAND_EINK_TIMING\n", ""))


def test_negative_a_diag_flag_only_in_a_comment_is_caught():
    _fails_with("must set OFFBAND_BOOT_BEACON",
                ini=_sub(_real()[0], "  -D OFFBAND_BOOT_BEACON", "  ; -D OFFBAND_BOOT_BEACON"))


def test_negative_an_env_without_the_diag_block_is_caught():
    _fails_with("must carry ${tdeck_pro_diag.build_flags}",
                ini=_sub(_real()[0], "  ${tdeck_pro_diag.build_flags}\n", ""))


def test_negative_a_non_diag_env_is_caught():
    _fails_with("is not a _diag env", ini=_real()[0] + "\n[env:LilyGo_TDeck_Pro_companion_radio_ble]\n"
                "extends = LilyGo_TDeck_Pro_hw\nbuild_flags =\n  ${tdeck_pro_diag.build_flags}\n")


def test_negative_dropping_the_env_from_ci_is_caught():
    _fails_with("not in ci.yml's build matrix",
                ci=_sub(_real()[3], "- LilyGo_TDeck_Pro_companion_radio_ble_diag\n", ""))


def test_negative_the_radio_on_its_own_spiclass_is_caught():
    _fails_with("second SPIClass", target=_sub(_real()[1], "extern SPIClass SPI1;", "extern SPIClass SPI1;\nstatic SPIClass spi;"))


def test_negative_radio_init_on_another_bus_is_caught():
    _fails_with("std_init(&SPI1)", target=_sub(_real()[1], "radio.std_init(&SPI1)", "radio.std_init()"))


def test_negative_the_sd_cs_left_floating_is_caught():
    _fails_with("must drive TDECK_PRO_SD_CS high", board=_sub(_real()[2], ", TDECK_PRO_SD_CS}", "}"))


def test_negative_octal_psram_is_caught():
    _fails_with("memory_type = qio_qspi", ini=_sub(_real()[0], "board_build.arduino.memory_type = qio_qspi",
                                                    "board_build.arduino.memory_type = qio_opi"))


def test_negative_the_board_default_psram_mode_is_caught():
    _fails_with("memory_type = qio_qspi", ini=_sub(_real()[0], "board_build.arduino.memory_type = qio_qspi\n", ""))


def test_negative_the_gui_card_driver_turned_on_is_caught():
    _fails_with("sets PIN_SD_CS", ini=_sub(_real()[0], "  -D TDECK_PRO_SD_CS=48\n",
                                           "  -D TDECK_PRO_SD_CS=48\n  -D PIN_SD_CS=48\n"))


def test_negative_the_gui_env_on_the_tft_panel_is_caught():
    _fails_with("must set GUI_PANEL_EINK=1", ini=_sub(_real()[0], "  -D GUI_PANEL_EINK=1\n", ""))


def test_negative_the_gui_env_on_the_text_ui_is_caught():
    _fails_with("must set DISPLAY_CLASS=GuiPanel",
                ini=_sub(_real()[0], "-D DISPLAY_CLASS=GuiPanel", "-D DISPLAY_CLASS=GxEPDDisplay"))


def test_negative_dropping_the_gui_env_from_ci_is_caught():
    _fails_with("not in ci.yml's build matrix",
                ci=_sub(_real()[3], "- LilyGo_TDeck_Pro_companion_radio_ble_gui_diag\n", ""))


def _panel_fails_with(fragment, panel):
    r = _real()
    got = failures(r[0], r[1], r[2], r[3], panel=panel)
    assert any(fragment in f for f in got), "expected a failure naming %r, got %s" % (fragment, got)


def test_negative_the_gui_panel_without_spi1_is_caught():
    _panel_fails_with("define SPI1 on FSPI",
                      _sub(EINK_PANEL.read_text(), "SPIClass SPI1 = SPIClass(FSPI);", "SPIClass SPI1 = SPIClass(HSPI);"))


def test_negative_the_gui_panel_busy_timeout_left_to_serial_is_caught():
    _panel_fails_with("BUSY timeout", EINK_PANEL.read_text().replace(
        'mesh_log_line(MLOG_BOOT, "[gui] eink: BUSY timeout', 'Serial.printf("[gui] eink: BUSY timeout'))


def test_negative_the_gui_panel_untimed_partial_is_caught():
    _panel_fails_with("time the partial refresh",
                      _sub(EINK_PANEL.read_text(), 'noteRefresh("partial refresh"', 'noteRefresh("refresh"'))


def test_negative_the_init_time_not_kept_is_caught():
    _panel_fails_with("keep the init time in _init_ms",
                      _sub(EINK_PANEL.read_text(), "  _init_ms = millis() - t0;\n", ""))


def test_negative_the_init_line_not_replayed_is_caught():
    r = _real()
    uit = _sub(UITASK.read_text(), "panel->replayInitTiming();", "")
    got = failures(r[0], r[1], r[2], r[3], uitask=uit)
    assert any("replayInitTiming() after LVGL starts" in f for f in got), got


def test_negative_hardware_cs_in_the_gui_panel_is_caught():
    _panel_fails_with("hardware chip-select", _sub(EINK_PANEL.read_text(), "  s_epd.init(0, true, 2, false);",
                                                   "  SPI1.setHwCs(true);\n  s_epd.init(0, true, 2, false);"))


def test_negative_the_radio_left_unpowered_is_caught():
    _fails_with("must drive P_LORA_EN high",
                board=_sub(_real()[2], "digitalWrite(P_LORA_EN, HIGH);", "digitalWrite(P_LORA_EN, LOW);"))


def test_negative_hardware_cs_on_the_shared_bus_is_caught():
    disp = _sub(DISPLAY.read_text(), "  display.init(115200, true, 2, false);",
                "  SPI1.setHwCs(true);\n  display.init(115200, true, 2, false);")
    r = _real()
    got = failures(r[0], r[1], r[2], r[3], display=disp)
    assert any("hardware chip-select" in f for f in got), got


def test_negative_a_busy_timeout_left_to_serial_is_caught():
    disp = DISPLAY.read_text().replace('MESH_DEBUG_PRINTLN("eink: BUSY timeout', 'Serial.printf("eink: BUSY timeout')
    r = _real()
    got = failures(r[0], r[1], r[2], r[3], display=disp)
    assert any("a BUSY timeout" in f for f in got), got


def _fonts_fail_with(fragment, ini=None, fonts=None):
    r = _real()
    got = failures(ini if ini is not None else r[0], r[1], r[2], r[3], fonts=fonts)
    assert any(fragment in f for f in got), "expected a failure naming %r, got %s" % (fragment, got)


def test_negative_the_gui_env_on_the_color_fonts_is_caught():
    ini = _sub(_real()[0], "+<helpers/gui/fonts_eink/*.c>", "+<helpers/gui/fonts/*.c>")
    _fonts_fail_with("must build +<helpers/gui/fonts_eink/*.c>", ini=ini)
    _fonts_fail_with("anti-aliased fonts lose strokes", ini=ini)


def test_negative_a_4bpp_font_in_the_eink_set_is_caught():
    color, eink = _real_fonts()
    eink = dict(eink)
    eink["ob_font_label_9.c"] = eink["ob_font_label_9.c"].replace(" * Bpp: 1", " * Bpp: 4")
    _fonts_fail_with("ob_font_label_9.c is not 1 bpp", fonts=(color, eink))


def test_negative_the_condensed_9px_label_is_caught():
    color, eink = _real_fonts()
    eink = dict(eink)
    eink["ob_font_label_9.c"] = (eink["ob_font_label_9.c"].replace("BarlowSemiCondensed-Medium.ttf",
                                                                  "BarlowCondensed-Regular.ttf")
                                 .replace(" * Size: 10 px", " * Size: 9 px"))
    _fonts_fail_with("must be Barlow Semi Condensed Medium at 10 px", fonts=(color, eink))


def test_negative_a_font_missing_from_the_eink_set_is_caught():
    color, eink = _real_fonts()
    eink = {k: v for k, v in eink.items() if k != "ob_font_clock_46.c"}
    _fonts_fail_with("missing ['ob_font_clock_46.c']", fonts=(color, eink))


def _touch_fails_with(fragment, ini=None, uitask=None, driver=None):
    r = _real()
    got = failures(ini if ini is not None else r[0], r[1], r[2], r[3], uitask=uitask, driver=driver)
    assert any(fragment in f for f in got), "expected a failure naming %r, got %s" % (fragment, got)


def test_negative_the_touch_driver_not_selected_is_caught():
    _touch_fails_with("must set TOUCH_CST66XX", ini=_sub(_real()[0], "  -D TOUCH_CST66XX\n", ""))


def test_negative_a_second_touch_driver_is_caught():
    _touch_fails_with("sets TOUCH_GT911 as well",
                      ini=_sub(_real()[0], "  -D TOUCH_CST66XX\n", "  -D TOUCH_CST66XX\n  -D TOUCH_GT911\n"))


def test_negative_the_wrong_touch_int_pin_is_caught():
    _touch_fails_with("must set PIN_TOUCH_INT=12", ini=_sub(_real()[0], "-D PIN_TOUCH_INT=12", "-D PIN_TOUCH_INT=15"))


def test_negative_the_touch_reset_missing_is_caught():
    _touch_fails_with("must set PIN_TOUCH_RST=38", ini=_sub(_real()[0], "  -D PIN_TOUCH_RST=38\n", ""))


def test_negative_touch_off_the_board_bus_is_caught():
    _touch_fails_with("must set PIN_TOUCH_SDA to PIN_BOARD_SDA",
                      ini=_sub(_real()[0], "-D PIN_TOUCH_SDA=13", "-D PIN_TOUCH_SDA=18"))


def test_negative_touch_bus_pins_missing_is_caught():
    _touch_fails_with("must set PIN_TOUCH_SCL to PIN_BOARD_SCL", ini=_sub(_real()[0], "  -D PIN_TOUCH_SCL=14\n", ""))


def test_negative_the_driver_on_a_second_wire_is_caught():
    _touch_fails_with("on the board's Wire",
                      uitask=_sub(UITASK.read_text(), "Cst66xxTouch s_touch(Wire);", "Cst66xxTouch s_touch(Wire1);"))


def test_negative_the_driver_starting_the_bus_is_caught():
    _touch_fails_with("starts the I2C bus", driver=_sub(TOUCH_DRIVER.read_text(), "  _present = false;\n",
                                                        "  _present = false;\n  _wire.begin(13, 14);\n"))


def _kb_fails_with(fragment, ini=None, uitask=None, kb_driver=None, root_ini=None):
    r = _real()
    got = failures(ini if ini is not None else r[0], r[1], r[2], r[3], uitask=uitask, kb_driver=kb_driver,
                   root_ini=root_ini)
    assert any(fragment in f for f in got), "expected a failure naming %r, got %s" % (fragment, got)


def test_negative_the_keyboard_not_selected_is_caught():
    _kb_fails_with("must set TDECK_PRO_KEYBOARD", ini=_sub(_real()[0], "  -D TDECK_PRO_KEYBOARD\n", ""))


def test_negative_the_tdeck_keyboard_on_the_fuel_gauge_address_is_caught():
    _kb_fails_with("this board's fuel gauge",
                   ini=_sub(_real()[0], "  -D TDECK_PRO_KEYBOARD\n", "  -D TDECK_PRO_KEYBOARD\n  -D TDECK_KEYBOARD=1\n"))


def test_negative_the_wrong_keyboard_int_pin_is_caught():
    _fails_with("PIN_KEYBOARD_INT must be 15", ini=_sub(_real()[0], "-D PIN_KEYBOARD_INT=15", "-D PIN_KEYBOARD_INT=12"))


def test_negative_the_keyboard_on_a_second_wire_is_caught():
    _kb_fails_with("on the board's Wire with its INT", uitask=_sub(
        UITASK.read_text(), "Tca8418Keyboard s_keyboard(Wire, PIN_KEYBOARD_INT, PIN_KEYBOARD_LED);",
        "Tca8418Keyboard s_keyboard(Wire1, PIN_KEYBOARD_INT, PIN_KEYBOARD_LED);"))


def test_negative_the_keyboard_not_drained_in_a_refresh_is_caught():
    _kb_fails_with("BUSY wait", uitask=_sub(UITASK.read_text(), "lvglSetBusyClient([]() { s_keyboard.service(); });", ""))


def test_negative_a_repeat_that_becomes_back_is_caught():
    _kb_fails_with("drop a Backspace repeat", uitask=_sub(UITASK.read_text(), "      if (repeat) continue;\n", ""))


def test_negative_the_keyboard_driver_starting_the_bus_is_caught():
    _kb_fails_with("Tca8418Keyboard.cpp starts the I2C bus", kb_driver=_sub(
        KB_DRIVER.read_text(), "  _present = false;\n", "  _present = false;\n  _wire.begin(13, 14);\n"))


def test_negative_the_key_map_tests_ignored_is_caught():
    root = ROOT_INI.read_text()
    _kb_fails_with("never run the key-map tests",
                   root_ini=_sub(root, "test_ignore = test_kiss_modem,", "test_ignore = test_kiss_modem, test_tdeck_pro_keys,"))


def test_negative_the_key_map_tests_ignored_on_a_continuation_line_is_caught():
    root = ROOT_INI.read_text()
    _kb_fails_with("never run the key-map tests",
                   root_ini=_sub(root, "test_ignore = test_kiss_modem,", "test_ignore =\n  test_tdeck_pro_keys\n  test_kiss_modem,"))


def test_negative_a_renamed_section_is_not_a_silent_pass():
    _fails_with("is missing", ini=_sub(_real()[0], "[LilyGo_TDeck_Pro_hw]", "[LilyGo_TDeckPro_hw]"))


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
