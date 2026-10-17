// Native tests for the GPS duty probe's sequence (#1553).
//
// These exist because the probe runs ~2.8 hours unattended, on battery, on the owner's
// badge, with the current trace coming off a separate rig. A sequencing bug costs a night
// and a reflash, and it would not show up as a compile error. So the whole run is walked
// here in milliseconds, including the legs that take two hours on hardware.
//
// They earned it on the first run: the machine entered Locked and Done without emitting
// any phase marker, so the locked-current sample would have had nothing in the capture to
// join against.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <helpers/GpsDutyProbe.h>

using namespace offband;

namespace {

// Drives the probe forward and records everything it emits. `fix_after_ms` is how long
// after GPS comes on the module reports a fix; UINT32_MAX means never.
struct ProbeRun {
  struct Ev {
    bool phase_start;
    GpsProbePhase phase;
    GpsProbeResult result;
    GpsProbePhase result_phase;
    uint32_t off_s;
    uint32_t elapsed_ms;
    uint32_t at_ms;
    bool gps_on;
    bool uart_up;
  };
  std::vector<Ev> events;
  uint32_t ms = 0;

  void go(GpsDutyProbe& p, uint32_t fix_after_ms, uint32_t limit_ms, uint32_t step_ms = 250) {
    uint32_t gps_on_since = 0;
    bool was_on = false;
    bool fix = false;
    while (ms < limit_ms) {
      const GpsProbeAction a = p.tick(ms, fix);
      if (a.gps_on && !was_on) gps_on_since = ms;
      was_on = a.gps_on;
      // The module only has a fix while powered, and only after its acquisition time.
      fix = a.gps_on && fix_after_ms != UINT32_MAX && (ms - gps_on_since) >= fix_after_ms;
      if (a.phase_start || a.result != GpsProbeResult::None) {
        events.push_back({a.phase_start, a.phase, a.result, a.result_phase, a.off_s,
                          a.elapsed_ms, ms, a.gps_on, a.uart_up});
      }
      ms += step_ms;
    }
  }

  int phaseStarts(GpsProbePhase ph) const {
    int n = 0;
    for (const auto& e : events) {
      if (e.phase_start && e.phase == ph) n++;
    }
    return n;
  }
};

uint32_t totalRunMs() {
  uint32_t t = 3 * kGpsProbeHoldMs + kGpsProbeFixTimeoutMs * (kGpsProbeSweepCount + 1);
  for (uint8_t i = 0; i < kGpsProbeSweepCount; i++) t += kGpsProbeSweepS[i] * 1000UL;
  return t + 60000;
}

}  // namespace

// The sweep is the whole point: four legs, and each one's result has to carry the off
// duration it actually belongs to. A leg reporting the wrong off_s would silently pair a
// time-to-fix with the wrong power-off interval -- the one number E4's defaults rest on.
TEST(GpsDutyProbe, EverySweepLegReportsItsOwnOffDuration) {
  GpsDutyProbe p;
  ProbeRun r;
  r.go(p, 4000, totalRunMs());

  std::vector<uint32_t> legs;
  for (const auto& e : r.events) {
    if (e.result == GpsProbeResult::Fix && e.result_phase == GpsProbePhase::Sweep) {
      legs.push_back(e.off_s);
    }
  }
  ASSERT_EQ((size_t)kGpsProbeSweepCount, legs.size())
      << "every sweep leg must produce exactly one result";
  for (uint8_t i = 0; i < kGpsProbeSweepCount; i++) {
    EXPECT_EQ(kGpsProbeSweepS[i], legs[i])
        << "leg " << (int)i << " reported the wrong off duration";
  }
}

// The measured value. A fix 4 s after power-up has to come back as ~4000 ms, not as the
// time since the leg began -- the leg includes its own off period, and conflating the two
// would report a 30-minute time-to-fix for a hot start.
TEST(GpsDutyProbe, TimeToFixIsMeasuredFromPowerUpNotFromTheLegsStart) {
  GpsDutyProbe p;
  ProbeRun r;
  r.go(p, 4000, totalRunMs());

  int checked = 0;
  for (const auto& e : r.events) {
    if (e.result == GpsProbeResult::Fix && e.result_phase == GpsProbePhase::Sweep) {
      EXPECT_GE(e.elapsed_ms, 4000u);
      EXPECT_LT(e.elapsed_ms, 6000u)
          << "off_s=" << e.off_s << " reported ttff " << e.elapsed_ms
          << " ms -- the off period is being counted as acquisition time";
      checked++;
    }
  }
  EXPECT_EQ((int)kGpsProbeSweepCount, checked);
}

// The three current phases, in order, each held for its full duration. The rig averages a
// flat section of the current trace, so a phase that ends early has no usable sample.
TEST(GpsDutyProbe, TheThreeCurrentPhasesAreHeldInOrderForTheirFullDuration) {
  GpsDutyProbe p;
  ProbeRun r;
  r.go(p, 4000, totalRunMs());

  uint32_t locked_at = 0, off_up_at = 0, off_down_at = 0, sweep_at = 0;
  for (const auto& e : r.events) {
    if (!e.phase_start) continue;
    if (e.phase == GpsProbePhase::Locked && !locked_at) locked_at = e.at_ms;
    if (e.phase == GpsProbePhase::OffUartUp && !off_up_at) off_up_at = e.at_ms;
    if (e.phase == GpsProbePhase::OffUartDown && !off_down_at) off_down_at = e.at_ms;
    if (e.phase == GpsProbePhase::Sweep && !sweep_at) sweep_at = e.at_ms;
  }
  ASSERT_TRUE(locked_at && off_up_at && off_down_at && sweep_at)
      << "a current-measurement phase began with no marker in the capture to join it to";
  EXPECT_LT(locked_at, off_up_at);
  EXPECT_LT(off_up_at, off_down_at);
  EXPECT_LT(off_down_at, sweep_at);
  EXPECT_GE(off_up_at - locked_at, kGpsProbeHoldMs);
  EXPECT_GE(off_down_at - off_up_at, kGpsProbeHoldMs);
  EXPECT_GE(sweep_at - off_down_at, kGpsProbeHoldMs);
}

// The UART teardown happens in exactly one phase. Tearing it down anywhere else would
// change what the sweep measures, and leaving it down afterwards would kill the GPS for
// the rest of the run -- the sweep would then report NoFix for every leg and look like a
// hardware result rather than a probe bug.
TEST(GpsDutyProbe, TheUartIsDownInExactlyOnePhaseAndComesBackUp) {
  GpsDutyProbe p;
  uint32_t ms = 0;
  bool fix = false, seen_down = false, up_again_after_down = false;
  uint32_t gps_on_since = 0;
  bool was_on = false;
  const uint32_t limit = totalRunMs();
  while (ms < limit) {
    const GpsProbeAction a = p.tick(ms, fix);
    if (a.gps_on && !was_on) gps_on_since = ms;
    was_on = a.gps_on;
    fix = a.gps_on && (ms - gps_on_since) >= 4000;

    if (!a.uart_up) {
      EXPECT_EQ(GpsProbePhase::OffUartDown, a.phase)
          << "the UART is down outside the one phase that measures it";
      EXPECT_FALSE(a.gps_on) << "the UART must never be torn down while GPS is powered";
      seen_down = true;
    } else if (seen_down) {
      up_again_after_down = true;
    }
    ms += 250;
  }
  EXPECT_TRUE(seen_down) << "the UART-down phase never ran, so #1556 has no measurement";
  EXPECT_TRUE(up_again_after_down) << "the UART stayed down -- the rest of the run is void";
}

// A module that never fixes must not hang the run. Every leg times out, the sweep still
// completes, and each is reported as NoFix rather than as nothing -- "it never fixed" is
// itself an answer to G0's first question.
TEST(GpsDutyProbe, AModuleThatNeverFixesStillCompletesTheRun) {
  GpsDutyProbe p;
  ProbeRun r;
  r.go(p, UINT32_MAX, totalRunMs());

  int nofix = 0;
  for (const auto& e : r.events) {
    if (e.result == GpsProbeResult::NoFix && e.result_phase == GpsProbePhase::Sweep) nofix++;
  }
  EXPECT_EQ((int)kGpsProbeSweepCount, nofix) << "a leg neither fixed nor timed out";
  EXPECT_EQ(GpsProbePhase::Done, p.phase()) << "the run did not finish";
}

// Done leaves GPS on. The badge is the owner's; a probe that finishes by leaving the GPS
// powered down looks identical to a probe that crashed.
TEST(GpsDutyProbe, TheRunEndsWithGpsOn) {
  GpsDutyProbe p;
  ProbeRun r;
  r.go(p, 4000, totalRunMs());
  ASSERT_EQ(GpsProbePhase::Done, p.phase());
  const GpsProbeAction a = p.tick(r.ms, true);
  EXPECT_TRUE(a.gps_on);
  EXPECT_TRUE(a.uart_up);
  EXPECT_FALSE(a.phase_start) << "Done must stop emitting markers";
  EXPECT_EQ(GpsProbeResult::None, a.result);
}

// One marker per phase, so the capture has exactly one join point per current sample.
//
// The sweep is the deliberate exception: each leg marks its power-DOWN and its power-UP
// separately, so the current trace can be segmented per leg -- the off stretch and the
// searching stretch are different measurements. That is TWO markers per leg, and the
// marker entering the sweep IS leg 0's power-down, so the total is 2 x the leg count.
// gps_on on the line tells the two apart.
TEST(GpsDutyProbe, EachPhaseIsMarkedOnceAndTheSweepMarksEveryLegsPowerUp) {
  GpsDutyProbe p;
  ProbeRun r;
  r.go(p, 4000, totalRunMs());

  EXPECT_EQ(1, r.phaseStarts(GpsProbePhase::WarmUp));
  EXPECT_EQ(1, r.phaseStarts(GpsProbePhase::Locked));
  EXPECT_EQ(1, r.phaseStarts(GpsProbePhase::OffUartUp));
  EXPECT_EQ(1, r.phaseStarts(GpsProbePhase::OffUartDown));
  EXPECT_EQ(1, r.phaseStarts(GpsProbePhase::Done));
  EXPECT_EQ(2 * (int)kGpsProbeSweepCount, r.phaseStarts(GpsProbePhase::Sweep))
      << "each leg needs a power-down marker and a power-up marker";

  int powered_up_markers = 0;
  for (const auto& e : r.events) {
    if (e.phase_start && e.phase == GpsProbePhase::Sweep && e.gps_on) powered_up_markers++;
  }
  EXPECT_EQ((int)kGpsProbeSweepCount, powered_up_markers)
      << "a leg powered up without a marker, so its time-to-fix cannot be placed";
}

// A result and the next phase's marker land on the same tick, and the capture needs both:
// the result to pair a time-to-fix with its off duration, and the marker to join what
// follows against the current trace. The first version of this machine returned one or
// the other, which is the bug these tests caught.
//
// The two labels are independent, not necessarily different: an inter-leg tick ends a
// Sweep leg AND begins the next leg's off period, so both read Sweep. They differ only
// where the phase itself changes, and those two transitions are the ones pinned here.
TEST(GpsDutyProbe, AResultAndTheNextPhaseMarkerShareATickAndAreLabelledIndependently) {
  GpsDutyProbe p;
  ProbeRun r;
  r.go(p, 4000, totalRunMs());

  int shared = 0, warmup_to_locked = 0, sweep_to_done = 0;
  for (const auto& e : r.events) {
    if (e.result == GpsProbeResult::None || !e.phase_start) continue;
    shared++;
    if (e.result_phase == GpsProbePhase::WarmUp && e.phase == GpsProbePhase::Locked) {
      warmup_to_locked++;
    }
    if (e.result_phase == GpsProbePhase::Sweep && e.phase == GpsProbePhase::Done) {
      sweep_to_done++;
    }
  }
  // Four legs plus the warm-up each end on a tick that also starts something.
  EXPECT_EQ(1 + (int)kGpsProbeSweepCount, shared)
      << "a result or a marker is being dropped when they coincide";
  EXPECT_EQ(1, warmup_to_locked)
      << "the warm-up's fix must be labelled WarmUp while the tick starts Locked";
  EXPECT_EQ(1, sweep_to_done)
      << "the last leg's result must be labelled Sweep while the tick starts Done";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
