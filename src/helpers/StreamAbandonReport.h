#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// #1316: the rate limit and the wording for "a stream gave up and data was lost".
//
// WHY THIS IS A FREE FUNCTION IN A HEADER, not just a method body in MyMesh:
// MyMesh cannot be instantiated in the native test env, so logic living only inside it is
// unreachable by a test and in practice goes unverified. MeshLog made the same call for the
// same reason -- meshLogRoute() exists as a pure function so "the whole decision, not just
// the ceiling, is unit-testable natively" (MeshLog.h). The decision here deserves it: it is
// the difference between one useful line in a tester's crashlog ring and either silence or a
// flood that evicts the context around it.
//
// Arduino-free (stdint/stddef/stdio/string only), so it compiles natively unchanged.

namespace offband {

// The sink this feeds is a ~4 KB retained ring holding roughly 50 short lines, and on a
// stock build it is the ONLY copy of the evidence. Two different failure modes to defend
// against, which is why there are two limits rather than one:
//
//   PER-STREAM WINDOW -- a single wedged stream aborting over and over must not repeat
//   itself. But the limit must NOT be global across streams: a host that hangs its read
//   endpoint stalls every open stream at once, and a global limiter names whichever aborted
//   first and silently swallows the rest. "contacts died" and "contacts, blocklist, caplog
//   and obconfig all died in the same second" point at completely different causes, and the
//   second one is the diagnosis. Adversarial review constructed exactly that case.
//
//   PER-BOOT CAP -- the window alone is not enough. A persistently failing host aborts a
//   stream every 15 s forever, and at one line per window the reporter alone fills all ~50
//   lines in a few minutes, overwriting the boot lines and reset reason that give an
//   abandonment its meaning. A reporter that destroys the context of the crash it is
//   reporting is worse than silence. After the cap it keeps COUNTING but stops writing, so
//   the ring keeps the first occurrences plus everything else about the boot.
#define ABANDON_WINDOW_MS_DEFAULT   30000UL
#define ABANDON_MAX_LINES_DEFAULT   12

struct AbandonLimiter {
  // One slot per distinct stream name. Four exist today (contacts, blocklist, caplog,
  // obconfig); a fifth would land in the overflow counter rather than silently stealing
  // another stream's window.
  enum : uint8_t { kSlots = 4 };
  struct Slot {
    const char* key = nullptr;   // stream name; compared by value, not pointer (see below)
    uint32_t    last_ms = 0;
    bool        ever = false;    // distinguishes "never reported" from "reported at t=0"
    uint16_t    suppressed = 0;  // swallowed for THIS stream since its last emitted line
  };
  Slot     slots[kSlots];
  uint16_t unslotted = 0;   // reports from a 5th+ stream name, with nowhere to track them
  uint16_t emitted = 0;     // lines actually written this boot, against the cap
};

struct AbandonDecision {
  bool     emit;
  uint16_t suppressed;   // for THIS stream, already cleared from the limiter
};

// Decide whether to write this report, and hand back the count swallowed since the last one.
//
// The suppressed count is CLEARED HERE, not by the caller. An earlier revision left the
// reset to the caller and review was right to call that a trap: a caller that forgets it
// makes every later line carry a cumulative count, which reads as a storm that never
// happened. State the limiter owns, the limiter clears.
inline AbandonDecision abandonTake(AbandonLimiter& st, const char* stream, uint32_t now_ms,
                                   uint32_t window_ms = ABANDON_WINDOW_MS_DEFAULT,
                                   uint16_t max_lines = ABANDON_MAX_LINES_DEFAULT) {
  AbandonDecision out = {false, 0};
  if (stream == nullptr) return out;

  // Keyed by strcmp, not pointer equality. The same stream name is passed from more than
  // one call site ("contacts" from both the sync and the terminator path), and whether two
  // identical literals share an address is a toolchain decision, not a guarantee.
  AbandonLimiter::Slot* slot = nullptr;
  for (uint8_t i = 0; i < AbandonLimiter::kSlots; ++i) {
    if (st.slots[i].key != nullptr && strcmp(st.slots[i].key, stream) == 0) {
      slot = &st.slots[i];
      break;
    }
  }
  if (slot == nullptr) {
    for (uint8_t i = 0; i < AbandonLimiter::kSlots; ++i) {
      if (st.slots[i].key == nullptr) { slot = &st.slots[i]; slot->key = stream; break; }
    }
  }
  if (slot == nullptr) {
    // More distinct streams than slots. Count it somewhere rather than losing it silently;
    // this is a "the table needs another slot" signal, not an expected path.
    if (st.unslotted < 0xFFFF) st.unslotted++;
    return out;
  }

  // Unsigned subtraction, so this is correct across a millis() rollover: the difference is
  // the true elapsed gap even when now_ms has wrapped past last_ms. These nodes run for
  // months and millis() wraps about every 49.7 days.
  const bool inside_window = slot->ever && (uint32_t)(now_ms - slot->last_ms) < window_ms;
  const bool capped = st.emitted >= max_lines;

  if (inside_window || capped) {
    // Saturate rather than wrap: 65535 and "more than 65535" support the same conclusion,
    // while a wrapped count reading 3 after a storm would actively mislead.
    if (slot->suppressed < 0xFFFF) slot->suppressed++;
    return out;
  }

  slot->last_ms = now_ms;
  slot->ever = true;
  out.emit = true;
  out.suppressed = slot->suppressed;
  slot->suppressed = 0;
  if (st.emitted < 0xFFFF) st.emitted++;
  return out;
}

// Render one report line into `out`. snprintf semantics: returns the length the full line
// WOULD have taken, so a caller can detect truncation.
//
// `unit` names what `progress` counts, and it is not optional padding. The four streams
// measure completely different things -- contacts count items, the caplog download counts
// bytes, the observer config stream counts a buffer offset, the block list counts a slot
// index -- and review was right that a bare `progress=1024` sends whoever reads the ring
// back to the source to find out which. The unit travels with the number.
//
// `total` of 0 means the stream has no meaningful denominator. It then prints `progress=`
// alone rather than a `/0` that reads like a bug or, worse, an invented total. Streams that
// do know their count print `delivered=n/total`, the form that answers whether a client died
// one item in or three hundred.
inline int abandonFormat(char* out, size_t cap, const char* stream, const char* phase,
                         uint32_t stalled_ms, uint32_t progress, const char* unit,
                         uint32_t total, uint16_t suppressed) {
  if (total > 0) {
    return snprintf(out, cap,
                    "[stream] ABANDONED %s/%s stalled=%lums delivered=%lu/%lu%s "
                    "suppressed=%u (#1316)",
                    stream, phase, (unsigned long)stalled_ms, (unsigned long)progress,
                    (unsigned long)total, unit ? unit : "", (unsigned)suppressed);
  }
  return snprintf(out, cap,
                  "[stream] ABANDONED %s/%s stalled=%lums progress=%lu%s "
                  "suppressed=%u (#1316)",
                  stream, phase, (unsigned long)stalled_ms, (unsigned long)progress,
                  unit ? unit : "", (unsigned)suppressed);
}

}  // namespace offband
