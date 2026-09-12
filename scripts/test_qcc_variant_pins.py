"""Pin-map guard for variants/qcc_badge (#1172).

The badge drives its buzzer from P0.06 and its LED from P0.08 -- the ProMicro
variant's default Serial1 TX/RX. Opening Serial1 on those defaults would hold the
buzzer on. This test fails if a badge role, or Serial1, lands on the wrong GPIO.
Run: python -m pytest scripts/test_qcc_variant_pins.py -q
"""
import re
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
    # SPI1 would share P1.01 with the diag log mirror; Wire1 would share the LoRa NSS/MOSI.
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
