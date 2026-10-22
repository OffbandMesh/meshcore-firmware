"""T-Deck GUI env invariants: pins from LilyGo's schematic, one SPI bus (#1724, #1725).

The T-Deck's display (CS 12), micro-SD (CS 39) and SX1262 (CS 9) share one SPI bus,
SCK 40 / MOSI 41 / MISO 38. A pin routes to one SPI peripheral at a time, so the GUI
build puts all three on SPI2 under ESP-IDF's bus lock (owner, 2026-10-21, option A):

    the radio goes through EspSharedSpiHal on SPI2_HOST, and radio_init begins no
        Arduino SPI in the GUI build: an Arduino SPIClass on the same pins takes them
        from the display and the card
    every pin matches LilyGo's schematic and the display shares the radio's SCK/MOSI
    the GUI env builds on [LilyGo_TDeck_hw], never on [LilyGo_TDeck], whose old
        ST7789LCDDisplay would put two display classes on one command line
    a landscape board starts in class B (GUI_DEFAULT_ROTATION 90)

Source assertions: a tester's T-Deck (#1730) is what shows the bus works.

Run: python scripts/test_tdeck_gui_env.py
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_r8_tft_gui_env import flag, ini_sections, strip_comments  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
INI = ROOT / "variants" / "lilygo_tdeck" / "platformio.ini"
TARGET = ROOT / "variants" / "lilygo_tdeck" / "target.cpp"
HAL = ROOT / "src" / "helpers" / "radiolib" / "EspSharedSpiHal.h"
BOARD = ROOT / "variants" / "lilygo_tdeck" / "TDeckBoard.cpp"
SD = ROOT / "src" / "helpers" / "gui" / "hw" / "SdCard.cpp"
SPI_CALLS = ("spiBegin", "spiBeginTransaction", "spiTransfer", "spiEndTransaction", "spiEnd")
CI = ROOT / ".github" / "workflows" / "ci.yml"

HW = "LilyGo_TDeck_hw"
OLD = "LilyGo_TDeck"
GUI = "env:LilyGo_TDeck_companion_radio_ble_gui"
DIAG = "env:LilyGo_TDeck_companion_radio_ble_gui_diag"

# LilyGo's schematic and utilities.h (#1724).
HW_PINS = {
    "P_LORA_SCLK": "40", "P_LORA_MISO": "38", "P_LORA_MOSI": "41", "P_LORA_NSS": "9",
    "P_LORA_RESET": "17", "P_LORA_BUSY": "13", "P_LORA_DIO_1": "45",
    "PIN_TFT_SCL": "40", "PIN_TFT_SDA": "41", "PIN_TFT_CS": "12", "PIN_TFT_DC": "11",
    "PIN_TFT_LEDA_CTL": "42", "PIN_TFT_RST": "-1", "PIN_PERF_POWERON": "10", "PIN_USER_BTN": "0",
}
GUI_FLAGS = {"PIN_TFT_MISO": "38", "PIN_SD_CS": "39", "DISPLAY_CLASS": "GuiPanel",
             "GUI_DEFAULT_ROTATION": "90", "OFFBAND_GUI": "1",
             # GT911 touch on the board bus with the keyboard; reset is RC-only (#1724, #1726).
             "TOUCH_GT911": "1", "PIN_BOARD_SDA": "18", "PIN_BOARD_SCL": "8",
             "PIN_TOUCH_SDA": "18", "PIN_TOUCH_SCL": "8", "PIN_TOUCH_RST": "-1",
             "GUI_TOUCH_SWAP_XY": "1",
             # Keyboard and trackball (J4 #1727); the trackball's press is USR (GPIO0).
             "TDECK_KEYBOARD": "1", "PIN_TRACKBALL_UP": "3", "PIN_TRACKBALL_DOWN": "15",
             "PIN_TRACKBALL_LEFT": "1", "PIN_TRACKBALL_RIGHT": "2"}
TRACKBALL = ("PIN_TRACKBALL_UP", "PIN_TRACKBALL_DOWN", "PIN_TRACKBALL_LEFT", "PIN_TRACKBALL_RIGHT")


def pin_flags(text):
    """Every -D PIN_*/P_LORA_* flag with a GPIO number (-1 is "none"), as {name: value}."""
    return {m.group(1): m.group(2)
            for m in re.finditer(r"-D\s+((?:PIN|P_LORA)_\w+)=(\d+)\b", strip_comments(text, ";"))}


def gui_branch(target):
    """The OFFBAND_GUI branch of target.cpp that constructs the radio, comments removed.

    radio_init has an OFFBAND_GUI branch too; only the one building a Module counts.
    """
    code = strip_comments(target, "//")
    for m in re.finditer(r"#if\s+defined\(OFFBAND_GUI\)\s*&&\s*defined\(P_LORA_SCLK\)(.*?)#elif", code, re.S):
        if "Module(" in m.group(1):
            return m.group(1)
    return None


def hal_failures(hal):
    """The HAL itself: every SPI call overridden, and its SPIClass never used.

    ArduinoHal's own SPI methods go through the SPIClass it was given. Any one left
    un-overridden, or any call into that SPIClass, puts Arduino's SPI driver on the
    pins ESP-IDF owns, and the call-site checks in target.cpp can't see it.
    """
    out = []
    code = strip_comments(hal, "//")
    for call in SPI_CALLS:
        if not re.search(r"\bvoid\s+%s\s*\([^)]*\)\s*override" % call, code):
            out.append("EspSharedSpiHal must override %s(): ArduinoHal's version uses Arduino SPI" % call)
    if re.search(r"\bspi\s*->|\bunused\s*\.\s*\w+\s*\(|SPIClass::", code):
        out.append("EspSharedSpiHal must never call into its SPIClass: it would take SPI2's pins")
    return out


def board_failures(board):
    """TDeckBoard::begin() drives every SPI chip-select high (#1728).

    The radio, display and micro-SD share one bus; a card whose CS floats can answer
    on MISO while the radio is read, which looks like a radio fault, not an SD one.
    """
    code = strip_comments(board, "//")
    m = re.search(r"for\s*\(\s*int\s+cs\s*:\s*\{([^}]*)\}\s*\)\s*\{\s*pinMode\(\s*cs\s*,\s*OUTPUT\s*\)\s*;"
                  r"\s*digitalWrite\(\s*cs\s*,\s*HIGH\s*\)", code)
    pins = {p.strip() for p in m.group(1).split(",")} if m else set()
    out = []
    for need in ("P_LORA_NSS", "PIN_TFT_CS", "kSdCardCs"):
        if need not in pins:
            out.append("TDeckBoard::begin() must drive %s high: the three chips share one SPI bus" % need)
    if not re.search(r"kSdCardCs\s*=\s*39\s*;", code):
        out.append("TDeckBoard's kSdCardCs must default to 39 (LilyGo's schematic, #1724)")
    return out


def sd_failures(sd):
    """SdCard holds the card's CS high whenever the driver lets go of it (#1728).

    A failed mount, an unmount and format()'s raw bring-up all release the pin; each
    must be followed by parkCs(), or the card's CS floats on a bus the radio uses.
    """
    code = strip_comments(sd, "//")
    out = []
    if not re.search(r"void\s+parkCs\(\)\s*\{[^}]*gpio_set_direction\(\s*cs\s*,\s*GPIO_MODE_OUTPUT\s*\)\s*;"
                     r"[^}]*gpio_set_level\(\s*cs\s*,\s*1\s*\)", code):
        out.append("SdCard's parkCs() must drive the card's CS as an output, high")
    for m in re.finditer(r"(sdspi_host_deinit\(\)|esp_vfs_fat_sdcard_unmount\([^)]*\))\s*;", code):
        after = code[m.end():].split(";")[:3]
        if not any("parkCs()" in s for s in after):
            out.append("SdCard: %s must be followed by parkCs(): the card's CS floats after it" % m.group(1))
    failed = re.search(r"esp_vfs_fat_sdspi_mount\((.*?)switch\s*\(\s*e\s*\)", code, re.S)
    if not failed or "parkCs()" not in failed.group(1).split("return MountOutcome::Ok;", 1)[-1]:
        out.append("SdCard::mount() must parkCs() when the mount fails")
    return out


def failures(ini, target, ci, hal=None, board=None, sd=None):
    out = []
    if hal is not None:
        out.extend(hal_failures(hal))
    if board is not None:
        out.extend(board_failures(board))
    if sd is not None:
        out.extend(sd_failures(sd))
    secs = ini_sections(ini)
    for s in (HW, OLD, GUI):
        if s not in secs:
            return ["[%s] is missing: this check cannot see what it is protecting" % s]
    hw, old, gui = secs[HW], secs[OLD], secs[GUI]

    for name, want in HW_PINS.items():
        if flag(hw, name) != want:
            out.append("%s must be %s (LilyGo's schematic, #1724); got %r" % (name, want, flag(hw, name)))
    for name, want in GUI_FLAGS.items():
        if flag(gui, name) != want:
            out.append("the GUI env's %s must be %s; got %r" % (name, want, flag(gui, name)))
    # A trackball line on a pin something else uses gets that device's traffic
    # counted as rolls (or its interrupt steals the pin).
    pins = pin_flags(hw)
    pins.update(pin_flags(gui))
    for t in TRACKBALL:
        gpio = pins.get(t)
        clash = sorted(n for n, v in pins.items() if v == gpio and n != t)
        if gpio is not None and clash:
            out.append("%s=%s is also %s: a trackball line must have its pin to itself" % (t, gpio, ", ".join(clash)))
    if flag(hw, "PIN_TFT_SCL") != flag(hw, "P_LORA_SCLK") or flag(hw, "PIN_TFT_SDA") != flag(hw, "P_LORA_MOSI"):
        out.append("the display and the radio must share SCK and MOSI: they are one bus")

    if len(re.findall(r"-D\s+DISPLAY_CLASS=", strip_comments(gui, ";"))) != 1:
        out.append("the GUI env must set DISPLAY_CLASS exactly once: a second one is a second display")
    if flag(hw, "DISPLAY_CLASS") is not None:
        out.append("[%s] must not set DISPLAY_CLASS: every env built on it would inherit one" % HW)
    if flag(old, "DISPLAY_CLASS") != "ST7789LCDDisplay":
        out.append("[%s] must keep DISPLAY_CLASS=ST7789LCDDisplay for the existing envs" % OLD)
    if not re.search(r"^extends\s*=\s*%s\s*$" % HW, gui, re.M):
        out.append("the GUI env must extend [%s], not [%s]: two display classes on one command line" % (HW, OLD))
    if "${%s.build_flags}" % OLD in strip_comments(gui, ";"):
        out.append("the GUI env must not inherit [%s]'s build_flags (its ST7789LCDDisplay)" % OLD)

    branch = gui_branch(target)
    if branch is None:
        out.append("target.cpp has no OFFBAND_GUI radio branch: the GUI build would put the radio on "
                   "Arduino SPI, which takes SPI2's pins from the display and the card")
    else:
        if not re.search(r"EspSharedSpiHal\s+\w+\(\s*\w+\s*,\s*SPI2_HOST", branch):
            out.append("the GUI radio must use EspSharedSpiHal on SPI2_HOST, the display's bus")
        if not re.search(r"new\s+Module\(\s*&\w+_hal", branch):
            out.append("the GUI radio's Module must be built on the shared-bus HAL")
        if re.search(r"\.begin\s*\(", branch):
            out.append("the GUI branch begins an SPIClass: it would take the pins from SPI2")
    code = strip_comments(target, "//")
    if not re.search(r"#if\s+defined\(OFFBAND_GUI\)\s*&&\s*defined\(P_LORA_SCLK\)\s*\n\s*return\s+radio\.std_init\(\s*nullptr\s*\)",
                     code):
        out.append("radio_init must call std_init(nullptr) in the GUI build: std_init(&spi) begins Arduino SPI")

    if DIAG not in secs or "extends = %s" % GUI not in secs[DIAG]:
        out.append("the _diag twin must extend the GUI env")
    elif flag(secs[DIAG], "OFFBAND_SD_DIAG_LOG") is None:
        out.append("the _diag twin must set OFFBAND_SD_DIAG_LOG: a tester hands the log over on the card (#1730)")
    if flag(gui, "OFFBAND_SD_DIAG_LOG") is not None:
        out.append("the GUI env must not set OFFBAND_SD_DIAG_LOG: only the diag build writes its log to the card")
    for env in (GUI, DIAG):
        name = env.split(":", 1)[1]
        if not re.search(r"^\s*-\s*%s\s*$" % re.escape(name), ci, re.M):
            out.append("%s is not in the CI build matrix" % name)
    return out


def _real():
    return (INI.read_text(encoding="utf-8"), TARGET.read_text(encoding="utf-8"), CI.read_text(encoding="utf-8"),
            HAL.read_text(encoding="utf-8"), BOARD.read_text(encoding="utf-8"), SD.read_text(encoding="utf-8"))


def _fails_with(fragment, **swap):
    parts = dict(zip(("ini", "target", "ci", "hal", "board", "sd"), _real()))
    parts.update(swap)
    got = failures(**parts)
    assert any(fragment in f for f in got), "expected a failure naming %r, got %s" % (fragment, got)


def _swap_in(section, old, new):
    ini = _real()[0]
    m = re.search(r"^\[%s\]\s*$" % re.escape(section), ini, re.M)   # the header, not a comment naming it
    assert m, "sanity: [%s] must exist" % section
    start = m.start()
    nxt = ini.find("\n[", start + 1)
    end = nxt if nxt >= 0 else len(ini)
    body = ini[start:end]
    assert old in body, "sanity: %r must be in [%s]" % (old, section)
    return ini[:start] + body.replace(old, new, 1) + ini[end:]


def test_the_real_tree_passes():
    got = failures(*_real())
    assert not got, "\n".join(got)


def test_negative_a_wrong_schematic_pin_is_caught():
    _fails_with("P_LORA_NSS must be 9", ini=_swap_in(HW, "-D P_LORA_NSS=9", "-D P_LORA_NSS=8"))


def test_negative_the_wrong_sd_cs_is_caught():
    _fails_with("PIN_SD_CS must be 39", ini=_swap_in(GUI, "-D PIN_SD_CS=39", "-D PIN_SD_CS=3"))


def test_negative_touch_off_the_board_bus_is_caught():
    _fails_with("PIN_TOUCH_SDA must be 18", ini=_swap_in(GUI, "-D PIN_TOUCH_SDA=18", "-D PIN_TOUCH_SDA=47"))


def test_negative_the_chsc6x_driver_on_the_tdeck_is_caught():
    _fails_with("TOUCH_GT911 must be 1", ini=_swap_in(GUI, "-D TOUCH_GT911=1", ""))


def test_negative_the_keyboard_left_out_is_caught():
    _fails_with("TDECK_KEYBOARD must be 1", ini=_swap_in(GUI, "-D TDECK_KEYBOARD=1", ""))


def test_negative_a_wrong_trackball_pin_is_caught():
    _fails_with("PIN_TRACKBALL_DOWN must be 15", ini=_swap_in(GUI, "-D PIN_TRACKBALL_DOWN=15", "-D PIN_TRACKBALL_DOWN=16"))


def test_negative_a_trackball_line_on_a_used_pin_is_caught():
    # 0 is USR (the trackball's press): a roll line there would count presses as rolls.
    _fails_with("a trackball line must have its pin to itself",
                ini=_swap_in(GUI, "-D PIN_TRACKBALL_LEFT=1", "-D PIN_TRACKBALL_LEFT=0"))


def test_negative_a_portrait_start_is_caught():
    _fails_with("GUI_DEFAULT_ROTATION must be 90", ini=_swap_in(GUI, "-D GUI_DEFAULT_ROTATION=90", ""))


def test_negative_the_gui_on_the_old_display_section_is_caught():
    _fails_with("must extend", ini=_swap_in(GUI, "extends = LilyGo_TDeck_hw", "extends = LilyGo_TDeck"))


def test_negative_a_display_class_in_the_hw_section_is_caught():
    _fails_with("must not set DISPLAY_CLASS",
                ini=_swap_in(HW, "-D LILYGO_TDECK", "-D LILYGO_TDECK\n  -D DISPLAY_CLASS=ST7789LCDDisplay"))


def test_negative_the_old_envs_losing_their_display_is_caught():
    _fails_with("must keep DISPLAY_CLASS", ini=_swap_in(OLD, "-D DISPLAY_CLASS=ST7789LCDDisplay", ""))


def test_negative_the_radio_on_arduino_spi_in_the_gui_is_caught():
    t = _real()[1]
    start = t.index("#if defined(OFFBAND_GUI) && defined(P_LORA_SCLK)")
    end = t.index("#elif defined(P_LORA_SCLK)")
    _fails_with("no OFFBAND_GUI radio branch", target=t[:start] + "#if 0\n" + t[end:].replace("#elif", "#elif", 1))


def test_negative_the_hal_on_another_host_is_caught():
    _fails_with("EspSharedSpiHal on SPI2_HOST", target=_real()[1].replace(
        "s_radio_hal(s_unbegun_spi, SPI2_HOST", "s_radio_hal(s_unbegun_spi, SPI3_HOST"))


def test_negative_beginning_the_spiclass_is_caught():
    _fails_with("begins an SPIClass", target=_real()[1].replace(
        "RADIO_CLASS radio = new Module(&s_radio_hal",
        "static bool s_b = (s_unbegun_spi.begin(40, 38, 41), true);\n  RADIO_CLASS radio = new Module(&s_radio_hal", 1))


def test_negative_radio_init_beginning_arduino_spi_is_caught():
    _fails_with("std_init(nullptr)", target=_real()[1].replace("radio.std_init(nullptr)", "radio.std_init(&s_unbegun_spi)"))


def test_negative_dropping_the_env_from_ci_is_caught():
    _fails_with("not in the CI build matrix", ci=_real()[2].replace(
        "          - LilyGo_TDeck_companion_radio_ble_gui\n", ""))


def test_negative_an_un_overridden_spi_call_is_caught():
    _fails_with("must override spiEnd()", hal=_real()[3].replace(
        "void spiEnd() override {}", "// spiEnd left to ArduinoHal"))


def test_negative_the_hal_calling_its_spiclass_is_caught():
    _fails_with("never call into its SPIClass", hal=_real()[3].replace(
        "void spiEnd() override {}", "void spiEnd() override { spi->end(); }"))


def test_negative_a_second_display_class_is_caught():
    _fails_with("exactly once", ini=_swap_in(GUI, "-D DISPLAY_CLASS=GuiPanel",
                                             "-D DISPLAY_CLASS=GuiPanel\n  -D DISPLAY_CLASS=ST7789LCDDisplay"))


def test_negative_a_renamed_section_is_not_a_silent_pass():
    _fails_with("cannot see what it is protecting", ini=_real()[0].replace("[LilyGo_TDeck_hw]", "[LilyGo_TDeck_base]"))


def test_negative_the_sd_card_cs_left_floating_is_caught():
    _fails_with("must drive kSdCardCs high",
                board=_real()[4].replace("{P_LORA_NSS, PIN_TFT_CS, kSdCardCs}", "{P_LORA_NSS, PIN_TFT_CS}"))


def test_negative_the_chip_selects_driven_low_is_caught():
    _fails_with("must drive P_LORA_NSS high", board=_real()[4].replace("digitalWrite(cs, HIGH)", "digitalWrite(cs, LOW)"))


def test_negative_the_wrong_sd_cs_default_is_caught():
    _fails_with("kSdCardCs must default to 39", board=_real()[4].replace("kSdCardCs = 39;", "kSdCardCs = 38;"))


def test_negative_an_unmount_leaving_the_cs_floating_is_caught():
    sd = _real()[5]
    i = sd.index("esp_vfs_fat_sdcard_unmount(")
    j = sd.index("parkCs();", i)
    _fails_with("esp_vfs_fat_sdcard_unmount", sd=sd[:j] + sd[j + len("parkCs();"):])


def test_negative_format_leaving_the_cs_floating_is_caught():
    sd = _real()[5]
    i = sd.rindex("sdspi_host_deinit();")
    j = sd.index("parkCs();", i)
    _fails_with("sdspi_host_deinit() must be followed by parkCs()", sd=sd[:j] + sd[j + len("parkCs();"):])


def test_negative_a_failed_mount_leaving_the_cs_floating_is_caught():
    sd = _real()[5]
    i = sd.index("esp_vfs_fat_sdspi_mount(")
    j = sd.index("parkCs();", i)
    _fails_with("must parkCs() when the mount fails", sd=sd[:j] + sd[j + len("parkCs();"):])


def test_negative_parking_the_cs_low_is_caught():
    _fails_with("parkCs() must drive the card's CS as an output, high",
                sd=_real()[5].replace("gpio_set_level(cs, 1)", "gpio_set_level(cs, 0)"))


def test_negative_the_diag_build_without_the_sd_log_is_caught():
    _fails_with("must set OFFBAND_SD_DIAG_LOG", ini=_swap_in(DIAG, "-D OFFBAND_SD_DIAG_LOG", ""))


def test_negative_the_sd_log_in_the_plain_gui_build_is_caught():
    _fails_with("must not set OFFBAND_SD_DIAG_LOG",
                ini=_swap_in(GUI, "-D OFFBAND_GUI=1", "-D OFFBAND_GUI=1\n  -D OFFBAND_SD_DIAG_LOG"))


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
