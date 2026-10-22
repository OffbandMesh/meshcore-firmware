#!/usr/bin/env python3
"""Self-test for scripts/check_us_english_identifiers.py (#864).

The real tree is clean, so a passing run there proves only that the guard does
not false-positive. These tests plant the exact defect it was written for -- a
UK-spelled identifier inside an `#ifdef` that most envs never compile -- and the
prose cases it must stay quiet about.

Run: python scripts/test_check_us_english_identifiers.py
"""
from __future__ import annotations
import contextlib
import io
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_us_english_identifiers as g


def tree(root: Path, files: dict[str, str]) -> Path:
    for rel, text in files.items():
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8")
    return root


def quiet_main(root: Path) -> int:
    with contextlib.redirect_stdout(io.StringIO()):
        return g.main(root)


def test_real_tree_is_clean():
    assert g.scan() == [], g.scan()


def test_the_defect_this_was_written_for():
    # #1122's rename met #1182's event splash on a rebase: both sides merged
    # cleanly, and the stale name sat behind a flag only the badge env sets.
    src = ("void drawSplash(DisplayDriver& d) {\n"
           "  const bool color = true;\n"
           "#ifdef OFFBAND_EVENT_SPLASH\n"
           "  if (!colour) { return; }\n"
           "#endif\n"
           "}\n")
    with tempfile.TemporaryDirectory() as t:
        root = tree(Path(t), {"src/helpers/ui/OffbandSplash.cpp": src})
        found = g.scan(root)
        assert found == [("src/helpers/ui/OffbandSplash.cpp", 4, "colour")], found


def test_identifier_shapes_are_all_caught():
    shapes = {
        "member":     "int colourSplashArt();\n",
        "macro":      "#define OFFBAND_COLOUR_SPLASH 1\n",
        "type":       "struct ColourArt { int w; };\n",
        "local":      "void f() { bool colour = false; }\n",
        "other word": "void centreText(int x);\n",
    }
    for label, src in shapes.items():
        with tempfile.TemporaryDirectory() as t:
            root = tree(Path(t), {"src/x.h": src})
            assert len(g.scan(root)) == 1, f"missed: {label}"


def test_prose_is_not_flagged():
    # Comments and strings are stripped on purpose: prose does not break a build,
    # and flagging it would make the guard something people switch off.
    quiet = {
        "line comment":  "// the colour roles live here\nint x;\n",
        "block comment": "/* one colour, two cues */\nint x;\n",
        "string":        'const char* s = "colour";\n',
        "char literal":  "char c = 'x'; // centre\n",
    }
    for label, src in quiet.items():
        with tempfile.TemporaryDirectory() as t:
            root = tree(Path(t), {"src/x.cpp": src})
            assert g.scan(root) == [], f"false positive: {label}"


def test_a_comment_cannot_hide_a_later_identifier():
    # Line numbers must survive stripping, and code after a block comment must
    # still be scanned.
    src = "/* colour\n   colour */\nint colourMode;\n"
    with tempfile.TemporaryDirectory() as t:
        root = tree(Path(t), {"src/x.cpp": src})
        assert g.scan(root) == [("src/x.cpp", 3, "colourMode")], g.scan(root)


def test_protocol_namespace_is_allowed():
    # Cayenne LPP's own spelling. Renaming these would diverge from the spec.
    src = "#define LPP_COLOUR 135\n#define LPP_COLOUR_MULT 1\nenum { kLppColour = 135 };\n"
    with tempfile.TemporaryDirectory() as t:
        root = tree(Path(t), {"src/helpers/sensors/LPPDataHelpers.h": src})
        assert g.scan(root) == []


def test_main_exit_codes():
    with tempfile.TemporaryDirectory() as t:
        assert quiet_main(tree(Path(t), {"src/x.cpp": "int colourX;\n"})) == 1
    with tempfile.TemporaryDirectory() as t:
        assert quiet_main(tree(Path(t), {"src/x.cpp": "int colorX;\n"})) == 0


def test_scanning_nothing_fails_loudly():
    # A guard that scanned zero files would report every tree as clean.
    with tempfile.TemporaryDirectory() as t:
        assert quiet_main(Path(t)) == 1


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
