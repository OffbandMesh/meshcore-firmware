#!/usr/bin/env python3
"""Guard: no file-static function in a display driver may share a name with a
DisplayDriver virtual (#812).

WHY THIS EXISTS
    ST7735Display.cpp defined `static void setRotation(uint8_t)` and called it
    from a MEMBER function. When #148 added `virtual void setRotation(uint8_t)`
    to DisplayDriver, C++ name lookup -- which searches class scope, including
    base classes, BEFORE file scope -- silently rebound that call to the new,
    empty base method. The driver stopped writing MADCTL. Boot rotation was
    discarded on 15 envs across four boards for two months, shipped in every
    release from offband-v1.0.0.

    It compiled clean and emitted no warning, and no compiler flag catches it:
    `-Woverloaded-virtual` fires when a MEMBER hides a base virtual, whereas here
    a free function was hidden BY the inherited member -- ordinary, legal lookup.
    So the only practical guard is this one.

WHAT IT FLAGS
    A `static <type> <name>(` at file scope in src/helpers/ui/*Display.cpp (or
    *Driver.cpp) whose <name> matches any `virtual` declared in DisplayDriver.h.

    The fix is never to qualify the call -- that leaves the trap armed for the
    next reader. Rename it, ideally to a private member, so a future collision
    becomes member-hides-virtual and a compiler can actually see it.

Run: python scripts/check_display_virtuals.py
Exit 0 = clean, 1 = collisions found.
"""
from __future__ import annotations
import re
import sys
from pathlib import Path

UI = Path(__file__).resolve().parent.parent / "src" / "helpers" / "ui"
BASE = UI / "DisplayDriver.h"

VIRTUAL_RE = re.compile(r"\bvirtual\b[^;{]*?\b([A-Za-z_]\w*)\s*\(")
STATIC_FN_RE = re.compile(r"^static\s+[A-Za-z_][\w:<>,*&\s]*?\b([A-Za-z_]\w*)\s*\(", re.MULTILINE)


def base_virtuals() -> set[str]:
    src = BASE.read_text(encoding="utf-8", errors="replace")
    return {m.group(1) for m in VIRTUAL_RE.finditer(src)}


def driver_sources():
    yield from sorted(UI.glob("*Display.cpp"))
    yield from sorted(UI.glob("*Driver.cpp"))


def main() -> int:
    virtuals = base_virtuals()
    if not virtuals:
        print("FAIL: parsed zero virtuals from DisplayDriver.h -- the guard is broken, not the tree")
        return 1

    findings = []
    for path in driver_sources():
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in STATIC_FN_RE.finditer(text):
            name = m.group(1)
            if name in virtuals:
                line = text[: m.start()].count("\n") + 1
                findings.append((path.name, line, name))

    print(f"checked {len(list(driver_sources()))} driver source(s) "
          f"against {len(virtuals)} DisplayDriver virtuals")

    if not findings:
        print("OK: no file-static function collides with a DisplayDriver virtual")
        return 0

    for fname, line, name in findings:
        print(f"COLLISION {fname}:{line}  static '{name}' shares its name with a "
              f"DisplayDriver virtual.")
        print(f"          A call to '{name}' from a member function binds to the "
              f"BASE method, not this one, silently.")
    print(f"\n{len(findings)} collision(s). See #812.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
