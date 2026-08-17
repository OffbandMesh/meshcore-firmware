#!/usr/bin/env python3
"""Generate offband_logo.h -- 1-bit Offband brand splash bitmaps (#153).

The OLED splash renders the Offband brand lockup (radar mark + "offband"
wordmark). 1-bit display logos must be produced at native size, not shrunk
from a large raster (an 8:1 downscale smears sub-pixel detail). So:

  * wordmark -- cropped from the brand lockup PNG's alpha channel (the real
    JetBrains Mono wordmark), then resized to target width.
  * mark     -- rendered from the lockup SVG's vector geometry (two concentric
    arcs r=12/r=19 about centre (28,38), centre dot, corner blip), supersampled.

Output is MSB-first byte arrays for Adafruit_GFX drawBitmap (SSD1306Display).
The morse underline in the brand lockup is intentionally omitted -- its dots are
sub-pixel at OLED size and only ever read as a smear.

Usage:
    python scripts/gen-offband-logo.py [--src <lockup.png>] [--out <header>]

--src defaults to the offband-site brand asset (per-host path); regenerate after
any brand-mark change. Output header is committed so a build needs no PIL.
"""
from __future__ import annotations
import argparse
from pathlib import Path

# Pillow is needed only to RASTERISE. The pure arithmetic below (packing,
# quantisation, compositing, placement) has no image dependency, and the
# self-tests exercise it without Pillow present -- so import defensively
# rather than making the whole module unimportable on a host without it.
try:
    from PIL import Image, ImageDraw
    HAVE_PIL = True
except ImportError:  # pragma: no cover - exercised by CI hosts without Pillow
    HAVE_PIL = False

# Sizes tuned for the 128x64 OLED lockup (mark + 4px gap + wordmark = 119px).
MARK_TARGET_H = 30
WORD_TARGET_W = 86

# #749: colour splash width in PHYSICAL panel pixels. 200 of the RC32's 220 leaves
# a 10px margin each side; the lockup's ~3.9:1 aspect then lands ~51px tall, which
# clears the version text the splash already draws from physical y=70 down.
SPLASH_TARGET_W = 200

# Physical panel geometry for the colour splash. Mirrors NV3001B_SCREEN_WIDTH /
# NV3001B_SCREEN_HEIGHT in src/helpers/ui/NV3001BDisplay.cpp -- note those are the
# POST-ROTATION dimensions the driver actually draws in, not NV3001B_PANEL_*, which
# describe the panel's memory orientation (128x220) and are not what you want here.
SPLASH_PANEL_W = 220
SPLASH_PANEL_H = 128

# The logical canvas the rest of the UI draws on, for deriving the scale factor.
SPLASH_LOGICAL_W = 128
SPLASH_LOGICAL_H = 64

# Logical y at which the COLOUR splash draws its first version line -- i.e.
# SPLASH_LINE_1 inside the OFFBAND_COLOUR_SPLASH branch of
# examples/companion_radio/ui-new/UITask.cpp. It is 29 rather than the mono
# arrangement's 35 because #758's 18px line box needs the room (see that branch).
# The artwork is centred in the band ABOVE this line.
SPLASH_TEXT_TOP_LOGICAL = 29

# The same point in PHYSICAL pixels: logical y scaled by DISPLAY_SCALE_Y
# (NV3001B_SCREEN_HEIGHT / NV3001B_LOGICAL_HEIGHT = 128/64 = 2.0). Artwork is
# centred above this.
#
# ⚠ These four constants MIRROR values owned by the C++ driver. A comment saying so
# is not enforcement -- if the driver's geometry changes and these do not, the
# generator silently emits an asset that overlaps the version text, and nothing
# fails. scripts/test_gen_offband_logo.py therefore PARSES the real macros out of
# NV3001BDisplay.{h,cpp} and asserts agreement, so drift breaks CI instead of the
# splash.
SPLASH_TEXT_TOP_PHYS = SPLASH_TEXT_TOP_LOGICAL * SPLASH_PANEL_H // SPLASH_LOGICAL_H
WORD_THRESHOLD = 120
MARK_THRESHOLD = 110


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
    # Work in the SVG mark-group coordinate space (centre dot at (28,38)).
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

    dot(28, 38, 3.6)      # centre
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
# The 1-bit path above serves the mono OLEDs. Colour panels (today: the RadioCore
# RC32's 220x128 NV3001B) get this instead -- a full-colour asset blitted at native
# panel resolution by rgb565::blitSwapped, bypassing the 128x64 logical canvas that
# would otherwise stretch it 1.72x horizontally and 2.0x vertically.

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


def splash_origin(panel_w: int, panel_h: int, art_w: int, art_h: int):
    """Where the colour lockup sits, in PHYSICAL panel pixels.

    Computed here rather than on-device: DisplayDriver exposes only the 128x64
    LOGICAL canvas, and widening that interface with a physical-size accessor for
    one board's splash buys less than emitting two constants does.

    Centred horizontally; vertically centred in the band ABOVE the version text,
    which the splash draws from logical y=35 (= physical y=70 on this panel).
    Clamped at 0 so an oversized asset is clipped on its far edges by the blit
    rather than having its near edges pushed off-panel by a negative origin.
    """
    x = max(0, (panel_w - art_w) // 2)
    y = max(0, (SPLASH_TEXT_TOP_PHYS - art_h) // 2)
    return x, y


def lockup_rgb565(png_path: str, target_w: int, bg_rgb):
    """Trim the brand lockup to its ink and scale it to target_w, preserving aspect."""
    im = Image.open(png_path).convert("RGBA")
    bbox = im.split()[3].getbbox()
    if bbox:
        im = im.crop(bbox)
    w0, h0 = im.size
    th = max(1, round(target_w * h0 / w0))
    im = im.resize((target_w, th), Image.LANCZOS)
    return image_to_rgb565(im, bg_rgb)


def main():
    ap = argparse.ArgumentParser()
    # Default: the offband-site repo checked out as a sibling of this one
    # (../offband-site). Override with --src if it lives elsewhere.
    default_src = (Path(__file__).resolve().parent.parent.parent /
                   "offband-site/static/img/logo-lockup-light.png")
    ap.add_argument("--src", default=str(default_src))
    ap.add_argument("--out", default=str(
        Path(__file__).resolve().parent.parent /
        "examples/companion_radio/ui-new/offband_logo.h"))
    # #749: colour output. Separate file and separate flag so a regeneration of the
    # 1-bit header can never disturb the colour asset or vice versa.
    ap.add_argument("--rgb565-out", default=str(
        Path(__file__).resolve().parent.parent /
        "examples/companion_radio/ui-new/offband_logo_rgb565.h"))
    # The colour asset may come from a DIFFERENT lockup than the 1-bit one, and
    # normally does: mono panels want the light lockup, a dark colour panel wants
    # logo-lockup-dark.png. Sharing --src between them silently rewrites the 1-bit
    # header (which ~115 mono envs ship) whenever the colour source changes.
    ap.add_argument("--rgb565-src", default=None,
                    help="source for the colour asset; defaults to --src")
    ap.add_argument("--rgb565-width", type=int, default=SPLASH_TARGET_W)
    ap.add_argument("--rgb565-bg", default="ffffff",
                    help="splash background as RRGGBB; baked in, since the blit is opaque")
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
    Path(a.out).write_text(header)
    print(f"wrote {a.out}\n  mark {mw}x{mh} ({len(md)}B), word {ww}x{wh} ({len(wd)}B)")

    if a.skip_rgb565:
        return

    bg = tuple(int(a.rgb565_bg[i:i + 2], 16) for i in (0, 2, 4))
    rgb_src = a.rgb565_src or a.src
    px, cw, ch = lockup_rgb565(rgb_src, a.rgb565_width, bg)
    sx, sy = splash_origin(SPLASH_PANEL_W, SPLASH_PANEL_H, cw, ch)
    colour = f"""// offband_logo_rgb565.h -- Offband colour splash asset (#749).
// GENERATED by scripts/gen-offband-logo.py from {Path(rgb_src).name}. Do not
// hand-edit; regenerate.
//
// Full-colour RGB565, drawn at NATIVE PANEL RESOLUTION via
// DisplayDriver::drawRGB565 -> rgb565::blitSwapped. It deliberately does NOT go
// through the 128x64 logical canvas, which scales 1.72x horizontally and 2.0x
// vertically and would render the mark's circular arcs as ellipses.
//
// Values are host-order RGB565 (same encoding as ColorVal); the driver applies its
// own storage byte-swap. Alpha is already flattened onto the splash background
// #{a.rgb565_bg} -- CHANGE THAT BACKGROUND AND THIS MUST BE REGENERATED.
//
// Cost: {cw}x{ch} = {len(px) * 2:,} B of flash.
#pragma once
#ifdef OFFBAND_VERSION

#define OFFBAND_SPLASH_RGB565_W {cw}
#define OFFBAND_SPLASH_RGB565_H {ch}
// Physical-pixel origin, centred on the {SPLASH_PANEL_W}x{SPLASH_PANEL_H} panel and
// above the version text at physical y={SPLASH_TEXT_TOP_PHYS}.
#define OFFBAND_SPLASH_RGB565_X {sx}
#define OFFBAND_SPLASH_RGB565_Y {sy}
{carray16('offband_splash_rgb565', px)}

#endif // OFFBAND_VERSION
"""
    Path(a.rgb565_out).write_text(colour)
    print(f"wrote {a.rgb565_out}\n  splash {cw}x{ch} ({len(px) * 2:,}B flash)")


if __name__ == "__main__":
    main()
