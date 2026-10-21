#!/usr/bin/env python
"""Offband #1601: tests for check_variant_pin_collisions.py.

Run:  python scripts/test_check_variant_pin_collisions.py
Exit: 0 on pass, nonzero with a clear failure message otherwise.
No external deps -- stdlib only, matching the other scripts/test_*.py.

These are deliberately NEGATIVE tests. A checker exercised only against a clean tree
proves nothing about what it CATCHES -- only that it stays quiet. Every case below is
either a defect the tool must fail on, or a legitimate pattern it must NOT flag. The
false-positive cases matter as much as the true-positive ones: a noisy guard gets
switched off, and then it guards nothing.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_variant_pin_collisions as chk  # noqa: E402

FAILURES = []


def expect(condition, label):
    if condition:
        print("  pass  %s" % label)
    else:
        print("  FAIL  %s" % label)
        FAILURES.append(label)


def problems_for(defines, name="testboard"):
    probs, _groups, _pins = chk.check_variant(name, defines)
    return probs


# ---------------------------------------------------------------------------
# Resolution: if these are wrong, every distinctness result below is meaningless.
# ---------------------------------------------------------------------------

def test_resolution():
    print("resolution")

    res, _ = chk.resolve({"PIN_WIRE_SDA": "(0 + 7)"})
    expect(res.get("PIN_WIRE_SDA") == 7, "integer arithmetic (0 + 7) -> 7")

    res, _ = chk.resolve({"PIN_BUTTON1": "(32 + 10)"})
    expect(res.get("PIN_BUTTON1") == 42, "nRF52 port arithmetic (32 + 10) -> 42")

    # The alias chain that a naive one-pass resolver gets wrong.
    res, _ = chk.resolve({"LORA_CS": "(0 + 5)", "PIN_SPI_NSS": "LORA_CS"})
    expect(res.get("PIN_SPI_NSS") == 5, "alias chain PIN_SPI_NSS -> LORA_CS -> 5")

    res, _ = chk.resolve({"PIN_SPI1_MISO": "(-1)"})
    expect(res.get("PIN_SPI1_MISO") == -1, "unset pin (-1) resolves to -1")

    res, _ = chk.resolve({"PIN_LED": "LED_BUILTIN"})
    expect("PIN_LED" not in res,
           "core macro LED_BUILTIN left UNRESOLVED rather than guessed")

    expect(chk.stm32_pin_number("PB7") == chk.stm32_pin_number("PB7"),
           "STM32 PB7 resolves deterministically")
    expect(chk.stm32_pin_number("PB7") != chk.stm32_pin_number("PA7"),
           "STM32 PB7 and PA7 are different pins")
    expect(chk.stm32_pin_number("PB7") != 7,
           "STM32 pin numbers cannot collide with a raw GPIO 7")
    expect(chk.stm32_pin_number("NOT_A_PIN") is None,
           "non-STM32 token is not mistaken for a pin")


# ---------------------------------------------------------------------------
# TRUE POSITIVES -- the tool must fail on each of these.
# ---------------------------------------------------------------------------

def test_catches_intra_group_collision():
    print("catches: two pins of one bus on the same GPIO")

    probs = problems_for({
        "PIN_SPI1_MISO": "(0 + 20)",   # same GPIO as SCK below -- the defect
        "PIN_SPI1_MOSI": "(0 + 17)",
        "PIN_SPI1_SCK":  "(0 + 20)",
    })
    expect(any("both on GPIO 20" in p for p in probs),
           "SPI1 MISO and SCK on GPIO 20 is reported")

    probs = problems_for({"PIN_WIRE_SDA": "7", "PIN_WIRE_SCL": "7"})
    expect(any("both on GPIO 7" in p for p in probs),
           "I2C SDA and SCL on the same pin is reported")

    probs = problems_for({
        "P_LORA_SCLK": "9", "P_LORA_MISO": "11", "P_LORA_MOSI": "11",
    })
    expect(any("both on GPIO 11" in p for p in probs),
           "ESP32 LoRa SPI MISO/MOSI collision is reported (-D flag naming)")

    probs = problems_for({"PIN_SERIAL_RX": "PB7", "PIN_SERIAL_TX": "PB7"})
    expect(any("both on GPIO" in p for p in probs),
           "STM32 UART RX and TX on PB7 is reported")


def test_catches_half_declared_bus():
    print("catches: a bus declared but incomplete")

    probs = problems_for({"PIN_SPI1_MOSI": "(0 + 17)"})   # SCK missing
    expect(any("missing required" in p and "PIN_SPI1_SCK" in p for p in probs),
           "SPI1 without SCK is reported")

    probs = problems_for({"PIN_GPS_RX": "47"})            # TX missing
    expect(any("missing required" in p and "PIN_GPS_TX" in p for p in probs),
           "GPS RX without TX is reported")


def test_catches_stale_group_table():
    print("catches: bus-shaped pins matching no known group")

    probs = problems_for({"SOMENEWBUS_SDA": "4", "SOMENEWBUS_SCL": "5"})
    expect(any("GROUPS is stale" in p for p in probs),
           "an unrecognised SDA/SCL pair is reported as a stale table")

    probs = problems_for({"WEIRD_UART_RX": "1", "WEIRD_UART_TX": "2"})
    expect(any("GROUPS is stale" in p for p in probs),
           "an unrecognised _RX/_TX pair is reported as a stale table")


# ---------------------------------------------------------------------------
# FALSE POSITIVES -- legitimate patterns the tool must stay quiet about.
# A noisy guard gets disabled, and a disabled guard protects nothing.
# ---------------------------------------------------------------------------

def test_allows_legitimate_patterns():
    print("allows: patterns that are correct by design")

    # heltec_t096's real shape: the TFT rides SPI1 on purpose, and PIN_SPI_NSS is a
    # deliberate alias of LORA_CS. Cross-group sharing must not be flagged.
    probs = problems_for({
        "PIN_SPI1_MISO": "(-1)",
        "PIN_SPI1_MOSI": "(0+17)",
        "PIN_SPI1_SCK":  "(0+20)",
        "PIN_TFT_SDA":   "(0 + 17)",   # shares SPI1 MOSI, by design
        "PIN_TFT_SCL":   "(0 + 20)",   # shares SPI1 SCK, by design
        "PIN_SPI_MISO":  "(0 + 14)",
        "PIN_SPI_MOSI":  "(0 + 11)",
        "PIN_SPI_SCK":   "(32 + 8)",
        "PIN_SPI_NSS":   "LORA_CS",
        "LORA_CS":       "(0 + 5)",
    }, name="heltec_t096_shape")
    expect(probs == [], "the real heltec_t096 pin shape is clean (got %r)" % (probs,))

    # Two unset pins are not a collision -- "not connected" twice is legitimate.
    probs = problems_for({
        "PIN_SPI1_MISO": "(-1)", "PIN_SPI1_MOSI": "(-1)", "PIN_SPI1_SCK": "(0+20)",
    })
    expect(probs == [], "two pins both unset at -1 is not a collision")

    probs = problems_for({
        "P_LORA_NSS": "8", "P_LORA_RESET": "RADIOLIB_NC", "P_LORA_BUSY": "13",
        "P_LORA_DIO_1": "14",
    })
    expect(probs == [], "RADIOLIB_NC is treated as not-connected, not as a pin")

    # The env sensor bus aliased onto an existing bus -- wio-tracker-l1-eink's shape.
    probs = problems_for({
        "PIN_WIRE1_SDA": "(0 + 4)", "PIN_WIRE1_SCL": "(0 + 27)",
        "ENV_PIN_SDA": "PIN_WIRE1_SDA", "ENV_PIN_SCL": "PIN_WIRE1_SCL",
    })
    expect(probs == [], "ENV_PIN_* aliased onto PIN_WIRE1_* is not a collision")

    # A board with no buses at all must not trip the stale-table rule.
    probs = problems_for({"PIN_VBAT_READ": "1", "PIN_USER_BTN": "0"})
    expect(probs == [], "a variant with no bus pins is clean, not 'stale table'")

    # Exempted names that look bus-shaped but are not bus members.
    probs = problems_for({"P_LORA_TX_LED": "35", "LORA_TX_POWER": "22"})
    expect(probs == [], "P_LORA_TX_LED / LORA_TX_POWER are not treated as UART lines")


# ---------------------------------------------------------------------------
# The checker must also actually be measuring something on the real tree.
# ---------------------------------------------------------------------------

def test_real_tree_coverage_is_not_empty():
    print("coverage: the real tree is genuinely being compared")

    total_groups = total_pins = variants = 0
    for entry in sorted(os.listdir(chk.VARIANTS_DIR)):
        vdir = os.path.join(chk.VARIANTS_DIR, entry)
        if not os.path.isdir(vdir):
            continue
        defines = chk.collect_variant_defines(vdir)
        if defines is None:
            continue
        variants += 1
        _probs, groups, pins = chk.check_variant(entry, defines)
        total_groups += groups
        total_pins += pins

    expect(variants >= 50, "at least 50 variants are read (got %d)" % variants)
    expect(total_groups >= 200,
           "at least 200 peripheral groups are found (got %d)" % total_groups)
    # The guard that matters: an "OK" from a checker that compared nothing would look
    # exactly like an "OK" from one that compared everything.
    expect(total_pins >= 500,
           "at least 500 pins are compared for distinctness (got %d)" % total_pins)


def main():
    test_resolution()
    test_catches_intra_group_collision()
    test_catches_half_declared_bus()
    test_catches_stale_group_table()
    test_allows_legitimate_patterns()
    test_real_tree_coverage_is_not_empty()

    print()
    if FAILURES:
        print("FAIL: %d case(s) failed:" % len(FAILURES), file=sys.stderr)
        for f in FAILURES:
            print("  - %s" % f, file=sys.stderr)
        return 1
    print("OK: all check_variant_pin_collisions cases pass "
          "(negative cases included).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
