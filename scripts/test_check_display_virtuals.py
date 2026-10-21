#!/usr/bin/env python3
"""Self-test for scripts/check_display_virtuals.py (#812, #1123).

The guard has nothing real to catch -- the tree is clean -- so a passing run on
the real tree proves only that it does not false-positive. These tests plant a
collision in a throwaway tree to prove it actually finds one, and in particular
that it finds one in a VARIANT-LOCAL driver, which the original guard could not
see (RC52Display lives in variants/heltec_rc52/).

Run: python scripts/test_check_display_virtuals.py
"""
from __future__ import annotations
import contextlib
import io
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_display_virtuals as g

REAL_BASE = (g.ROOT / g.BASE_REL).read_text(encoding="utf-8", errors="replace")

COLLIDING = "static void setRotation(uint8_t m) {\n}\n"
HARMLESS = "static void rotateHelper(uint8_t m) {\n}\n"


def make_tree(root: Path, files: dict[str, str], base: str = REAL_BASE) -> Path:
    """A minimal repo shape: the real DisplayDriver.h plus the given driver files."""
    (root / g.BASE_REL).parent.mkdir(parents=True, exist_ok=True)
    (root / g.BASE_REL).write_text(base, encoding="utf-8")
    for rel, text in files.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return root


def quiet_main(root: Path) -> int:
    with contextlib.redirect_stdout(io.StringIO()):
        return g.main(root)


def test_real_tree_is_clean():
    assert g.find_collisions() == [], g.find_collisions()


def test_real_base_parses_the_virtuals_this_guard_depends_on():
    v = g.base_virtuals()
    assert "setRotation" in v, "the #812 virtual is not being parsed"
    assert "colorSplashArt" in v, "the #1122 color-art virtual is not being parsed"


def test_rc52_variant_driver_is_scanned():
    rels = {p.relative_to(g.ROOT).as_posix() for p in g.driver_sources()}
    assert "variants/heltec_rc52/RC52Display.cpp" in rels, sorted(rels)


def test_planted_variant_collision_is_reported():
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"variants/fakeboard/FakeDisplay.cpp": COLLIDING})
        found = g.find_collisions(root)
        assert found == [("variants/fakeboard/FakeDisplay.cpp", 1, "setRotation")], found


def test_old_scan_misses_the_variant_collision_the_new_scan_catches():
    # The red proof for #1123. With the pre-#1123 globs -- src/helpers/ui/ only --
    # a variant-local collision is invisible; with the extended globs it is found.
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"variants/fakeboard/FakeDisplay.cpp": COLLIDING})
        saved = g.SCAN_GLOBS
        try:
            g.SCAN_GLOBS = ("src/helpers/ui/*Display.cpp", "src/helpers/ui/*Driver.cpp")
            assert g.find_collisions(root) == [], "old globs unexpectedly saw the variant file"
        finally:
            g.SCAN_GLOBS = saved
        assert len(g.find_collisions(root)) == 1


def test_planted_shared_driver_collision_is_still_reported():
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"src/helpers/ui/FakeDisplay.cpp": COLLIDING})
        found = g.find_collisions(root)
        assert found == [("src/helpers/ui/FakeDisplay.cpp", 1, "setRotation")], found


def test_variant_driver_named_driver_is_scanned_too():
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"variants/fakeboard/FakeDriver.cpp": COLLIDING})
        assert len(g.find_collisions(root)) == 1


def test_static_shapes_that_must_all_be_caught():
    # Each is a file-static named like a virtual. The first two and the last three
    # were MISSED before #1123 (Gemini-review findings, confirmed by running the
    # old regex).
    shapes = {
        "same-line template":      "template <typename T> static void setRotation(T m) {\n}\n",
        "indented (in namespace)": "namespace {\n  static void setRotation(uint8_t m) {\n  }\n}\n",
        "template on own line":    "template <typename T>\nstatic void setRotation(T m) {\n}\n",
        "static inline":           "static inline void setRotation(uint8_t m) {\n}\n",
        "attribute":               "static IRAM_ATTR void setRotation(uint8_t m) {\n}\n",
        "return type split":       "static void\nsetRotation(uint8_t m) {\n}\n",
        "inline before static":    "inline static void setRotation(uint8_t m) {\n}\n",
        "return type before":      "void static setRotation(uint8_t m) {\n}\n",
        "pointer type before":     "const char* static setRotation(uint8_t m) {\n}\n",
    }
    for label, src in shapes.items():
        with tempfile.TemporaryDirectory() as d:
            root = make_tree(Path(d), {"variants/fakeboard/FakeDisplay.cpp": src})
            assert len(g.find_collisions(root)) == 1, f"missed: {label}"


def test_non_colliding_static_is_ignored():
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"variants/fakeboard/FakeDisplay.cpp": HARMLESS})
        assert g.find_collisions(root) == []


def test_main_exit_codes():
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"variants/fakeboard/FakeDisplay.cpp": COLLIDING})
        assert quiet_main(root) == 1
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"variants/fakeboard/FakeDisplay.cpp": HARMLESS})
        assert quiet_main(root) == 0


def test_unparseable_base_fails_loudly_rather_than_passing():
    # A guard that parses zero virtuals would report "no collisions" for any tree.
    with tempfile.TemporaryDirectory() as d:
        root = make_tree(Path(d), {"variants/fakeboard/FakeDisplay.cpp": COLLIDING}, base="")
        assert quiet_main(root) == 1


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
