// Native tests for the station Wi-Fi event line (#2232,
// src/helpers/wifi_telemetry/WifiEventLog.h). Pure logic, no Arduino.

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "helpers/wifi_telemetry/WifiEventLog.h"

using namespace offband;

static std::string fmt(WifiEv ev, uint32_t now, const WifiEventTrack& t, uint8_t reason = 0, int rssi = 0) {
  char b[200];
  formatWifiEvent(b, sizeof(b), ev, now, t, reason, rssi);
  return b;
}

TEST(WifiEventLog, StationLeavingIsNamed) {
  // The router's reason for every R2-SAW leave: the station left on its own.
  EXPECT_STREQ("ASSOC_LEAVE", wifiReasonName(8));
  EXPECT_STREQ("AUTH_LEAVE", wifiReasonName(3));
  EXPECT_STREQ("4WAY_HANDSHAKE_TIMEOUT", wifiReasonName(15));
  EXPECT_STREQ("BEACON_TIMEOUT", wifiReasonName(200));
}

TEST(WifiEventLog, UnknownReasonPrintsNumberOnly) {
  EXPECT_EQ(nullptr, wifiReasonName(99));
  WifiEventTrack t;
  const std::string s = fmt(WifiEv::Disconnected, 5000, t, 99);
  EXPECT_NE(std::string::npos, s.find(" reason=99 ")) << s;
  EXPECT_EQ(std::string::npos, s.find("reason=99(")) << s;
}

TEST(WifiEventLog, SubSecondSessionIsCountedShort) {
  WifiEventTrack t;
  t.begin_ms = 1000;
  wifiEventApply(t, WifiEv::StaConnected, 1500);
  wifiEventApply(t, WifiEv::GotIp, 1900);
  wifiEventApply(t, WifiEv::Disconnected, 2300);
  EXPECT_EQ(1u, t.joins);
  EXPECT_EQ(1u, t.short_sessions);
  const std::string s = fmt(WifiEv::Disconnected, 2300, t, 8);
  EXPECT_EQ("[WIFI] disconnected reason=8(ASSOC_LEAVE) since_begin=1300ms since_end=none "
            "since_assoc=800ms since_ip=400ms joins=1 short=1", s);
}

TEST(WifiEventLog, NormalBurstIsNotShort) {
  WifiEventTrack t;
  wifiEventApply(t, WifiEv::StaConnected, 10000);
  wifiEventApply(t, WifiEv::Disconnected, 16000);
  EXPECT_EQ(0u, t.short_sessions);
  // A disconnect exactly at the threshold is not short.
  wifiEventApply(t, WifiEv::StaConnected, 20000);
  wifiEventApply(t, WifiEv::Disconnected, 20000 + kShortSessionMs);
  EXPECT_EQ(0u, t.short_sessions);
  EXPECT_EQ(2u, t.joins);
}

TEST(WifiEventLog, DisconnectWithoutAssociationIsNotShort) {
  // A failed join never associated, so it has no session to be short.
  WifiEventTrack t;
  wifiEventApply(t, WifiEv::Disconnected, 3000);
  EXPECT_EQ(0u, t.short_sessions);
  EXPECT_NE(std::string::npos, fmt(WifiEv::Disconnected, 3000, t, 201).find("since_assoc=none"));
}

TEST(WifiEventLog, OurOwnTeardownShowsSinceEnd) {
  // A leave right after end() reads as ours; one with since_end=none or a large
  // since_end did not come from the teardown.
  WifiEventTrack t;
  t.end_ms = 7000;
  wifiEventApply(t, WifiEv::StaConnected, 1000);
  EXPECT_NE(std::string::npos, fmt(WifiEv::Disconnected, 7012, t, 8).find(" since_end=12ms "));
}

TEST(WifiEventLog, AssocCarriesRssi) {
  WifiEventTrack t;
  t.begin_ms = 100;
  wifiEventApply(t, WifiEv::StaConnected, 900);
  EXPECT_EQ("[WIFI] assoc rssi=-57 since_begin=800ms since_end=none joins=1 short=0",
            fmt(WifiEv::StaConnected, 900, t, 0, -57));
}

TEST(WifiEventLog, TruncatesSafelyInATinyBuffer) {
  WifiEventTrack t;
  char b[12];
  memset(b, 'x', sizeof(b));
  const int n = formatWifiEvent(b, sizeof(b), WifiEv::Disconnected, 5, t, 8, 0);
  EXPECT_GT(n, (int)sizeof(b));          // reports the full length it wanted
  EXPECT_EQ('\0', b[sizeof(b) - 1]);     // and stays terminated
  EXPECT_EQ(0, formatWifiEvent(nullptr, 10, WifiEv::GotIp, 0, t, 0, 0));
  EXPECT_EQ(0, formatWifiEvent(b, 0, WifiEv::GotIp, 0, t, 0, 0));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(WifiEventLog, EveryEventHasAName) {
  for (WifiEv e : {WifiEv::StaStart, WifiEv::StaConnected, WifiEv::GotIp, WifiEv::LostIp,
                   WifiEv::Disconnected, WifiEv::StaStop}) {
    EXPECT_STRNE("?", wifiEvName(e));
  }
}
