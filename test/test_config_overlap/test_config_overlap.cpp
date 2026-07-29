// Native regression test for the config-provider key-space overlap detector
// (#366). Proves that the silent first-provider-wins shadow — specifically the
// observer's `wifi.` prefix vs a repeater's `wifi.mode` (the #301 trap) — is
// detected at registration instead of silently swallowing the second provider.
//
// The provider table is a process-global that only accumulates (no reset API in
// production code, by design). So the whole scenario runs as ONE ordered test
// in its own binary: fresh process => all counters start at zero.

#include <gtest/gtest.h>
#include "helpers/config/ConfigDispatch.h"

using namespace offband::config;

// Stub set/get: overlap is decided from the declared manifest at registration,
// not from these, so they only need to be valid pointers.
static bool stubSet(const char*, const char*, char*, size_t) { return false; }
static bool stubGet(const char*, char*, size_t) { return false; }

TEST(ConfigOverlap, DetectsWifiPrefixShadowAtRegistration) {
  // Fresh process: nothing registered, no overlaps.
  EXPECT_EQ(0, providerCount());
  EXPECT_EQ(0, overlapWarningCount());

  // 1) First provider (observer-like), incl. the broad `wifi.` claim.
  static const char* const kObserver[] = {"mqtt.iata", "mqtt.broker.", "wifi."};
  ASSERT_TRUE(registerProvider(&stubSet, &stubGet, "observer", kObserver, 3));
  EXPECT_EQ(1, providerCount());
  EXPECT_EQ(0, overlapWarningCount());   // first provider: nothing to collide with

  // 2) A disjoint provider must NOT trip the detector (no false positive).
  static const char* const kDisjoint[] = {"display.", "gps."};
  ASSERT_TRUE(registerProvider(&stubSet, &stubGet, "other", kDisjoint, 2));
  EXPECT_EQ(2, providerCount());
  EXPECT_EQ(0, overlapWarningCount());

  // 3) The concrete #301 case: repeater declares `wifi.mode`, which the
  //    observer's `wifi.` prefix would silently shadow. Detector must fire.
  static const char* const kRepeater[] = {"wifi.mode", "cmd."};
  ASSERT_TRUE(registerProvider(&stubSet, &stubGet, "repeater", kRepeater, 2));
  EXPECT_EQ(3, providerCount());
  EXPECT_GT(overlapWarningCount(), 0);   // `wifi.mode` vs `wifi.` detected
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
