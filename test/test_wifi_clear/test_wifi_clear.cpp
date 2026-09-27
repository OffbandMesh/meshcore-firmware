// Native regression tests for #696 (under #689): the `wifi clear` command and
// the STA disconnect-reason surfacing that #692 needed.
//
// What is and isn't covered on host: the NVS writes in handleClearWifi sit
// behind #ifdef ARDUINO, so these tests pin the ARGUMENT CONTRACT and the
// REPLY STRINGS -- which is where the field-visible behaviour lives. The
// Preferences::remove() call itself is hardware-verified on the bench (#692),
// not here; claiming otherwise would be a test that proves less than it looks.

#include <gtest/gtest.h>
#include <cstring>
#include "helpers/config/WifiConfigProvider.h"
#include "helpers/config/ConfigDispatch.h"
#include "helpers/wifi_observer/WifiBootstrap.h"

using offband::handleClearWifi;
using offband::WifiBootstrap;

// ---------------------------------------------------------------------------
// wifi clear -- argument contract
// ---------------------------------------------------------------------------

TEST(WifiClear, BareAndPwdClearThePasswordOnly) {
  char reply[160];

  // Bare `wifi clear` arrives as the empty remainder from skipPrefix.
  ASSERT_TRUE(handleClearWifi(reply, sizeof(reply), ""));
  EXPECT_NE(nullptr, strstr(reply, "wifi.pwd cleared"));
  // Must NOT claim the SSID went with it -- the user asked for less.
  EXPECT_EQ(nullptr, strstr(reply, "ssid"));

  // Explicit "pwd" is the same path.
  ASSERT_TRUE(handleClearWifi(reply, sizeof(reply), "pwd"));
  EXPECT_NE(nullptr, strstr(reply, "wifi.pwd cleared"));
  EXPECT_EQ(nullptr, strstr(reply, "ssid"));

  // nullptr is reachable via the config wire path (configSet with no value)
  // and must behave as the safe default rather than crashing.
  ASSERT_TRUE(handleClearWifi(reply, sizeof(reply), nullptr));
  EXPECT_NE(nullptr, strstr(reply, "wifi.pwd cleared"));
}

TEST(WifiClear, AllClearsBoth) {
  char reply[160];
  ASSERT_TRUE(handleClearWifi(reply, sizeof(reply), "all"));
  EXPECT_NE(nullptr, strstr(reply, "ssid"));
  EXPECT_NE(nullptr, strstr(reply, "pwd"));
}

TEST(WifiClear, UnknownArgumentIsRejectedNotSilentlyTreatedAsPwd) {
  // The regression that matters: a typo must NOT quietly perform a partial
  // clear. `wifi clear everything` has to error, not drop the password.
  char reply[160];
  ASSERT_TRUE(handleClearWifi(reply, sizeof(reply), "everything"));
  EXPECT_EQ(0, strncmp(reply, "ERROR:", 6)) << "reply was: " << reply;
  EXPECT_NE(nullptr, strstr(reply, "wifi clear [pwd|all]"));
}

// ---------------------------------------------------------------------------
// Disconnect-reason naming -- the instrument #692 lacked
// ---------------------------------------------------------------------------

TEST(WifiReason, MapsTheCodesThatDiscriminateAFieldFailure) {
  // 201 vs 15 is the whole point: "never matched an AP" vs "AP rejected the
  // key". These two send the investigation in opposite directions.
  EXPECT_STREQ("NO_AP_FOUND", WifiBootstrap::disconnectReasonName(201));
  EXPECT_STREQ("4WAY_HANDSHAKE_TIMEOUT",
               WifiBootstrap::disconnectReasonName(15));
  EXPECT_STREQ("CONNECTION_FAIL", WifiBootstrap::disconnectReasonName(205));
  EXPECT_STREQ("AUTH_EXPIRE", WifiBootstrap::disconnectReasonName(2));
  EXPECT_STREQ("BEACON_TIMEOUT", WifiBootstrap::disconnectReasonName(200));
}

TEST(WifiReason, UnknownCodesReturnNullSoCallersPrintTheNumber) {
  // Deliberate: an unmapped code must not borrow a neighbouring label. The
  // callers fall back to printing the raw number, which stays truthful.
  EXPECT_EQ(nullptr, WifiBootstrap::disconnectReasonName(99));
  EXPECT_EQ(nullptr, WifiBootstrap::disconnectReasonName(255));
}

TEST(WifiReason, ZeroIsTheNoEventYetSentinel) {
  // WIFI_REASON_* has no zero member, so 0 unambiguously means "no disconnect
  // event has fired". Callers special-case it rather than printing "0(UNKNOWN)".
  EXPECT_EQ(nullptr, WifiBootstrap::disconnectReasonName(0));
}

// ---------------------------------------------------------------------------
// Dispatch routing -- the ordering regression the provider comment warns about
// ---------------------------------------------------------------------------

TEST(WifiClearDispatch, WifiDotClearRoutesToClearNotTheFieldSetter) {
  // WifiConfigProvider self-registers at static init, so the real dispatch
  // table is live here. `wifi.clear` MUST be matched before the generic
  // `wifi.` prefix: if that ordering is ever reversed, this key falls through
  // to handleSetWifiField and writes an NVS key literally named "clear"
  // instead of erasing the credentials -- a silent failure of the one command
  // that exists to rescue a stranded device (#692).
  char reply[160];

  ASSERT_TRUE(offband::config::dispatchSet("wifi.clear", "all",
                                           reply, sizeof(reply)));
  EXPECT_NE(nullptr, strstr(reply, "cleared"))
      << "wifi.clear was not routed to handleClearWifi; reply: " << reply;
  // The field-setter's ACK shape must NOT appear -- that is the regression.
  EXPECT_EQ(nullptr, strstr(reply, "chars entered"));

  // And the bad-argument contract survives the wire path too.
  ASSERT_TRUE(offband::config::dispatchSet("wifi.clear", "everything",
                                           reply, sizeof(reply)));
  EXPECT_EQ(0, strncmp(reply, "ERROR:", 6)) << "reply was: " << reply;
}

// ---------------------------------------------------------------------------
// #697 -- the setters' success replies, guarded against the checked-write change
//
// Scope, stated honestly: the new failure branches live inside #ifdef ARDUINO,
// so a host build cannot reach them and no test here proves a failed NVS write
// is reported. What these DO prove is that adding the checks did not disturb
// the success path -- the ACK strings a user actually sees, including the
// PSK-redacting one. Fault injection on the failure branch needs hardware.
// ---------------------------------------------------------------------------

TEST(WifiSetAck, SsidSuccessReplyUnchanged) {
  char reply[160];
  ASSERT_TRUE(offband::handleSetWifiField(reply, sizeof(reply), "ssid", "MyNet"));
  EXPECT_NE(nullptr, strstr(reply, "wifi.ssid = MyNet"));
  EXPECT_EQ(nullptr, strstr(reply, "ERROR"));
}

TEST(WifiSetAck, PwdSuccessReplyReportsLengthAndNeverThePsk) {
  char reply[160];
  ASSERT_TRUE(offband::handleSetWifiField(reply, sizeof(reply), "pwd", "hunter2"));
  EXPECT_NE(nullptr, strstr(reply, "7 chars entered"));
  // The PSK must never appear in any reply, success or failure.
  EXPECT_EQ(nullptr, strstr(reply, "hunter2"));
  EXPECT_EQ(nullptr, strstr(reply, "ERROR"));
}

TEST(WifiSetAck, EnabledSuccessReplyUnchanged) {
  char reply[160];
  ASSERT_TRUE(offband::handleSetWifiEnabled(reply, sizeof(reply), false));
  EXPECT_NE(nullptr, strstr(reply, "wifi.enabled = 0"));
  EXPECT_EQ(nullptr, strstr(reply, "ERROR"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
