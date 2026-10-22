#!/usr/bin/env python3
"""Guard: no file-static function in a display driver may share a name with a
DisplayDriver virtual (#812).

WHY THIS EXISTS
    ST7735Display.cpp defined `static void setRotation(uint8_t)` and called it
    from a MEMBER function. When #148 added `virtual void setRotation(uint8_t)`
    to DisplayDriver, C++ name lookup -- which searches class scope, including
    base classes, BEFORE file scope -- silently rebound that call to the new,
    empty base method. The driver stopped writing MADCTL. Boot rotation was
    discarded on 27 envs across four boards for two months, shipped in every
    release from offband-v1.0.0.

    It compiled clean and emitted no warning, and no compiler flag catches it:
    `-Woverloaded-virtual` fires when a MEMBER hides a base virtual, whereas here
    a free function was hidden BY the inherited member -- ordinary, legal lookup.
    So the only practical guard is this one.

WHAT IT FLAGS
    A `static <type> <name>(` at file scope in any display driver source whose
    <name> matches a `virtual` declared in DisplayDriver.h. Drivers are found in
    two places:

      src/helpers/ui/*Display.cpp, *Driver.cpp   -- the shared drivers
      variants/*/*Display.cpp, *Driver.cpp       -- variant-local drivers (#1123)

    The second location exists because a board can carry its own driver rather
    than modify a shared one -- RC52Display lives in variants/heltec_rc52/. A
    guard that only looked in src/helpers/ui/ would pass while missing it.

    The fix is never to qualify the call -- that leaves the trap armed for the
    next reader. Rename it, ideally to a private member, so a future collision
    becomes member-hides-virtual and a compiler can actually see it.

Run: python scripts/check_display_virtuals.py
Exit 0 = clean, 1 = collisions found (or the guard could not parse the base).
"""
from __future__ import annotations
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE_REL = Path("src") / "helpers" / "ui" / "DisplayDriver.h"

# Relative to the repo root. Order is the report order.
SCAN_GLOBS = (
    "src/helpers/ui/*Display.cpp",
    "src/helpers/ui/*Driver.cpp",
    "variants/*/*Display.cpp",
    "variants/*/*Driver.cpp",
)

VIRTUAL_RE = re.compile(r"\bvirtual\b[^;{]*?\b([A-Za-z_]\w*)\s*\(")
# Leading whitespace and a same-line `template <...>` prefix are allowed: an
# indented static (inside a namespace) and `template <typename T> static ...` are
# both file-statics this guard exists to catch, and both were missed before #1123.
# `static` need not be the first specifier either (`inline static void f(`,
# `void static f(`), and when the return type comes first nothing sits between
# `static` and the name, so the post-`static` type is optional.
STATIC_FN_RE = re.compile(
    r"^[ \t]*(?:template\s*<[^>]*>\s*)?(?:[A-Za-z_][\w:<>,*&]*\s+)*?"
    r"static\s+(?:[A-Za-z_][\w:<>,*&\s]*?\b)?([A-Za-z_]\w*)\s*\(",
    re.MULTILINE)


def base_virtuals(root: Path = ROOT) -> set[str]:
    src = (root / BASE_REL).read_text(encoding="utf-8", errors="replace")
    return {m.group(1) for m in VIRTUAL_RE.finditer(src)}


def driver_sources(root: Path = ROOT) -> list[Path]:
    # SCAN_GLOBS are disjoint (distinct directories or distinct suffixes), so the
    # dedupe only guards a future overlapping pattern. Paths are deliberately not
    # resolve()d: on Windows that can expand an 8.3 short root and break
    # relative_to(root) in find_collisions.
    found: list[Path] = []
    for pattern in SCAN_GLOBS:
        for path in sorted(root.glob(pattern)):
            if path not in found:
                found.append(path)
    return found


def find_collisions(root: Path = ROOT) -> list[tuple[str, int, str]]:
    """(repo-relative path, line, name) for every file-static that shadows a virtual."""
    virtuals = base_virtuals(root)
    findings = []
    for path in driver_sources(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in STATIC_FN_RE.finditer(text):
            name = m.group(1)
            if name in virtuals:
                line = text[: m.start()].count("\n") + 1
                findings.append((path.relative_to(root).as_posix(), line, name))
    return findings


def main(root: Path = ROOT) -> int:
    virtuals = base_virtuals(root)
    if not virtuals:
        print("FAIL: parsed zero virtuals from DisplayDriver.h -- the guard is broken, not the tree")
        return 1

    sources = driver_sources(root)
    findings = find_collisions(root)

    print(f"checked {len(sources)} driver source(s) "
          f"against {len(virtuals)} DisplayDriver virtuals")

    if not findings:
        print("OK: no file-static function collides with a DisplayDriver virtual")
        return 0

    for rel, line, name in findings:
        print(f"COLLISION {rel}:{line}  static '{name}' shares its name with a "
              f"DisplayDriver virtual.")
        print(f"          A call to '{name}' from a member function binds to the "
              f"BASE method, not this one, silently.")
    print(f"\n{len(findings)} collision(s). See #812.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
