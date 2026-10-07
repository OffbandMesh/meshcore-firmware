// Native unit tests for the canonical Offband version string (#1391).
// Exercises the pure core offbandVersionStringFrom() directly, so each case can set a
// different (stock, offband-version, sha, build-tag) combination without build macros.
// Contract (owner-agreed 2026-10-07): OB-first `<ob>-<stock>`; the SHA stands in for the
// tag on a tagless build; it NEVER silently collapses to a bare stock version.

#include <gtest/gtest.h>
#include <cstring>
#include "helpers/OffbandVersion.h"

namespace {

// A tagged release build: offband-v* describe -> OB tag core, stock core, OB-first.
TEST(OffbandVersion, TaggedBuildIsObFirst) {
  EXPECT_STREQ("1.5.0-1.17.0",
    offbandVersionStringFrom("v1.17.0", "offband-v1.5.0-beta7-6-g1ec09a1-dirty", "1ec09a1", nullptr));
}

// Tagless build: `git describe --match offband-v*` found no tag, so OFFBAND_VERSION is a
// bare SHA (no "offband-v" substring). Must carry the SHA, NOT drop to bare stock. This
// is the "mostly shows stockver" regression the fix targets.
TEST(OffbandVersion, TaglessUsesShaNotBareStock) {
  EXPECT_STREQ("1ec09a1-1.17.0",
    offbandVersionStringFrom("v1.17.0", "1ec09a1", "1ec09a1", nullptr));
}

// OFFBAND_VERSION not injected at all, but a SHA is: still OB-first with the SHA.
TEST(OffbandVersion, NullObVersionUsesSha) {
  EXPECT_STREQ("1ec09a1-1.17.0",
    offbandVersionStringFrom("v1.17.0", nullptr, "1ec09a1", nullptr));
}

// #222 build tag (e.g. diag) self-identifies, appended after the cores.
TEST(OffbandVersion, BuildTagSuffix) {
  EXPECT_STREQ("1.5.0-1.17.0-diag",
    offbandVersionStringFrom("v1.17.0", "offband-v1.5.0", "1ec09a1", "diag"));
}

// A stock -rc/-dev suffix is trimmed to the core.
TEST(OffbandVersion, StockSuffixTrimmed) {
  EXPECT_STREQ("1.5.0-1.17.0",
    offbandVersionStringFrom("v1.17.0-rc1", "offband-v1.5.0", "1ec09a1", nullptr));
}

// Pure upstream build with no Offband identity at all -> stock CORE (leading 'v' and any
// -suffix trimmed), consistent with the OB paths; never a raw string (#1391 Gemini).
TEST(OffbandVersion, PureUpstreamStockCore) {
  EXPECT_STREQ("1.17.0",
    offbandVersionStringFrom("v1.17.0", nullptr, nullptr, nullptr));
}

// A messy pure-upstream FIRMWARE_VERSION is still reduced to its core, not shown raw.
TEST(OffbandVersion, PureUpstreamMessySuffixTrimmed) {
  EXPECT_STREQ("1.17.0",
    offbandVersionStringFrom("v1.17.0-rc1-needs-testing", nullptr, nullptr, nullptr));
}

// No tag and an empty SHA -> stock core (no spurious "-" prefix).
TEST(OffbandVersion, NoTagEmptyShaStockCore) {
  EXPECT_STREQ("1.17.0",
    offbandVersionStringFrom("v1.17.0", "nope", "", nullptr));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
