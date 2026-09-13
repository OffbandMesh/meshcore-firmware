"""Pin-map guard for variants/qcc_badge (#1172).

The badge drives its buzzer from P0.06 and its LED from P0.08 -- the ProMicro
variant's default Serial1 TX/RX. Opening Serial1 on those defaults would hold the
buzzer on. This test fails if a badge role, or Serial1, lands on the wrong GPIO.
Run: python scripts/test_qcc_variant_pins.py
  or python -m pytest scripts/test_qcc_variant_pins.py -q
"""
import re
import sys
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


def test_badge_env_enables_the_keyboard():
    # The badge's CardKB-compatible keyboard (#1204/#1205): the UI only polls it when the
    # flag is set, and the driver must be compiled into the env for that to link.
    ini = (QCC / "platformio.ini").read_text()
    assert re.search(r"-D\s+UI_HAS_CARDKB=1\b", ini)
    assert "+<helpers/ui/CardKbInput.cpp>" in ini


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
