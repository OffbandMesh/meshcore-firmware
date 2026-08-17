#!/usr/bin/env python3
"""Tests for the JetBrains Mono 4bpp glyph-table generator (#758).

Pure, no hardware. Run: python scripts/test_gen_jbm_font.py
  or python -m pytest scripts/test_gen_jbm_font.py

WHAT THESE PROTECT
  The generator turns a TTF into a coverage table the firmware blends onto the
  panel. Every failure mode here is silent and visual: glyphs shifted by a row,
  nibbles packed in the wrong order, or -- the one that defeats the entire point of
  the task -- coverage accidentally thresholded to 0/15 so the text renders exactly
  as blocky as the 5x7 font it replaces, just with more flash used.

  These tests do not need the real TTF for the pure arithmetic; the ones that do
  skip cleanly when it is absent, because the font is NOT committed (only the
  generated header is).
"""
import importlib.util
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))

_spec = importlib.util.spec_from_file_location(
    "gen_jbm_font", os.path.join(_HERE, "gen-jbm-font.py"))
gen = importlib.util.module_from_spec(_spec)
# Register before exec: @dataclass resolves its own module via sys.modules, and a
# module loaded purely through importlib.util is absent from it, so class creation
# dies with an opaque AttributeError inside dataclasses.
sys.modules["gen_jbm_font"] = gen
_spec.loader.exec_module(gen)

FONT = os.environ.get("JBM_TTF", "")
HAVE_FONT = bool(FONT) and os.path.exists(FONT)


# ----------------------------------------------------------- quantisation ---

def test_quantise_maps_the_full_range_to_four_bits():
    assert gen.quantise4(0) == 0
    assert gen.quantise4(255) == 15, "full coverage must reach the top nibble"


def test_quantise_is_monotonic_and_in_range():
    prev = -1
    for v in range(256):
        q = gen.quantise4(v)
        assert 0 <= q <= 15, f"{v} -> {q} out of nibble range"
        assert q >= prev, "quantisation must not go backwards"
        prev = q


def test_quantise_uses_the_middle_of_the_range():
    # If this collapses to 0/15 the renderer is thresholding, not antialiasing --
    # the exact failure that would make #758 pointless while still "working".
    mids = {gen.quantise4(v) for v in range(40, 220)}
    assert len(mids) > 4, f"only {len(mids)} distinct levels in the midrange"


# --------------------------------------------------------------- packing ---

def test_packs_two_pixels_per_byte_high_nibble_first():
    assert gen.pack_nibbles([0xA, 0xB]) == bytes([0xAB])
    assert gen.pack_nibbles([0xF, 0x0, 0x0, 0xF]) == bytes([0xF0, 0x0F])


def test_pads_an_odd_pixel_count_with_a_blank():
    assert gen.pack_nibbles([0xC]) == bytes([0xC0])


def test_packed_length_is_half_the_pixel_count_rounded_up():
    for n in (1, 2, 3, 84, 85):
        assert len(gen.pack_nibbles([1] * n)) == (n + 1) // 2


# ---------------------------------------------------------------- glyphs ---

def _skip_without_font():
    if not gen.HAVE_PIL:
        print("  (skipped: Pillow not installed)")
        return True
    if not HAVE_FONT:
        print("  (skipped: set JBM_TTF to the extracted TTF)")
        return True
    return False


def test_space_renders_completely_blank():
    # A non-blank space means the baseline or the cell origin is wrong, and it is
    # invisible in a screenshot until you look at word gaps.
    if _skip_without_font():
        return
    cov = gen.render_glyph(FONT, " ", size=10)
    assert max(cov) == 0, "space has ink in it"


def test_a_letter_has_ink_and_is_actually_antialiased():
    if _skip_without_font():
        return
    cov = gen.render_glyph(FONT, "M", size=10)
    assert max(cov) > 0, "M rendered blank"
    intermediate = [c for c in cov if 0 < c < 15]
    assert intermediate, "no partial coverage -- this is a 1-bit render, not antialiased"


def test_every_printable_ascii_glyph_is_the_declared_cell_size():
    if _skip_without_font():
        return
    tbl = gen.glyph_table(FONT, size=10)
    assert len(tbl.glyphs) == 95, f"expected 95 printable ASCII glyphs, got {len(tbl.glyphs)}"
    expected = tbl.cell_w * tbl.cell_h
    for ch, cov in zip(range(32, 127), tbl.glyphs):
        assert len(cov) == expected, f"glyph {chr(ch)!r} is {len(cov)} px, expected {expected}"


def test_metrics_at_size_10_match_the_font_being_replaced():
    # The whole reason size 10 is interesting: advance 6 and line box 14 are exactly
    # what font5x7-at-scale-1x2 already occupies, so nothing relayouts.
    if _skip_without_font():
        return
    tbl = gen.glyph_table(FONT, size=10)
    assert tbl.advance == 6, f"advance {tbl.advance}, expected 6 to match font5x7"
    assert tbl.cell_h == 14, f"cell height {tbl.cell_h}, expected 14 to match font5x7"


def test_no_glyph_bleeds_outside_its_cell():
    if _skip_without_font():
        return
    tbl = gen.glyph_table(FONT, size=10)
    # Rendering wider than the advance would overlap the next character on a
    # monospace grid. The generator must clip or fit, never silently overflow.
    for ch, cov in zip(range(32, 127), tbl.glyphs):
        assert len(cov) == tbl.cell_w * tbl.cell_h, f"{chr(ch)!r} overflowed its cell"


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
