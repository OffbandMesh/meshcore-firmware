#!/usr/bin/env python3
"""Tests for the RGB565 emitter in gen-offband-logo.py (#749, #1124).

Pure, no hardware, no device. Run: python scripts/test_gen_offband_logo.py
  or python -m pytest scripts/test_gen_offband_logo.py

WHAT THESE PROTECT
  The generator turns a brand PNG into a C array the firmware blits verbatim. If
  the packing is wrong there is no runtime error -- the splash just renders in the
  wrong colors, on a board most people do not have, and the failure looks like a
  display driver bug. The channel arithmetic is worth pinning down here, on the
  host, where it is cheap to check.

  The compositing tests exist because the brand asset is RGBA and the blit is
  OPAQUE (no driver's RGB565 path blends alpha). Transparency has to be resolved
  at generation time against the splash background, so the background color is
  baked into the asset -- change the splash background and the asset must be
  regenerated. That coupling is deliberate and these tests document it.

  The fit tests (#1124) pin the sizing rule and tie every committed header back
  to it, so a margin or panel change that is not regenerated fails here.
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
    # shift these and quietly change every color in the asset.
    assert gen.rgb565(8, 4, 8) == 0x0821


def test_brand_colors_survive_the_round_trip():
    # Guards against a mono/threshold path being wired in by mistake: these must
    # be real colors, not collapsed to black or white.
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


def _target(out_name):
    matches = [t for t in gen.SPLASH_TARGETS if t.out.endswith(out_name)]
    assert len(matches) == 1, f"expected exactly one target writing {out_name}"
    return matches[0]


RC32 = _target("OffbandLogoRGB565.h")
ST7735 = _target("OffbandLogoST7735.h")


def _read(relpath):
    return (Path(_HERE).parent / relpath).read_text(encoding="utf-8", errors="replace")


def test_nv3001b_row_matches_the_driver():
    assert RC32.panel_w == _cpp_macro("src/helpers/ui/NV3001BDisplay.cpp", "NV3001B_SCREEN_WIDTH")
    assert RC32.panel_h == _cpp_macro("src/helpers/ui/NV3001BDisplay.cpp", "NV3001B_SCREEN_HEIGHT")
    assert RC32.logical_w == _cpp_macro("src/helpers/ui/NV3001BDisplay.h", "NV3001B_LOGICAL_WIDTH")
    assert RC32.logical_h == _cpp_macro("src/helpers/ui/NV3001BDisplay.h", "NV3001B_LOGICAL_HEIGHT")


def test_st7735_row_matches_the_driver():
    # The ST7735 driver has no geometry macros: its canvas is the sprite it
    # creates, and it scales logical coordinates by two float literals.
    src = _read("src/helpers/ui/ST7735Display.cpp")
    m = re.search(r"createSprite\(\s*(\d+)\s*,\s*(\d+)\s*\)", src)
    assert m, "createSprite(w, h) not found in ST7735Display.cpp"
    assert (ST7735.panel_w, ST7735.panel_h) == (int(m.group(1)), int(m.group(2)))
    for axis, panel, logical in (("X", ST7735.panel_w, ST7735.logical_w),
                                 ("Y", ST7735.panel_h, ST7735.logical_h)):
        s = re.search(rf"^\s*#define\s+SCALE_{axis}\s+([\d.]+)f?", src, re.MULTILINE)
        assert s, f"SCALE_{axis} not found in ST7735Display.cpp"
        assert panel / logical == float(s.group(1)), f"SCALE_{axis} disagrees with the table"


def test_st7789lcd_row_matches_the_driver():
    # No geometry macros here either: the panel size is implied by the scale
    # factors the driver applies to the 128x64 canvas.
    t = _target("OffbandLogoST7789LCD.h")
    src = _read("src/helpers/ui/ST7789LCDDisplay.cpp")
    for axis, panel, logical in (("X", t.panel_w, t.logical_w),
                                 ("Y", t.panel_h, t.logical_h)):
        m = re.search(rf"#define DISPLAY_SCALE_{axis} ([\d.]+)f?", src)
        assert m, f"DISPLAY_SCALE_{axis} not found in ST7789LCDDisplay.cpp"
        assert panel / logical == float(m.group(1)), f"DISPLAY_SCALE_{axis} disagrees"


def test_st7789_rows_match_the_constructors():
    # One driver, three panels, chosen by #if. The sizes live in the constructor
    # arguments, so that is what this reads.
    src = _read("src/helpers/ui/ST7789Display.h")
    ctors = re.findall(r"GEOMETRY_RAWMODE,\s*(\d+)\s*,\s*(\d+)", src)
    assert len(ctors) == 3, f"expected three ST7789Spi constructors, found {len(ctors)}"
    from_driver = {(int(w), int(h)) for w, h in ctors}
    from_table = {(t.panel_w, t.panel_h) for t in gen.SPLASH_TARGETS
                  if t.driver.startswith("ST7789 (")}
    assert from_table == from_driver, f"table {from_table} vs driver {from_driver}"


def test_scindicator_row_matches_the_sprite_not_the_panel():
    # The asset is sized for the sprite LGFXDisplay draws into -- the panel
    # divided by UI_ZOOM -- not for the 480x480 panel itself.
    t = _target("OffbandLogoSCIndicator.h")
    hdr = _read("variants/sensecap_indicator-espnow/SCIndicatorDisplay.h")
    m = re.search(r"LGFXDisplay\((\d+),\s*(\d+),", hdr)
    assert m, "LGFXDisplay(w, h, ...) not found in SCIndicatorDisplay.h"
    ini = _read("variants/sensecap_indicator-espnow/platformio.ini")
    z = re.search(r"-D UI_ZOOM=([\d.]+)", ini)
    assert z, "UI_ZOOM not found in the sensecap platformio.ini"
    assert t.panel_w == int(int(m.group(1)) / float(z.group(1)))
    assert t.panel_h == int(int(m.group(2)) / float(z.group(1)))


def test_every_color_driver_has_a_target():
    # The failure this catches: a new color driver ships with the mono lockup
    # because nobody added its row. Every driver that overrides colorSplashArt
    # must have an asset in the table.
    import subprocess
    # Shipped drivers only -- test/ carries fakes that override it on purpose.
    out = subprocess.run(["git", "grep", "-l", "colorSplashArt() const override",
                          "--", "src", "variants"],
                         cwd=str(Path(_HERE).parent), capture_output=True, text=True).stdout
    declared = {Path(p).stem for p in out.split() if p}
    covered = {c for t in gen.SPLASH_TARGETS for c in t.classes}

    # Exact class names both ways. Substring matching was the first version of this
    # test and it was vacuous: a new ST7735SpecialDisplay would have passed on the
    # strength of containing "ST7735" (found in review).
    missing = declared - covered
    assert not missing, f"{sorted(missing)} declare color art with no row in SPLASH_TARGETS"
    stale = covered - declared
    assert not stale, f"SPLASH_TARGETS names {sorted(stale)}, which no longer declares color art"


def test_the_rc32_asset_is_still_shared_with_rc52():
    # Why the RC32 row is handled with care: RC52Display consumes the same header,
    # so a change to that row lands on two boards. If RC52 ever gets its own asset,
    # this test is the reminder to give it its own row.
    src = _read("variants/heltec_rc52/RC52Display.cpp")
    assert re.search(r'^\s*#include\s+[<"]helpers/ui/OffbandLogoRGB565\.h[>"]', src, re.MULTILINE)


def test_text_band_is_derived_from_the_real_scale_not_hardcoded():
    # The exact drift Gemini described: change the logical height and the scale
    # factor changes, so the physical text top must move with it.
    for t in gen.SPLASH_TARGETS:
        scale_y = t.panel_h / t.logical_h
        assert gen.text_top_phys(t) == int(gen.SPLASH_TEXT_TOP_LOGICAL * scale_y), t.driver


def test_default_background_is_the_splash_ink():
    # The baked background must be INK, which both color palettes use as
    # WINDOW_BKG. Parse it from the brand header rather than trusting a comment.
    m = re.search(r"INK\s*=\s*0x([0-9A-Fa-f]{4})", _read("src/helpers/ui/OffbandBrand.h"))
    assert m, "INK not found in OffbandBrand.h"
    bg = tuple(int(gen.SPLASH_BG_HEX[i:i + 2], 16) for i in (0, 2, 4))
    assert gen.rgb565(*bg) == int(m.group(1), 16)


def test_splash_still_draws_its_first_version_line_where_we_think():
    # The other half of the mirror: SPLASH_TEXT_TOP_LOGICAL must equal COLOR_L1,
    # the color layout's first version line in OffbandSplash.cpp.
    #
    # This originally matched a bare literal at the call site. #758 replaced those
    # literals with named constants for the 13px layout, and this test caught the
    # resulting staleness -- the generator still believed the text began at logical
    # 35 while the color splash had moved it to 29, so the artwork was being centered
    # in a band 12 physical pixels taller than actually existed. That is precisely
    # the drift this guard was written for, and it fired two commits after it was
    # written rather than on hardware.
    ui = (Path(_HERE).parent / "src/helpers/ui/OffbandSplash.cpp").read_text(
        encoding="utf-8", errors="replace")
    # #822: the color/mono choice is no longer a #ifdef -- the component asks the
    # DRIVER whether it has color art, so a board cannot be missed by forgetting a
    # build flag. The layout constants are therefore plain named constants, and this
    # guard matches COLOR_L1 directly. The invariant it protects is unchanged: the
    # generator centers the artwork in the band ABOVE the first version line, so if
    # that line moves and the asset is not regenerated, the two overlap.
    m = re.search(r"COLOR_L1\s*=\s*(\d+)", ui)
    assert m, "COLOR_L1 was not found in OffbandSplash.cpp"
    assert int(m.group(1)) == gen.SPLASH_TEXT_TOP_LOGICAL, (
        f"color splash line 1 is {m.group(1)} but the generator assumes "
        f"{gen.SPLASH_TEXT_TOP_LOGICAL}; regenerate the asset")


# ------------------------------------------------------------- fit (#1124) ---
# Each asset is sized from the artwork's own aspect and the space its panel has,
# taking whichever axis binds first -- not from a fixed width proportion, which
# was inherited from one panel rather than designed.

# The dark lockup's ink bounding box, measured from logo-lockup-dark.png. The
# committed-header tests below tie this back to real generator output.
LOCKUP = (892, 187)


def _limits(t):
    side = round(t.panel_w * gen.SIDE_MARGIN_FRAC)
    band = gen.text_top_phys(t)
    gap = round(band * gen.BAND_MARGIN_FRAC)
    return t.panel_w - 2 * side, band - 2 * gap


def _hypothetical(w, h):
    return gen.SplashTarget("test", w, h, 128, 64, "x.h", "X", "x")


def test_a_wide_panel_is_width_bound():
    avail_w, avail_h = _limits(RC32)
    w, h = gen.fit_size(*LOCKUP, RC32)
    assert w == avail_w, "the RC32 is wide: width must be the axis that binds"
    assert h <= avail_h


def test_a_short_panel_is_height_bound():
    # The ST7735's text starts at physical y=36 of 80, so its band is shallow and
    # height runs out first. A width proportion would ignore that.
    avail_w, avail_h = _limits(ST7735)
    w, h = gen.fit_size(*LOCKUP, ST7735)
    assert h == avail_h, "the ST7735 band is shallow: height must be the axis that binds"
    assert w < avail_w


def test_a_square_panel_is_height_bound_and_clears_the_text():
    # The case the addendum on #866 used against the old rule: 91% of a 480 panel's
    # width at the lockup's aspect is a 437x92 strip. Fit keeps it inside the band.
    t = _hypothetical(480, 480)
    avail_w, avail_h = _limits(t)
    w, h = gen.fit_size(*LOCKUP, t)
    x, y = gen.splash_origin(t, w, h)
    assert w <= avail_w and h <= avail_h
    assert y + h <= gen.text_top_phys(t)


def test_fit_never_exceeds_the_space_and_keeps_the_aspect():
    for pw, ph in ((96, 48), (128, 64), (160, 80), (220, 128), (240, 135),
                   (240, 240), (320, 170), (320, 240), (480, 320), (480, 480)):
        t = _hypothetical(pw, ph)
        avail_w, avail_h = _limits(t)
        for art in (LOCKUP, (100, 100), (40, 400), (1000, 50)):
            w, h = gen.fit_size(*art, t)
            assert w <= avail_w and h <= avail_h, (pw, ph, art, w, h)
            # Each side is rounded independently from an exact w_t = a * h_t, so
            # w - a*h = (w - w_t) - a*(h - h_t), and neither term exceeds half a pixel.
            a = art[0] / art[1]
            assert abs(w - a * h) <= 0.5 + 0.5 * a + 1e-9, (pw, ph, art, w, h)


def _header_macros(relpath, prefix):
    return tuple(_cpp_macro(relpath, f"{prefix}_{k}") for k in ("W", "H", "X", "Y"))


def test_every_committed_header_is_what_the_rule_produces():
    # Guards the table against the committed output: change a margin or a panel
    # row without regenerating, and this fails. For the RC32 it is also the check
    # #1124 asked for -- the rule must reproduce the 200x42 asset already signed
    # off on hardware, not silently resize it.
    for t in gen.SPLASH_TARGETS:
        w, h = gen.fit_size(*LOCKUP, t)
        x, y = gen.splash_origin(t, w, h)
        assert _header_macros(t.out, t.prefix) == (w, h, x, y), (
            f"{t.out} is stale for the fit rule: regenerate it")


def test_the_rc32_asset_is_the_one_signed_off_on_hardware():
    assert _header_macros(RC32.out, RC32.prefix) == (200, 42, 10, 8)


# --------------------------------------------------------------- placement ---
# The asset is positioned at generation time rather than by the firmware, so the
# boot path does no float scaling and the placement ships with its pixels.

def test_centers_the_asset_horizontally_on_the_panel():
    x, y = gen.splash_origin(RC32, 200, 42)
    assert x == 10, "200 wide on a 220 panel leaves 10px each side"


def test_places_the_asset_in_the_band_above_the_version_text():
    for t in gen.SPLASH_TARGETS:
        w, h = gen.fit_size(*LOCKUP, t)
        x, y = gen.splash_origin(t, w, h)
        assert y >= 0
        assert y + h <= gen.text_top_phys(t), f"{t.driver}: art collides with the version text"


def test_clamps_rather_than_going_negative_for_an_oversized_asset():
    # An asset wider or taller than its band must still land on-panel; the blit
    # would clip it, but a negative origin would clip the WRONG edge.
    x, y = gen.splash_origin(RC32, 300, 90)
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
