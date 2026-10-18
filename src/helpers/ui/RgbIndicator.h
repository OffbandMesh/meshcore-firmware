#pragma once

// #1558 (R1): what an RGB indicator on the badge would SAY, decided here, separately from
// how it would be driven.
//
// WHAT THIS IS, AND WHAT IT DELIBERATELY IS NOT.
//
// There is no RGB LED on any board yet, and no env sets OFFBAND_RGB_LED_PIN. The task is
// the seam, not a feature: when hardware arrives, the policy is already decided, reviewed
// and tested, and whoever fits the part writes only the few lines that push a colour at
// it.
//
// It stops short of a driver ON PURPOSE. A single-wire addressable part (WS2812 and kin)
// and a three-pin PWM LED share nothing in software -- one is a bit-banged protocol with
// timing constraints, the other is three analogWrite calls -- and nobody has said which
// the badge will carry. Writing one of them now would be a guess that compiles, which is
// the worst kind: it would look finished and be wrong half the time. The LED type is
// recorded as the open question on #1558 instead.
//
// WHY THE POLICY IS WORTH HAVING ON ITS OWN. It is the part that does not depend on the
// part number, it is the part a person actually experiences, and it is the part that gets
// argued about. The badge already has two single-colour indicators -- the module LED's
// heartbeat (#1366) and the activity LED's traffic and message-waiting (#1367) -- and the
// only thing an RGB adds over those is telling states APART by colour rather than by
// blink pattern. So the whole design is: which states, which colours, and who wins when
// two are true at once.

#include <stdint.h>

// The flag is RESERVED, and setting it fails loudly rather than quietly doing nothing.
//
// Without this, a future person wires an LED, sets OFFBAND_RGB_LED_PIN because the name is
// obvious, gets a clean build and a dark LED, and goes looking for the bug in their
// soldering. PadBeacon.h does the same thing for the same reason. When the driver lands it
// replaces this block.
#if defined(OFFBAND_RGB_LED_PIN)
#error "OFFBAND_RGB_LED_PIN is reserved but has no driver yet (#1558). The colour policy in \
helpers/ui/RgbIndicator.h is decided and tested; what is missing is the part that pushes a \
colour at the hardware, because nobody has said whether the badge carries a single-wire \
addressable LED or a three-pin PWM one -- they share no code. Say which, write that, and \
delete this #error."
#endif

namespace offband {

struct RgbColor {
  uint8_t r, g, b;
  bool operator==(const RgbColor& o) const { return r == o.r && g == o.g && b == o.b; }
};

// What the badge can be saying. Ordered by PRIORITY, lowest first: when several are true
// the highest-numbered wins. The order is the design, so it is stated once, here.
enum class RgbState : uint8_t {
  Dark,        // nothing to say
  Traffic,     // a packet decoded recently -- the mesh is reaching us
  Unread,      // a message is waiting
  LowBattery,  // the one state that outranks a message, because it ends the others
};

// The brightness ceiling, as a fraction of full scale, applied to every colour.
//
// Not a style choice. A badge RGB at full white draws roughly 60 mA against the board's
// measured 13.7 mA idle (#1553), so an indicator left bright would be the single largest
// consumer on a conference badge -- more than the GPS this Feature's other half exists to
// duty-cycle. It is also worn at chest height in a dark room, where full brightness is
// unpleasant rather than informative. A quarter is legible across a room and costs ~15 mA
// at its brightest, and only while something is being said.
constexpr uint8_t kRgbMaxLevel = 64;   // of 255

// Scale a channel to the ceiling. Integer maths, rounding down, so nothing can exceed it.
inline uint8_t rgbLevel(uint8_t full) {
  return (uint8_t)(((uint16_t)full * kRgbMaxLevel) / 255u);
}

// Which state the badge is in, given what it knows. Priority lives here rather than at
// the call site so a caller cannot get the order wrong.
//
// Unread beats Traffic: a packet passing through is ambient and constant in a busy mesh,
// and letting it mask a waiting message would make the indicator useless exactly when it
// matters. LowBattery beats everything, because the others stop mattering when the badge
// dies.
inline RgbState rgbStateFor(bool low_battery, bool unread, bool traffic) {
  if (low_battery) return RgbState::LowBattery;
  if (unread) return RgbState::Unread;
  if (traffic) return RgbState::Traffic;
  return RgbState::Dark;
}

// The colour for a state, already scaled to the ceiling.
//
// Red is spent on low battery and nothing else, so red always means the same thing.
// Traffic is a dim blue-green: present, unobtrusive, clearly not a message. Unread is
// amber, which reads as "attention" without competing with red.
inline RgbColor rgbColorFor(RgbState s) {
  switch (s) {
    case RgbState::Dark:       return RgbColor{0, 0, 0};
    case RgbState::Traffic:    return RgbColor{0, rgbLevel(120), rgbLevel(160)};
    case RgbState::Unread:     return RgbColor{rgbLevel(255), rgbLevel(140), 0};
    case RgbState::LowBattery: return RgbColor{rgbLevel(255), 0, 0};
  }
  return RgbColor{0, 0, 0};
}

inline const char* rgbStateName(RgbState s) {
  switch (s) {
    case RgbState::Dark:       return "dark";
    case RgbState::Traffic:    return "traffic";
    case RgbState::Unread:     return "unread";
    case RgbState::LowBattery: return "low battery";
  }
  return "?";
}

}  // namespace offband
