#!/usr/bin/env python3
"""Guard: no UK-spelled identifier in our own C++ (#864).

WHY THIS EXISTS
    #1122 renamed the color-splash API to US English. Forty days later the
    branch was rebased onto a base that had grown an event-splash block using
    the OLD name -- `if (!colour && ...)` -- inside `#ifdef OFFBAND_EVENT_SPLASH`.
    git merged both sides cleanly because they touched different lines, and the
    only env that compiles that block is the QCC badge, which none of the five
    builds run locally covered. It reached CI as a broken matrix leg.

    That is the shape this catches: a rename that is complete everywhere the
    author happened to build, and stale behind a build flag they did not.

WHAT IT FLAGS
    A UK spelling used as an IDENTIFIER -- in code, not in prose. Comments and
    string literals are stripped first, deliberately: prose is a style question
    and does not break a build, while an identifier either resolves or does not.

    Protocol names we do not own are allowed. `LPP_COLOUR` and `kLppColour` are
    Cayenne LPP's own spelling; renaming them would diverge from the spec they
    implement.

Run: python scripts/check_us_english_identifiers.py
Exit 0 = clean, 1 = UK-spelled identifiers found (or nothing could be scanned).
"""
from __future__ import annotations
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SCAN_GLOBS = (
    "src/**/*.h", "src/**/*.cpp",
    "variants/**/*.h", "variants/**/*.cpp",
    "examples/**/*.h", "examples/**/*.cpp",
)

# Each is a word we would never choose ourselves. Keep this list to spellings
# that have actually appeared in our identifiers -- a speculative list produces
# noise and gets switched off.
UK_WORDS = ("colour", "centre", "behaviour", "initialise", "serialise", "analyse")

# Namespaces owned by a spec or a third-party library, which must keep their
# spelling. Cayenne LPP calls data type 135 COLOUR; our constants mirror the
# protocol deliberately (LPP_COLOUR, LPP_COLOUR_MULT, kLppColour), and renaming
# them would diverge from the spec they implement.
ALLOWED_PREFIXES = ("LPP_", "kLpp")


def allowed(name: str) -> bool:
    return name.startswith(ALLOWED_PREFIXES)

WORD_RE = re.compile("|".join(UK_WORDS), re.IGNORECASE)
IDENT_RE = re.compile(r"[A-Za-z_]\w*")
# Block comments, line comments, then string and char literals. Order matters:
# a // inside a string would otherwise swallow the rest of the line.
STRIP_RE = re.compile(
    r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'",
    re.DOTALL)


def strip_prose(text: str) -> str:
    """Blank out comments and literals, keeping newlines so line numbers hold."""
    return STRIP_RE.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def sources(root: Path = ROOT) -> list[Path]:
    found: list[Path] = []
    for pattern in SCAN_GLOBS:
        found.extend(sorted(root.glob(pattern)))
    return found


def find_identifiers(text: str) -> list[tuple[int, str]]:
    """(line, identifier) for every UK-spelled identifier outside prose."""
    hits = []
    code = strip_prose(text)
    for m in IDENT_RE.finditer(code):
        name = m.group(0)
        if allowed(name) or not WORD_RE.search(name):
            continue
        hits.append((code[: m.start()].count("\n") + 1, name))
    return hits


def scan(root: Path = ROOT) -> list[tuple[str, int, str]]:
    findings = []
    for path in sources(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        for line, name in find_identifiers(text):
            findings.append((path.relative_to(root).as_posix(), line, name))
    return findings


def main(root: Path = ROOT) -> int:
    files = sources(root)
    if not files:
        print("FAIL: scanned zero sources -- the guard is broken, not the tree")
        return 1

    findings = scan(root)
    print(f"checked {len(files)} source(s) for UK-spelled identifiers")

    if not findings:
        print("OK: no UK-spelled identifiers")
        return 0

    for rel, line, name in findings:
        print(f"UK SPELLING {rel}:{line}  '{name}'")
    print(f"\n{len(findings)} identifier(s). US English everywhere, including "
          f"code behind build flags you are not compiling.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
