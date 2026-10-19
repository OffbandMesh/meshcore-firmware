// Epic B (#1426): the GUI data layer's pure logic, pinned before it is
// built. Each block names the task that implements it.

#include <gtest/gtest.h>

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "helpers/gui/ChunkedSave.h"
#include "helpers/gui/GuiCommands.h"
#include "helpers/gui/GuiGps.h"
#include "helpers/gui/GuiMetrics.h"
#include "helpers/gui/GuiPrefs.h"
#include "helpers/gui/GuiReplies.h"
#include "helpers/sensors/NmeaGsa.h"

using namespace offband::gui;

// ----------------------------------------------------------- B2 command API

// The ranges CMD_SET_RADIO_PARAMS has always enforced (MyMesh.cpp).
TEST(Commands, RadioParamsInRange) {
  EXPECT_EQ(checkRadioParams(910525, 250000, 11, 5), CmdResult::Ok);
  EXPECT_EQ(checkRadioParams(150000, 7000, 5, 5), CmdResult::Ok);
  EXPECT_EQ(checkRadioParams(2500000, 500000, 12, 8), CmdResult::Ok);
}

TEST(Commands, RadioParamsOutOfRange) {
  EXPECT_EQ(checkRadioParams(149999, 250000, 11, 5), CmdResult::BadArgs);
  EXPECT_EQ(checkRadioParams(2500001, 250000, 11, 5), CmdResult::BadArgs);
  EXPECT_EQ(checkRadioParams(910525, 6999, 11, 5), CmdResult::BadArgs);
  EXPECT_EQ(checkRadioParams(910525, 500001, 11, 5), CmdResult::BadArgs);
  EXPECT_EQ(checkRadioParams(910525, 250000, 4, 5), CmdResult::BadArgs);
  EXPECT_EQ(checkRadioParams(910525, 250000, 13, 5), CmdResult::BadArgs);
  EXPECT_EQ(checkRadioParams(910525, 250000, 11, 4), CmdResult::BadArgs);
  EXPECT_EQ(checkRadioParams(910525, 250000, 11, 9), CmdResult::BadArgs);
}

TEST(Commands, TxPower) {
  EXPECT_EQ(checkTxPower(-9, 22), CmdResult::Ok);
  EXPECT_EQ(checkTxPower(22, 22), CmdResult::Ok);
  EXPECT_EQ(checkTxPower(-10, 22), CmdResult::BadArgs);
  EXPECT_EQ(checkTxPower(23, 22), CmdResult::BadArgs);
}

TEST(Commands, MessageText) {
  EXPECT_EQ(checkMessageText(1, 160), CmdResult::Ok);
  EXPECT_EQ(checkMessageText(160, 160), CmdResult::Ok);
  EXPECT_EQ(checkMessageText(0, 160), CmdResult::BadArgs);
  EXPECT_EQ(checkMessageText(161, 160), CmdResult::BadArgs);
}

// ---------------------------------------------------------- B3 reply routing

namespace {
const uint8_t kPeerA[4] = {1, 2, 3, 4};
const uint8_t kPeerB[4] = {5, 6, 7, 8};
}  // namespace

TEST(Replies, KeptUntilTaken) {
  ReplyBox box;
  const uint8_t body[3] = {9, 8, 7};
  EXPECT_FALSE(box.waiting(ReplyKind::Status));
  box.post(ReplyKind::Status, kPeerA, 42, 1000, true, body, sizeof(body));
  EXPECT_TRUE(box.waiting(ReplyKind::Status));
  Reply r;
  ASSERT_TRUE(box.take(ReplyKind::Status, &r));
  EXPECT_EQ(r.kind, ReplyKind::Status);
  EXPECT_EQ(memcmp(r.peer, kPeerA, 4), 0);
  EXPECT_EQ(r.tag, 42u);
  EXPECT_EQ(r.at_ms, 1000u);
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(r.len, 3);
  EXPECT_EQ(memcmp(r.data, body, 3), 0);
  EXPECT_FALSE(box.take(ReplyKind::Status, &r));  // taken once
}

TEST(Replies, KindsAreIndependentAndLatestWins) {
  ReplyBox box;
  box.post(ReplyKind::Login, kPeerA, 0, 10, false, nullptr, 0);
  box.post(ReplyKind::Trace, kPeerA, 7, 20, true, nullptr, 0);
  box.post(ReplyKind::Login, kPeerB, 0, 30, true, nullptr, 0);
  Reply r;
  ASSERT_TRUE(box.take(ReplyKind::Login, &r));
  EXPECT_EQ(memcmp(r.peer, kPeerB, 4), 0);
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(box.waiting(ReplyKind::Trace));
  EXPECT_FALSE(box.waiting(ReplyKind::Path));
}

TEST(Replies, LongBodyIsCutToFit) {
  ReplyBox box;
  uint8_t big[300];
  for (size_t i = 0; i < sizeof(big); i++) big[i] = uint8_t(i);
  box.post(ReplyKind::Path, kPeerA, 1, 1, true, big, sizeof(big));
  Reply r;
  ASSERT_TRUE(box.take(ReplyKind::Path, &r));
  EXPECT_EQ(r.len, sizeof(r.data));
  EXPECT_EQ(r.data[159], 159);
}

// ----------------------------------------------------------------- B4 metrics

TEST(Metrics, SnrMedianOddAndEven) {
  SnrMedian m;
  EXPECT_TRUE(m.empty());
  EXPECT_FLOAT_EQ(m.median(), 0.0f);
  m.add(5.0f);
  m.add(-3.0f);
  m.add(10.0f);
  EXPECT_FLOAT_EQ(m.median(), 5.0f);
  m.add(0.0f);
  EXPECT_FLOAT_EQ(m.median(), 2.5f);  // (0 + 5) / 2
}

TEST(Metrics, SnrMedianKeepsOnlyTheLastEight) {
  SnrMedian m;
  for (int i = 0; i < 8; i++) m.add(-20.0f);
  for (int i = 0; i < 8; i++) m.add(float(i));  // 0..7 replace every -20
  EXPECT_FLOAT_EQ(m.median(), 3.5f);
}

TEST(Metrics, AirtimeCountsTheLastHour) {
  AirtimeWindow w;
  w.addTx(0, 1000);
  w.addTx(30 * 60000, 2000);
  EXPECT_EQ(w.lastHourMs(30 * 60000), 3000u);
  // At 60.5 min the first minute's bucket has aged out.
  EXPECT_EQ(w.lastHourMs(60 * 60000 + 30000), 2000u);
  // Well past an hour after the last send, nothing is left.
  EXPECT_EQ(w.lastHourMs(200 * 60000), 0u);
}

// millis() wraps after ~49.7 days; the window must not lose or invent
// airtime across it (Gemini, #1452).
TEST(Metrics, AirtimeAcrossMillisWrap) {
  AirtimeWindow w;
  w.addTx(UINT32_MAX - 10000, 3000);
  w.addTx(5000, 2000);
  EXPECT_EQ(w.lastHourMs(6000), 5000u);
  EXPECT_EQ(w.lastHourMs(5000 + 61u * 60000), 0u);
}

// A non-finite SNR from a driver glitch is dropped, not sorted in.
TEST(Metrics, SnrMedianIgnoresNonFinite) {
  SnrMedian m;
  m.add(4.0f);
  m.add(NAN);
  m.add(INFINITY);
  m.add(-INFINITY);
  EXPECT_FLOAT_EQ(m.median(), 4.0f);
}

TEST(Metrics, AirtimeDuty) {
  AirtimeWindow w;
  w.addTx(1000, 36000);  // 36 s of 3600 s = 1.0 %
  EXPECT_EQ(w.dutyPermille(2000), 10);
}

TEST(Metrics, HeardCountsByType) {
  HeardCounts h;
  h.add(4);
  h.add(4);
  h.add(5);
  h.add(99);  // out of range: ignored, not counted under some other type
  EXPECT_EQ(h.count(4), 2u);
  EXPECT_EQ(h.count(5), 1u);
  EXPECT_EQ(h.count(0), 0u);
  EXPECT_EQ(h.total(), 3u);
}

TEST(Metrics, LastAdvert) {
  LastAdvert a;
  EXPECT_FALSE(a.sent);
  a.record(5000, true);
  EXPECT_TRUE(a.sent);
  EXPECT_TRUE(a.flood);
  EXPECT_EQ(a.at_ms, 5000u);
}

namespace {
const uint8_t kKeyA[6] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6};
const uint8_t kKeyB[6] = {0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6};
}  // namespace

TEST(Metrics, ContactSignalAndUnread) {
  ContactStats s;
  ASSERT_TRUE(s.begin(350));
  EXPECT_EQ(s.find(kKeyA), nullptr);
  s.onHeard(kKeyA, 6.25f, -97);
  s.onDirectMessage(kKeyA);
  s.onDirectMessage(kKeyA);
  s.onDirectMessage(kKeyB);
  const ContactStat* a = s.find(kKeyA);
  ASSERT_NE(a, nullptr);
  EXPECT_TRUE(a->heard);
  EXPECT_FLOAT_EQ(a->snr_db, 6.25f);
  EXPECT_EQ(a->rssi, -97);
  EXPECT_EQ(a->unread, 2);
  const ContactStat* b = s.find(kKeyB);
  ASSERT_NE(b, nullptr);
  EXPECT_FALSE(b->heard);  // a DM arrived, but no signal was recorded for it
  EXPECT_EQ(s.totalUnread(), 3u);
  s.markRead(kKeyA);
  EXPECT_EQ(s.find(kKeyA)->unread, 0);
  EXPECT_EQ(s.totalUnread(), 1u);
}

TEST(Metrics, ContactForgetAndCapacity) {
  ContactStats s;
  ASSERT_TRUE(s.begin(1));
  s.onHeard(kKeyA, 1.0f, -100);
  s.onHeard(kKeyB, 2.0f, -90);  // table full: B isn't tracked, A is untouched
  EXPECT_EQ(s.size(), 1u);
  EXPECT_EQ(s.find(kKeyB), nullptr);
  s.forget(kKeyA);
  EXPECT_EQ(s.find(kKeyA), nullptr);
  EXPECT_EQ(s.size(), 0u);
  s.onHeard(kKeyB, 2.0f, -90);  // the freed slot is reused
  EXPECT_NE(s.find(kKeyB), nullptr);
}

// ---------------------------------------------------------------------- B5 GPS

TEST(Gps, FixType) {
  EXPECT_EQ(fixType(false, false), FixType::None);
  EXPECT_EQ(fixType(false, true), FixType::None);
  EXPECT_EQ(fixType(true, false), FixType::Fix2D);
  EXPECT_EQ(fixType(true, true), FixType::Fix3D);
}

TEST(Gps, TimeSinceFixLost) {
  FixTracker t;
  t.update(0, false);
  EXPECT_FALSE(t.everFixed());
  EXPECT_EQ(t.secsSinceLost(10000), 0u);  // never had one: nothing was lost
  t.update(1000, true);
  EXPECT_TRUE(t.hasFix());
  EXPECT_EQ(t.secsSinceLost(5000), 0u);
  t.update(6000, false);
  EXPECT_FALSE(t.hasFix());
  EXPECT_TRUE(t.everFixed());
  EXPECT_EQ(t.secsSinceLost(6000), 0u);
  EXPECT_EQ(t.secsSinceLost(71999), 65u);
  t.update(80000, false);  // still lost: the clock doesn't restart
  EXPECT_EQ(t.secsSinceLost(80000), 74u);
  t.update(90000, true);
  EXPECT_EQ(t.secsSinceLost(95000), 0u);
}

// GGA can't tell 2D from 3D and MicroNMEA's altitude flag sticks true after
// the first GGA, so the mode comes from GSA field 2 (Gemini, #1456).
TEST(Gps, GsaFixMode) {
  EXPECT_EQ(offband::parseGsaMode("$GNGSA,A,3,04,05,,,,,,,,,,,2.5,1.3,2.1*3A"), 3);
  EXPECT_EQ(offband::parseGsaMode("$GPGSA,A,2,04,05,09,,,,,,,,,,3.1,2.0,2.4*30"), 2);
  EXPECT_EQ(offband::parseGsaMode("$GPGSA,M,1,,,,,,,,,,,,,,,*12"), 1);
  EXPECT_EQ(offband::parseGsaMode("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"), 0);
  EXPECT_EQ(offband::parseGsaMode("$GPGSA,A,,04*30"), 0);    // empty mode
  EXPECT_EQ(offband::parseGsaMode("$GPGSA,A,4,04*30"), 0);   // out of range
  EXPECT_EQ(offband::parseGsaMode("$GPGSA,A,33,04*30"), 0);  // not one digit
  EXPECT_EQ(offband::parseGsaMode("$GPGSA"), 0);
  EXPECT_EQ(offband::parseGsaMode(""), 0);
  EXPECT_EQ(offband::parseGsaMode(nullptr), 0);
}

TEST(Gps, TimeSinceFixLostAcrossMillisWrap) {
  FixTracker t;
  t.update(UINT32_MAX - 30000, true);
  t.update(UINT32_MAX - 19999, false);  // lost 20 s before the wrap
  EXPECT_EQ(t.secsSinceLost(10000), 30u);
}

// --------------------------------------------------------------------- B6 prefs

namespace {

// A Stream that keeps what is written and reads it back, with real number
// formatting (the native mock's print() overloads return 0).
class RoundTripStream : public Stream {
  char _buf[4096];
  size_t _len = 0;
  size_t _pos = 0;

  size_t printf_(const char* fmt, ...) {
    char tmp[64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    for (int i = 0; i < n; i++) write(uint8_t(tmp[i]));
    return size_t(n);
  }

public:
  size_t write(uint8_t b) override {
    if (_len < sizeof(_buf)) _buf[_len++] = char(b);
    return 1;
  }
  size_t print(unsigned char v, int) override { return printf_("%u", unsigned(v)); }
  size_t print(int v, int) override { return printf_("%d", v); }
  size_t print(unsigned int v, int) override { return printf_("%u", v); }
  size_t print(long v, int) override { return printf_("%ld", v); }
  size_t print(unsigned long v, int) override { return printf_("%lu", v); }
  size_t print(long long v, int) override { return printf_("%lld", v); }
  size_t print(unsigned long long v, int) override { return printf_("%llu", v); }
  size_t print(double v, int p) override { return printf_("%.*f", p, v); }
  int available() override { return int(_len - _pos); }
  int read() override { return _pos < _len ? _buf[_pos++] : -1; }
  int peek() override { return _pos < _len ? _buf[_pos] : -1; }
  void rewind() { _pos = 0; }
  void setText(const char* s) {
    _len = strlen(s);
    memcpy(_buf, s, _len);
    _pos = 0;
  }
};

}  // namespace

TEST(Prefs, Defaults) {
  GuiPrefs p;
  EXPECT_EQ(p.notify_default, uint8_t(NotifyMode::All));
  for (uint8_t i = 0; i < kGuiMaxChannels; i++) EXPECT_EQ(p.channelMode(i), NotifyMode::All);
  for (uint8_t i = 0; i < kCannedCount; i++) EXPECT_STREQ(p.canned[i], "");
  EXPECT_EQ(p.theme, 0);
  EXPECT_EQ(p.accent, kDefaultAccentRgb);
  EXPECT_EQ(p.look, 0);
  EXPECT_EQ(p.brightness, 100);
  EXPECT_EQ(p.dim_secs, 30);
  EXPECT_EQ(p.rotation, 0);
}

TEST(Prefs, NewChannelTakesTheDefault) {
  GuiPrefs p;
  p.notify_default = uint8_t(NotifyMode::Mentions);
  p.setChannelMode(3, NotifyMode::Off);
  p.onChannelJoined(3);
  EXPECT_EQ(p.channelMode(3), NotifyMode::Mentions);
  p.setChannelMode(200, NotifyMode::Off);           // out of range: ignored
  EXPECT_EQ(p.channelMode(200), NotifyMode::All);   // out of range reads as All
}

TEST(Prefs, SanitizePullsValuesIntoRange) {
  GuiPrefs p;
  p.notify_default = 7;
  p.channel_notify[5] = 9;
  p.theme = 4;
  p.accent = 0xFF123456;
  p.look = 2;
  p.brightness = 0;
  p.dim_secs = 9999;
  p.rotation = 45;
  p.canned[0][kCannedLen - 1] = 'x';  // unterminated
  p.sanitize();
  EXPECT_EQ(p.notify_default, uint8_t(NotifyMode::All));
  EXPECT_EQ(p.channelMode(5), NotifyMode::All);
  EXPECT_EQ(p.theme, 0);
  EXPECT_EQ(p.accent, 0x123456u);
  EXPECT_EQ(p.look, 0);
  EXPECT_EQ(p.brightness, 10);
  EXPECT_EQ(p.dim_secs, 3600);
  EXPECT_EQ(p.rotation, 0);
  EXPECT_EQ(p.canned[0][kCannedLen - 1], '\0');
}

TEST(Prefs, RoundTrip) {
  GuiPrefs a;
  a.notify_default = uint8_t(NotifyMode::Off);
  a.setChannelMode(0, NotifyMode::Mentions);
  a.setChannelMode(39, NotifyMode::Off);
  strcpy(a.canned[0], "On my way");
  strcpy(a.canned[8], "Copy that, \"over\"");
  a.theme = 3;
  a.accent = 0x4EA1F2;
  a.look = 1;
  a.brightness = 55;
  a.dim_secs = 120;
  a.rotation = 180;

  RoundTripStream s;
  ASSERT_TRUE(a.saveSerial(s));
  s.rewind();
  GuiPrefs b;
  ASSERT_TRUE(b.loadSerial(s));
  EXPECT_EQ(b.notify_default, uint8_t(NotifyMode::Off));
  EXPECT_EQ(b.channelMode(0), NotifyMode::Mentions);
  EXPECT_EQ(b.channelMode(1), NotifyMode::All);
  EXPECT_EQ(b.channelMode(39), NotifyMode::Off);
  EXPECT_STREQ(b.canned[0], "On my way");
  EXPECT_STREQ(b.canned[8], "Copy that, \"over\"");
  EXPECT_EQ(b.theme, 3);
  EXPECT_EQ(b.accent, 0x4EA1F2u);
  EXPECT_EQ(b.look, 1);
  EXPECT_EQ(b.brightness, 55);
  EXPECT_EQ(b.dim_secs, 120);
  EXPECT_EQ(b.rotation, 180);
}

// Messages & alerts and Display (Epic G, G3): defaults as drawn, and kept.
TEST(Prefs, AlertsAndClockDefaultsAsDrawn) {
  GuiPrefs p;
  EXPECT_EQ(p.clock_12h, 0);
  EXPECT_EQ(p.notify_on, 1);
  EXPECT_EQ(p.notify_dm, 1);
  EXPECT_EQ(p.notify_channels, 1);
  EXPECT_EQ(p.notify_new_node, 0);
  EXPECT_EQ(p.clear_path_on_fail, 0);
  EXPECT_EQ(p.jump_to_unread, 0);
  EXPECT_EQ(p.wake_touch, 1);   // Power › Wake on touch (G5), on as drawn
}

TEST(Prefs, AlertsAndClockRoundTrip) {
  GuiPrefs a;
  a.clock_12h = 1;
  a.notify_on = 0;
  a.notify_dm = 0;
  a.notify_channels = 0;
  a.notify_new_node = 1;
  a.clear_path_on_fail = 1;
  a.jump_to_unread = 1;
  a.wake_touch = 0;
  RoundTripStream s;
  ASSERT_TRUE(a.saveSerial(s));
  s.rewind();
  GuiPrefs b;
  ASSERT_TRUE(b.loadSerial(s));
  EXPECT_EQ(b.clock_12h, 1);
  EXPECT_EQ(b.notify_on, 0);
  EXPECT_EQ(b.notify_dm, 0);
  EXPECT_EQ(b.notify_channels, 0);
  EXPECT_EQ(b.notify_new_node, 1);
  EXPECT_EQ(b.clear_path_on_fail, 1);
  EXPECT_EQ(b.jump_to_unread, 1);
  EXPECT_EQ(b.wake_touch, 0);
}

TEST(Prefs, AlertFlagsSanitizeToZeroOrOne) {
  GuiPrefs p;
  p.clock_12h = 7;
  p.notify_on = 2;
  p.jump_to_unread = 9;
  p.wake_touch = 5;
  p.sanitize();
  EXPECT_EQ(p.clock_12h, 1);
  EXPECT_EQ(p.notify_on, 1);
  EXPECT_EQ(p.jump_to_unread, 1);
  EXPECT_EQ(p.wake_touch, 1);
}

// Epic L (#1610): the one-time zone prompt's flag. Off on a new or older
// file, so the prompt can show; kept once set, so it never shows again.
TEST(Prefs, ZoneAskedDefaultsOffAndIsKept) {
  GuiPrefs p;
  EXPECT_EQ(p.zone_asked, 0);
  p.zone_asked = 1;
  RoundTripStream s;
  ASSERT_TRUE(p.saveSerial(s));
  s.rewind();
  GuiPrefs b;
  ASSERT_TRUE(b.loadSerial(s));
  EXPECT_EQ(b.zone_asked, 1);
}

TEST(Prefs, ZoneAskedSanitizesToZeroOrOne) {
  GuiPrefs p;
  p.zone_asked = 9;
  p.sanitize();
  EXPECT_EQ(p.zone_asked, 1);
}

// Epic M (#1661): Sensors › Units. °F unless set otherwise; kept; 0 or 1.
TEST(Prefs, TemperatureUnitDefaultsToFahrenheitAndIsKept) {
  GuiPrefs p;
  EXPECT_EQ(p.temp_f, 1);
  p.temp_f = 0;
  RoundTripStream s;
  ASSERT_TRUE(p.saveSerial(s));
  s.rewind();
  GuiPrefs b;
  ASSERT_TRUE(b.loadSerial(s));
  EXPECT_EQ(b.temp_f, 0);
  b.temp_f = 7;
  b.sanitize();
  EXPECT_EQ(b.temp_f, 1);
}

TEST(Prefs, MissingKeysKeepDefaults) {
  RoundTripStream s;
  s.setText("{theme:2}");  // ConfigSerializer keys are unquoted
  GuiPrefs p;
  ASSERT_TRUE(p.loadSerial(s));
  EXPECT_EQ(p.theme, 2);
  EXPECT_EQ(p.brightness, 100);
  EXPECT_EQ(p.dim_secs, 30);
  EXPECT_EQ(p.accent, kDefaultAccentRgb);
}

// ------------------------------------------------------------ B7 contacts save

TEST(ChunkedSave, HandsOutEveryRecordOnceInOrder) {
  ChunkedSave c;
  c.begin(350, 25);
  EXPECT_TRUE(c.active());
  size_t from, to, expect = 0, steps = 0;
  while (c.next(&from, &to)) {
    EXPECT_EQ(from, expect);
    EXPECT_LE(to - from, 25u);
    EXPECT_GT(to, from);
    expect = to;
    steps++;
  }
  EXPECT_EQ(expect, 350u);
  EXPECT_EQ(steps, 14u);
  EXPECT_FALSE(c.active());
}

TEST(ChunkedSave, EmptyListFinishesAtOnce) {
  ChunkedSave c;
  c.begin(0, 25);
  size_t from, to;
  EXPECT_FALSE(c.next(&from, &to));
  EXPECT_FALSE(c.active());
}

TEST(ChunkedSave, RestartBeginsAgainWithTheNewTotal) {
  ChunkedSave c;
  c.begin(100, 40);
  size_t from, to;
  ASSERT_TRUE(c.next(&from, &to));
  EXPECT_EQ(to, 40u);
  c.restart(99);  // a contact was deleted mid-save
  ASSERT_TRUE(c.next(&from, &to));
  EXPECT_EQ(from, 0u);
  size_t last = to;
  while (c.next(&from, &to)) last = to;
  EXPECT_EQ(last, 99u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
