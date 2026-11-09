#pragma once

// Offband #2074: what a board may REPORT about its LED, as arithmetic on two booleans so the
// rule is host-tested rather than restated in every board class.
//
// The rule exists because the enable flag and the ability to honor it are separate facts, and
// a caller can set the first without the second. `MainBoard::setLedEnabled()` returns false
// and changes nothing on a board with no controllable LED -- but its return value is
// discarded at call sites (the same discarded-return shape as #2048's TonePlay), and a
// caller that persists its own copy of the setting will then believe the LED is off. If
// `isLedEnabled()` echoed that belief, a board would report a dark LED while it was visibly
// lit. So the report is gated on the capability, not on the request.
//
// The inverse matters as much: a board that CANNOT control its LED must report it ENABLED,
// because the LED is behaving normally. "Not controllable" is not "off".

#include <stdint.h>

namespace offband {

// What isLedEnabled() should answer. A board that cannot control its LED always reports the
// LED as enabled, whatever was asked of it.
inline bool ledEnabledReport(bool can_control_led, bool enable_flag) {
  return can_control_led ? enable_flag : true;
}

// Whether a transmit hook should light the LED. Same shape, and deliberately the same answer:
// a board that cannot control its LED still lights it on transmit, because that is what it
// did before anyone could ask otherwise.
inline bool ledLightsOnTransmit(bool can_control_led, bool enable_flag) {
  return can_control_led ? enable_flag : true;
}

}  // namespace offband
