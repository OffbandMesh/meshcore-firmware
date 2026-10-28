#!/usr/bin/env python
"""Offband #1692: every nRF52 SafeBoot variant can actually wake from its sleep.

Why this exists
---------------
nRF52 SYSTEM_OFF has no timer. It ends only on a wake source -- the LPCOMP battery
comparator, a GPIO SENSE on the button, NFC, or reset. #1687 measured what happens when
that goes wrong: on the QCC badge SafeBoot armed its button wake on the wrong pin at the
wrong polarity, the wake condition was already true, and the board reset every 128 ms
instead of sleeping. Nothing caught it -- it compiled, shipped and drained the cell.

Two kinds of rule, both needed
------------------------------
Per variant: an nRF52 variant with SafeBoot enabled must declare a wake source --
`BATTERY_LPCOMP_INPUT`, or a connected `PIN_USER_BTN` -- or appear in EXEMPT with the
reason written down. Since #1687 a board with neither does not loop (SafeBoot falls back
to staying on and restarting on a timer), but it spends milliamps where a declared source
would spend microamps, so declaring neither must be a decision somebody made.

In SafeBoot.cpp: a declared button only counts if SafeBoot arms it correctly. So the
source must reach the button through the variant's pin map (`g_ADigitalPinMap`), never
hand PIN_USER_BTN to the nRF HAL as a chip pin, honour `USER_BTN_PRESSED`, and gate
SYSTEM_OFF on there being a wake source at all. Without these the per-variant rule would
pass the exact defect #1687 fixed.

Limits, stated rather than hidden
---------------------------------
The unit is the variant DIRECTORY, not each env in it: a macro set in any env counts for
all of them. And the source rules are text rules -- they stop the #1687 shapes from coming
back, not every way of writing a new defect. The runtime falls back to a timed restart
when no source is usable, so what slips past here costs current, not a loop.

Run:  python scripts/check_safeboot_wake_source.py [--verbose]
Exit: 0 clean, 1 on any violation. Paired negative tests in
      scripts/test_check_safeboot_wake_source.py
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VARIANTS_DIR = os.path.join(REPO_ROOT, "variants")
SAFEBOOT_CPP = os.path.join(REPO_ROOT, "src", "SafeBoot.cpp")

HEADER_SUFFIXES = (".h", ".cpp")

# Variants that run SafeBoot on nRF52 with no wake source, on purpose. Each takes the
# timed-restart path: it stays on at a few mA and restarts to recheck. An entry that
# stops being true -- the board gains a source, or SafeBoot is turned off for it -- is a
# failure, so this list cannot quietly rot.
EXEMPT = {
    # No user button (target.cpp has it commented out) and no LPCOMP wiring declared.
    "ikoka_nano_nrf": "no button, no comparator declared; timed restart",
    # PIN_BUTTON1 is (-1): the board has no user button. No LPCOMP wiring declared.
    "wio_wm1110": "no button, no comparator declared; timed restart",
}

# #1889: variants whose SSD1306 is always powered (no reset line, no power switch), so
# SafeBoot must put it to sleep first. Each must declare SAFEBOOT_SSD1306_I2C_ADDR.
REQUIRE_DISPLAY_SLEEP = {
    "qcc_badge": "owner-seen 2026-10-28: the panel stays lit through a refused sleep",
}

# Below this many checked variants the check is not looking at the tree it thinks it is.
COVERAGE_FLOOR = 25


def strip_ini_comments(text):
    return "\n".join(line.split(";", 1)[0] for line in text.splitlines())


def strip_c_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def is_nrf52(ini):
    return bool(re.search(r"^\s*(platform\s*=.*nordicnrf52|extends\s*=.*nrf52)", ini,
                          re.M | re.I))


def define_values(name, ini, headers):
    """Every value the macro is given, as written ('' for a bare define)."""
    values = [v or "" for v in
              re.findall(r"-D\s*%s(?:=(\S+))?(?=\s|$)" % re.escape(name), ini)]
    values += [v.strip() for v in
               re.findall(r"^\s*#\s*define\s+%s\b[ \t]*([^\n]*)" % re.escape(name),
                          headers, re.M)]
    return values


def define_value(name, ini, headers):
    """The first value the macro is given, or None when it is never defined."""
    values = define_values(name, ini, headers)
    return values[0] if values else None


UNCONNECTED = ("-1", "RADIOLIB_NC")


def connected(values, ini="", headers="", depth=0):
    # A target.cpp often carries `#ifndef PIN_USER_BTN / #define PIN_USER_BTN (-1)` as a
    # fallback beside the real definition in variant.h, so ANY connected value counts.
    # An alias (`PIN_USER_BTN PIN_BUTTON1`, `PIN_BUTTON1 (-1)`) is followed to its value:
    # a button that resolves to -1 is no button however many names it passes through.
    for v in values:
        bare = re.sub(r"[()\s]", "", v)
        if bare in UNCONNECTED:
            continue
        if re.fullmatch(r"[A-Za-z_]\w*", bare) and depth < 5:
            inner = define_values(bare, ini, headers)
            if inner:
                if connected(inner, ini, headers, depth + 1):
                    return True
                continue
        return True  # a number, an expression, or a core macro this script cannot see
    return False


def check_variant(name, ini, headers):
    """(applies, problems): applies = nRF52 with SafeBoot enabled."""
    ini = strip_ini_comments(ini)
    headers = strip_c_comments(headers)
    if not is_nrf52(ini):
        return False, []
    enabled = define_value("SAFE_BOOT_DISABLED", ini, headers) is None and (
        define_value("SAFEBOOT_PIN_VBAT_READ", ini, headers) is not None
        or define_value("PIN_VBAT_READ", ini, headers) is not None)
    has_source = (define_value("BATTERY_LPCOMP_INPUT", ini, headers) is not None
                  or connected(define_values("PIN_USER_BTN", ini, headers), ini, headers))
    problems = []
    if name in EXEMPT:
        if not enabled:
            problems.append("%s: in EXEMPT but SafeBoot is not enabled for it -- remove "
                            "the entry" % name)
        elif has_source:
            problems.append("%s: in EXEMPT but now declares a wake source -- remove the "
                            "entry" % name)
    elif enabled and not has_source:
        problems.append("%s: nRF52 SafeBoot with no wake source -- declare "
                        "BATTERY_LPCOMP_INPUT or a connected PIN_USER_BTN, or add it to "
                        "EXEMPT with the reason" % name)
    return enabled, problems


def check_display_sleep(name, ini, headers):
    """#1889: the display-sleep lever is declared where needed, and only on an SSD1306."""
    ini = strip_ini_comments(ini)
    headers = strip_c_comments(headers)
    addr = define_value("SAFEBOOT_SSD1306_I2C_ADDR", ini, headers)
    problems = []
    if addr is None:
        if name in REQUIRE_DISPLAY_SLEEP:
            problems.append("%s: must declare SAFEBOOT_SSD1306_I2C_ADDR (%s)"
                            % (name, REQUIRE_DISPLAY_SLEEP[name]))
        return problems
    if define_value("DISPLAY_CLASS", ini, headers) != "SSD1306Display":
        problems.append("%s: SAFEBOOT_SSD1306_I2C_ADDR is set but DISPLAY_CLASS is not "
                        "SSD1306Display -- the sleep stream is SSD1306 commands" % name)
    display_addr = define_value("DISPLAY_ADDRESS", ini, headers) or "0x3C"
    try:
        if int(addr, 0) != int(display_addr, 0):
            problems.append("%s: SAFEBOOT_SSD1306_I2C_ADDR %s is not the display's address %s"
                            % (name, addr, display_addr))
    except ValueError:
        problems.append("%s: SAFEBOOT_SSD1306_I2C_ADDR %r is not a number" % (name, addr))
    return problems


def check_safeboot_source(src):
    src = strip_c_comments(src)
    problems = []
    if re.search(r"nrf_gpio_\w+\s*\(\s*PIN_USER_BTN\b", src):
        problems.append("SafeBoot.cpp hands PIN_USER_BTN to the nRF HAL as a chip pin; "
                        "map it through g_ADigitalPinMap (#1687)")
    if "g_ADigitalPinMap[PIN_USER_BTN]" not in src:
        problems.append("SafeBoot.cpp no longer maps the button through g_ADigitalPinMap "
                        "(#1687)")
    # A comparison, not a mention: the file's own `#define USER_BTN_PRESSED LOW` default
    # would satisfy a bare substring test while the arming code ignored it.
    if not re.search(r"USER_BTN_PRESSED\s*==\s*HIGH", src):
        problems.append("SafeBoot.cpp ignores USER_BTN_PRESSED; an active-high button "
                        "armed active-low wakes at once (#1687)")
    if not (re.search(r"p\s*>=\s*0", src) and re.search(r"if\s*\(\s*safeBootButtonInMap\(\)\s*\)", src)):
        problems.append("SafeBoot.cpp no longer keeps PIN_USER_BTN=-1 out of the pin map "
                        "(lilygo_t_impulse_plus declares its button that way)")
    # #1889: before anything else -- the battery reads and the refusal both come after.
    if not re.search(r"void\s+SafeBoot::checkAndMaybeSleep\(\)\s*\{\s*#if\s+defined\("
                     r"SAFEBOOT_SSD1306_I2C_ADDR\)\s*safeBootDisplayOff\(\);", src):
        problems.append("SafeBoot.cpp no longer puts an always-powered display to sleep as "
                        "the first thing it does (#1889)")
    m = re.search(r"void\s+safeBootDisplayOff\(\)\s*\{(.*?)\n\}", src, re.S)
    body = m.group(1) if m else ""
    guard = re.search(r"if\s*\(\s*!\s*safeBootI2cBusIdle\(\)\s*\)\s*return\s*;", body)
    begin = body.find("Wire.begin()")
    if not guard or begin < 0 or guard.start() > begin:
        problems.append("safeBootDisplayOff() must check the bus is idle before Wire.begin(): "
                        "the nRF52 TwoWire has no timeout, so a stuck line hangs SafeBoot "
                        "before it reads the battery (#1889)")
    if not re.search(r"Wire\.write\(\s*offband::kSsd1306SleepStream", body):
        problems.append("safeBootDisplayOff() no longer sends the SSD1306 sleep stream (#1889)")
    if not re.search(r"if\s*\(\s*wake\s*!=\s*offband::SafeBootWake::TimedRestart\s*\)", src):
        problems.append("SafeBoot.cpp no longer gates SYSTEM_OFF on having a wake source "
                        "(#1687)")
    return problems


def read_variant(path):
    ini, headers = "", []
    for fname in sorted(os.listdir(path)):
        full = os.path.join(path, fname)
        if fname == "platformio.ini":
            with open(full, encoding="utf-8", errors="replace") as f:
                ini = f.read()
        elif fname.endswith(HEADER_SUFFIXES):
            with open(full, encoding="utf-8", errors="replace") as f:
                headers.append(f.read())
    return ini, "\n".join(headers)


def main(argv):
    verbose = "--verbose" in argv
    problems, checked = [], []
    seen = set()
    for name in sorted(os.listdir(VARIANTS_DIR)):
        path = os.path.join(VARIANTS_DIR, name)
        if not os.path.isfile(os.path.join(path, "platformio.ini")):
            continue
        seen.add(name)
        ini, headers = read_variant(path)
        applies, probs = check_variant(name, ini, headers)
        problems += probs
        problems += check_display_sleep(name, ini, headers)
        if applies:
            checked.append(name)
    for name in sorted((set(EXEMPT) | set(REQUIRE_DISPLAY_SLEEP)) - seen):
        problems.append("%s: listed here but no such variant -- remove the entry" % name)
    with open(SAFEBOOT_CPP, encoding="utf-8") as f:
        problems += check_safeboot_source(f.read())
    if len(checked) < COVERAGE_FLOOR:
        problems.append("only %d nRF52 SafeBoot variants found (floor %d) -- the variant "
                        "scan is broken" % (len(checked), COVERAGE_FLOOR))
    if verbose:
        for name in checked:
            print("  checked %s%s" % (name, "  (exempt)" if name in EXEMPT else ""))
    for p in problems:
        print("FAIL " + p)
    print("%d nRF52 SafeBoot variants checked, %d exempt, %d problem(s)"
          % (len(checked), len(EXEMPT), len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
