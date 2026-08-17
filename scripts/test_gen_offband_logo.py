#!/usr/bin/env python3
"""Tests for the RGB565 emitter in gen-offband-logo.py (#749).

Pure, no hardware, no device. Run: python scripts/test_gen_offband_logo.py
  or python -m pytest scripts/test_gen_offband_logo.py

WHAT THESE PROTECT
  The generator turns a brand PNG into a C array the firmware blits verbatim. If
  the packing is wrong there is no runtime error -- the splash just renders in the
  wrong colours, on a board most people do not have, and the failure looks like a
  display driver bug. The channel arithmetic is worth pinning down here, on the
  host, where it is cheap to check.

  The compositing tests exist because the brand asset is RGBA and the blit is
  OPAQUE (rgb565::blitSwapped has no alpha path). Transparency has to be resolved
  at generation time against the splash background, so the background colour is
  baked into the asset -- change the splash background and the asset must be
  regenerated. That coupling is deliberate and these tests document it.
"""
import importlib.util
import re
from pathlib import Path
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))

# The generator's filename has a hyphen, so it is not directly importable.
_spec = importlib.util.spec_from_file_location(
    "gen_offband_logo", os.path.join(_HERE, "gen-offband-logo.py"))
gen = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gen)

WHITE = (255, 255, 255)

# Straight from the brand SVG (offband-site/static/img/logo-lockup-light.svg), not
# eyeballed: the mark stroke and the wordmark fill.
BRAND_GREEN = (0x1A, 0x7A, 0x44)
BRAND_INK = (0x0F, 0x14, 0x12)


# ---------------------------------------------------------------- packing ---

def test_packs_primaries_into_5_6_5():
    assert gen.rgb565(255, 255, 255) == 0xFFFF
    assert gen.rgb565(0, 0, 0) == 0x0000
    assert gen.rgb565(255, 0, 0) == 0xF800, "red must occupy the top 5 bits"
    assert gen.rgb565(0, 255, 0) == 0x07E0, "green must occupy the middle 6 bits"
    assert gen.rgb565(0, 0, 255) == 0x001F, "blue must occupy the bottom 5 bits"


def test_truncates_low_bits_rather_than_rounding():
    # One unit in each destination channel. Rounding instead of truncating would
    # shift these and quietly change every colour in the asset.
    assert gen.rgb565(8, 4, 8) == 0x0821


def test_brand_colours_survive_the_round_trip():
    # Guards against a mono/threshold path being wired in by mistake: these must
    # be real colours, not collapsed to black or white.
    assert gen.rgb565(*BRAND_GREEN) == 0x1BC8
    assert gen.rgb565(*BRAND_INK) == 0x08A2
    for packed in (gen.rgb565(*BRAND_GREEN), gen.rgb565(*BRAND_INK)):
        assert packed not in (0x0000, 0xFFFF)


# ------------------------------------------------------------ compositing ---

def test_transparent_pixel_becomes_the_background():
    assert gen.composite_over((0, 0, 0, 0), WHITE) == WHITE


def test_opaque_pixel_is_unchanged():
    assert gen.composite_over((*BRAND_GREEN, 255), WHITE) == BRAND_GREEN


def test_half_alpha_lands_midway():
    # Antialiased glyph edges are these pixels; getting them wrong is what makes a
    # rendered wordmark look either bolded or eaten away.
    assert gen.composite_over((0, 0, 0, 128), WHITE) == (127, 127, 127)


# ------------------------------------------------------------------ image ---

def test_image_to_rgb565_is_row_major_and_complete():
    if not gen.HAVE_PIL:
        print("  (skipped: Pillow not installed)")
        return
    from PIL import Image
    im = Image.new("RGBA", (3, 2), (0, 0, 0, 0))
    im.putpixel((0, 0), (255, 0, 0, 255))
    im.putpixel((2, 1), (0, 0, 255, 255))

    px, w, h = gen.image_to_rgb565(im, WHITE)

    assert (w, h) == (3, 2)
    assert len(px) == 6, "one entry per pixel, row-major"
    assert px[0] == 0xF800, "first entry is the top-left pixel"
    assert px[5] == 0x001F, "last entry is the bottom-right pixel"
    assert px[1] == 0xFFFF, "transparent pixels composite to the background"


# ------------------------------------------------------ C++ agreement guard ---
# From the #749 pre-merge review: the generator mirrors panel geometry that the C++
# driver OWNS, and a comment saying "keep these in sync" is not enforcement. If the
# driver's geometry changes and these constants do not, the generator silently emits
# an asset positioned against a stale layout -- it overlaps the version text, nothing
# throws, and CI stays green. So parse the real macros and assert agreement; drift
# then breaks the build instead of the splash.

def _cpp_macro(relpath, name):
    """Read a #define <name> <int> out of a firmware source file."""
    src = (Path(_HERE).parent / relpath).read_text(encoding="utf-8", errors="replace")
    m = re.search(rf"^\s*#define\s+{re.escape(name)}\s+(\d+)\s*$", src, re.MULTILINE)
    assert m, f"{name} not found in {relpath} -- the driver's geometry macros moved"
    return int(m.group(1))


def test_panel_geometry_matches_the_driver():
    assert gen.SPLASH_PANEL_W == _cpp_macro("src/helpers/ui/NV3001BDisplay.cpp", "NV3001B_SCREEN_WIDTH")
    assert gen.SPLASH_PANEL_H == _cpp_macro("src/helpers/ui/NV3001BDisplay.cpp", "NV3001B_SCREEN_HEIGHT")


def test_logical_canvas_matches_the_driver():
    assert gen.SPLASH_LOGICAL_W == _cpp_macro("src/helpers/ui/NV3001BDisplay.h", "NV3001B_LOGICAL_WIDTH")
    assert gen.SPLASH_LOGICAL_H == _cpp_macro("src/helpers/ui/NV3001BDisplay.h", "NV3001B_LOGICAL_HEIGHT")


def test_text_band_is_derived_from_the_real_scale_not_hardcoded():
    # The exact drift Gemini described: change the logical height and the scale
    # factor changes, so the physical text top must move with it.
    scale_y = gen.SPLASH_PANEL_H / gen.SPLASH_LOGICAL_H
    assert gen.SPLASH_TEXT_TOP_PHYS == int(gen.SPLASH_TEXT_TOP_LOGICAL * scale_y)


def test_splash_still_draws_its_first_version_line_where_we_think():
    # The other half of the mirror: SPLASH_TEXT_TOP_LOGICAL must equal SPLASH_LINE_1
    # in the COLOUR branch of UITask.cpp.
    #
    # This originally matched a bare literal at the call site. #758 replaced those
    # literals with named constants for the 13px layout, and this test caught the
    # resulting staleness -- the generator still believed the text began at logical
    # 35 while the colour splash had moved it to 29, so the artwork was being centred
    # in a band 12 physical pixels taller than actually existed. That is precisely
    # the drift this guard was written for, and it fired two commits after it was
    # written rather than on hardware.
    ui = (Path(_HERE).parent / "examples/companion_radio/ui-new/UITask.cpp").read_text(
        encoding="utf-8", errors="replace")
    # Match the colour branch that DEFINES the constants, not the first
    # OFFBAND_COLOUR_SPLASH block in the file -- that one is the header include, and
    # splitting on it is how this assertion first went looking in the wrong place.
    m = re.search(r"#ifdef\s+OFFBAND_COLOUR_SPLASH\s*\n\s*static const int SPLASH_LINE_1\s*=\s*(\d+)", ui)
    assert m, "the OFFBAND_COLOUR_SPLASH branch defining SPLASH_LINE_1 was not found"
    assert int(m.group(1)) == gen.SPLASH_TEXT_TOP_LOGICAL, (
        f"colour splash line 1 is {m.group(1)} but the generator assumes "
        f"{gen.SPLASH_TEXT_TOP_LOGICAL}; regenerate the asset")


# --------------------------------------------------------------- placement ---
# The asset is positioned at generation time rather than by the firmware, so the
# splash does not need to learn the panel's physical size. DisplayDriver exposes
# only the 128x64 LOGICAL canvas, and adding a physical-size accessor to the base
# interface for one board's splash is a worse trade than emitting two constants.

def test_centres_the_asset_horizontally_on_the_panel():
    x, y = gen.splash_origin(panel_w=220, panel_h=128, art_w=200, art_h=42)
    assert x == 10, "200 wide on a 220 panel leaves 10px each side"


def test_places_the_asset_in_the_band_above_the_version_text():
    # The splash draws its version lines from logical y=35, i.e. physical y=70.
    x, y = gen.splash_origin(panel_w=220, panel_h=128, art_w=200, art_h=42)
    assert y >= 0
    assert y + 42 <= 70, "the asset must not collide with the version text band"


def test_clamps_rather_than_going_negative_for_an_oversized_asset():
    # An asset wider or taller than its band must still land on-panel; the blit
    # would clip it, but a negative origin would clip the WRONG edge.
    x, y = gen.splash_origin(panel_w=220, panel_h=128, art_w=300, art_h=90)
    assert x == 0
    assert y == 0


def test_emitted_array_is_valid_c_and_declares_its_size():
    src = gen.carray16("offband_splash", [0xF800, 0x07E0, 0x001F, 0x0000], per_row=2)

    assert "offband_splash" in src
    assert src.count("0x") == 4, "one literal per pixel"
    assert "0xf800" in src.lower()
    assert src.rstrip().endswith("};")
    assert src.count("\n") >= 3, "wrapped, not one unreadable line"


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
