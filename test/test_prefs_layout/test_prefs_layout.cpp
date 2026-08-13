// test/test_prefs_layout/test_prefs_layout.cpp -- Offband #627
//
// Byte-exact fixtures for legacy-prefs layout detection. Covers every length
// real firmware can leave on disk -- which on Path B means every APPEND
// BOUNDARY, not merely the lengths that coincide with a release tag (#631) --
// plus the one colliding length, truncation, and the fail-closed paths.
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
  // Computed at every upstream tag (#665): 166 = 1.10.0, 170 = 1.11.0,
  // 290 = 1.12.0..1.14.1, 291 = 1.15.0, 293 = 1.16.0. 295 is the collision and
  // is covered separately -- it must NOT resolve on length alone.
  //
  // 290 spans four releases with two internal arrangements: 1.14.0 replaced a
  // zero pad at 121 with path_hash_mode + loop_detect, so an older record reads
  // those as 0 (their default) and byte 124 onward realigns exactly. 1.14.1
  // additionally kept rx_boosted_gain at byte 79, which 1.15.0 moved to 290, so
  // a 1.14.1 record loses that one setting to a short-read. Nothing is
  // mis-assigned, and it matches what upstream 1.17.0 does with the same file.
  for (size_t len : {166u, 170u, 290u, 291u, 293u}) {
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
  // A short write must never be read with either table. NOTE 290 is NOT here:
  // it is upstream 1.12.0-1.14.1's real record length (#665), and asserting it
  // corrupt is what kept those users failing closed.
  for (size_t len : {0u, 1u, 100u, 289u, 300u, 363u, 365u, 1024u}) {
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
  for (size_t len : {166u, 170u, 290u, 291u, 292u, 293u, 294u, 296u, 298u, 364u}) {
    auto id = identifyLegacyPrefs(evA(len));
    EXPECT_EQ(id.family, PrefsFamily::Common) << "length " << len;
  }
}

TEST(Family, PathBLengthsIdentifyAsCompanion) {
  for (size_t len : {84u, 85u, 91u, 92u, 93u, 140u, 141u, 143u, 148u, 150u}) {
    auto id = identifyLegacyPrefs(evA(len));
    EXPECT_EQ(id.family, PrefsFamily::Companion) << "length " << len;
  }
}

// The two families' length sets must stay disjoint or identifyLegacyPrefs()
// refuses with LengthTableOverlap. Adding Path B entries is the change most
// likely to break that, so assert it directly rather than trusting inspection.
TEST(Family, PathALengthsAndPathBLengthsStayDisjoint) {
  for (size_t len : {84u, 85u, 91u, 92u, 93u, 140u, 141u, 143u, 148u, 150u}) {
    auto id = identifyLegacyPrefs(evA(len));
    EXPECT_NE(id.reason, PrefsLayoutReason::LengthTableOverlap) << "length " << len;
    EXPECT_EQ(id.family, PrefsFamily::Companion) << "length " << len;
  }
  for (size_t len : {166u, 170u, 290u, 291u, 292u, 293u, 294u, 296u, 298u, 364u}) {
    auto id = identifyLegacyPrefs(evA(len));
    EXPECT_NE(id.reason, PrefsLayoutReason::LengthTableOverlap) << "length " << len;
    EXPECT_EQ(id.family, PrefsFamily::Common) << "length " << len;
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

// REGRESSION (#668 review) -- the CALLERS must gate on FAMILY, not just layout.
//
// RoleSwappedNodePrefsIsNotMisreadAsCompanion (above) asserts that
// detectCompanionPrefsLayout() refuses a 294-byte record. But NEITHER loader calls
// that function -- both call identifyLegacyPrefs(), which for 294 returns
// family=Common with a PERFECTLY VALID layout=Offband. A caller checking only
// `layout != Unknown` therefore accepted it and read a CommonCLI record with the
// companion offset table.
//
// These assertions pin the property the loaders now enforce: for a cross-family
// length the layout is NOT Unknown, so layout alone can never be a sufficient
// guard. If someone "simplifies" a loader back to a layout-only check, the
// comments here explain why that is wrong even though the tests still pass.
TEST(Family, LayoutAloneIsNotASufficientGuardForEitherLoader) {
  // Path A lengths: valid layout, but a COMPANION loader must refuse them.
  for (size_t len : {292u, 294u, 364u}) {
    auto id = identifyLegacyPrefs(evA(len));
    EXPECT_EQ(id.family, PrefsFamily::Common) << "length " << len;
    EXPECT_NE(id.layout, PrefsLayout::Unknown)
        << "length " << len << " -- layout is VALID here, which is exactly why a "
           "layout-only check in DataStore::loadPrefs was unsafe";
  }
  // Path B lengths: valid layout, but a COMMON loader must refuse them.
  for (size_t len : {141u, 143u, 148u, 150u}) {
    auto id = identifyLegacyPrefs(evA(len));
    EXPECT_EQ(id.family, PrefsFamily::Companion) << "length " << len;
    EXPECT_NE(id.layout, PrefsLayout::Unknown)
        << "length " << len << " -- layout is VALID here, which is exactly why a "
           "layout-only check in CommonCLI::loadPrefs was unsafe";
  }
}

TEST(Family, UnknownLengthsIdentifyAsUnknownFamily) {
  for (size_t len : {0u, 100u, 136u, 200u, 289u, 400u}) {
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

// Every upstream length, computed at each tag (#665). Path B is pure append
// from 1.10.0 onward -- zero fields move -- so each of these is a strict prefix
// of the current layout and short-reads the rest to defaults. That is exactly
// what upstream 1.17.0's own migration does; refusing them protects nobody.
TEST(PathB, EveryUpstreamLengthIsUpstream) {
  //  84 = 1.7.0..1.10.0   85 = 1.11.0   91 = 1.12.0/1.13.0
  //  92 = 1.14.0          93 = 1.14.1   140 = 1.15.0..1.17.0
  for (size_t len : {84u, 85u, 91u, 92u, 93u, 140u}) {
    auto r = detectCompanionPrefsLayout(evA(len));
    EXPECT_EQ(r.layout, PrefsLayout::Upstream) << "length " << len;
    EXPECT_EQ(r.reason, PrefsLayoutReason::LengthUniqueUpstream) << "length " << len;
  }
}

// EVERY Offband append boundary, computed at the commit that introduced it --
// not derived, and not limited to the boundaries that coincide with a tag. The
// on-disk length is written by whatever firmware last called savePrefs(), so a
// device that upgraded across appends without changing a setting still carries
// the older, shorter record.
TEST(PathB, EveryAppendBoundaryIsOffband) {
  //  141 radio_fem_rxgain (#298, v1.2.0)   143 caplog (#435)
  //  148 notify_scope + button_actions (#510/#509, ONE commit)
  //  150 ui_led/ui_display_mode (#542, v1.3.0)
  for (size_t len : {141u, 143u, 148u, 150u}) {
    auto r = detectCompanionPrefsLayout(evA(len));
    EXPECT_EQ(r.layout, PrefsLayout::Offband) << "length " << len;
    EXPECT_EQ(r.reason, PrefsLayoutReason::LengthUniqueOffband) << "length " << len;
  }
}

// REGRESSION (#631/#665) -- observed on the owner's paired companion, and the
// case that exposed the whole table as fiction.
//
// The board ran offband-v1.3.0-6-g5e15510 (which WRITES 150) while its
// /new_prefs was still 140 -- the shared base, last written before v1.2.0 and
// never re-saved. 140 should resolve as Upstream: Offband had appended nothing
// at that length, so the layouts are the same bytes.
//
// It refused because the table claimed upstream was 137. 137 is not a length
// any firmware has ever written; it came from reading DataStore.cpp's offset
// comments, which treat `uint32_t gps_interval` as one byte and are therefore
// wrong by 3 from that field on. EVERY Path B entry was off by the same 3.
TEST(PathB, Regression631_SharedBaseRecordFromAnOlderBuildThanTheOneRunning) {
  auto r = detectCompanionPrefsLayout(evA(140));
  EXPECT_EQ(r.layout, PrefsLayout::Upstream);
  EXPECT_EQ(r.reason, PrefsLayoutReason::LengthUniqueUpstream);

  auto id = identifyLegacyPrefs(evA(140));
  EXPECT_EQ(id.family, PrefsFamily::Companion);
  EXPECT_EQ(id.layout, PrefsLayout::Upstream);
}

// REGRESSION -- the lengths the OLD table claimed were real. None of these is
// written by any firmware, upstream or Offband; they are the off-by-3 artefacts.
// If one of them ever resolves again, the comment-derived table is back.
TEST(PathB, OffByThreeArtefactLengthsAreRefused) {
  for (size_t len : {137u, 138u, 145u, 147u}) {
    auto r = detectCompanionPrefsLayout(evA(len));
    EXPECT_EQ(r.layout, PrefsLayout::Unknown) << "length " << len;
    EXPECT_EQ(r.reason, PrefsLayoutReason::UnknownLength) << "length " << len;
  }
}

// An earlier version accepted any length > upstream's end as Offband, which
// would have swallowed a truncated Path-A record or a file with garbage
// appended and read it with the companion table.
//
// 142/144..147/149 stay refused deliberately: each would land MID-append
// (caplog is a pair; notify_scope + button_actions landed together; ui_led +
// ui_display are a pair), so no build ever wrote them.
TEST(PathB, NonBoundaryOrCorruptLengthsAreRefused) {
  for (size_t len : {0u, 83u, 86u, 90u, 94u, 121u, 136u, 139u, 142u, 144u,
                     146u, 149u, 151u, 200u, 292u, 294u, 364u}) {
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
