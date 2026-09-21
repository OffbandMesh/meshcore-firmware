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

What it sees through (#1268, from adversarial review of the first version, which
both of these defeated):
  - a conditional split across lines with a trailing backslash. Lines are spliced
    first, as the preprocessor does, so `#if \\` + `defined(NRF52_PLATFORM)` is
    one directive. Findings still report the physical line of the call.
  - a platform test hidden behind a macro DEFINED IN THE SAME FILE, followed
    transitively: `#define ON_NRF defined(NRF52_PLATFORM)` then `#if ON_NRF`.

Limits -- this is a text scanner, not a preprocessor. It does NOT see:
  - a platform alias defined in another file (a header, or a -D build flag);
  - a watchdog call made from inside a helper function that is itself guarded.
Both need a real preprocessor pass over the include graph. If either pattern
appears in a role's main.cpp, this check will stay green and must not be trusted.

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
DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)(?:\([^)]*\))?\s*(.*)$")
CONTINUED = re.compile(r"\\\s*$")


def logical_lines(src):
    """Yield (first_physical_lineno, text) with backslash-newlines spliced.

    Translation phase 2, which the preprocessor runs before it looks for
    directives. Without it a conditional written as `#if \\` + `defined(X)` is
    seen as `#if` with an empty expression and escapes the platform test. The
    pieces are joined with a space so two tokens can never fuse into one. The
    line number is where the logical line STARTS, so a reported call still
    points at a line a person can find.
    """
    start, parts = None, []
    for lineno, line in enumerate(src.splitlines(), 1):
        if start is None:
            start = lineno
        if CONTINUED.search(line):
            parts.append(CONTINUED.sub("", line))
            continue
        parts.append(line)
        yield start, " ".join(parts)
        start, parts = None, []
    if parts:                       # file ends on a continuation
        yield start, " ".join(parts)


def platform_aliases(lines):
    """Names #define'd IN THIS FILE whose body names a platform, transitively."""
    bodies = {}
    for _, text in lines:
        m = DEFINE.match(text)
        if m:
            bodies[m.group(1)] = m.group(2)
    aliases = {name for name, body in bodies.items() if PLATFORM_TOKENS.search(body)}
    grew = True
    while grew:                     # A -> B -> platform
        grew = False
        for name, body in bodies.items():
            if name not in aliases and any(re.search(r"\b%s\b" % re.escape(a), body) for a in aliases):
                aliases.add(name)
                grew = True
    return aliases


def analyze_source(src):
    """Return findings for one file's text.

    Each finding: {"line": int, "call": str, "guard": str}. `guard` is the text
    of the innermost platform conditional the call sits under (its #if/#elif
    expression, or "#else of <expr>").
    """
    findings = []
    lines = list(logical_lines(src))
    aliases = platform_aliases(lines)

    def is_platform(expr):
        if PLATFORM_TOKENS.search(expr):
            return True
        return any(re.search(r"\b%s\b" % re.escape(a), expr) for a in aliases)

    # Stack entries: (is_platform_guard, description)
    stack = []
    for lineno, line in lines:
        m = DIRECTIVE.match(line)
        if m:
            kind, rest = m.group(1), m.group(2).strip()
            if kind in ("if", "ifdef", "ifndef"):
                stack.append((is_platform(rest), "#%s %s" % (kind, rest)))
            elif kind == "elif":
                if stack:
                    stack.pop()
                stack.append((is_platform(rest), "#elif %s" % rest))
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
