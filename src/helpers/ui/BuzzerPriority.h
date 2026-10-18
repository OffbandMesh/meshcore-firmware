#pragma once

// #1417: which of two tone requests gets the buzzer.
//
// `genericBuzzer::play()` has always preempted unconditionally: if something is
// playing it is stopped and the new melody starts. That is correct while every
// caller is a real notification, which was true until a tune picker existed. Once a
// settings screen can audition a tune, two bad orderings become reachable -- and the
// second one is the one that matters:
//
//   a real notification arrives mid-audition   -> it should win, and it did
//   an audition starts while a notification plays -> it would silence the one chime
//                                                    that mattered
//
// So a request carries a priority, and this header holds the rule. It is pure -- no
// Arduino, no RTTTL -- so the decision is unit-tested on the host, where the buzzer
// itself cannot be.

#include <stdint.h>

namespace offband {

// What happened to a play request. A plain bool cannot tell the two refusals apart, and
// the picker screen needs to: "something more important is playing" and "the buzzer is
// off" want different words on screen, and asking `isQuiet()` separately would be a
// second read of state that can move in between.
enum class TonePlay : uint8_t {
  Played = 0,        // the melody took the buzzer
  RefusedBusy = 1,   // something of higher priority is playing
  RefusedQuiet = 2,  // the buzzer is quiet, so nothing was started
  // #1422: the id names no melody this build can resolve -- an id past the table after
  // a downgrade, or a custom slot this firmware cannot reach. Distinct from Played
  // because a remote trigger needs to know the badge made no noise, where a picker
  // showing Silent selected does not: `Silent` is a choice and reports Played.
  NotFound = 3,
};

// Ascending importance. The ORDER IS LOAD-BEARING -- `toneTakesBuzzer()` compares these
// values rather than branching per case, so inserting a level in the middle silently
// re-ranks everything below it. New levels go on the end, and the asserts below pin the
// order so a reordering is a build failure rather than a behavior change nobody notices.
enum class TonePriority : uint8_t {
  // Auditioning a tune in Settings. Yields to anything real, replaces another preview
  // so moving down a list re-auditions without waiting.
  Preview = 0,
  // A notification, an ack, startup/shutdown. Wins over an audition.
  Event = 1,
  // A tune played deliberately to a room, which is a different thing from a chime that
  // says one message arrived. It is LONG -- the owner's reference payload runs 17.1 s
  // against the 0.06-0.25 s of every notification here -- so under the old two-level
  // rule the next arriving message would cut it off, and in a busy channel that is
  // every time. A truncated event tune is worse than a missed chime: the chime repeats
  // on the next message, the event does not happen again.
  Trigger = 2,
};

static_assert((uint8_t)TonePriority::Preview < (uint8_t)TonePriority::Event,
              "an audition must never outrank a notification");
static_assert((uint8_t)TonePriority::Event < (uint8_t)TonePriority::Trigger,
              "a notification must never cut off a deliberate event tune");

// Whether `incoming` should take the buzzer from what is playing now.
//
//   nothing playing      -> yes, whatever it is
//   something playing    -> yes if `incoming` ranks at or above it
//
//   a Preview playing    -> anything, including another Preview
//   an Event playing     -> another Event or a Trigger, not a Preview
//   a Trigger playing    -> only another Trigger
//
// AT OR ABOVE, not strictly above, and that is deliberate on both ends: two messages
// arriving together should sound twice rather than swallow the second, and a second
// trigger should restart rather than be dropped. Equal rank replacing equal rank is the
// behavior this had before a third level existed, and it is kept.
//
// One comparison rather than a branch per pair. The earlier two-level version read as a
// special case for Event, which stopped being honest the moment a level ranked above it:
// the rule was always an ordering, and writing it as one means adding a level does not
// mean finding every place that enumerated the old ones.
//
// `playing` is meaningless when `is_playing` is false and is not read in that case,
// so a caller need not keep it accurate while the buzzer is idle.
inline bool toneTakesBuzzer(bool is_playing, TonePriority playing, TonePriority incoming) {
  if (!is_playing) return true;
  return (uint8_t)incoming >= (uint8_t)playing;
}

}  // namespace offband
