// Epic D (#1428): what the core screens show, pinned before it is built (D1,
// #1495). Design of record: ObScreen.dc.html, the GUI spec, and the
// owner-approved gap mockups (epic-d-gap-mockups.html, 2026-10-16).

#include <gtest/gtest.h>

#include <string.h>

#include <string>

#include "helpers/TimeZones.h"
#include "helpers/gui/GuiViews.h"
#include "helpers/gui/InputMap.h"

using namespace offband::gui;

namespace {

const uint8_t kPublic[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
const uint8_t kHikers[16] = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
const uint8_t kWren[32] = {7};
const uint8_t kRiver[32] = {9};

// 2026-10-16 21:07:00 UTC, a Friday.
constexpr uint32_t kNow = 1792184820u;

uint32_t in(MsgStore& s, int t, uint32_t time, const char* sender, const char* text) {
  return s.addIncoming(t, time, sender, text, strlen(text), 2, 24);
}

}  // namespace

// ----------------------------------------------------------- D2 status bar

TEST(StatusBar, BatteryTone) {
  EXPECT_EQ(batteryTone(100), Tone::Ok);
  EXPECT_EQ(batteryTone(31), Tone::Ok);
  EXPECT_EQ(batteryTone(30), Tone::Warn);
  EXPECT_EQ(batteryTone(15), Tone::Warn);
  EXPECT_EQ(batteryTone(14), Tone::Bad);
  EXPECT_EQ(batteryTone(0), Tone::Bad);
}

TEST(StatusBar, BleIcon) {
  EXPECT_EQ(bleIcon(false, false), BleIcon::Hidden);
  EXPECT_EQ(bleIcon(false, true), BleIcon::Hidden);
  EXPECT_EQ(bleIcon(true, false), BleIcon::Advertising);
  EXPECT_EQ(bleIcon(true, true), BleIcon::Connected);
}

TEST(StatusBar, GpsIcon) {
  EXPECT_EQ(gpsIcon(false, FixType::Fix3D, 0), GpsIcon::Hidden);
  EXPECT_EQ(gpsIcon(true, FixType::Fix3D, 0), GpsIcon::Fix3D);
  EXPECT_EQ(gpsIcon(true, FixType::Fix2D, 0), GpsIcon::Fix2D);
  EXPECT_EQ(gpsIcon(true, FixType::None, 0), GpsIcon::Seeking);
  EXPECT_EQ(gpsIcon(true, FixType::None, kGpsTimeoutSecs - 1), GpsIcon::Seeking);
  EXPECT_EQ(gpsIcon(true, FixType::None, kGpsTimeoutSecs), GpsIcon::TimedOut);
}

TEST(StatusBar, GpsBlinksAtOneHertz) {
  EXPECT_TRUE(gpsBlinkLit(0));
  EXPECT_TRUE(gpsBlinkLit(499));
  EXPECT_FALSE(gpsBlinkLit(500));
  EXPECT_FALSE(gpsBlinkLit(999));
  EXPECT_TRUE(gpsBlinkLit(1000));
}

TEST(StatusBar, UnreadHiddenAtZero) {
  EXPECT_FALSE(showUnread(0));
  EXPECT_TRUE(showUnread(1));
}

// --------------------------------------------- D2 LoRa long-press popover

TEST(LoraPopover, SignalWords) {
  EXPECT_STREQ(signalWord(4), "Strong");
  EXPECT_STREQ(signalWord(3), "Good");
  EXPECT_STREQ(signalWord(2), "Marginal");
  EXPECT_STREQ(signalWord(1), "At the edge");
  EXPECT_STREQ(signalWord(0), "Below floor");
}

TEST(LoraPopover, Title) {
  char b[40];
  loraTitle(8, b, sizeof b);
  EXPECT_STREQ(b, "LoRa · last 8 packets");
  loraTitle(3, b, sizeof b);
  EXPECT_STREQ(b, "LoRa · last 3 packets");
  loraTitle(1, b, sizeof b);
  EXPECT_STREQ(b, "LoRa · last packet");
  loraTitle(0, b, sizeof b);
  EXPECT_STREQ(b, "No packets yet");
}

TEST(LoraPopover, Readings) {
  char b[24];
  formatSnr(6.5f, b, sizeof b);
  EXPECT_STREQ(b, "+6.5 dB");
  formatSnr(-3.0f, b, sizeof b);
  EXPECT_STREQ(b, "-3.0 dB");
  formatDbm(-92, b, sizeof b);
  EXPECT_STREQ(b, "-92 dBm");
  formatDbm(-118, b, sizeof b);
  EXPECT_STREQ(b, "-118 dBm");
}

// SF7's floor is -7.5 dB, so +6.5 dB of SNR is 14.0 dB of margin.
TEST(LoraPopover, MarginOverTheFloor) {
  char b[24];
  formatMargin(6.5f, 7, b, sizeof b);
  EXPECT_STREQ(b, "14.0 dB");
  formatMargin(-10.0f, 7, b, sizeof b);
  EXPECT_STREQ(b, "-2.5 dB");
  formatMargin(-15.0f, 12, b, sizeof b);   // SF12's floor is -20 dB
  EXPECT_STREQ(b, "5.0 dB");
}

// ----------------------------------------------------------------- D3 Home

TEST(Home, RadioCell) {
  char b[40];
  formatFreq(910.525f, b, sizeof b);
  EXPECT_STREQ(b, "910.525");
  formatFreq(869.618f, b, sizeof b);
  EXPECT_STREQ(b, "869.618");
  radioSub(7, 62.5f, 5, b, sizeof b);
  EXPECT_STREQ(b, "SF7 · BW62.5 · CR5");
  radioSub(11, 250.0f, 5, b, sizeof b);
  EXPECT_STREQ(b, "SF11 · BW250 · CR5");
  radioSub(12, 7.8f, 8, b, sizeof b);
  EXPECT_STREQ(b, "SF12 · BW7.8 · CR8");
}

TEST(Home, HeardCell) {
  char b[40];
  heardSub(3, 1, 8, b, sizeof b);
  EXPECT_STREQ(b, "3 rpt · 1 room · 8 chat");
}

TEST(Home, GpsCell) {
  EXPECT_STREQ(gpsValue(false, FixType::Fix3D), "Off");
  EXPECT_STREQ(gpsValue(true, FixType::None), "No fix");
  EXPECT_STREQ(gpsValue(true, FixType::Fix2D), "2D");
  EXPECT_STREQ(gpsValue(true, FixType::Fix3D), "3D");
  char b[40];
  gpsSub(9, true, b, sizeof b);
  EXPECT_STREQ(b, "9 sats · clock synced");
  gpsSub(1, false, b, sizeof b);
  EXPECT_STREQ(b, "1 sat");
  gpsSub(0, false, b, sizeof b);
  EXPECT_STREQ(b, "0 sats");
}

TEST(Home, Uptime) {
  char b[24];
  formatUptime(0, b, sizeof b);
  EXPECT_STREQ(b, "0m");
  formatUptime(42 * 60 + 59, b, sizeof b);
  EXPECT_STREQ(b, "42m");
  formatUptime(5 * 3600 + 12 * 60, b, sizeof b);
  EXPECT_STREQ(b, "5h 12m");
  formatUptime(3 * 86400 + 4 * 3600 + 59 * 60, b, sizeof b);
  EXPECT_STREQ(b, "3d 4h");
}

TEST(Home, BatteryCell) {
  char b[40];
  batterySub(4020, 3 * 86400 + 4 * 3600, b, sizeof b);
  EXPECT_STREQ(b, "4.02 V · 3d 4h up");
  batterySub(3605, 600, b, sizeof b);
  EXPECT_STREQ(b, "3.61 V · 10m up");
}

TEST(Home, AirtimeCell) {
  char b[16];
  formatAirtime(12, b, sizeof b);
  EXPECT_STREQ(b, "1.2%");
  formatAirtime(0, b, sizeof b);
  EXPECT_STREQ(b, "0.0%");
  formatAirtime(1000, b, sizeof b);
  EXPECT_STREQ(b, "100.0%");
}

TEST(Home, UnreadCellNamesNewestFirst) {
  MsgStore s;
  ASSERT_TRUE(s.begin(50, 8));
  char b[48];
  unreadSub(s, b, sizeof b);
  EXPECT_STREQ(b, "");

  const int wren = s.thread(ThreadKind::Direct, kWren, 32, "Wren");
  const int pub = s.thread(ThreadKind::Channel, kPublic, 16, "#Public");
  const int hik = s.thread(ThreadKind::Channel, kHikers, 16, "#Hikers");
  in(s, wren, kNow - 300, "", "on my way");
  in(s, pub, kNow - 60, "bob", "hello");
  unreadSub(s, b, sizeof b);
  EXPECT_STREQ(b, "#Public, Wren");

  // Newest by arrival, not by the sender's timestamp: a sender's clock can
  // be hours off, so this older-stamped message is still the latest news.
  in(s, hik, kNow - 600, "amy", "trail closed");
  unreadSub(s, b, sizeof b);
  EXPECT_STREQ(b, "#Hikers, #Public +1");

  s.markRead(pub);
  unreadSub(s, b, sizeof b);
  EXPECT_STREQ(b, "#Hikers, Wren");
}

TEST(Home, AdvertButtonFeedback) {
  AdvertButton a;
  EXPECT_EQ(a.state(0), AdvertButton::State::Idle);
  EXPECT_TRUE(a.press(1000));
  a.result(true, 1010);
  EXPECT_EQ(a.state(1010), AdvertButton::State::Sent);
  EXPECT_FALSE(a.press(1500));   // ignored while the feedback shows
  EXPECT_EQ(a.state(1010 + kAdvertFeedbackMs - 1), AdvertButton::State::Sent);
  EXPECT_EQ(a.state(1010 + kAdvertFeedbackMs), AdvertButton::State::Idle);
  EXPECT_TRUE(a.press(1010 + kAdvertFeedbackMs));
  a.result(false, 4000);
  EXPECT_EQ(a.state(4000), AdvertButton::State::NotSent);
  EXPECT_EQ(a.state(4000 + kAdvertFeedbackMs), AdvertButton::State::Idle);
}

// A second press before the first send reports back is ignored too.
TEST(Home, AdvertButtonIgnoresPressWhileSending) {
  AdvertButton a;
  EXPECT_TRUE(a.press(100));
  EXPECT_FALSE(a.press(150));
}

// ----------------------------------------------------------------- D3 Lock

TEST(Lock, Clock) {
  char b[16];
  formatClock(offband::tz::kUtc, kNow, b, sizeof b);
  EXPECT_STREQ(b, "21:07");
  formatClock(offband::tz::kEastern, kNow, b, sizeof b);   // EDT, UTC-4
  EXPECT_STREQ(b, "17:07");
  formatClock(offband::tz::kUtc, 1000, b, sizeof b);
  EXPECT_STREQ(b, "--:--");
}

TEST(Lock, Date) {
  char b[24];
  formatDate(offband::tz::kUtc, kNow, b, sizeof b);
  EXPECT_STREQ(b, "Fri 16 Oct");
  formatDate(offband::tz::kUtc, 1792108800u - 1, b, sizeof b);   // 23:59:59 the day before
  EXPECT_STREQ(b, "Thu 15 Oct");
  formatDate(offband::tz::kUtc, 1000, b, sizeof b);
  EXPECT_STREQ(b, "");
}

TEST(Lock, NewestUnreadPerThread) {
  MsgStore s;
  ASSERT_TRUE(s.begin(50, 8));
  uint32_t seqs[2];
  uint32_t more = 99;
  EXPECT_EQ(lockItems(s, seqs, 2, &more), 0u);
  EXPECT_EQ(more, 0u);

  const int wren = s.thread(ThreadKind::Direct, kWren, 32, "Wren");
  const int pub = s.thread(ThreadKind::Channel, kPublic, 16, "#Public");
  in(s, pub, kNow - 900, "bob", "first");
  const uint32_t w = in(s, wren, kNow - 300, "", "parking is full");
  EXPECT_EQ(lockItems(s, seqs, 2, &more), 2u);   // one per thread
  EXPECT_EQ(seqs[0], w);
  EXPECT_EQ(more, 0u);

  const uint32_t p = in(s, pub, kNow - 60, "bob", "second");
  EXPECT_EQ(lockItems(s, seqs, 2, &more), 2u);
  EXPECT_EQ(seqs[0], p);   // the newest of #Public, not "first"
  EXPECT_EQ(seqs[1], w);
  EXPECT_EQ(more, 0u);     // "first" is in #Public's item ("2 new")

  EXPECT_EQ(lockItems(s, seqs, 1, &more), 1u);
  EXPECT_EQ(seqs[0], p);
  EXPECT_EQ(more, 1u);     // Wren's thread isn't shown

  s.markRead(pub);
  EXPECT_EQ(lockItems(s, seqs, 2, &more), 1u);
  EXPECT_EQ(seqs[0], w);
  EXPECT_EQ(more, 0u);
}

// --------------------------------------------- D4 Chats and Conversation

TEST(Chats, UnreadFirstThenLatest) {
  MsgStore s;
  ASSERT_TRUE(s.begin(50, 8));
  uint16_t order[8];
  EXPECT_EQ(chatOrder(s, order, 8), 0u);

  const int quiet = s.thread(ThreadKind::Channel, kHikers, 16, "#Hikers");   // no messages
  const int pub = s.thread(ThreadKind::Channel, kPublic, 16, "#Public");
  const int wren = s.thread(ThreadKind::Direct, kWren, 32, "Wren");
  const int river = s.thread(ThreadKind::Direct, kRiver, 32, "River");
  in(s, wren, kNow - 600, "", "older, unread");
  in(s, pub, kNow - 60, "bob", "newest, read");
  s.markRead(pub);
  in(s, river, kNow - 300, "", "newer, unread");

  ASSERT_EQ(chatOrder(s, order, 8), 4u);
  EXPECT_EQ(order[0], river);
  EXPECT_EQ(order[1], wren);
  EXPECT_EQ(order[2], pub);
  EXPECT_EQ(order[3], quiet);

  EXPECT_EQ(chatOrder(s, order, 2), 2u);   // stops at max
}

TEST(Chats, Preview) {
  MsgStore s;
  ASSERT_TRUE(s.begin(50, 8));
  const int pub = s.thread(ThreadKind::Channel, kPublic, 16, "#Public");
  const int wren = s.thread(ThreadKind::Direct, kWren, 32, "Wren");
  char b[64];
  chatPreview(s, pub, b, sizeof b);
  EXPECT_STREQ(b, "No messages yet");

  in(s, pub, kNow, "bob", "anyone on the ridge?");
  chatPreview(s, pub, b, sizeof b);
  EXPECT_STREQ(b, "bob: anyone on the ridge?");

  in(s, wren, kNow, "", "ten minutes out");
  chatPreview(s, wren, b, sizeof b);
  EXPECT_STREQ(b, "ten minutes out");

  const char* mine = "be there at 21:20";
  s.addOwn(wren, kNow + 5, mine, strlen(mine), Delivery::Sending);
  chatPreview(s, wren, b, sizeof b);
  EXPECT_STREQ(b, "You: be there at 21:20");

  char tiny[8];
  chatPreview(s, pub, tiny, sizeof tiny);   // cut to fit, still terminated
  EXPECT_EQ(strlen(tiny), 7u);
}

// A cut never splits a UTF-8 character: "abcd" + "é" in 6 bytes is "abcd".
TEST(Chats, PreviewCutsAtACharacter) {
  MsgStore s;
  ASSERT_TRUE(s.begin(50, 8));
  const int wren = s.thread(ThreadKind::Direct, kWren, 32, "Wren");
  in(s, wren, kNow, "", "abcd\xC3\xA9");
  char b[6];
  chatPreview(s, wren, b, sizeof b);
  EXPECT_STREQ(b, "abcd");
}

TEST(Views, StoreNotReadyIsEmpty) {
  MsgStore s;   // begin() never called
  char b[32] = "x";
  unreadSub(s, b, sizeof b);
  EXPECT_STREQ(b, "");
  uint32_t seqs[2];
  uint32_t more = 9;
  EXPECT_EQ(lockItems(s, seqs, 2, &more), 0u);
  EXPECT_EQ(more, 0u);
  uint16_t order[4];
  EXPECT_EQ(chatOrder(s, order, 4), 0u);
  chatPreview(s, 0, b, sizeof b);
  EXPECT_STREQ(b, "");
}

TEST(Views, ZeroSizedOutputIsLeftAlone) {
  char b[4] = "abc";
  metaLine("a", "b", "c", b, 0);
  formatUptime(60, b, 0);
  EXPECT_STREQ(b, "abc");
}

TEST(Views, MaxZeroReturnsNothing) {
  MsgStore s;
  ASSERT_TRUE(s.begin(50, 8));
  const int wren = s.thread(ThreadKind::Direct, kWren, 32, "Wren");
  in(s, wren, kNow, "", "hi");
  uint32_t seqs[1] = {77};
  uint32_t more = 0;
  EXPECT_EQ(lockItems(s, seqs, 0, &more), 0u);
  EXPECT_EQ(seqs[0], 77u);
  EXPECT_EQ(more, 1u);   // Wren's thread, not shown
  uint16_t order[1] = {77};
  EXPECT_EQ(chatOrder(s, order, 0), 0u);
  EXPECT_EQ(order[0], 77u);
}

// Unread counts outlive evicted messages (Epic C). A channel whose unread
// messages were all evicted shows no lock item but still counts in "+N".
TEST(Lock, EvictedUnreadStillCounts) {
  MsgStore s;
  ASSERT_TRUE(s.begin(4, 4));
  const int pub = s.thread(ThreadKind::Channel, kPublic, 16, "#Public");
  const int wren = s.thread(ThreadKind::Direct, kWren, 32, "Wren");
  in(s, pub, kNow - 100, "bob", "one");
  in(s, pub, kNow - 90, "bob", "two");
  uint32_t last = 0;
  for (int i = 0; i < 4; i++) last = in(s, wren, kNow - 50 + i, "", "dm");   // evicts both
  uint32_t ids[1];
  ASSERT_EQ(s.threadMessages(pub, ids, 1), 0u);
  uint32_t seqs[2];
  uint32_t more = 0;
  ASSERT_EQ(lockItems(s, seqs, 2, &more), 1u);
  EXPECT_EQ(seqs[0], last);
  EXPECT_EQ(more, 2u);   // #Public's 2, with nothing left to show
}

TEST(Lock, Labels) {
  Thread t = {};
  strcpy(t.name, "#Public");
  t.kind = uint8_t(ThreadKind::Channel);
  t.unread = 3;
  char b[48];
  lockLabel(t, b, sizeof b);
  EXPECT_STREQ(b, "#Public · 3 new");
  t.unread = 1;
  lockLabel(t, b, sizeof b);
  EXPECT_STREQ(b, "#Public · 1 new");
  strcpy(t.name, "Wren");
  t.kind = uint8_t(ThreadKind::Direct);
  lockLabel(t, b, sizeof b);
  EXPECT_STREQ(b, "Wren · direct");
  t.unread = 2;
  lockLabel(t, b, sizeof b);
  EXPECT_STREQ(b, "Wren · 2 new");
}

TEST(Lock, ItemText) {
  MsgRecord m = {};
  strcpy(m.sender, "Wren");
  memcpy(m.text, "Moving the node", 15);
  m.text_len = 15;
  char b[48];
  lockText(m, ThreadKind::Channel, b, sizeof b);
  EXPECT_STREQ(b, "Wren: Moving the node");
  lockText(m, ThreadKind::Direct, b, sizeof b);
  EXPECT_STREQ(b, "Moving the node");
}

TEST(Home, KeySummary) {
  uint8_t k[32] = {0xa3, 0xf1, 0x7e, 0x0c};
  k[30] = 0x9c;
  k[31] = 0x42;
  char b[24];
  keySummary(k, b, sizeof b);
  EXPECT_STREQ(b, "a3f1 7e0c … 9c42");
}

// The screens redraw a list only when the store's revision moves.
TEST(Chats, StoreRevisionMovesOnEveryChange) {
  MsgStore s;
  ASSERT_TRUE(s.begin(50, 8));
  uint32_t r = s.revision();
  const int wren = s.thread(ThreadKind::Direct, kWren, 32, "Wren");
  EXPECT_NE(s.revision(), r);
  r = s.revision();
  in(s, wren, kNow, "", "hi");
  EXPECT_NE(s.revision(), r);
  r = s.revision();
  s.markRead(wren);
  EXPECT_NE(s.revision(), r);
  r = s.revision();
  const uint32_t mine = s.addOwn(wren, kNow, "yo", 2, Delivery::Sending);
  r = s.revision();
  s.setDelivery(mine, Delivery::Delivered);
  EXPECT_NE(s.revision(), r);
  r = s.revision();
  s.threadAt(wren);   // a read changes nothing
  EXPECT_EQ(s.revision(), r);
}

TEST(Conversation, DeliveryMeta) {
  MsgRecord m = {};
  m.own = 1;
  char b[64];
  m.delivery = uint8_t(Delivery::Sending);
  deliveryMeta(m, false, 1, 3, b, sizeof b);
  EXPECT_STREQ(b, "sending…");
  deliveryMeta(m, false, 2, 3, b, sizeof b);
  EXPECT_STREQ(b, "sending… try 2/3");
  m.delivery = uint8_t(Delivery::Delivered);
  deliveryMeta(m, false, 0, 0, b, sizeof b);
  EXPECT_STREQ(b, "✓ delivered");
  m.delivery = uint8_t(Delivery::Failed);
  deliveryMeta(m, false, 0, 0, b, sizeof b);
  EXPECT_STREQ(b, "✕ not delivered · hold to resend");

  m.delivery = uint8_t(Delivery::None);
  m.echoes = 0;
  deliveryMeta(m, true, 0, 0, b, sizeof b);
  EXPECT_STREQ(b, "");
  m.echoes = 3;
  deliveryMeta(m, true, 0, 0, b, sizeof b);
  EXPECT_STREQ(b, "heard ×3");
}

TEST(Conversation, IncomingRoute) {
  char b[16];
  incomingMeta(0, b, sizeof b);
  EXPECT_STREQ(b, "direct");
  incomingMeta(0xFF, b, sizeof b);
  EXPECT_STREQ(b, "direct");
  incomingMeta(1, b, sizeof b);
  EXPECT_STREQ(b, "1 hop");
  incomingMeta(2, b, sizeof b);
  EXPECT_STREQ(b, "2 hops");
}

TEST(Conversation, MetaLineSkipsEmptyParts) {
  char b[64];
  metaLine("you", "21:01", "✓ delivered", b, sizeof b);
  EXPECT_STREQ(b, "you · 21:01 · ✓ delivered");
  metaLine("Wren", "20:58", "", b, sizeof b);
  EXPECT_STREQ(b, "Wren · 20:58");
  metaLine("", "20:58", nullptr, b, sizeof b);
  EXPECT_STREQ(b, "20:58");
}

// -------------------------------------------------------------- D5 Compose

TEST(Compose, StopsAtTheBudget) {
  ComposeBuffer c;
  c.start(5);
  EXPECT_EQ(c.budget(), 5u);
  for (char ch : std::string("abcd")) EXPECT_TRUE(c.insert(&ch, 1));
  EXPECT_FALSE(c.insert("\xC3\xA9", 2));   // é is 2 bytes; 1 left
  EXPECT_STREQ(c.text(), "abcd");
  EXPECT_TRUE(c.insert("e", 1));
  EXPECT_FALSE(c.insert("f", 1));
  EXPECT_EQ(c.length(), 5u);
}

TEST(Compose, BudgetNeverExceedsTheRecord) {
  ComposeBuffer c;
  c.start(500);
  EXPECT_EQ(c.budget(), kMsgTextMax);
}

TEST(Compose, BackspaceRemovesAWholeCharacter) {
  ComposeBuffer c;
  c.start(160);
  EXPECT_FALSE(c.backspace());
  EXPECT_TRUE(c.insert("a", 1));
  EXPECT_TRUE(c.insert("\xE2\x82\xAC", 3));   // €
  EXPECT_EQ(c.length(), 4u);
  EXPECT_TRUE(c.backspace());
  EXPECT_STREQ(c.text(), "a");
  EXPECT_TRUE(c.backspace());
  EXPECT_STREQ(c.text(), "");
}

TEST(Compose, RejectsMalformedCharacters) {
  ComposeBuffer c;
  c.start(160);
  EXPECT_FALSE(c.insert("\x80", 1));         // a lone continuation byte
  EXPECT_FALSE(c.insert("\xE2\x82", 2));     // cut short
  EXPECT_FALSE(c.insert("ab", 2));           // two characters
  EXPECT_FALSE(c.insert("", 0));
  EXPECT_EQ(c.length(), 0u);
}

TEST(Compose, CounterAndTone) {
  ComposeBuffer c;
  c.start(160);
  char b[16];
  c.counter(b, sizeof b);
  EXPECT_STREQ(b, "0/160");
  EXPECT_EQ(c.counterTone(), Tone::Dim);
  for (int i = 0; i < 149; i++) c.insert("x", 1);
  EXPECT_EQ(c.counterTone(), Tone::Dim);
  c.insert("x", 1);   // 150: the last 10 bytes
  EXPECT_EQ(c.counterTone(), Tone::Warn);
  c.counter(b, sizeof b);
  EXPECT_STREQ(b, "150/160");
}

// A channel's budget is 160 less "<name>: ", so amber starts 10 before it.
TEST(Compose, ChannelBudgetTone) {
  ComposeBuffer c;
  c.start(149);
  for (int i = 0; i < 138; i++) c.insert("x", 1);
  EXPECT_EQ(c.counterTone(), Tone::Dim);
  c.insert("x", 1);
  EXPECT_EQ(c.counterTone(), Tone::Warn);
}

TEST(Compose, ShiftOnceThenOff) {
  ShiftState s;
  EXPECT_EQ(s.state(), Shift::Off);
  s.tap(1000);
  EXPECT_EQ(s.state(), Shift::Once);
  s.typedLetter();
  EXPECT_EQ(s.state(), Shift::Off);
}

TEST(Compose, ShiftDoubleTapLocks) {
  ShiftState s;
  s.tap(1000);
  s.tap(1000 + kMultiClickMs - 1);
  EXPECT_EQ(s.state(), Shift::Lock);
  s.typedLetter();
  EXPECT_EQ(s.state(), Shift::Lock);
  s.tap(5000);
  EXPECT_EQ(s.state(), Shift::Off);
}

TEST(Compose, ShiftSlowSecondTapClears) {
  ShiftState s;
  s.tap(1000);
  s.tap(1000 + kMultiClickMs + 1);
  EXPECT_EQ(s.state(), Shift::Off);
}

TEST(Compose, AutoCapAtSentenceStart) {
  ShiftState s;
  s.autoCap("", 0);
  EXPECT_EQ(s.state(), Shift::Once);
  s.autoCap("Hi", 2);
  EXPECT_EQ(s.state(), Shift::Off);
  s.autoCap("Hi. ", 4);
  EXPECT_EQ(s.state(), Shift::Once);
  s.autoCap("Hi.", 3);
  EXPECT_EQ(s.state(), Shift::Off);
  s.autoCap("Ok? ", 4);
  EXPECT_EQ(s.state(), Shift::Once);
  s.autoCap("Go! ", 4);
  EXPECT_EQ(s.state(), Shift::Once);
}

TEST(Compose, AutoCapLeavesCapsLock) {
  ShiftState s;
  s.tap(1000);
  s.tap(1100);
  s.autoCap("HI", 2);
  EXPECT_EQ(s.state(), Shift::Lock);
}

TEST(Compose, SymbolsReturnToLettersAfterASpace) {
  PageState p;
  EXPECT_EQ(p.page(), KeyPage::Letters);
  p.select(KeyPage::Symbols);
  p.typed(' ');   // a space alone doesn't leave
  EXPECT_EQ(p.page(), KeyPage::Symbols);
  p.typed('@');
  EXPECT_EQ(p.page(), KeyPage::Symbols);
  p.typed(' ');
  EXPECT_EQ(p.page(), KeyPage::Letters);
  p.select(KeyPage::Symbols2);
  p.typed('~');
  p.typed(' ');
  EXPECT_EQ(p.page(), KeyPage::Letters);
}

TEST(Compose, LettersLayout) {
  KeyRect k[40];
  const size_t n = lettersLayout(240, 100, k, 40);
  ASSERT_EQ(n, 31u);   // 10 + 9 + (shift, 7, backspace) + (?123, space, Send)
  EXPECT_STREQ(k[0].label, "q");
  EXPECT_STREQ(k[9].label, "p");
  EXPECT_STREQ(k[10].label, "a");
  EXPECT_STREQ(k[19].label, "⇧");
  EXPECT_STREQ(k[27].label, "⌫");
  EXPECT_STREQ(k[28].label, "?123");
  EXPECT_STREQ(k[29].label, "space");
  EXPECT_STREQ(k[30].label, "Send");
  for (size_t i = 0; i < n; i++) {
    EXPECT_EQ(k[i].h, 32) << k[i].label;
    EXPECT_GE(k[i].w, 20) << k[i].label;
    EXPECT_GE(k[i].x, 0) << k[i].label;
    EXPECT_LE(k[i].x + k[i].w, 240) << k[i].label;
    EXPECT_GE(k[i].y, 100) << k[i].label;
    for (size_t j = i + 1; j < n; j++) {
      const bool apart = k[i].x + k[i].w <= k[j].x || k[j].x + k[j].w <= k[i].x ||
                         k[i].y + k[i].h <= k[j].y || k[j].y + k[j].h <= k[i].y;
      EXPECT_TRUE(apart) << k[i].label << " overlaps " << k[j].label;
    }
  }
  EXPECT_EQ(lettersLayout(240, 100, k, 10), 10u);   // stops at max
}

TEST(Compose, TouchSnapsToTheNearestKey) {
  KeyRect k[40];
  const size_t n = lettersLayout(240, 100, k, 40);
  ASSERT_EQ(n, 31u);
  const KeyRect& q = k[0];
  const KeyRect& w = k[1];
  // Dead centre of w.
  EXPECT_EQ(nearestKey(k, n, w.x + w.w / 2, w.y + w.h / 2), 1);
  // In the gap between q and w, nearer w.
  const int16_t gap_x = q.x + q.w + (w.x - (q.x + q.w)) / 2 + 1;
  EXPECT_EQ(nearestKey(k, n, gap_x + 1, q.y + q.h / 2), 1);
  // Above the keyboard snaps to the top row; below it, to the bottom row.
  EXPECT_EQ(nearestKey(k, n, q.x + q.w / 2, 90), 0);
  const KeyRect& send = k[30];
  EXPECT_EQ(nearestKey(k, n, send.x + send.w / 2, 330), 30);
  EXPECT_EQ(nearestKey(k, 0, 10, 10), -1);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
