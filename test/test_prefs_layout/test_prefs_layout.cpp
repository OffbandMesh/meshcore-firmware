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
  // byte 291 <= 1 (boolean) and byte 292 = 64 (flood limit) -> Offband order.
  auto r = detectCommonPrefsLayout(evA(295, offbandTail295()));
  EXPECT_EQ(r.layout, PrefsLayout::Offband);
  EXPECT_EQ(r.reason, PrefsLayoutReason::ContentFieldRange);
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
  // Exhaustive sweep: every length 0..500 either resolves for a documented
  // reason or returns Unknown. Nothing may resolve via UnknownLength.
  for (size_t len = 0; len <= 500; ++len) {
    auto r = detectCommonPrefsLayout(evA(len, tail(1, 64, 8, 1, 0)));
    if (r.layout != PrefsLayout::Unknown) {
      EXPECT_NE(r.reason, PrefsLayoutReason::UnknownLength) << "length " << len;
      EXPECT_NE(r.reason, PrefsLayoutReason::AmbiguousLength) << "length " << len;
    }
  }
}

// ---------------------------------------------------------------- Path B ---

TEST(PathB, UpstreamAndOffbandLengths) {
  auto up = detectCompanionPrefsLayout(evA(137));
  EXPECT_EQ(up.layout, PrefsLayout::Upstream);
  EXPECT_EQ(up.reason, PrefsLayoutReason::LengthUniqueUpstream);

  auto ob = detectCompanionPrefsLayout(evA(147));
  EXPECT_EQ(ob.layout, PrefsLayout::Offband);
  EXPECT_EQ(ob.reason, PrefsLayoutReason::LengthUniqueOffband);
}

TEST(PathB, AnythingBeyondUpstreamsEndIsOffband) {
  // Offband appends strictly after 137, so intermediate lengths are builds
  // carrying some but not all appended fields.
  for (size_t len : {138u, 140u, 141u, 145u, 146u, 200u}) {
    auto r = detectCompanionPrefsLayout(evA(len));
    EXPECT_EQ(r.layout, PrefsLayout::Offband) << "length " << len;
  }
}

TEST(PathB, ShorterThanUpstreamsEndIsUndetermined) {
  // Predates fields on both sides. Not a mis-assignment hazard (both readers
  // short-read to EOF and keep defaults) but still not a determination.
  for (size_t len : {0u, 100u, 121u, 136u}) {
    auto r = detectCompanionPrefsLayout(evA(len));
    EXPECT_EQ(r.layout, PrefsLayout::Unknown) << "length " << len;
  }
}

// -------------------------------------------------------------- Reporting --

TEST(Reporting, EveryVerdictAndReasonHasALogString) {
  // The migration MUST log what it chose and why (SAFELANE 6). A missing
  // string would degrade that to "unknown".
  EXPECT_STREQ(toString(PrefsLayout::Offband),  "offband");
  EXPECT_STREQ(toString(PrefsLayout::Upstream), "upstream");
  EXPECT_STREQ(toString(PrefsLayout::Unknown),  "unknown");

  EXPECT_STREQ(toString(PrefsLayoutReason::ContentFieldRange), "content-field-range");
  EXPECT_STREQ(toString(PrefsLayoutReason::NvsMarker),         "nvs-marker-cw_boot");
  EXPECT_STREQ(toString(PrefsLayoutReason::AmbiguousLength),   "ambiguous-length");
  EXPECT_STREQ(toString(PrefsLayoutReason::TailUnavailable),   "tail-unavailable");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
