#pragma once

#include <stdint.h>

// #785: the Offband brand colours, in one place.
//
// WHY THIS HEADER EXISTS
//   #757 put these values in Rc32Palette.h because there was one colour panel.
//   There are now two (#785 ST7735) and a third is planned (#793 ST7789LCD).
//   Copying six constants into each driver guarantees they drift -- and the drift
//   is invisible until two boards are held side by side, which is exactly when it
//   is most embarrassing and least convenient to fix.
//
//   So: primitives here, ROLE ASSIGNMENTS per driver. Two different panels may
//   legitimately want a colour in different roles; they must not disagree about
//   what the colour IS.
//
// PROVENANCE -- every value is taken from the Offband brand assets
// (offband-site/static/img/logo-lockup-{light,dark}.svg), not chosen by eye:
//
//   #0F1412  ink    -- wordmark on the light lockup
//   #EFF3E8  paper  -- wordmark on the dark lockup
//   #1A7A44  deep   -- mark stroke on the light lockup
//   #7BEFA8  mint   -- mark dots on the dark lockup
//
// Editing anything here repaints EVERY colour board at once. test/test_palettes
// pins these to the documented hexes so that cannot happen by accident.

namespace offband_brand {

constexpr uint16_t INK   = 0x08A2;   // #0F1412
constexpr uint16_t PAPER = 0xEF9D;   // #EFF3E8
constexpr uint16_t DEEP  = 0x1BC8;   // #1A7A44
constexpr uint16_t MINT  = 0x7F75;   // #7BEFA8

// Derived, not from the lockups: a dimmed paper for secondary text, and the amber
// kept from upstream because it still reads on a dark ground and no brand colour
// carries "warning".
constexpr uint16_t MUTED = 0x8CB1;
constexpr uint16_t AMBER = 0xFD20;

}  // namespace offband_brand
