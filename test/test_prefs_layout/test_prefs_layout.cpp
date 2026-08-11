// test/test_prefs_layout/test_prefs_layout.cpp -- Offband #627
//
// Byte-exact fixtures for legacy-prefs layout detection. Every released
// Offband and upstream record length is covered, plus the one colliding
// length, truncation, and the fail-closed paths.
//
// Folder MUST stay `test_`-prefixed and this file provides its own main() --
// this repo does not link gtest_main.

#include <gtest/gtest.h>
#include "../../src/helpers/prefs/PrefsLayout.h"

using namespace offband;

namespace {

// Build the contested window (bytes 290..294) for a record.
PrefsTail tail(uint8_t b290, uint8_t b291, uint8_t b292, uint8_t b293, uint8_t b294) {
  PrefsTail t;
  t.valid = true;
  t.b290 = b290; t.b291 = b291; t.b292 = b292; t.b293 = b293; t.b294 = b294;
  return t;
}

PrefsLayoutEvidence evA(size_t len, PrefsTail t = PrefsTail{},
                        bool marker = false, bool marker_supported = false) {
  PrefsLayoutEvidence e;
  e.length = len;
  e.tail = t;
  e.offband_nvs_marker = marker;
  e.nvs_marker_supported = marker_supported;
  return e;
}

// A realistic upstream tail at length 295: rx_gain=1, flood_max_unscoped=64,
// flood_max_advert=8, fem_rxgain=1, cad=0  (upstream's field order + defaults)
PrefsTail upstreamTail295() { return tail(1, 64, 8, 1, 0); }

// A realistic Offband tail at length 295: rx_gain=1, fem_rxgain=1,
// flood_max_unscoped=64, flood_max_advert=8, ui_led=1 (Offband's field order)
PrefsTail offbandTail295() { return tail(1, 1, 64, 8, 1); }

// The genuinely indistinguishable case: flood limits deliberately set <= 1,
// so both layouts read as "boolean, boolean, ..." and content cannot decide.
PrefsTail degenerateTail295() { return tail(1, 1, 1, 1, 1); }

}  // namespace

// ---------------------------------------------------------------- Path A ---

TEST(PathA, ReleasedOffbandLengthsAreOffband) {
  for (size_t len : {292u, 294u, 364u}) {
    auto r = detectCommonPrefsLayout(evA(len));
    EXPECT_EQ(r.layout, PrefsLayout::Offband) << "length " << len;
    EXPECT_EQ(r.reason, PrefsLayoutReason::LengthUniqueOffband) << "length " << len;
  }
}

TEST(PathA, ReleasedUpstreamLengthsAreUpstream) {
  // 291 = companion-v1.15.0, 293 = companion-v1.16.0. 295 is the collision
  // and is covered separately -- it must NOT resolve on length alone.
  for (size_t len : {291u, 293u}) {
    auto r = detectCommonPrefsLayout(evA(len));
    EXPECT_EQ(r.layout, PrefsLayout::Upstream) << "length " << len;
    EXPECT_EQ(r.reason, PrefsLayoutReason::LengthUniqueUpstream) << "length " << len;
  }
}

TEST(PathA, OffbandBenchOnlyLengthsAreOffband) {
  // Unreleased dev/bench builds with no upstream counterpart.
  for (size_t len : {296u, 298u}) {
    auto r = detectCommonPrefsLayout(evA(len));
    EXPECT_EQ(r.layout, PrefsLayout::Offband) << "length " << len;
  }
}

TEST(PathA, CollidingLengthNeverResolvesOnLengthAlone) {
  // 295 is upstream 1.17.0 AND an unreleased Offband build. With no tail and
  // no marker there is nothing to decide on -- it must fail closed.
  auto r = detectCommonPrefsLayout(evA(295));
  EXPECT_EQ(r.layout, PrefsLayout::Unknown);
  EXPECT_EQ(r.reason, PrefsLayoutReason::TailUnavailable);
}

TEST(PathA, CollisionResolvedByContent_Upstream) {
  // byte 291 = 64 -> a flood limit, which fem_rxgain (0..1) can never be.
  auto r = detectCommonPrefsLayout(evA(295, upstreamTail295()));
  EXPECT_EQ(r.layout, PrefsLayout::Upstream);
  EXPECT_EQ(r.reason, PrefsLayoutReason::ContentFieldRange);
}

TEST(PathA, CollisionResolvedByContent_Offband) {
  // byte 293 = 8 (flood_max_advert) -> cannot be upstream, whose byte 293 is
  // fem_rxgain clamped 0..1.
  auto r = detectCommonPrefsLayout(evA(295, offbandTail295()));
  EXPECT_EQ(r.layout, PrefsLayout::Offband);
  EXPECT_EQ(r.reason, PrefsLayoutReason::ContentFieldRange);
}

// REGRESSION -- the adversarial review's counterexample. An UPSTREAM record
// whose owner set flood_max_unscoped to 1 and left flood_max_advert at its
// default 8 gives b291=1, b292=8, b293=fem_rxgain(0/1). An earlier heuristic
// tested `b292 > 1 -> Offband` and confidently mis-classified exactly this,
// corrupting a real user's config. b292 is a flood limit under BOTH layouts
// and discriminates nothing.
TEST(PathA, UpstreamWithLowUnscopedLimitIsNotMisreadAsOffband) {
  for (uint8_t unscoped : {uint8_t{0}, uint8_t{1}}) {
    for (uint8_t fem : {uint8_t{0}, uint8_t{1}}) {
      // upstream order: rx_gain, fld_unscop, fld_advert, fem_rxgain, cad
      auto ev = evA(295, tail(1, unscoped, 8, fem, 0));
      auto r = detectCommonPrefsLayout(ev);
      EXPECT_NE(r.layout, PrefsLayout::Offband)
          << "unscoped=" << int(unscoped) << " fem=" << int(fem)
          << " -- upstream record must never be classified Offband";
    }
  }
}

TEST(PathA, ContradictoryContentFailsClosed) {
  // b291 > 1 rules out Offband and b293 > 1 rules out Upstream: the record
  // fits neither layout, so it is corrupt, not merely ambiguous.
  auto r = detectCommonPrefsLayout(evA(295, tail(1, 64, 8, 64, 0)));
  EXPECT_EQ(r.layout, PrefsLayout::Unknown);
  EXPECT_EQ(r.reason, PrefsLayoutReason::ContentContradiction);
}

TEST(PathA, ContentWorksWithoutNvsMarker_nRF52Case) {
  // nRF52 has no cw_boot marker at all. Content must still decide, otherwise
  // every nRF52 node at the colliding length would fail closed.
  auto ev = evA(295, upstreamTail295(), /*marker=*/false, /*supported=*/false);
  auto r = detectCommonPrefsLayout(ev);
  EXPECT_EQ(r.layout, PrefsLayout::Upstream);
  EXPECT_EQ(r.reason, PrefsLayoutReason::ContentFieldRange);
}

TEST(PathA, DegenerateContentFallsBackToNvsMarker) {
  // Flood limits set <= 1: content cannot decide. On ESP32 the cw_boot marker
  // proves an Offband boot.
  auto ev = evA(295, degenerateTail295(), /*marker=*/true, /*supported=*/true);
  auto r = detectCommonPrefsLayout(ev);
  EXPECT_EQ(r.layout, PrefsLayout::Offband);
  EXPECT_EQ(r.reason, PrefsLayoutReason::NvsMarker);
}

TEST(PathA, DegenerateContentWithoutMarkerFailsClosed) {
  // No content signal, no marker -> refuse. This is the case that must NEVER
  // silently pick a table; picking "upstream" here was the withdrawn design.
  auto ev = evA(295, degenerateTail295(), /*marker=*/false, /*supported=*/true);
  auto r = detectCommonPrefsLayout(ev);
  EXPECT_EQ(r.layout, PrefsLayout::Unknown);
  EXPECT_EQ(r.reason, PrefsLayoutReason::AmbiguousLength);
}

TEST(PathA, DegenerateContentOnNrf52FailsClosed) {
  // Marker unsupported -> its absence proves nothing -> refuse.
  auto ev = evA(295, degenerateTail295(), /*marker=*/false, /*supported=*/false);
  auto r = detectCommonPrefsLayout(ev);
  EXPECT_EQ(r.layout, PrefsLayout::Unknown);
  EXPECT_EQ(r.reason, PrefsLayoutReason::AmbiguousLength);
}

TEST(PathA, MarkerAloneNeverOverridesAnUnambiguousLength) {
  // A stock-written 293-byte record on a device that ONCE ran Offband still
  // has cw_boot in NVS. Length is unambiguous, so the marker must not flip it.
  auto ev = evA(293, PrefsTail{}, /*marker=*/true, /*supported=*/true);
  auto r = detectCommonPrefsLayout(ev);
  EXPECT_EQ(r.layout, PrefsLayout::Upstream);
  EXPECT_EQ(r.reason, PrefsLayoutReason::LengthUniqueUpstream);
}

TEST(PathA, TruncatedRecordFailsClosed) {
  // A short write must never be read with either table.
  for (size_t len : {0u, 1u, 100u, 290u, 300u, 363u, 365u, 1024u}) {
    auto r = detectCommonPrefsLayout(evA(len, tail(1, 64, 8, 1, 0)));
    EXPECT_EQ(r.layout, PrefsLayout::Unknown) << "length " << len;
    EXPECT_EQ(r.reason, PrefsLayoutReason::UnknownLength) << "length " << len;
  }
}

TEST(PathA, NoInputEverYieldsASilentGuess) {
  // Sweep every length 0..500 AGAINST EVERY TAIL SHAPE -- the previous version
  // swept length only, with one hardcoded tail, and so proved nothing about
  // the content path. A verdict must always carry a reason that justifies it.
  const PrefsTail tails[] = {
    PrefsTail{},                    // no tail
    upstreamTail295(),              // decisive: upstream
    offbandTail295(),               // decisive: offband
    degenerateTail295(),            // inconclusive
    tail(1, 64, 8, 64, 0),          // contradictory
    tail(0, 0, 0, 0, 0),            // all zero
    tail(255, 255, 255, 255, 255),  // all max
  };
  for (size_t len = 0; len <= 500; ++len) {
    for (const auto& t : tails) {
      auto r = detectCommonPrefsLayout(evA(len, t));
      if (r.layout == PrefsLayout::Unknown) continue;
      // A positive verdict may only come from a reason that establishes one.
      EXPECT_TRUE(r.reason == PrefsLayoutReason::LengthUniqueOffband ||
                  r.reason == PrefsLayoutReason::LengthUniqueUpstream ||
                  r.reason == PrefsLayoutReason::ContentFieldRange ||
                  r.reason == PrefsLayoutReason::NvsMarker)
          << "length " << len << " resolved via a non-determining reason";
    }
  }
}

TEST(PathA, AllZeroTailAtCollisionFailsClosed) {
  // A wiped or never-written tail is boolean-plausible under both layouts.
  auto r = detectCommonPrefsLayout(evA(295, tail(0, 0, 0, 0, 0)));
  EXPECT_EQ(r.layout, PrefsLayout::Unknown);
  EXPECT_EQ(r.reason, PrefsLayoutReason::AmbiguousLength);
}

// ---------------------------------------------------- family identification --

TEST(Family, PathALengthsIdentifyAsCommon) {
  for (size_t len : {291u, 292u, 293u, 294u, 296u, 298u, 364u}) {
    auto id = identifyLegacyPrefs(evA(len));
    EXPECT_EQ(id.family, PrefsFamily::Common) << "length " << len;
  }
}

TEST(Family, PathBLengthsIdentifyAsCompanion) {
  for (size_t len : {137u, 138u, 147u}) {
    auto id = identifyLegacyPrefs(evA(len));
    EXPECT_EQ(id.family, PrefsFamily::Companion) << "length " << len;
  }
}

// REGRESSION -- the adversarial review's role-swap scenario. A device that ran
// as a repeater leaves a 294-byte /node_prefs; it is re-flashed as a companion.
// Dispatching on the RUNNING ROLE would hand that record to the companion
// reader, which previously accepted anything > 137 as Offband and would have
// read a repeater record with the companion table.
TEST(Family, RoleSwappedNodePrefsIsNotMisreadAsCompanion) {
  auto id = identifyLegacyPrefs(evA(294));
  EXPECT_EQ(id.family, PrefsFamily::Common)
      << "a 294-byte record is a CommonCLI record whatever role is running";

  // And the companion reader alone must refuse it outright.
  auto direct = detectCompanionPrefsLayout(evA(294));
  EXPECT_EQ(direct.layout, PrefsLayout::Unknown);
  EXPECT_EQ(direct.reason, PrefsLayoutReason::UnknownLength);
}

TEST(Family, UnknownLengthsIdentifyAsUnknownFamily) {
  for (size_t len : {0u, 100u, 136u, 200u, 290u, 400u}) {
    auto id = identifyLegacyPrefs(evA(len));
    EXPECT_EQ(id.family, PrefsFamily::Unknown) << "length " << len;
    EXPECT_EQ(id.layout, PrefsLayout::Unknown) << "length " << len;
  }
}

TEST(Family, IdentityAgreesWithThePerPathFunction) {
  for (size_t len : {291u, 293u, 294u, 364u}) {
    auto id = identifyLegacyPrefs(evA(len));
    auto direct = detectCommonPrefsLayout(evA(len));
    EXPECT_EQ(id.layout, direct.layout) << "length " << len;
    EXPECT_EQ(id.reason, direct.reason) << "length " << len;
  }
}

// ---------------------------------------------------------------- Path B ---

TEST(PathB, ReleasedLengths) {
  // 137 is written by upstream 1.15.0/1.16.0/1.17.0 AND by Offband <= v1.1.2.
  // Shared, but harmless: Offband had appended nothing yet, so the layouts are
  // byte-identical there and "upstream" is the correct table for either writer.
  auto up = detectCompanionPrefsLayout(evA(137));
  EXPECT_EQ(up.layout, PrefsLayout::Upstream);
  EXPECT_EQ(up.reason, PrefsLayoutReason::LengthUniqueUpstream);

  // 138 = Offband v1.2.0 (radio_fem_rxgain @137); 147 = v1.3.0.
  for (size_t len : {138u, 147u}) {
    auto r = detectCompanionPrefsLayout(evA(len));
    EXPECT_EQ(r.layout, PrefsLayout::Offband) << "length " << len;
    EXPECT_EQ(r.reason, PrefsLayoutReason::LengthUniqueOffband) << "length " << len;
  }
}

// REGRESSION -- an earlier version accepted any length > 137 as Offband, which
// would have swallowed a truncated Path-A record or a file with garbage
// appended and read it with the companion table.
TEST(PathB, UnreleasedOrCorruptLengthsAreRefused) {
  for (size_t len : {0u, 100u, 121u, 136u, 139u, 140u, 141u, 145u, 146u,
                     148u, 200u, 292u, 294u, 364u}) {
    auto r = detectCompanionPrefsLayout(evA(len));
    EXPECT_EQ(r.layout, PrefsLayout::Unknown) << "length " << len;
    EXPECT_EQ(r.reason, PrefsLayoutReason::UnknownLength) << "length " << len;
  }
}

// -------------------------------------------------------------- Reporting --

TEST(Reporting, EveryVerdictAndReasonHasALogString) {
  // The migration MUST log what it chose and why (SAFELANE 6). A missing
  // string would degrade that to "unknown".
  EXPECT_STREQ(toString(PrefsLayout::Offband),  "offband");
  EXPECT_STREQ(toString(PrefsLayout::Upstream), "upstream");
  EXPECT_STREQ(toString(PrefsLayout::Unknown),  "unknown");

  EXPECT_STREQ(toString(PrefsFamily::Common),    "common");
  EXPECT_STREQ(toString(PrefsFamily::Companion), "companion");
  EXPECT_STREQ(toString(PrefsFamily::Unknown),   "unknown");

  // Every reason must map to a distinct, non-placeholder string: the migration
  // log is the only record of why a device chose a table, and "invalid-reason"
  // appearing in the field would mean an enum value nobody handled.
  const PrefsLayoutReason all[] = {
    PrefsLayoutReason::LengthUniqueOffband,  PrefsLayoutReason::LengthUniqueUpstream,
    PrefsLayoutReason::ContentFieldRange,    PrefsLayoutReason::NvsMarker,
    PrefsLayoutReason::AmbiguousLength,      PrefsLayoutReason::UnknownLength,
    PrefsLayoutReason::TailUnavailable,      PrefsLayoutReason::ContentContradiction,
    PrefsLayoutReason::LengthTableOverlap,
  };
  for (auto r : all) {
    EXPECT_STRNE(toString(r), "invalid-reason") << "unhandled reason " << int(r);
  }
  EXPECT_STREQ(toString(PrefsLayoutReason::ContentFieldRange),    "content-field-range");
  EXPECT_STREQ(toString(PrefsLayoutReason::NvsMarker),            "nvs-marker-cw_boot");
  EXPECT_STREQ(toString(PrefsLayoutReason::AmbiguousLength),      "ambiguous-length");
  EXPECT_STREQ(toString(PrefsLayoutReason::TailUnavailable),      "tail-unavailable");
  EXPECT_STREQ(toString(PrefsLayoutReason::UnknownLength),        "unknown-length");
  EXPECT_STREQ(toString(PrefsLayoutReason::ContentContradiction), "content-contradiction");
  EXPECT_STREQ(toString(PrefsLayoutReason::LengthTableOverlap),   "length-table-overlap");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
