#pragma once

#include <stdint.h>

#include "OffbandBrand.h"

// #785: the ST7735 colour scheme -- 15 envs across heltec_t096, t1, tracker and
// tracker_v2. Role-for-role identical to the RC32 (Rc32Palette.h), deliberately:
// the goal is ONE Offband look across every colour panel, not a per-board palette.
//
// Replaces a light theme with a pure-blue banner: window white, title_bkg 0x001F,
// popup cyan, corp_blue 0x001A. Two roles were blue-dominant; neither is now.
//
// #790 agreed to UNIFY two roles that previously differed from the RC32:
//   warning_txt    0xFC00 -> AMBER 0xFD20
//   secondary_txt  0x9492 -> MUTED 0x8CB1
// Neither is load-bearing and both IMPROVE contrast here -- they were the two
// tightest pairs on this driver, and inverting widens them (see #789's baseline:
// warning 102.7 -> 157.4, secondary 108.3 -> 129.4).
//
// NO BAKED SPLASH ASSET on this driver. OFFBAND_COLOUR_SPLASH occurs zero times
// across its four boards (#789), so unlike the RC32 there is no asset background
// to keep in step -- hence no SPLASH_BAKED_BKG here, and test/test_palettes skips
// that invariant for this palette rather than asserting it vacuously.
//
// Tests: test/test_palettes. ST7735Display.cpp cannot be compiled natively, which
// is why these values live in a header at all.

namespace st7735_palette {

using namespace offband_brand;

constexpr uint16_t WINDOW_BKG    = INK;
constexpr uint16_t PRIMARY_TXT   = PAPER;
constexpr uint16_t SECONDARY_TXT = MUTED;   // was 0x9492
constexpr uint16_t TITLE_BKG     = DEEP;    // was ST77XX_BLUE 0x001F
constexpr uint16_t TITLE_TXT     = PAPER;
constexpr uint16_t POPUP_BKG     = MINT;    // was ST77XX_CYAN 0x07FF
constexpr uint16_t POPUP_TXT     = INK;
constexpr uint16_t WARNING_TXT   = AMBER;   // was ST77XX_ORANGE 0xFC00

// Value only -- the symbol is tree-wide, see the note in Rc32Palette.h.
constexpr uint16_t CORP_ACCENT = DEEP;      // was 0x001A

}  // namespace st7735_palette
