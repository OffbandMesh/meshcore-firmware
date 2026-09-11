#!/usr/bin/env python3
"""Static guard for the runtime watchdog wiring (#1083).

For fifteen months the ESP32 repeater and companion never armed their task
watchdog: `board.startWatchdog(30)` and `board.feedWatchdog()` sat inside
`#if defined(NRF52_PLATFORM)`, so a hung ESP32 node stayed hung until someone
climbed to it. It compiled, it linked, the sensor role next door had the same
calls unguarded and worked, and nothing noticed -- a watchdog that is not armed
looks exactly like one that never had to fire.

This check reads every role's main.cpp and reports any `startWatchdog(` or
`feedWatchdog(` call that sits inside a preprocessor conditional naming a
PLATFORM. Feature guards (`#ifdef DISPLAY_CLASS`, `#if ENV_INCLUDE_GPS`) are not
platform guards and are not reported: the rule is "every platform arms it",
not "every build arms it".

Escape hatch, for a call that is deliberately platform-specific:

    board.feedWatchdog();   // wdt-guard: allow-platform <reason>

Usage:  python scripts/check_wdt_guard.py [--json-out FILE] [files...]
Exit 0 clean, 1 on any finding.
"""
import argparse
import json
import re
import sys
from pathlib import Path

# Every role entry point. Listed, not globbed, so a new role is a deliberate
# addition here rather than a silent omission.
DEFAULT_TARGETS = [
    "examples/simple_repeater/main.cpp",
    "examples/companion_radio/main.cpp",
    "examples/simple_room_server/main.cpp",
    "examples/simple_sensor/main.cpp",
]

WATCHDOG_CALLS = re.compile(r"\b(startWatchdog|feedWatchdog)\s*\(")
ALLOW = "wdt-guard: allow-platform"

# A conditional is a PLATFORM guard if its expression names one of these.
PLATFORM_TOKENS = re.compile(
    r"\b(NRF52_PLATFORM|ESP32|ESP_PLATFORM|ESP8266|STM32_PLATFORM|STM32|RP2040_PLATFORM|"
    r"RP2040|ARDUINO_ARCH_[A-Z0-9_]+|NRF52|SAMD)\b"
)

DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def analyze_source(src):
    """Return findings for one file's text.

    Each finding: {"line": int, "call": str, "guard": str}. `guard` is the text
    of the innermost platform conditional the call sits under (its #if/#elif
    expression, or "#else of <expr>").
    """
    findings = []
    # Stack entries: (is_platform_guard, description)
    stack = []
    for lineno, line in enumerate(src.splitlines(), 1):
        m = DIRECTIVE.match(line)
        if m:
            kind, rest = m.group(1), m.group(2).strip()
            if kind in ("if", "ifdef", "ifndef"):
                stack.append((bool(PLATFORM_TOKENS.search(rest)), "#%s %s" % (kind, rest)))
            elif kind == "elif":
                if stack:
                    stack.pop()
                stack.append((bool(PLATFORM_TOKENS.search(rest)), "#elif %s" % rest))
            elif kind == "else":
                if stack:
                    was_platform, desc = stack.pop()
                    stack.append((was_platform, "#else of %s" % desc))
            elif kind == "endif":
                if stack:
                    stack.pop()
            continue
        if ALLOW in line:
            continue
        for call in WATCHDOG_CALLS.finditer(line):
            platform_guards = [d for is_p, d in stack if is_p]
            if platform_guards:
                findings.append({
                    "line": lineno,
                    "call": call.group(1),
                    "guard": platform_guards[-1],
                })
    return findings


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("files", nargs="*", default=DEFAULT_TARGETS)
    ap.add_argument("--json-out")
    args = ap.parse_args(argv)

    root = Path(__file__).resolve().parent.parent
    report = {}
    total = 0
    for rel in args.files:
        path = Path(rel)
        if not path.is_absolute():
            path = root / rel
        if not path.exists():
            print("check_wdt_guard: missing target %s" % rel, file=sys.stderr)
            return 2
        found = analyze_source(path.read_text(encoding="utf-8", errors="replace"))
        report[rel] = found
        for f in found:
            total += 1
            print("%s:%d: %s() is inside a platform guard: %s" % (rel, f["line"], f["call"], f["guard"]))
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(report, indent=2))
    if total:
        print("check_wdt_guard: %d finding(s). The watchdog must be armed and fed on every platform (#1083)." % total)
        return 1
    print("check_wdt_guard: clean (%d file(s))" % len(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
