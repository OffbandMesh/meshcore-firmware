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

enum class TonePriority : uint8_t {
  // Auditioning a tune in Settings. Yields to anything real, replaces another preview
  // so moving down a list re-auditions without waiting.
  Preview = 0,
  // A notification, an ack, startup/shutdown, or the remote trigger. Wins.
  Event = 1,
};

// Whether `incoming` should take the buzzer from what is playing now.
//
//   nothing playing     -> yes, whatever it is
//   an Event playing    -> only another Event (today's behavior, kept)
//   a Preview playing   -> anything, including another Preview
//
// `playing` is meaningless when `is_playing` is false and is not read in that case,
// so a caller need not keep it accurate while the buzzer is idle.
inline bool toneTakesBuzzer(bool is_playing, TonePriority playing, TonePriority incoming) {
  if (!is_playing) return true;
  if (playing == TonePriority::Event) return incoming == TonePriority::Event;
  return true;
}

}  // namespace offband
