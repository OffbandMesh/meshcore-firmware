#pragma once

// #1554 (G1a): GPS Off / On / Power save, as rules with no Arduino in them.
//
// Pure so the whole machine is host-tested before it ever touches a badge. #1553 made the
// case for that the expensive way: its harness ran 2.8 hours unattended per attempt, and a
// sequencing bug cost a night. This machine decides when a GPS radio burns 66-77 mA, so
// getting it wrong costs a conference badge its weekend rather than a test run.
//
// THE DESIGN IS BACKOFF, NOT A FIXED TIMEOUT, and that came from measurement (#1553) plus
// the owner's reading of where this thing actually lives.
//
// The badge sits indoors, and so will the conference. GPS modules of this class struggle
// inside a building -- #1553's 30-minute and 2-hour legs never fixed at all. So the value
// of this feature is NOT position. It is not spending 50 mA on a search that cannot
// succeed: searching measured 66-77 mA against 13.7 mA idle, so a badge left hunting
// indoors all weekend runs at ~4.5x its idle draw and achieves nothing.
//
// A longer attempt timeout is right with sky and actively harmful without it, because
// every extra second is spent at searching current for a fix that is not coming. One fixed
// timeout cannot serve both, so this does not ship one. A failed attempt costs about
// (70 - 13.7) mA x its duration; over a 48 h indoor weekend the difference is stark:
//
//     fixed 180 s attempt, retried hourly   ~48 attempts   ~135 mAh wasted
//     90 s attempt with backoff to 8 h       ~9 attempts    ~13 mAh wasted
//
// Backoff is what makes a badge with sky settle into its normal interval while a badge
// indoors quietly stops trying. Any successful fix resets it, so walking outside brings
// GPS straight back with no reboot and no visit to Settings.

#include <stdint.h>

namespace offband {

enum class GpsMode : uint8_t {
  Off = 0,        // powered down, stays down
  On = 1,         // powered, stays powered -- today's gps_enabled = 1
  PowerSave = 2,  // attempt, sleep, back off on repeated failure
};

// What the UI should say GPS is doing. #1557 needs every one of these: "off" and "asleep
// between fixes" are not the same thing to someone looking at a screen, and neither is
// "given up for now", which is the state a conference attendee will actually be in and the
// one most likely to read as a fault if it is left unlabelled.
//
// Both enums go on the companion wire as raw bytes (0xC5 GPS, #1791): append only.
enum class GpsActivity : uint8_t {
  Off,         // mode Off
  On,          // mode On
  Searching,   // Power save, an attempt is running
  Sleeping,    // Power save, waiting out the interval after a success
  BackedOff,   // Power save, waiting longer because attempts keep failing
};

// ---- the numbers, and where each one comes from ---------------------------------------

// How long an attempt runs before giving up. There are TWO, and the difference is the
// whole point.
//
// Review caught a real trap in having one: #1553 measured time-to-fix after power-offs of
// 30 s (16.0 s) and 5 min (69.0 s), and from a cold boot (39.8 s) -- but the machine
// actually sleeps for `kGpsIntervalMs`, an HOUR, and the fix time after an hour off was
// never measured because the only legs that long were indoors and failed. A single 90 s
// timeout tuned on short sleeps would fail a genuine outdoor cold start, increment the
// failure count, and back a badge off **while it is standing outside** -- punishing the
// user for ordinary GPS behaviour and contradicting the one promise backoff rests on,
// that walking outside brings GPS straight back.
//
// So: generous while there is still reason to believe a fix is possible, short once
// repeated failure says there is not.
//
// `[needs the #1553 re-run with sky]` 180 s for the first attempt. It covers a cold start
// comfortably and is the number the re-run must confirm -- the acquisition cost after an
// hour off is the one measurement this task is missing.
constexpr uint32_t kGpsFirstAttemptTimeoutMs = 180000;

// `[measured: #1553]` 90 s once already failing. At that point the environment has said
// it has no fix in it, so an attempt is a cheap probe rather than a serious try: it costs
// ~1.4 mAh instead of ~2.8 mAh, and over an indoor weekend that is the difference between
// ~14 mAh and ~27 mAh spent confirming the obvious. 90 s still clears every successful
// acquisition #1553 measured.
constexpr uint32_t kGpsProbeAttemptTimeoutMs = 90000;

// The wait after a SUCCESSFUL fix, before attempting again.
//
// `[owner's plan, not measured]` 60 minutes, carried from the Feature plan. #1553 cannot
// ground this one: both of its legs at 30 minutes and beyond failed indoors, so there is
// no measured acquisition cost anywhere in this range. Stated as the owner's choice rather
// than dressed up as a measurement.
constexpr uint32_t kGpsIntervalMs = 3600000;

// The ceiling on backoff, as a doubling count. 3 means the wait grows
// interval -> 2x -> 4x -> 8x and stops, so an hourly interval tops out at 8 hours.
//
// `[flagged: needs the #1553 re-run with sky]` This is the one number this task should not
// invent and does not pretend to have measured. 8x is chosen because it takes an indoor
// 48 h weekend from ~48 attempts to ~9 while still retrying often enough that a badge
// carried outside is found within a few hours. Confirming it needs the acquisition cost at
// 30 min and beyond, which only a run with a sky view can give.
constexpr uint8_t kGpsMaxBackoffShift = 3;

struct GpsPowerConfig {
  uint32_t interval_ms = kGpsIntervalMs;
  uint32_t first_attempt_timeout_ms = kGpsFirstAttemptTimeoutMs;
  uint32_t probe_attempt_timeout_ms = kGpsProbeAttemptTimeoutMs;
  uint8_t max_backoff_shift = kGpsMaxBackoffShift;
};

// The longest Power-save interval that can be stored, in seconds. Beyond this the
// milliseconds do not fit a uint32, and a wrapped interval comes back SHORT -- which is
// the one failure mode that turns a power saver into a battery drain.
constexpr uint32_t kGpsMaxPsIntervalS = 0xFFFFFFFFu / 1000u;

// Config from the stored pref (#1556).
//
// A stored 0 means "use the compiled default", which is what an older prefs file reads as.
// It must NOT become a zero-length sleep: that would attempt continuously and be worse
// than never having had Power save. An absurd value is clamped rather than multiplied into
// a wrap, for the same reason gpsBackoffWaitMs saturates.
inline GpsPowerConfig gpsConfigFromPrefs(uint32_t ps_interval_s) {
  GpsPowerConfig cfg;
  if (ps_interval_s == 0) return cfg;                       // the compiled default
  if (ps_interval_s > kGpsMaxPsIntervalS) ps_interval_s = kGpsMaxPsIntervalS;
  cfg.interval_ms = ps_interval_s * 1000u;
  return cfg;
}

// The shortest Power-save interval a client may store (#1791).
//
// An interval shorter than an attempt is not a power saver: the badge would finish one
// search and start the next almost at once, which is GPS held on with extra steps. The
// first attempt may run kGpsFirstAttemptTimeoutMs (180 s), so the floor sits above it.
constexpr uint32_t kGpsMinPsIntervalS = 300;
static_assert(kGpsMinPsIntervalS * 1000u > kGpsFirstAttemptTimeoutMs,
              "the shortest interval must outlast the longest attempt");

// What to store for a Power-save interval a client asked for (#1791).
//
// 0 stays 0 -- "use the compiled default" -- so a client can hand the choice back. Anything
// else is clamped into [kGpsMinPsIntervalS, kGpsMaxPsIntervalS] rather than refused, and
// the reply carries the stored value, so the client sees what actually landed.
inline uint32_t gpsPsIntervalForWrite(uint32_t requested_s) {
  if (requested_s == 0) return 0;
  if (requested_s < kGpsMinPsIntervalS) return kGpsMinPsIntervalS;
  if (requested_s > kGpsMaxPsIntervalS) return kGpsMaxPsIntervalS;
  return requested_s;
}

// How long the next attempt gets, given how many have failed in a row.
//
// Nothing has failed yet -- either a success preceded it or this is the first try -- so a
// fix is plausible and the attempt is given the generous timeout. Once anything has
// failed, later attempts are cheap probes. See the two constants for why.
inline uint32_t gpsAttemptTimeoutMs(const GpsPowerConfig& cfg, uint8_t failures) {
  return failures == 0 ? cfg.first_attempt_timeout_ms : cfg.probe_attempt_timeout_ms;
}

// How long to wait before the next attempt, after `failures` consecutive failures.
//
// **Growth starts at the SECOND consecutive failure, not the first.** One missed fix is not
// a pattern -- a single transient miss should cost the normal interval, the same as a
// success, because a badge that briefly loses the sky is not a badge that has given up. It
// is consecutive failure that means "this environment has no fix in it", and that is what
// earns a longer wait. So 0 or 1 failures both wait the plain interval, and doubling begins
// from the second: 1x, 1x, 2x, 4x, 8x, capped.
//
// The first version doubled on the first failure and a test caught it: an indoor badge
// jumped straight to a 2 h wait, which is the right instinct applied one step too early.
//
// Saturating, and that matters: the shift is bounded by the cap AND the result is clamped,
// so a large configured interval cannot overflow into a SHORT wait -- which would turn
// backoff into the tight retry loop it exists to prevent, silently.
inline uint32_t gpsBackoffWaitMs(const GpsPowerConfig& cfg, uint8_t failures) {
  const uint8_t steps = failures > 1 ? (uint8_t)(failures - 1) : 0;
  const uint8_t shift = steps > cfg.max_backoff_shift ? cfg.max_backoff_shift : steps;
  // One pre-check rather than a check inside a doubling loop (review: the loop read as
  // unusual, and unusual invites a maintainer to simplify it wrongly). If the shift would
  // not fit, saturate -- a wrapped wait would come back SHORTER than the interval and turn
  // backoff into the tight retry loop it exists to prevent, silently.
  if (shift >= 32 || cfg.interval_ms > (0xFFFFFFFFu >> shift)) return 0xFFFFFFFFu;
  return cfg.interval_ms << shift;
}

// A stored mode byte as a mode.
//
// An unrecognised value means a pref written by a build that knew more modes than this one.
// It falls back to **On**, which is what this firmware did before Power save existed and
// what a board with gps_enabled = 1 does today. Never to Power save: that is the new
// behaviour, and arriving in it by accident would be a badge that mysteriously stops
// holding a fix. Never to Off either -- silence is a choice someone makes, the same
// argument #1522 made for tune ids.
//
// Where the byte lives, and its migration from the gps_enabled bool, is #1555.
inline GpsMode gpsModeFromStored(uint8_t stored) {
  switch (stored) {
    case (uint8_t)GpsMode::Off:       return GpsMode::Off;
    case (uint8_t)GpsMode::On:        return GpsMode::On;
    case (uint8_t)GpsMode::PowerSave: return GpsMode::PowerSave;
    default:                          return GpsMode::On;
  }
}

inline bool isValidGpsMode(uint8_t stored) {
  return stored <= (uint8_t)GpsMode::PowerSave;
}

// ---- the stored pref, and living alongside the gps_enabled bool (#1555) ----------------
//
// Prefs are keyed now (ConfigSerializer / prefs.json), so adding a mode key is additive:
// a file written before this existed simply has no `mode`, and the member keeps its
// default. That default has to mean "nothing stored", NOT a mode -- defaulting to Off
// would stop GPS working for every badge that has gps_enabled = 1 today, which is the same
// trap #1522's encoding was built to avoid for tune ids.
constexpr uint8_t kGpsModeUnset = 0xFF;
static_assert(kGpsModeUnset > (uint8_t)GpsMode::PowerSave,
              "the unset sentinel must not name a mode");

// HOW THE TWO FIELDS DIVIDE THE WORK, and this split is the design.
//
//   gps_enabled  -- is GPS on at all. Meaning UNCHANGED from today, so every existing
//                   reader and writer stays correct with no translation: the app's toggle,
//                   `gps on|off`, the observer's time-state report, ui-tiny's Power row,
//                   applyGpsPrefs.
//   gps_mode     -- HOW it runs while enabled. Only On and Power save are meaningful;
//                   unset means On, which is what the firmware did before this existed.
//
// The first version made the mode authoritative and kept the bool as a mirror. Review
// broke it with a sequence anyone would do: in Power save, toggle GPS off from the phone,
// then back on. The off write had to collapse the mode to Off, and the on write had no
// memory of what it had been, so the user landed in On and silently lost the battery
// saving. Splitting it this way removes that entirely -- **a bool write never touches the
// mode byte, so turning GPS off and on again restores Power save** -- and it removes the
// whole class of mode-versus-mirror drift along with it, because there is no mirror.

// The mode in effect, from the stored byte and the boolean.
//
// The unset check must stay AHEAD of gpsModeFromStored(), which maps anything
// unrecognised to On. That is right for a byte from a newer firmware and wrong for unset:
// if a maintainer ever collapses the two, GpsModePrefs.UnsetIsNotTreatedAsAnUnknownMode
// fails on purpose.
inline GpsMode gpsModeFromPrefs(uint8_t stored_mode, uint8_t enabled_bool) {
  if (!enabled_bool) return GpsMode::Off;
  if (stored_mode == kGpsModeUnset) return GpsMode::On;
  const GpsMode m = gpsModeFromStored(stored_mode);
  // A stored Off while the boolean says enabled is a contradiction -- it can only come
  // from an older layout or a partial write. The boolean is the one every other reader
  // obeys, so it wins, and "enabled" with no usable HOW means On.
  return m == GpsMode::Off ? GpsMode::On : m;
}

// The boolean a chosen mode implies, for the writer that sets both.
inline uint8_t gpsEnabledForMode(GpsMode m) { return m == GpsMode::Off ? 0 : 1; }

// What to STORE in the mode byte when the user picks `m` on the GPS screen.
//
// Picking Off deliberately LEAVES THE BYTE ALONE: it records how GPS ran, and the user has
// not changed their mind about that, only switched it off. So Off then On returns them to
// Power save rather than to continuous On. This is the #1523 rule applied to a different
// field -- a writer must not clear what it is not being asked about.
inline uint8_t gpsStoredModeForPick(GpsMode m, uint8_t current_stored) {
  if (m == GpsMode::Off) return current_stored;
  return (uint8_t)m;
}

// ---- the machine ----------------------------------------------------------------------

struct GpsPowerAction {
  bool gps_on = true;
  GpsActivity activity = GpsActivity::On;
  bool attempt_started = false;    // an attempt just began
  bool fix_acquired = false;       // an attempt just succeeded
  bool attempt_failed = false;     // an attempt just timed out
  uint8_t failures = 0;            // consecutive failures, after this tick
  uint32_t next_wait_ms = 0;       // how long the wait now in progress is
};

class GpsPowerMachine {
public:
  explicit GpsPowerMachine(GpsPowerConfig cfg = GpsPowerConfig()) : _cfg(cfg) {}

  // Boot, or a mode change. Power save begins with an ATTEMPT rather than a wait: the
  // clock is the thing GPS is actually for on this badge, so a boot should try for one
  // promptly instead of sitting dark for the first interval.
  //
  // It arranges that by backdating the wait rather than by setting _attempting directly,
  // so the first attempt starts through the SAME path as every later one and therefore
  // reports attempt_started like they do. Setting the flag here instead left the boot
  // attempt unannounced, which a test caught as attempts and failures disagreeing by one --
  // and which would have left the screen and the log silent about the attempt that matters
  // most, the one that sets the clock.
  void setMode(GpsMode m, uint32_t now_ms) {
    _mode = m;
    _failures = 0;
    _attempting = false;
    _since_ms = now_ms - gpsBackoffWaitMs(_cfg, 0);
  }

  // Re-arm with a (possibly new) config. Named rather than done by assigning a fresh
  // instance over the old one: review asked whether reassigning a static was safe, and
  // while this type is scalars-only and trivially assignable, a reader should not have to
  // establish that to trust the code.
  void reset(const GpsPowerConfig& cfg, GpsMode m, uint32_t now_ms) {
    _cfg = cfg;
    setMode(m, now_ms);
  }

  GpsMode mode() const { return _mode; }
  uint8_t failures() const { return _failures; }

  GpsPowerAction tick(uint32_t now_ms, bool fix) {
    GpsPowerAction a;
    a.failures = _failures;

    if (_mode == GpsMode::Off) {
      a.gps_on = false;
      a.activity = GpsActivity::Off;
      return a;
    }
    if (_mode == GpsMode::On) {
      a.gps_on = true;
      a.activity = GpsActivity::On;
      return a;
    }

    // Power save. Signed difference throughout, so a millis() rollover cannot strand the
    // machine in a wait that never ends.
    const int32_t elapsed = (int32_t)(now_ms - _since_ms);

    if (_attempting) {
      if (fix) {
        _failures = 0;
        _attempting = false;
        _since_ms = now_ms;
        a.fix_acquired = true;
        a.failures = 0;
      } else if (elapsed >= (int32_t)gpsAttemptTimeoutMs(_cfg, _failures)) {
        if (_failures < 255) _failures++;
        _attempting = false;
        _since_ms = now_ms;
        a.attempt_failed = true;
        a.failures = _failures;
      } else {
        a.gps_on = true;
        a.activity = GpsActivity::Searching;
        return a;
      }
      // Fell out of an attempt: a wait begins on this same tick.
      a.gps_on = false;
      a.next_wait_ms = gpsBackoffWaitMs(_cfg, _failures);
      a.activity = _failures == 0 ? GpsActivity::Sleeping : GpsActivity::BackedOff;
      return a;
    }

    // Waiting.
    const uint32_t wait = gpsBackoffWaitMs(_cfg, _failures);
    if (elapsed >= (int32_t)wait) {
      _attempting = true;
      _since_ms = now_ms;
      a.gps_on = true;
      a.activity = GpsActivity::Searching;
      a.attempt_started = true;
      return a;
    }
    a.gps_on = false;
    a.next_wait_ms = wait;
    a.activity = _failures == 0 ? GpsActivity::Sleeping : GpsActivity::BackedOff;
    return a;
  }

private:
  GpsPowerConfig _cfg;
  GpsMode _mode = GpsMode::On;
  uint32_t _since_ms = 0;
  uint8_t _failures = 0;
  bool _attempting = false;
};

// Should the duty cycle re-arm: pick the mode up again, clear its failure count and
// attempt now?
//
// `gen` is a counter bumped by every writer of the GPS prefs. Watching it, and not just
// the mode, is the ENTIRE reason the machine is a member rather than a loop-local: a
// re-selection of the mode already in effect must still force a retry. Someone looking at
// "no sky; next try 4h" has that one gesture and nothing else short of a reboot, and a
// check for a changed mode alone would make it a silent no-op.
//
// Pure so the rule is tested; the driver around it needs hardware.
inline bool gpsShouldRearm(bool primed, GpsMode seen_mode, uint32_t seen_gen,
                           GpsMode now_mode, uint32_t now_gen) {
  return !primed || seen_mode != now_mode || seen_gen != now_gen;
}

// The next mode as the GPS screen's row cycles. Power save is only offered where the
// variant actually compiles the duty cycle (OFFBAND_GPS_POWER_SAVE) -- offering a mode
// that nothing drives would leave GPS simply on while the screen claimed otherwise, which
// is worse than not offering it.
inline GpsMode nextGpsMode(GpsMode m, bool power_save_available) {
  switch (m) {
    case GpsMode::Off: return GpsMode::On;
    case GpsMode::On:  return power_save_available ? GpsMode::PowerSave : GpsMode::Off;
    case GpsMode::PowerSave: return GpsMode::Off;
  }
  return GpsMode::On;
}

inline const char* gpsModeName(GpsMode m) {
  switch (m) {
    case GpsMode::Off:       return "Off";
    case GpsMode::On:        return "On";
    case GpsMode::PowerSave: return "Power save";
  }
  return "?";
}

}  // namespace offband
