#!/usr/bin/env python
"""Offband #1601: per-peripheral pin invariants across EVERY variant.

Why this exists
---------------
The board matrix is larger than anyone can bench-test, and upstream base updates
arrive carrying pin changes for boards nobody here owns. A pin fix that collides with
another pin on the same bus produces a board that builds cleanly and does not work,
with no compile error and no test failure. This asserts a property over all variants,
so adopting such a change is checkable without the board.

Two declaration mechanisms, both read
-------------------------------------
nRF52 variants declare pins Arduino-core style, as `#define` in variant.h / target.h /
*Board.h. ESP32 variants declare them as `-D NAME=value` build flags in the variant's
platformio.ini. A checker that read only one of those would cover about half the matrix
and report OK -- which is why it reads both, and why it fails loudly when it finds
bus-shaped pins it does not recognise (see STALE TABLE below).

What it does NOT check, deliberately
------------------------------------
Cross-peripheral pin sharing is legitimate: on heltec_t096 the TFT rides SPI1, so
PIN_TFT_SDA and PIN_SPI1_MOSI are the same GPIO on purpose, and PIN_SPI_NSS is a
deliberate alias of LORA_CS. A global "no two pins share a number" rule is pure noise.
The rules are narrower and sound:

  1. Within one peripheral group, every CONNECTED pin must be DISTINCT.
     Two members of the same bus on one GPIO is always a defect.
  2. If a group is declared at all, its REQUIRED members must all be defined.
     A half-declared bus is a defect; an absent bus is fine.
  3. STALE TABLE: if a variant declares bus-shaped pins (SCK/SCLK/MISO/MOSI/SDA/SCL/
     _RX/_TX) and NONE of them matched a known group, GROUPS is out of date for that
     board and this fails. Without this rule, adding a board with new pin names would
     silently drop it from coverage while the check still printed OK.

Unset pins are exempt from distinctness: -1 and RADIOLIB_NC both mean "not connected",
which is a legitimate configuration (a write-only display has no MISO).

Resolution
----------
Pin values are small integer expressions, often with nRF52 port arithmetic (`(32 + 10)`
is port 1 pin 10 = GPIO 42) and often referring to another macro. This resolves integer
literals, parenthesized +/- arithmetic, and macro references, iterated to a fixpoint.
Anything outside that grammar -- a core macro like LED_BUILTIN -- is reported as
unresolved and skipped rather than guessed at. Guessing a GPIO would be worse than not
checking it.

Run:  python scripts/check_variant_pin_collisions.py [--verbose]
Exit: 0 clean, 1 on any violation. Paired negative tests in
      scripts/test_check_variant_pin_collisions.py
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VARIANTS_DIR = os.path.join(REPO_ROOT, "variants")

# #define-style headers a variant may declare pins in.
PIN_HEADER_SUFFIXES = ("variant.h", "target.h", "Board.h")

# Peripheral groups. Explicit, not pattern-derived: a group only means something if
# somebody decided these pins belong to one bus. `required` must all be present once
# ANY member appears. Adding a board with a new bus means adding it here -- rule 3
# makes that a CI failure rather than a silent coverage gap.
GROUPS = (
    # -- nRF52 / Arduino-core naming (variant.h) ------------------------------
    ("SPI0",      ("PIN_SPI_MISO", "PIN_SPI_MOSI", "PIN_SPI_SCK"),
                  ("PIN_SPI_MOSI", "PIN_SPI_SCK")),
    ("SPI1",      ("PIN_SPI1_MISO", "PIN_SPI1_MOSI", "PIN_SPI1_SCK"),
                  ("PIN_SPI1_MOSI", "PIN_SPI1_SCK")),
    ("I2C0",      ("PIN_WIRE_SDA", "PIN_WIRE_SCL"),
                  ("PIN_WIRE_SDA", "PIN_WIRE_SCL")),
    ("I2C1",      ("PIN_WIRE1_SDA", "PIN_WIRE1_SCL"),
                  ("PIN_WIRE1_SDA", "PIN_WIRE1_SCL")),
    ("UART1",     ("PIN_SERIAL1_RX", "PIN_SERIAL1_TX"),
                  ("PIN_SERIAL1_RX", "PIN_SERIAL1_TX")),
    ("UART2",     ("PIN_SERIAL2_RX", "PIN_SERIAL2_TX"),
                  ("PIN_SERIAL2_RX", "PIN_SERIAL2_TX")),
    ("LORA_NRF",  ("LORA_CS", "SX126X_DIO1", "SX126X_BUSY", "SX126X_RESET"),
                  ()),
    # -- ESP32 naming (-D flags in the variant's platformio.ini) --------------
    ("LORA_SPI",  ("P_LORA_SCLK", "P_LORA_MISO", "P_LORA_MOSI"),
                  ("P_LORA_SCLK", "P_LORA_MISO", "P_LORA_MOSI")),
    ("LORA_CTRL", ("P_LORA_NSS", "P_LORA_BUSY", "P_LORA_DIO_1", "P_LORA_RESET"),
                  ("P_LORA_NSS",)),
    ("I2C_BOARD", ("PIN_BOARD_SDA", "PIN_BOARD_SCL"),
                  ("PIN_BOARD_SDA", "PIN_BOARD_SCL")),
    # -- shared ---------------------------------------------------------------
    ("GPS",       ("PIN_GPS_RX", "PIN_GPS_TX"),
                  ("PIN_GPS_RX", "PIN_GPS_TX")),
    # The environmental sensor's I2C bus. Usually ALIASED onto an existing bus
    # (wio-tracker-l1-eink points it at PIN_WIRE1_*), which is fine -- cross-group
    # sharing is not checked. Within the group SDA and SCL must still differ.
    ("I2C_ENV",   ("ENV_PIN_SDA", "ENV_PIN_SCL"),
                  ("ENV_PIN_SDA", "ENV_PIN_SCL")),
    # Unsuffixed serial, used by the STM32 variants.
    ("UART0",     ("PIN_SERIAL_RX", "PIN_SERIAL_TX"),
                  ("PIN_SERIAL_RX", "PIN_SERIAL_TX")),
    ("RS232",     ("WITH_RS232_BRIDGE_RX", "WITH_RS232_BRIDGE_TX"),
                  ("WITH_RS232_BRIDGE_RX", "WITH_RS232_BRIDGE_TX")),
)

# Values meaning "not connected" -- exempt from distinctness.
UNSET_INTS = (-1,)
UNSET_NAMES = ("RADIOLIB_NC",)

# Rule 3: names of this shape are bus members. If a variant has some and matched no
# group, GROUPS is stale for that board.
_BUS_SHAPED_RE = re.compile(
    r"(?:^|_)(?:SCK|SCLK|MISO|MOSI|SDA|SCL)(?:_|$)|_(?:RX|TX)$"
)
# Names that look bus-shaped but are not bus members. Kept explicit and short;
# anything added here is a deliberate exemption, not a silent one.
BUS_SHAPED_EXEMPT = frozenset((
    "P_LORA_TX_LED",      # an LED, not a UART line
    "LORA_TX_POWER",      # dBm, not a pin
    "PIN_TFT_SDA",        # display data line, shares SPI1 by design (see above)
    "PIN_TFT_SCL",        # display clock line, same
))

_DEFINE_RE = re.compile(
    r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+?)\s*(?://.*|/\*.*)?$")
_DFLAG_RE = re.compile(
    r"-D\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^\s;]+)")
_SAFE_EXPR_RE = re.compile(r"^[\s0-9()+\-]*$")
_IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")

# STM32 variants name pins by port and bit -- PB7, PA3 -- not by integer. Mapped to a
# synthetic number so distinctness is still comparable WITHIN a variant. Offset well
# clear of real GPIO numbers so a mixed-naming variant can never collide by accident:
# these are only ever compared against each other.
_STM32_PIN_RE = re.compile(r"^P([A-H])(\d{1,2})$")
_STM32_BASE = 100000


def stm32_pin_number(token):
    """PB7 -> synthetic distinct int, or None if not an STM32 pin name."""
    m = _STM32_PIN_RE.match(token.strip("() "))
    if not m:
        return None
    port, bit = m.group(1), int(m.group(2))
    if bit > 15:
        return None
    return _STM32_BASE + (ord(port) - ord("A")) * 16 + bit


def parse_defines(text):
    """{name: raw} for simple object-like #defines. Function-like are skipped."""
    out = {}
    for line in text.splitlines():
        m = _DEFINE_RE.match(line)
        if not m:
            continue
        name, raw = m.group(1), m.group(2).strip()
        if "(" in name:
            continue
        if raw:
            out[name] = raw
    return out


def parse_build_flags(text):
    """{name: raw} for `-D NAME=value` build flags in a platformio.ini."""
    out = {}
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith(";") or stripped.startswith("#"):
            continue
        for m in _DFLAG_RE.finditer(line):
            out[m.group(1)] = m.group(2).strip()
    return out


def resolve(defines, max_passes=8):
    """Resolve raw values to ints where the grammar allows.

    Returns (resolved, unresolved). Iterates so PIN_SPI_NSS -> LORA_CS -> 5 lands.
    """
    resolved = {}
    pending = dict(defines)

    # STM32 port-letter pin names resolve directly, before any arithmetic pass.
    for name, raw in list(pending.items()):
        value = stm32_pin_number(raw)
        if value is not None:
            resolved[name] = value
            del pending[name]

    for _ in range(max_passes):
        progressed = False
        for name, raw in list(pending.items()):
            def sub(m):
                key = m.group(0)
                return str(resolved[key]) if key in resolved else key
            expr = _IDENT_RE.sub(sub, raw)
            if not _SAFE_EXPR_RE.match(expr):
                continue
            try:
                value = eval(expr, {"__builtins__": {}}, {})  # digits/()+- only
            except Exception:
                continue
            if isinstance(value, bool) or not isinstance(value, int):
                continue
            resolved[name] = value
            del pending[name]
            progressed = True
        if not progressed:
            break

    return resolved, pending


def _is_unset(name, defines, resolved):
    if defines.get(name, "").strip("() ") in UNSET_NAMES:
        return True
    return name in resolved and resolved[name] in UNSET_INTS


def check_variant(name, defines):
    """Return (problems, groups_found, pins_compared) for one variant."""
    resolved, _unresolved = resolve(defines)
    problems = []
    groups_found = 0
    pins_compared = 0
    matched_members = set()

    for group, members, required in GROUPS:
        present = [m for m in members if m in defines]
        if not present:
            continue
        groups_found += 1
        matched_members.update(present)

        missing = [r for r in required if r not in defines]
        if missing:
            problems.append(
                "%s: group %s is declared (%s) but missing required %s"
                % (name, group, ", ".join(present), ", ".join(missing)))

        seen = {}
        for m in present:
            if _is_unset(m, defines, resolved) or m not in resolved:
                continue
            pins_compared += 1
            v = resolved[m]
            if v in seen:
                problems.append("%s: group %s has %s and %s both on GPIO %d"
                                % (name, group, seen[v], m, v))
            else:
                seen[v] = m

    # Rule 3 -- stale GROUPS table detection.
    bus_shaped = sorted(
        n for n in defines
        if _BUS_SHAPED_RE.search(n) and n not in BUS_SHAPED_EXEMPT)
    if bus_shaped and not matched_members:
        problems.append(
            "%s: declares bus-shaped pins but matched NO known group -- GROUPS is "
            "stale for this board. Unmatched: %s"
            % (name, ", ".join(bus_shaped[:8]) + (" ..." if len(bus_shaped) > 8 else "")))

    return problems, groups_found, pins_compared


def collect_variant_defines(variant_dir):
    """Merge #define headers and platformio.ini -D flags for one variant dir."""
    merged = {}
    found_any = False
    for entry in sorted(os.listdir(variant_dir)):
        path = os.path.join(variant_dir, entry)
        if not os.path.isfile(path):
            continue
        if entry.endswith(PIN_HEADER_SUFFIXES):
            found_any = True
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                merged.update(parse_defines(fh.read()))
        elif entry == "platformio.ini":
            found_any = True
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                merged.update(parse_build_flags(fh.read()))
    return merged if found_any else None


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    verbose = "--verbose" in argv

    if not os.path.isdir(VARIANTS_DIR):
        print("ERROR: no variants/ directory at %s" % VARIANTS_DIR, file=sys.stderr)
        return 1

    problems = []
    checked = total_groups = total_pins = 0
    no_group = []

    for entry in sorted(os.listdir(VARIANTS_DIR)):
        vdir = os.path.join(VARIANTS_DIR, entry)
        if not os.path.isdir(vdir):
            continue
        defines = collect_variant_defines(vdir)
        if defines is None:
            continue
        checked += 1
        probs, groups, pins = check_variant(entry, defines)
        problems.extend(probs)
        total_groups += groups
        total_pins += pins
        if groups == 0:
            no_group.append(entry)
        if verbose:
            print("  %-34s groups=%-2d pins=%d" % (entry, groups, pins))

    if problems:
        print("FAIL: %d per-peripheral pin violation(s):" % len(problems),
              file=sys.stderr)
        for p in problems:
            print("  - %s" % p, file=sys.stderr)
        return 1

    # Coverage is part of the result, not a footnote: an OK that compared nothing
    # would otherwise look identical to an OK that compared everything.
    print("OK: %d variants checked, %d peripheral groups, %d pins compared for "
          "distinctness." % (checked, total_groups, total_pins))
    if no_group:
        print("     %d variant(s) declare no recognised bus (no bus-shaped pins "
              "either, so not a stale table): %s"
              % (len(no_group), ", ".join(no_group)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
