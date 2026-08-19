#pragma once

#include <stdint.h>

#include "OffbandBrand.h"

// #757: the RC32 panel's colour scheme. #785 moved the brand primitives out to
// OffbandBrand.h so a second driver could share them; the ROLE ASSIGNMENTS below
// are unchanged and every value is identical to what #757 shipped.
//
// WHY THIS IS A HEADER AND NOT JUST LITERALS IN THE DRIVER
//   NV3001BDisplay.cpp cannot be compiled natively (SPI, board pins), so palette
//   values living there are unreachable by any test. The failure mode of a bad
//   palette is not a crash -- it is a screen nobody can read, found by a human
//   squinting at one board. Pulling the constants out makes the invariants
//   checkable in CI. Tests: test/test_palettes.
//
// Owner-approved 2026-08-16, including the instruction that no blue survives on
// this panel: the title banner was 0x001F and corp_blue was 0x001A.

namespace rc32_palette {

using namespace offband_brand;

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
// so the background is baked into the asset at generation time -- if this and
// WINDOW_BKG ever diverge, the artwork renders on a mismatched rectangle. Change
// one, regenerate the other (scripts/gen-offband-logo.py --rgb565-bg).
constexpr uint16_t SPLASH_BAKED_BKG = INK;

}  // namespace rc32_palette
