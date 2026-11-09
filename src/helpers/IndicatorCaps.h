#pragma once

// Offband #2068: what a radio may claim about its own indicators, as arithmetic on two
// booleans so the truth table is host-tested rather than inferred from preprocessor.
//
// THIS EXISTS BECAUSE THE OLD GATE COULD NOT BE TESTED. It was `#ifdef DISPLAY_CLASS`,
// and that macro is defined on 15 variants whose DISPLAY_CLASS is NullDisplayDriver -- a
// stub whose begin() returns false and whose header marks it "not present". A compile-time
// gate cannot tell a driver from a placeholder, no host test can reach a preprocessor
// branch, and the result shipped: a Heltec T096, a SenseCAP T1000-E and a QCC badge all
// advertised LED control and refused the write (#2065). The same gate let a display SET
// through on boards with no screen, where it was persisted to flash and echoed back as the
// stored value -- indistinguishable from success (#1992's shape, a different path).
//
// The two inputs are deliberately NOT symmetrical in origin, and that is the whole point:
//
//   has_display       main.cpp's own answer. `the_mesh.begin(disp != NULL)`, and `disp` is
//                     assigned only inside `if (display_begin_ok)`. So it already means
//                     "a display came up", not "a display class was compiled in".
//   can_control_led   the board's answer, `MainBoard::canControlLed()`. False on
//                     MeshCore's base class, so a board that has a TX LED and never
//                     overrides it reports no LED control -- which is true of 61 of the 63
//                     variants that define P_LORA_TX_LED. #2073 is what changes that, and
//                     this header needs no edit when it does.

#include <stdint.h>

namespace offband {

// TAG_DISPLAY_CONTROL (0x04) -- the display mode can be set over 0xC5 0x05/0x06.
inline bool advertisesDisplayControl(bool has_display) { return has_display; }

// TAG_LED_CONTROL (0x05) -- the LED can be switched over 0xC5 0x07/0x08.
inline bool advertisesLedControl(bool can_control_led) { return can_control_led; }

// OFFBAND_CAP2_INDICATORS (caps byte 2 bit 2) -- the COARSE answer, kept for clients that
// predate the tags. OR, not AND: a board with one of the two still has an Indicators
// screen worth opening, and a client that only reads this bit must still be able to reach
// the control that does work. Where the tags are present they are authoritative and this
// bit is ignored; see the precedence table in OffbandConfigProtocol.h.
inline bool indicatorsCapabilityBit(bool has_display, bool can_control_led) {
  return advertisesDisplayControl(has_display) || advertisesLedControl(can_control_led);
}

// A display or rotation sub-code arriving at a radio with no display is refused with
// OFFBAND_UI_ERR_UNSUPPORTED_INDICATOR. Rotation has a SECOND, narrower refusal of its own
// (NO_ROTATION, reason 10) for a display that exists and cannot turn; this one is only the
// "there is no screen at all" case, so the two reasons stay distinguishable to a client.
inline bool refusesDisplaySubCode(bool has_display) { return !has_display; }

// A LED sub-code arriving at a radio that cannot drive its LED is refused the same way.
inline bool refusesLedSubCode(bool can_control_led) { return !can_control_led; }

}  // namespace offband
