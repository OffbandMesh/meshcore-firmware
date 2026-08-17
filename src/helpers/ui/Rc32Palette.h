#pragma once

#include <stdint.h>

// #757: the RC32 panel's colour scheme.
//
// WHY THIS IS A HEADER AND NOT JUST LITERALS IN THE DRIVER
//   NV3001BDisplay.cpp cannot be compiled natively (SPI, board pins), so palette
//   values living there are unreachable by any test. The failure mode of a bad
//   palette is not a crash — it is a screen nobody can read, found by a human
//   squinting at one board. Pulling the constants out makes the invariants
//   checkable in CI. Same rationale as Rgb565Blit.h and helpers/BleFrameSizing.h.
//   Tests: test/test_rc32_palette.
//
// SOURCE OF THE COLOURS
//   Every value is taken from the Offband brand assets
//   (offband-site/static/img/logo-lockup-{light,dark}.svg), not chosen by eye:
//
//     #0F1412  ink     — wordmark on the light lockup
//     #EFF3E8  paper   — wordmark on the dark lockup
//     #1A7A44  deep    — mark stroke on the light lockup
//     #7BEFA8  mint    — mark dots on the dark lockup
//
//   Owner-approved 2026-08-16, including the instruction that no blue survives on
//   this panel: the title banner was 0x001F and corp_blue was 0x001A.

namespace rc32_palette {

// --- brand primitives (RGB565 of the hexes above) ---
constexpr uint16_t INK   = 0x08A2;   // #0F1412
constexpr uint16_t PAPER = 0xEF9D;   // #EFF3E8
constexpr uint16_t DEEP  = 0x1BC8;   // #1A7A44
constexpr uint16_t MINT  = 0x7F75;   // #7BEFA8
constexpr uint16_t MUTED = 0x8CB1;   // dimmed paper, for secondary text
constexpr uint16_t AMBER = 0xFD20;   // unchanged; still reads on a dark ground

// --- role assignments ---
constexpr uint16_t WINDOW_BKG    = INK;
constexpr uint16_t PRIMARY_TXT   = PAPER;
constexpr uint16_t SECONDARY_TXT = MUTED;
constexpr uint16_t TITLE_BKG     = DEEP;    // was 0x001F, pure blue
constexpr uint16_t TITLE_TXT     = PAPER;
constexpr uint16_t POPUP_BKG     = MINT;    // was 0x07FF, cyan
constexpr uint16_t POPUP_TXT     = INK;
constexpr uint16_t WARNING_TXT   = AMBER;

// corp_blue is declared in DisplayDriver.h, defined by all 12 drivers, and called
// from all three UI variants, so the SYMBOL cannot be removed without a tree-wide
// change. What changes here is its VALUE on this board, so no blue survives on this
// panel while every other board is untouched. (Owner-confirmed reading, 2026-08-16.)
constexpr uint16_t CORP_ACCENT = DEEP;

// What the #749 splash asset had its alpha flattened against. The blit is opaque,
// so the background is baked into the asset at generation time — if this and
// WINDOW_BKG ever diverge, the artwork renders on a mismatched rectangle. Change
// one, regenerate the other (scripts/gen-offband-logo.py --rgb565-bg).
constexpr uint16_t SPLASH_BAKED_BKG = INK;

}  // namespace rc32_palette
