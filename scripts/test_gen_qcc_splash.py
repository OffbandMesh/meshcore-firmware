"""Tests for scripts/gen-qcc-splash.py (#1172).
Run: python -m pytest scripts/test_gen_qcc_splash.py -q
"""
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "variants" / "qcc_badge" / "art" / "qcc_eye_50x50.xbm"
HDR = ROOT / "variants" / "qcc_badge" / "QccSplashArt.h"

_spec = importlib.util.spec_from_file_location("gen_qcc_splash", ROOT / "scripts" / "gen-qcc-splash.py")
gen = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gen)


def pixels_lsb(w, h, data):
    rb = (w + 7) // 8
    return {(c, r) for r in range(h) for c in range(w) if data[r * rb + c // 8] & (1 << (c % 8))}


def pixels_msb(w, h, data):
    rb = (w + 7) // 8
    return {(c, r) for r in range(h) for c in range(w) if data[r * rb + c // 8] & (0x80 >> (c % 8))}


def test_reverse_bits():
    assert gen.reverse_bits(0x01) == 0x80
    assert gen.reverse_bits(0xE0) == 0x07
    assert gen.reverse_bits(0x0F) == 0xF0
    assert gen.reverse_bits(0x00) == 0x00


def test_source_is_the_approved_art():
    w, h, data = gen.parse_xbm(SRC.read_text())
    assert (w, h, len(data)) == (50, 50, 350)
    assert len(pixels_lsb(w, h, data)) == 649   # ink count of the art approved 2026-09-12


def test_conversion_keeps_every_pixel_in_place():
    w, h, data = gen.parse_xbm(SRC.read_text())
    assert pixels_msb(w, h, gen.to_msb_first(data)) == pixels_lsb(w, h, data)


def test_a_short_source_is_rejected():
    with pytest.raises(ValueError):
        gen.parse_xbm("#define a_width 50\n#define a_height 50\nstatic unsigned char a_bits[] = { 0x00 };")


def test_a_source_without_dimensions_is_rejected():
    with pytest.raises(ValueError):
        gen.parse_xbm("static char a_bits[] = { 0x00 };")


def test_dimensions_come_from_the_bits_arrays_own_name():
    # Braces in a comment, a look-alike define and hotspot defines must not mislead it.
    text = (
        "/* art {v2} */\n"
        "#define logo_hotspot_width 25\n"
        "#define logo_width 9\n"
        "#define logo_height 2\n"
        "#define logo_x_hot 1\n"
        "static const unsigned char logo_bits[] PROGMEM = {\n"
        "  0x01, 0x00, 0xFF, 0x01 };\n"
    )
    assert gen.parse_xbm(text) == (9, 2, [0x01, 0x00, 0xFF, 0x01])


def test_committed_header_matches_the_generator():
    w, h, data = gen.parse_xbm(SRC.read_text())
    assert HDR.read_text() == gen.render_header(w, h, gen.to_msb_first(data))
