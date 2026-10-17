#pragma once

// #1553 (G0): the measurement harness for E4's GPS power modes.
//
// This exists to answer two questions nobody can answer from a datasheet, because the
// module is a generic one:
//
//   1. How long does a fix take, as a function of how long GPS was powered off? Under
//      ~5 s means it kept ephemeris across the power cut (hot start); ~30 s means almanac
//      only; 60 s+ means it starts cold every cycle. That number IS the duty-cycle
//      economics, and E4's interval default is a consequence of it rather than a guess.
//   2. What does each state cost in current -- searching, locked, off as the firmware
//      leaves it today, and off with the GPS UART torn down. Only the last pair decides
//      whether #1556 touches EnvironmentSensorManager at all.
//
// WHY THIS IS PURE. The run is ~2.8 hours unattended on the owner's badge, on battery,
// with the current trace coming off a separate rig. A sequencing bug does not show up as
// a compile error -- it shows up as a wasted night and a badge that has to be reflashed
// and left alone again. So the sequence is a state machine with no Arduino in it, driven
// by the caller, and test/test_gps_duty_probe walks the whole run in milliseconds. It
// earned that immediately: the first version emitted no phase marker when it entered
// Locked or Done, which would have left the locked-current sample with nothing in the
// capture to join it to.
//
// The caller applies what tick() returns and does the logging. This decides nothing about
// how a line is formatted and touches no hardware.

#include <stdint.h>

namespace offband {

enum class GpsProbePhase : uint8_t {
  WarmUp,        // boot -> first fix, so the module has ephemeris to maybe retain
  Locked,        // GPS on and fixed: steady-state locked current
  OffUartUp,     // GPS off, UART still up -- today's "off" baseline
  OffUartDown,   // GPS off, UART torn down -- the #1556 candidate
  Sweep,         // the time-to-fix sweep: off for N, then on until fix or timeout
  Done,          // GPS back on, nothing further
  Count,
};

// What ended, when something did. Distinct from the phase marker: a tick can both finish
// a leg and begin the next phase, and the capture needs BOTH -- the result to pair with
// its off duration, and the marker to join the next phase against the current trace.
enum class GpsProbeResult : uint8_t { None, Fix, NoFix };

struct GpsProbeAction {
  bool gps_on = true;
  bool uart_up = true;

  GpsProbePhase phase = GpsProbePhase::WarmUp;   // the phase now in effect
  bool phase_start = false;                      // log a marker for `phase` this tick

  GpsProbeResult result = GpsProbeResult::None;  // a leg just ended
  GpsProbePhase result_phase = GpsProbePhase::WarmUp;   // which phase produced it
  uint32_t off_s = 0;                            // that leg's off duration, 0 for WarmUp
  uint32_t elapsed_ms = 0;                       // time-to-fix, or time waited
};

// The phase's name for the log. A number would make the capture unreadable months from
// now, and the capture is the deliverable -- these lines are what G0 reports.
inline const char* gpsProbePhaseName(GpsProbePhase p) {
  switch (p) {
    case GpsProbePhase::WarmUp:      return "warmup";
    case GpsProbePhase::Locked:      return "locked";
    case GpsProbePhase::OffUartUp:   return "off_uart_up";
    case GpsProbePhase::OffUartDown: return "off_uart_down";
    case GpsProbePhase::Sweep:       return "sweep";
    case GpsProbePhase::Done:        return "done";
    case GpsProbePhase::Count:       break;
  }
  return "?";
}

// The off durations swept, in seconds. Chosen to bracket the three start classes rather
// than to be round numbers: 30 s is far shorter than any ephemeris validity, 2 h is far
// longer, and the two in between are where the answer actually changes.
constexpr uint32_t kGpsProbeSweepS[] = {30, 300, 1800, 7200};
constexpr uint8_t kGpsProbeSweepCount =
    (uint8_t)(sizeof(kGpsProbeSweepS) / sizeof(kGpsProbeSweepS[0]));

// How long each current-measurement phase is held. Long enough that the rig's current
// trace has an unmistakable flat section to average over, short enough that the three of
// them are a rounding error against the 2 h sweep leg.
constexpr uint32_t kGpsProbeHoldMs = 120000;

// How long a wake waits for a fix before giving up. The plan's placeholder, used here as
// a BOUND on the measurement rather than as a default: if a leg ever reports NoFix, the
// real timeout for #1554 has to be larger than this, and that is itself a result.
constexpr uint32_t kGpsProbeFixTimeoutMs = 180000;

class GpsDutyProbe {
public:
  // `now_ms` is millis(); `fix` is whether the GPS currently reports a fix.
  //
  // Call it every loop. It returns what the caller should apply, whether a new phase began
  // and whether a leg just ended. A phase is marked exactly once, on the tick it begins,
  // so the caller emits one line per phase and nothing per idle tick.
  GpsProbeAction tick(uint32_t now_ms, bool fix) {
    GpsProbeAction a;

    if (!_started) {
      _started = true;
      _phase_ms = now_ms;
      a.phase_start = true;
      return apply(a);
    }

    const uint32_t held = now_ms - _phase_ms;

    switch (_phase) {
      case GpsProbePhase::WarmUp:
        // A warm-up that never fixes is not a failure of the probe -- it means the sweep's
        // numbers would be meaningless, so it is reported and the run continues anyway.
        if (fix || held >= kGpsProbeFixTimeoutMs) {
          finish(a, fix, GpsProbePhase::WarmUp, 0, held);
          enter(GpsProbePhase::Locked, now_ms, a);
        }
        break;

      case GpsProbePhase::Locked:
        if (held >= kGpsProbeHoldMs) enter(GpsProbePhase::OffUartUp, now_ms, a);
        break;

      case GpsProbePhase::OffUartUp:
        if (held >= kGpsProbeHoldMs) enter(GpsProbePhase::OffUartDown, now_ms, a);
        break;

      case GpsProbePhase::OffUartDown:
        if (held >= kGpsProbeHoldMs) {
          _sweep = 0;
          _sweep_on = false;
          enter(GpsProbePhase::Sweep, now_ms, a);
        }
        break;

      case GpsProbePhase::Sweep: {
        const uint32_t off_ms = kGpsProbeSweepS[_sweep] * 1000UL;
        if (!_sweep_on) {
          // Powered down for this leg's duration. Powering up is its own marker, because
          // time-to-fix is measured from there and not from the start of the leg.
          if (held >= off_ms) {
            _sweep_on = true;
            _phase_ms = now_ms;
            a.phase_start = true;
          }
          break;
        }
        if (fix || held >= kGpsProbeFixTimeoutMs) {
          finish(a, fix, GpsProbePhase::Sweep, kGpsProbeSweepS[_sweep], held);
          _sweep++;
          _sweep_on = false;
          if (_sweep >= kGpsProbeSweepCount) {
            enter(GpsProbePhase::Done, now_ms, a);
          } else {
            _phase_ms = now_ms;        // the next leg's off period starts here
            a.phase_start = true;
          }
        }
        break;
      }

      case GpsProbePhase::Done:
      case GpsProbePhase::Count:
        break;
    }
    return apply(a);
  }

  GpsProbePhase phase() const { return _phase; }
  uint8_t sweepLeg() const { return _sweep; }
  // True while a sweep leg has GPS powered up waiting for its fix.
  bool sweepSearching() const { return _phase == GpsProbePhase::Sweep && _sweep_on; }
  // The off duration of the leg in progress, for the caller's marker line. 0 outside the
  // sweep and once the sweep is spent.
  uint32_t legOffS() const {
    if (_phase != GpsProbePhase::Sweep || _sweep >= kGpsProbeSweepCount) return 0;
    return kGpsProbeSweepS[_sweep];
  }

private:
  void enter(GpsProbePhase p, uint32_t now_ms, GpsProbeAction& a) {
    _phase = p;
    _phase_ms = now_ms;
    a.phase_start = true;
  }

  static void finish(GpsProbeAction& a, bool fix, GpsProbePhase which, uint32_t off_s,
                     uint32_t elapsed_ms) {
    a.result = fix ? GpsProbeResult::Fix : GpsProbeResult::NoFix;
    a.result_phase = which;
    a.off_s = off_s;
    a.elapsed_ms = elapsed_ms;
  }

  // What each phase wants of the hardware. One place, so the caller cannot disagree with
  // the machine about whether GPS should be powered.
  GpsProbeAction& apply(GpsProbeAction& a) const {
    a.phase = _phase;
    switch (_phase) {
      case GpsProbePhase::WarmUp:
      case GpsProbePhase::Locked:
      case GpsProbePhase::Done:
      case GpsProbePhase::Count:
        a.gps_on = true;
        a.uart_up = true;
        break;
      case GpsProbePhase::OffUartUp:
        a.gps_on = false;
        a.uart_up = true;
        break;
      case GpsProbePhase::OffUartDown:
        a.gps_on = false;
        a.uart_up = false;
        break;
      case GpsProbePhase::Sweep:
        a.gps_on = _sweep_on;
        a.uart_up = true;   // the teardown is measured once, not once per leg
        break;
    }
    return a;
  }

  GpsProbePhase _phase = GpsProbePhase::WarmUp;
  uint32_t _phase_ms = 0;
  uint8_t _sweep = 0;
  bool _sweep_on = false;
  bool _started = false;
};

}  // namespace offband
