#pragma once

// #2324: when the side-port log mirror sends, probes, drops and resumes.
//
// Pure, so it is host-tested (test_log_mirror_gate). LogMirrorUart.h's back
// ends do the hardware wait and report each byte's outcome here.
//
// The rule, in full:
//   * UP (fewer than `giveup` timeouts in a row): every byte is a full, bounded
//     send. A timed-out byte is lost and counted.
//   * DOWN (`giveup` timeouts in a row): the wire is treated as stalled. A byte
//     is a PROBE (one cheap readiness check, no long wait) once every
//     `probe_every` bytes, and the rest are skipped and counted. A probe that
//     finds the wire ready sends its byte and brings the wire back UP.
//   * Coming back after any lost byte leaves a resume notice pending, so the
//     loss is reported on the wire itself rather than vanishing.
//
// What this replaces: the #887 bound returned early once the wire was down, so
// no byte was ever attempted again and the counter that should have reset on
// the next good byte never could. A transient stall cost the channel for the
// rest of the boot, which #887's own comment said it must not do.
//
// Zero is the correct initial state, so a gate can live in static storage and
// be used from a constructor(101) that runs before any C++ initializer.

#include <stddef.h>
#include <stdint.h>

namespace offband {

enum class MirrorStep : uint8_t {
  Send,    // up: the full, bounded wait, then the byte
  Probe,   // down: one cheap readiness check
  Skip,    // down, between probes: the byte is dropped
};

struct LogMirrorGate {
  uint8_t timeouts;        // consecutive
  uint16_t since_probe;    // bytes skipped since the last probe, while down
  uint32_t stalls;         // times the wire has gone down this boot
  uint32_t dropped;        // bytes lost since the last resume notice
  uint32_t first_status;   // the back end's status word at the first lost byte
  bool resume_pending;     // the wire is back after a loss; a notice is owed
};

inline bool mirrorDown(const LogMirrorGate& g, uint8_t giveup) { return g.timeouts >= giveup; }

// The next byte's treatment. A skip counts the byte as dropped, since nothing
// else will see it.
inline MirrorStep mirrorNextStep(LogMirrorGate& g, uint8_t giveup, uint16_t probe_every) {
  if (!mirrorDown(g, giveup)) return MirrorStep::Send;
  if (probe_every <= 1 || ++g.since_probe >= probe_every) {
    g.since_probe = 0;
    return MirrorStep::Probe;
  }
  g.dropped++;
  return MirrorStep::Skip;
}

// The byte went out.
inline void mirrorOnSent(LogMirrorGate& g) {
  if (g.dropped) g.resume_pending = true;
  g.timeouts = 0;
  g.since_probe = 0;
}

// The byte did not go out: a full send that timed out, or a probe that found
// the wire still not ready. `status` is the back end's raw status word, kept
// from the first lost byte so the notice shows the state the loss began in.
inline void mirrorOnTimeout(LogMirrorGate& g, uint8_t giveup, uint32_t status) {
  if (g.dropped == 0) g.first_status = status;
  g.dropped++;
  if (mirrorDown(g, giveup)) return;   // a failed probe: already down
  if (++g.timeouts == giveup) g.stalls++;
}

struct MirrorResume {
  bool pending;
  uint32_t stalls;
  uint32_t dropped;
  uint32_t status;
};

// Takes the pending notice, if any, and starts a fresh loss count.
inline MirrorResume mirrorTakeResume(LogMirrorGate& g) {
  MirrorResume r = {g.resume_pending, g.stalls, g.dropped, g.first_status};
  if (g.resume_pending) {
    g.resume_pending = false;
    g.dropped = 0;
  }
  return r;
}

// The notice, as the wire carries it:
//   "\r\n[mirror] resumed: dropped=<n> stalls=<n> status=0x<8 hex>\r\n"
// The leading CRLF ends whatever line the loss cut short. No printf, so it is
// usable from the earliest init and from fault paths. Returns the length
// written, not counting the NUL, or 0 when it doesn't fit.
inline size_t mirrorFormatResume(char* out, size_t cap, const MirrorResume& r) {
  if (!out) return 0;
  char buf[80];
  size_t n = 0;
  auto lit = [&](const char* s) { while (*s) buf[n++] = *s++; };
  auto dec = [&](uint32_t v) {
    char d[10];
    int k = 0;
    do { d[k++] = char('0' + v % 10); v /= 10; } while (v);
    while (k) buf[n++] = d[--k];
  };
  lit("\r\n[mirror] resumed: dropped=");
  dec(r.dropped);
  lit(" stalls=");
  dec(r.stalls);
  lit(" status=0x");
  for (int s = 28; s >= 0; s -= 4) buf[n++] = "0123456789abcdef"[(r.status >> s) & 0xF];
  lit("\r\n");
  if (n + 1 > cap) return 0;
  for (size_t i = 0; i < n; i++) out[i] = buf[i];
  out[n] = '\0';
  return n;
}

}  // namespace offband
