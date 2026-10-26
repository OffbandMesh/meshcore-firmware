// #1830: native tests for the BLE trace's naming and change detection, which
// diag GUI builds use to log the NimBLE stack (OFFBAND_BLE_TRACE). Pure inline
// logic -- no Arduino, no BLE stack. SerialBLEInterface.cpp static_asserts the
// numbers against NimBLE's own defines on the device build.

#include <gtest/gtest.h>
#include <string.h>
#include "helpers/BleTrace.h"

using namespace ble_trace;

TEST(BleTraceEvents, TheEventsThatEndAdvertisingAreNamed) {
  EXPECT_STREQ("connect", gapEventName(0));
  EXPECT_STREQ("disconnect", gapEventName(1));
  EXPECT_STREQ("adv-complete", gapEventName(9));
  EXPECT_STREQ("term-failure", gapEventName(6));
  EXPECT_STREQ("link-estab", gapEventName(38));
}

TEST(BleTraceEvents, AnUnlistedEventIsOtherNotMislabeled) {
  EXPECT_STREQ("other", gapEventName(2));     // unused by NimBLE
  EXPECT_STREQ("other", gapEventName(7));     // discovery: central role only
  EXPECT_STREQ("other", gapEventName(255));
}

TEST(BleTraceEvents, PerFrameNotificationsAreNotLogged) {
  EXPECT_FALSE(logged(kNotifyTx));
  EXPECT_FALSE(logged(kNotifyRx));
}

TEST(BleTraceEvents, EveryOtherEventIsLogged) {
  for (int t = 0; t < 256; t++) {
    if (t == kNotifyTx || t == kNotifyRx) continue;
    EXPECT_TRUE(logged(uint8_t(t))) << "event " << t;
  }
}

TEST(BleTraceHostErrors, TheAdvertisingStopReasonsNimbleDocumentsAreNamed) {
  EXPECT_STREQ("ok", hostErrorName(0));          // ended by a connection
  EXPECT_STREQ("timeout", hostErrorName(13));    // its duration ran out
  EXPECT_STREQ("preempted", hostErrorName(29));  // host stopped it for identity setup
}

TEST(BleTraceHostErrors, TheFourHostResetReasonsAreNamed) {
  EXPECT_STREQ("os", hostErrorName(11));
  EXPECT_STREQ("controller", hostErrorName(12));
  EXPECT_STREQ("hci-timeout", hostErrorName(19));
  EXPECT_STREQ("not-synced", hostErrorName(22));
}

TEST(BleTraceHostErrors, AnUnlistedCodeIsGenericNotMislabeled) {
  EXPECT_STREQ("host-error", hostErrorName(1));
  EXPECT_STREQ("host-error", hostErrorName(-1));
  EXPECT_STREQ("host-error", hostErrorName(0x208));   // an HCI code: the caller decodes those
}

TEST(BleTraceTransition, TheFirstSampleIsReported) {
  Transition t;
  EXPECT_TRUE(t.update(true));
  Transition f;
  EXPECT_TRUE(f.update(false));
}

TEST(BleTraceTransition, OnlyChangesAreReportedAfterThat) {
  Transition t;
  t.update(true);
  EXPECT_FALSE(t.update(true));
  EXPECT_FALSE(t.update(true));
  EXPECT_TRUE(t.update(false));   // advertising stopped
  EXPECT_FALSE(t.update(false));
  EXPECT_TRUE(t.update(true));    // and started again
}

TEST(BleTraceEvents, TheTraceOwnRecordsAreNamedAndLogged) {
  EXPECT_STREQ("host-reset", gapEventName(kHostReset));
  EXPECT_STREQ("host-sync", gapEventName(kHostSync));
  EXPECT_TRUE(logged(kHostReset));
}

static Event ev(uint8_t type, int32_t status) {
  Event e = {};
  e.type = type;
  e.status = status;
  return e;
}

TEST(BleTraceRing, RecordsComeOutInTheOrderTheyWentIn) {
  EventRing<4> r;
  r.push(ev(kConnect, 1));
  r.push(ev(kDisconnect, 2));
  Event out;
  ASSERT_TRUE(r.pop(&out));
  EXPECT_EQ(kConnect, out.type);
  ASSERT_TRUE(r.pop(&out));
  EXPECT_EQ(kDisconnect, out.type);
  EXPECT_FALSE(r.pop(&out));
}

TEST(BleTraceRing, OrderHoldsAcrossTheWrap) {
  EventRing<3> r;
  Event out;
  for (int i = 0; i < 10; i++) {
    ASSERT_TRUE(r.push(ev(kMtu, i)));
    ASSERT_TRUE(r.push(ev(kMtu, i + 100)));
    ASSERT_TRUE(r.pop(&out));
    EXPECT_EQ(i, out.status);
    ASSERT_TRUE(r.pop(&out));
    EXPECT_EQ(i + 100, out.status);
  }
}

TEST(BleTraceRing, AFullRingDropsTheNewestAndCountsIt) {
  EventRing<2> r;
  EXPECT_TRUE(r.push(ev(kConnect, 1)));
  EXPECT_TRUE(r.push(ev(kConnect, 2)));
  EXPECT_FALSE(r.push(ev(kConnect, 3)));
  EXPECT_FALSE(r.push(ev(kConnect, 4)));
  EXPECT_EQ(2, r.takeDropped());
  EXPECT_EQ(0, r.takeDropped());   // reported once
  Event out;
  r.pop(&out);
  EXPECT_EQ(1, out.status);        // the oldest survived
}

TEST(BleTraceRing, TheDropCountSaturatesRatherThanWrapping) {
  EventRing<1> r;
  r.push(ev(kConnect, 0));
  for (int i = 0; i < 70000; i++) r.push(ev(kConnect, 0));
  EXPECT_EQ(UINT16_MAX, r.takeDropped());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
