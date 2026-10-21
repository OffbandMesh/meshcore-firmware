#!/usr/bin/env python3
"""Generate the Offband brand splash assets (#153, #749, #1124).

Two kinds of output:

  * OffbandLogo.h -- 1-bit bitmaps for the mono OLED splash. 1-bit display
    logos must be produced at native size, not shrunk from a large raster (an
    8:1 downscale smears sub-pixel detail). So:
      - wordmark -- cropped from the brand lockup PNG's alpha channel (the real
        JetBrains Mono wordmark), then resized to target width.
      - mark     -- rendered from the lockup SVG's vector geometry (two
        concentric arcs r=12/r=19 about center (28,38), center dot, corner
        blip), supersampled.
    Output is MSB-first byte arrays for Adafruit_GFX drawBitmap (SSD1306Display).
    The morse underline is intentionally omitted -- its dots are sub-pixel at
    OLED size and only ever read as a smear.

  * One RGB565 header per color panel in SPLASH_TARGETS -- the full-color
    lockup, sized to fit the space that panel actually has (see fit_size).
    Adding a panel is a row in that table, not a code change.

Usage:
    python scripts/gen-offband-logo.py [--src <light lockup.png>]
        [--rgb565-src <dark lockup.png>] [--out <mono header>] [--out-root <dir>]

The sources default to the offband-site brand assets (per-host path);
regenerate after any brand-mark change. Output headers are committed so a build
needs no Pillow.
"""
from __future__ import annotations
import argparse
from pathlib import Path
from typing import NamedTuple

# Pillow is needed only to RASTERIZE. The pure arithmetic below (packing,
# quantization, compositing, sizing, placement) has no image dependency, and the
# self-tests exercise it without Pillow present -- so import defensively rather
# than making the whole module unimportable on a host without it.
try:
    from PIL import Image, ImageDraw
    HAVE_PIL = True
except ImportError:  # pragma: no cover - exercised by CI hosts without Pillow
    HAVE_PIL = False

REPO_ROOT = Path(__file__).resolve().parent.parent

# Sizes tuned for the 128x64 OLED lockup (mark + 4px gap + wordmark = 119px).
MARK_TARGET_H = 30
WORD_TARGET_W = 86
WORD_THRESHOLD = 120
MARK_THRESHOLD = 110

# ------------------------------------------------------- color splash layout ---

# Logical y at which the color splash draws its first version line -- COLOR_L1 in
# src/helpers/ui/OffbandSplash.cpp. It is 29 rather than the mono arrangement's
# 35 because #758's 18px line box needs the room (see the layout comment there).
# Every color panel lays its text out on the same logical canvas, so this one
# value, scaled per panel, is where each panel's artwork band ends.
SPLASH_TEXT_TOP_LOGICAL = 29

# #1124: margins are FRACTIONS of the panel, so every panel gets the same framing
# whatever its size -- a fixed pixel count means something different on an 80px
# panel than on a 480px one.
#   SIDE_MARGIN_FRAC -- of the panel width, clear on each side.
#   BAND_MARGIN_FRAC -- of the band above the version text, clear above AND below
#                       the art. The art is centered in that band, so this is
#                       also its gap to the first version line.
# Both are the RC32's hardware-approved framing (10 of 220 px each side, 8 of its
# 58 px band) expressed as proportions, so the rule leaves the RC32 asset exactly
# as it was. Change one of these and every panel moves together.
SIDE_MARGIN_FRAC = 0.045
BAND_MARGIN_FRAC = 0.14

# What the RGB565 alpha is flattened against: INK (#0F1412), the dark splash
# background -- Rc32Palette.h SPLASH_BAKED_BKG. The blit is opaque, so the
# background is baked into every color asset.
SPLASH_BG_HEX = "0f1412"


# A NamedTuple rather than a dataclass: the self-test loads this file through
# importlib (the hyphenated name is not importable), and a dataclass then fails
# to resolve its own annotations.
class SplashTarget(NamedTuple):
    """One color panel. Geometry is POST-ROTATION physical pixels -- what the
    driver actually draws in -- and every value mirrors a number the C++ driver
    owns. scripts/test_gen_offband_logo.py parses those numbers out of the driver
    and asserts agreement, so drift fails CI instead of the splash."""
    driver: str      # which driver consumes it, for the header comment
    panel_w: int
    panel_h: int
    logical_w: int   # the logical canvas the UI (and the splash text) draws on
    logical_h: int
    out: str         # header path, relative to the repo root
    prefix: str      # macro prefix: <prefix>_W, _H, _X, _Y
    array: str
    # What this panel's alpha is flattened against. It MUST equal the driver's
    # window_bkg: the blit is opaque, so a mismatch renders the artwork on a
    # rectangle of the wrong color. Drivers do not share a background -- the RC32
    # and ST7735 run the Offband dark theme, the ST7789LCD is still light.
    bg: str = SPLASH_BG_HEX
    # Which brand lockup to draw from. The two are not interchangeable: the dark
    # lockup's ink is pale, for dark panels, and baking it onto a light panel
    # leaves the wordmark all but invisible. It must follow `bg`.
    light: bool = False
    # The driver CLASSES this row serves, spelled exactly. The test that checks
    # every color driver has an asset compares these names against the classes
    # that override colorSplashArt(); spelled out rather than matched on substrings,
    # which would let an ST7735SpecialDisplay pass on the strength of "ST7735".
    classes: tuple = ()


SPLASH_TARGETS = (
    # NV3001B_SCREEN_WIDTH/HEIGHT in NV3001BDisplay.cpp -- not NV3001B_PANEL_*,
    # which describe the panel's memory orientation (128x220). Also consumed by
    # RC52Display (variants/heltec_rc52/), so a change here lands on two boards.
    SplashTarget("NV3001B (RC32, RCC6) and RC52Display", 220, 128, 128, 64,
                 "src/helpers/ui/OffbandLogoRGB565.h",
                 "OFFBAND_SPLASH_RGB565", "offband_splash_rgb565",
                 classes=("NV3001BDisplay", "RC52Display")),
    # createSprite(160, 80) and SCALE_X/SCALE_Y in ST7735Display.cpp.
    SplashTarget("ST7735", 160, 80, 128, 64,
                 "src/helpers/ui/OffbandLogoST7735.h",
                 "OFFBAND_SPLASH_ST7735", "offband_splash_st7735",
                 classes=("ST7735Display",)),
    # DISPLAY_SCALE_X/Y in ST7789LCDDisplay.cpp: 320x240 after DISPLAY_ROTATION 3,
    # which is what the driver draws in -- not the 240x320 passed to init().
    # heltec_v4, heltec_v4_r8 and lilygo_tdeck. Light theme, so the art is baked
    # against white rather than INK (ST7789LCDDisplay.cpp window_bkg).
    SplashTarget("ST7789LCD", 320, 240, 128, 64,
                 "src/helpers/ui/OffbandLogoST7789LCD.h",
                 "OFFBAND_SPLASH_ST7789LCD", "offband_splash_st7789lcd",
                 bg="ffffff", light=True, classes=("ST7789LCDDisplay",)),
    # The three ST7789Spi panels, from the constructors in ST7789Display.h. That
    # driver keeps a 1-BIT buffer and renders each pixel as `bit ? _RGB : 0`, so
    # an off pixel is black and the art bakes against black -- and the art cannot
    # live in the buffer at all, which is why ST7789Spi composites it during the
    # blit instead (#868).
    SplashTarget("ST7789 (T114)", 240, 135, 128, 64,
                 "src/helpers/ui/OffbandLogoST7789T114.h",
                 "OFFBAND_SPLASH_ST7789_T114", "offband_splash_st7789_t114",
                 bg="000000", classes=("ST7789Display",)),
    SplashTarget("ST7789 (T190)", 320, 170, 128, 64,
                 "src/helpers/ui/OffbandLogoST7789T190.h",
                 "OFFBAND_SPLASH_ST7789_T190", "offband_splash_st7789_t190",
                 bg="000000"),
    SplashTarget("ST7789 (ThinkNode M9)", 320, 240, 128, 64,
                 "src/helpers/ui/OffbandLogoST7789M9.h",
                 "OFFBAND_SPLASH_ST7789_M9", "offband_splash_st7789_m9",
                 bg="000000"),
    # SenseCAP Indicator. LGFXDisplay draws into a sprite sized width()/height(),
    # which is the 480x480 panel divided by UI_ZOOM (3.5), and endFrame zooms that
    # sprite onto the panel. The art goes into the sprite with everything else, so
    # it is sized in sprite pixels and scales with the rest of the UI rather than
    # being the one element at native panel resolution. Light theme (LGFXDisplay.cpp
    # window_bkg = 0xFFFF).
    SplashTarget("SCIndicator", 137, 137, 137, 137,
                 "src/helpers/ui/OffbandLogoSCIndicator.h",
                 "OFFBAND_SPLASH_SCINDICATOR", "offband_splash_scindicator",
                 bg="ffffff", light=True, classes=("SCIndicatorDisplay",)),
)


def text_top_phys(t: SplashTarget) -> int:
    """The first version line in PHYSICAL pixels: the logical y scaled the way the
    driver scales it (and truncated the way its float-to-int cursor truncates)."""
    return SPLASH_TEXT_TOP_LOGICAL * t.panel_h // t.logical_h


def fit_size(art_w: int, art_h: int, t: SplashTarget):
    """Scale the artwork to the space available on this panel, preserving its
    aspect, and let whichever axis runs out first decide.

    A wide panel is width-bound; a short or square one is height-bound -- the
    case a fixed width proportion gets wrong, since it would push a wide strip
    down into the version text. Rounding cannot overshoot: the binding axis
    lands exactly on its limit and the other falls at or under its own.
    """
    side = round(t.panel_w * SIDE_MARGIN_FRAC)
    band = text_top_phys(t)
    gap = round(band * BAND_MARGIN_FRAC)
    avail_w, avail_h = t.panel_w - 2 * side, band - 2 * gap
    scale = min(avail_w / art_w, avail_h / art_h)
    return max(1, round(art_w * scale)), max(1, round(art_h * scale))


def splash_origin(t: SplashTarget, art_w: int, art_h: int):
    """Where the color lockup sits, in PHYSICAL panel pixels.

    Computed here rather than on-device, so the boot path does no float scaling
    (nRF52 builds carry this too) and the placement ships with the pixels it
    belongs to.

    Centered horizontally; vertically centered in the band ABOVE the version text.
    Clamped at 0 so an oversized asset is clipped on its far edges by the blit
    rather than having its near edges pushed off-panel by a negative origin.
    """
    x = max(0, (t.panel_w - art_w) // 2)
    y = max(0, (text_top_phys(t) - art_h) // 2)
    return x, y


# ------------------------------------------------------------------ 1-bit ---

def _bits_msb(mask):
    """Pack an L-mode mask to MSB-first bytes (Adafruit drawBitmap order)."""
    w, h = mask.size
    px = mask.load()
    row_bytes = (w + 7) // 8
    data = bytearray(row_bytes * h)
    thr = mask.info.get("thr", 128)
    for y in range(h):
        for x in range(w):
            if px[x, y] >= thr:
                data[y * row_bytes + (x >> 3)] |= (0x80 >> (x & 7))
    return data, row_bytes, w, h


def wordmark(png_path: str, target_w: int):
    im = Image.open(png_path).convert("RGBA")
    # Crop right of the mark and above the morse underline, then trim to ink.
    # NOTE: these bounds are coupled to the current logo-lockup-light.png layout
    # (1000x256). If the brand asset is resized/reflowed, re-validate them -- a bad
    # crop yields a garbled wordmark, which the on-device splash check would catch.
    w = im.crop((250, 0, 1000, 186))
    w = w.crop(w.split()[3].getbbox())
    alpha = w.split()[3]
    w0, h0 = w.size
    th = round(target_w * h0 / w0)
    m = alpha.resize((target_w, th), Image.LANCZOS)
    m.info["thr"] = WORD_THRESHOLD
    return _bits_msb(m)


def mark(target_h: int, supersample: int = 14):
    # Work in the SVG mark-group coordinate space (center dot at (28,38)).
    minx, miny, maxx, maxy = 8, 14, 50, 58
    bw, bh = maxx - minx, maxy - miny
    sc = (target_h * supersample) / bh
    cw, ch = round(bw * sc), round(bh * sc)
    img = Image.new("L", (cw, ch), 0)
    dr = ImageDraw.Draw(img)
    X = lambda x: (x - minx) * sc
    Y = lambda y: (y - miny) * sc
    stroke = max(2, round(2.4 * sc))

    def arc(r, start, end):
        dr.arc([X(28 - r), Y(38 - r), X(28 + r), Y(38 + r)], start, end,
               fill=255, width=stroke)

    # ~280deg concentric arcs (gap toward lower-right), per the brand mark.
    arc(19, 10, 290)
    arc(12, 10, 290)

    def dot(cx, cy, r):
        dr.ellipse([X(cx) - r * sc, Y(cy) - r * sc, X(cx) + r * sc, Y(cy) + r * sc],
                   fill=255)

    dot(28, 38, 3.6)      # center
    dot(46.1, 17.9, 3.0)  # corner blip
    small = img.resize((round(cw / supersample), round(ch / supersample)), Image.LANCZOS)
    small.info["thr"] = MARK_THRESHOLD
    return _bits_msb(small)


def carray(name: str, data: bytearray, row_bytes: int) -> str:
    out = [f"static const uint8_t {name}[] = {{"]
    for i in range(0, len(data), row_bytes):
        out.append("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + row_bytes]) + ",")
    out.append("};")
    return "\n".join(out)


# --------------------------------------------------------------- RGB565 (#749) ---
# The 1-bit path above serves the mono OLEDs. Color panels get a full-color asset
# drawn at native panel resolution, bypassing the 128x64 logical canvas -- whose
# upscale would blur the artwork and, where the two axes scale differently (the
# RC32's 1.72x by 2.0x), turn the mark's circular arcs into ellipses.

def rgb565(r: int, g: int, b: int) -> int:
    """Pack 8-8-8 into 5-6-5. Truncating, not rounding -- the panel's own encoding."""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def composite_over(src_rgba, bg_rgb):
    """Flatten one RGBA pixel onto an opaque background.

    The blit has no alpha path, so transparency must be resolved here. This BAKES
    THE BACKGROUND INTO THE ASSET: if the splash background changes, regenerate.
    """
    r, g, b, a = src_rgba
    return tuple(round((c * a + bg * (255 - a)) / 255) for c, bg in zip((r, g, b), bg_rgb))


def image_to_rgb565(im, bg_rgb):
    """Flatten and pack an RGBA image. Returns (values, w, h), row-major."""
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()
    return [rgb565(*composite_over(px[x, y], bg_rgb))
            for y in range(h) for x in range(w)], w, h


def carray16(name: str, values, per_row: int = 12) -> str:
    out = [f"static const uint16_t {name}[] = {{"]
    for i in range(0, len(values), per_row):
        out.append("  " + ", ".join(f"0x{v:04x}" for v in values[i:i + per_row]) + ",")
    out.append("};")
    return "\n".join(out)


def trimmed_lockup(png_path: str):
    """The brand lockup, cropped to its ink -- the artwork every size is fit from."""
    im = Image.open(png_path).convert("RGBA")
    bbox = im.split()[3].getbbox()
    return im.crop(bbox) if bbox else im


def rgb565_header(t: SplashTarget, src_name: str, bg_hex: str, px, w: int, h: int) -> str:
    x, y = splash_origin(t, w, h)
    return f"""// {Path(t.out).name} -- Offband color splash asset for {t.driver} (#749, #1124).
// GENERATED by scripts/gen-offband-logo.py from {src_name}. Do not
// hand-edit; regenerate.
//
// Full-color RGB565, drawn at NATIVE PANEL RESOLUTION via
// DisplayDriver::drawRGB565. It deliberately does NOT go through the
// {t.logical_w}x{t.logical_h} logical canvas, whose upscale would distort it.
//
// Sized by the generator's fit rule: the artwork's own aspect, scaled to the
// space above the version text on this {t.panel_w}x{t.panel_h} panel, inside
// SIDE_MARGIN_FRAC / BAND_MARGIN_FRAC.
//
// Values are host-order RGB565 (same encoding as ColorVal); the driver applies its
// own storage byte-swap. Alpha is already flattened onto the splash background
// #{bg_hex} -- CHANGE THAT BACKGROUND AND THIS MUST BE REGENERATED.
//
// Cost: {w}x{h} = {len(px) * 2:,} B of flash.
#pragma once
#ifdef OFFBAND_VERSION

#define {t.prefix}_W {w}
#define {t.prefix}_H {h}
// Physical-pixel origin, centered on the {t.panel_w}x{t.panel_h} panel and
// above the version text at physical y={text_top_phys(t)}.
#define {t.prefix}_X {x}
#define {t.prefix}_Y {y}
{carray16(t.array, px)}

#endif // OFFBAND_VERSION
"""


def main():
    ap = argparse.ArgumentParser()
    # Default: the offband-site repo checked out as a sibling of this one
    # (../offband-site). Override with --src if it lives elsewhere.
    site_img = REPO_ROOT.parent / "offband-site/static/img"
    ap.add_argument("--src", default=str(site_img / "logo-lockup-light.png"),
                    help="source for the 1-bit OLED header (the light lockup)")
    ap.add_argument("--out", default=str(REPO_ROOT / "src/helpers/ui/OffbandLogo.h"))
    # The color assets come from a DIFFERENT lockup than the 1-bit one: mono panels
    # want the light lockup, the dark color splash wants the dark one. Separate
    # flags, so a regeneration of the 1-bit header (which ~115 mono envs ship) can
    # never disturb the color assets or vice versa. The defaults are what ships.
    ap.add_argument("--rgb565-src", default=str(site_img / "logo-lockup-dark.png"),
                    help="source for the color assets (the dark lockup)")
    ap.add_argument("--rgb565-bg", default=None,
                    help="override every target's baked background (RRGGBB); "
                         "by default each target uses its own driver's window_bkg")
    ap.add_argument("--out-root", default=str(REPO_ROOT),
                    help="write the color headers under this root instead of the repo")
    ap.add_argument("--skip-rgb565", action="store_true")
    a = ap.parse_args()

    md, mrb, mw, mh = mark(MARK_TARGET_H)
    wd, wrb, ww, wh = wordmark(a.src, WORD_TARGET_W)

    header = f"""// offband_logo.h -- Offband brand splash bitmaps for the OLED splash (#153).
// GENERATED by scripts/gen-offband-logo.py from the brand lockup
//   ({Path(a.src).name}). Wordmark cropped from the PNG alpha mask; mark
//   rendered from the lockup's vector geometry (two arcs + two dots). The morse
//   underline is omitted (sub-pixel at this size). 1-bit, MSB-first
//   (Adafruit_GFX drawBitmap / SSD1306Display). Do not hand-edit; regenerate.
#pragma once
#ifdef OFFBAND_VERSION

#define OFFBAND_MARK_W {mw}
#define OFFBAND_MARK_H {mh}
{carray('offband_mark', md, mrb)}

#define OFFBAND_WORD_W {ww}
#define OFFBAND_WORD_H {wh}
{carray('offband_word', wd, wrb)}

#endif // OFFBAND_VERSION
"""
    Path(a.out).write_text(header, newline="\n")
    print(f"wrote {a.out}\n  mark {mw}x{mh} ({len(md)}B), word {ww}x{wh} ({len(wd)}B)")

    if a.skip_rgb565:
        return

    # One lockup per theme, loaded once. A light panel takes the light lockup: the
    # dark one's ink is pale by design and renders as a ghost on white.
    sources = {False: a.rgb565_src, True: a.src}
    lockups = {k: trimmed_lockup(v) for k, v in sources.items()}
    for t in SPLASH_TARGETS:
        # Each panel bakes against ITS OWN background, since the drivers do not
        # agree on one; --rgb565-bg overrides every target, for trying a theme out.
        bg_hex = a.rgb565_bg or t.bg
        bg = tuple(int(bg_hex[i:i + 2], 16) for i in (0, 2, 4))
        art = lockups[t.light]
        w, h = fit_size(*art.size, t)
        px, _, _ = image_to_rgb565(art.resize((w, h), Image.LANCZOS), bg)
        out = Path(a.out_root) / t.out
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(rgb565_header(t, Path(sources[t.light]).name, bg_hex, px, w, h),
                       newline="\n")
        print(f"wrote {out}\n  {t.driver}: {w}x{h} ({len(px) * 2:,}B flash, "
              f"bg #{bg_hex}, {Path(sources[t.light]).name})")


if __name__ == "__main__":
    main()
