// #1070: native tests for the shared BLE disconnect-reason decode used by the
// `[ble] disconnect` line on both the ESP32 (NimBLE) and nRF52 (SoftDevice)
// interfaces. Pure inline logic -- no Arduino, no BLE stack.

#include <gtest/gtest.h>
#include <string.h>
#include "helpers/BleReasonStrings.h"

using namespace ble_reason;

TEST(BleReasonHci, ClientClosedIsRemote) {
  Decoded d = fromHci(0x13);
  EXPECT_TRUE(d.is_hci);
  EXPECT_STREQ("remote-user-terminated", d.name);
  EXPECT_EQ(BY_REMOTE, d.by);
}

TEST(BleReasonHci, WeDroppedItIsLocal) {
  Decoded d = fromHci(0x16);
  EXPECT_STREQ("local-host-terminated", d.name);
  EXPECT_EQ(BY_LOCAL, d.by);
}

TEST(BleReasonHci, LinkFailuresAreLink) {
  EXPECT_EQ(BY_LINK, fromHci(0x08).by);   // supervision timeout: range / sleep
  EXPECT_EQ(BY_LINK, fromHci(0x22).by);   // LL response timeout
  EXPECT_EQ(BY_LINK, fromHci(0x3D).by);   // MIC failure: stale bond / encryption
  EXPECT_EQ(BY_LINK, fromHci(0x3E).by);   // failed to establish
  EXPECT_STREQ("supervision-timeout", fromHci(0x08).name);
  EXPECT_STREQ("mic-failure", fromHci(0x3D).name);
}

TEST(BleReasonHci, UnlistedCodeIsUnknownButKeepsTheRawCode) {
  Decoded d = fromHci(0x99);
  EXPECT_TRUE(d.is_hci);
  EXPECT_EQ(0x99, d.hci);
  EXPECT_STREQ("unknown", d.name);
  EXPECT_EQ(BY_UNKNOWN, d.by);
}

TEST(BleReasonNimble, HciBaseOffsetIsStripped) {
  Decoded d = fromNimble(0x213);            // BLE_HS_ERR_HCI_BASE + 0x13
  EXPECT_TRUE(d.is_hci);
  EXPECT_EQ(0x13, d.hci);
  EXPECT_EQ(BY_REMOTE, d.by);
  EXPECT_EQ(BY_LOCAL, fromNimble(0x216).by);
  EXPECT_EQ(BY_LINK, fromNimble(0x208).by);
}

TEST(BleReasonNimble, BareHostErrorIsNotReadAsHci) {
  // NimBLE 8 is a host error (BLE_HS_*), NOT HCI 0x08 supervision timeout.
  Decoded d = fromNimble(8);
  EXPECT_FALSE(d.is_hci);
  EXPECT_STREQ("host-error", d.name);
  EXPECT_EQ(BY_UNKNOWN, d.by);
  EXPECT_FALSE(fromNimble(0x300).is_hci);   // just past the HCI range
  EXPECT_FALSE(fromNimble(-1).is_hci);
}

TEST(BleReasonBy, Names) {
  EXPECT_STREQ("remote", byName(BY_REMOTE));
  EXPECT_STREQ("local", byName(BY_LOCAL));
  EXPECT_STREQ("link", byName(BY_LINK));
  EXPECT_STREQ("unknown", byName(BY_UNKNOWN));
}

TEST(BleReasonPeer, SuffixIsLastTwoBytesMsbFirst) {
  // Little-endian storage: addr[0] is the last byte of AA:BB:CC:DD:B2:A1.
  const uint8_t addr[6] = {0xA1, 0xB2, 0xDD, 0xCC, 0xBB, 0xAA};
  char out[16];
  peerSuffix(addr, out, sizeof(out));
  EXPECT_STREQ("..B2:A1", out);
}

TEST(BleReasonPeer, NeverPrintsMoreThanTwoBytes) {
  const uint8_t addr[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  char out[32];
  peerSuffix(addr, out, sizeof(out));
  EXPECT_EQ(nullptr, strstr(out, "03"));
  EXPECT_EQ(7u, strlen(out));
}

TEST(BleReasonPeer, NullAndTinyBuffersAreSafe) {
  char out[16];
  peerSuffix(nullptr, out, sizeof(out));
  EXPECT_STREQ("..??:??", out);
  char tiny[3];
  const uint8_t addr[6] = {0xA1, 0xB2, 0, 0, 0, 0};
  peerSuffix(addr, tiny, sizeof(tiny));
  EXPECT_EQ(2u, strlen(tiny));              // truncated, still terminated
  peerSuffix(addr, nullptr, 8);             // no crash
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
