// Native unit tests for the badge's message store (#1229): conversations, unread
// counts, the order the inbox shows them in, and which message goes when the pool is
// full.

#include <gtest/gtest.h>
#include <string>
#include "helpers/BadgeStore.h"

using offband::BadgeSend;
// 4 conversations, 6 messages, 16-byte text, 32-byte keys
using Store = offband::BadgeStore<4, 6, 16, 32>;

namespace {
struct Key {
  uint8_t b[32];
  explicit Key(uint8_t v) { memset(b, v, sizeof(b)); }
};

uint32_t incoming(Store& s, int c, const char* text, uint32_t time = 100) {
  return s.addIncoming(c, time, "", text, 0xFF, 0);
}
}  // namespace

TEST(BadgeStore, StartsEmpty) {
  Store s;
  uint8_t order[4];
  EXPECT_EQ(0, s.ordered(order, 4));
  EXPECT_EQ(0, s.totalUnread());
}

TEST(BadgeStore, AConversationIsItsKindAndKey) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "Public");
  ASSERT_GE(a, 0);
  EXPECT_EQ(a, s.convo(Store::Channel, Key(1).b, "Public"));
  const int b = s.convo(Store::Contact, Key(1).b, "Abend");   // same bytes, other kind
  EXPECT_NE(a, b);
  EXPECT_EQ(a, s.find(Store::Channel, Key(1).b));
  EXPECT_EQ(-1, s.find(Store::Channel, Key(2).b));
}

TEST(BadgeStore, LookingAConversationUpRefreshesItsName) {
  Store s;
  const int a = s.convo(Store::Contact, Key(1).b, "abend");
  s.convo(Store::Contact, Key(1).b, "Abend");
  EXPECT_STREQ("Abend", s.convoAt(a)->name);
}

TEST(BadgeStore, IncomingCountsAsUnreadUntilRead) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "Public");
  incoming(s, a, "one");
  incoming(s, a, "two");
  EXPECT_EQ(2, s.convoAt(a)->unread);
  EXPECT_EQ(2, s.totalUnread());
  s.markRead(a);
  EXPECT_EQ(0, s.convoAt(a)->unread);
}

// The open thread is on screen, so what arrives there is read as it lands.
TEST(BadgeStore, TheOpenConversationDoesNotCountUnread) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "Public");
  incoming(s, a, "one");
  s.setOpen(a);
  EXPECT_EQ(0, s.convoAt(a)->unread);   // opening reads it
  incoming(s, a, "two");
  EXPECT_EQ(0, s.convoAt(a)->unread);
  s.setOpen(-1);
  incoming(s, a, "three");
  EXPECT_EQ(1, s.convoAt(a)->unread);
}

// The design's order: pinned first, then unread, then the most recent. Conversations
// with nothing in them yet come last.
TEST(BadgeStore, InboxOrderIsPinnedThenUnreadThenRecent) {
  Store s;
  const int quiet = s.convo(Store::Channel, Key(1).b, "quiet");    // nothing in it
  const int old_read = s.convo(Store::Contact, Key(2).b, "old");
  const int new_read = s.convo(Store::Contact, Key(3).b, "new");
  const int unread = s.convo(Store::Channel, Key(4).b, "unread");
  incoming(s, unread, "x");
  incoming(s, old_read, "x");
  incoming(s, new_read, "x");
  s.markRead(old_read);
  s.markRead(new_read);
  uint8_t order[4];
  ASSERT_EQ(4, s.ordered(order, 4));
  EXPECT_EQ(unread, order[0]);
  EXPECT_EQ(new_read, order[1]);
  EXPECT_EQ(old_read, order[2]);
  EXPECT_EQ(quiet, order[3]);

  s.setPinned(quiet, true);
  ASSERT_EQ(4, s.ordered(order, 4));
  EXPECT_EQ(quiet, order[0]);
  EXPECT_EQ(unread, order[1]);
}

TEST(BadgeStore, AThreadIsOneConversationOldestFirst) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "a");
  const int b = s.convo(Store::Channel, Key(2).b, "b");
  const uint32_t a1 = incoming(s, a, "a1");
  incoming(s, b, "b1");
  const uint32_t a2 = s.addOutgoing(a, 100, "a2", 0, BadgeSend::None);
  uint32_t seqs[6];
  ASSERT_EQ(2, s.thread(a, seqs, 6));
  EXPECT_EQ(a1, seqs[0]);
  EXPECT_EQ(a2, seqs[1]);
  EXPECT_STREQ("a1", s.msg(seqs[0])->text);
  EXPECT_TRUE(s.msg(seqs[1])->outgoing);
}

TEST(BadgeStore, AShortThreadBufferGetsTheNewestMessages) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "a");
  incoming(s, a, "1");
  incoming(s, a, "2");
  const uint32_t three = incoming(s, a, "3");
  uint32_t seqs[2];
  ASSERT_EQ(2, s.thread(a, seqs, 2));
  EXPECT_EQ(three, seqs[1]);
}

// A full pool gives up the busiest conversation's oldest message, so a busy channel
// can't push a quiet DM out.
TEST(BadgeStore, AFullPoolEvictsFromTheBusiestConversation) {
  Store s;
  const int busy = s.convo(Store::Channel, Key(1).b, "Public");
  const int dm = s.convo(Store::Contact, Key(2).b, "Abend");
  const uint32_t dm_msg = incoming(s, dm, "hi");
  const uint32_t oldest_busy = incoming(s, busy, "b1");
  for (int i = 0; i < 5; i++) incoming(s, busy, "more");   // pool of 6 now full, then over
  EXPECT_NE(nullptr, s.msg(dm_msg));
  EXPECT_EQ(nullptr, s.msg(oldest_busy));
}

// Unread counts belong to the conversation and outlive its evicted messages.
TEST(BadgeStore, UnreadSurvivesEviction) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "Public");
  for (int i = 0; i < 9; i++) incoming(s, a, "x");
  EXPECT_EQ(9, s.convoAt(a)->unread);
}

TEST(BadgeStore, AnOutgoingDmFollowsItsTrackerHandle) {
  Store s;
  const int a = s.convo(Store::Contact, Key(1).b, "Abend");
  const uint32_t m = s.addOutgoing(a, 100, "omw", 7, BadgeSend::Sending);
  EXPECT_EQ(BadgeSend::Sending, s.msg(m)->status);
  EXPECT_TRUE(s.setStatusForHandle(7, BadgeSend::Delivered));
  EXPECT_EQ(BadgeSend::Delivered, s.msg(m)->status);
  EXPECT_FALSE(s.setStatusForHandle(8, BadgeSend::Failed));   // not ours
  EXPECT_FALSE(s.setStatusForHandle(0, BadgeSend::Failed));   // 0 is never a handle
}

// Each DM still sending takes the tracker's word; one the tracker no longer knows
// counts as failed, which offers a resend.
TEST(BadgeStore, RefreshSendingTakesTheTrackersWord) {
  Store s;
  const int a = s.convo(Store::Contact, Key(1).b, "Abend");
  const uint32_t m1 = s.addOutgoing(a, 100, "one", 1, BadgeSend::Sending);
  const uint32_t m2 = s.addOutgoing(a, 100, "two", 2, BadgeSend::Sending);
  const uint32_t ch = s.addOutgoing(a, 100, "chan", 0, BadgeSend::None);
  BadgeSend word[3] = {BadgeSend::None, BadgeSend::Delivered, BadgeSend::Sending};
  s.refreshSending([&](uint16_t h) { return word[h]; });
  EXPECT_EQ(BadgeSend::Delivered, s.msg(m1)->status);
  EXPECT_EQ(BadgeSend::Sending, s.msg(m2)->status);
  EXPECT_EQ(BadgeSend::None, s.msg(ch)->status);   // no handle: not the tracker's
  word[2] = BadgeSend::None;
  s.refreshSending([&](uint16_t h) { return word[h]; });
  EXPECT_EQ(BadgeSend::Failed, s.msg(m2)->status);
}

// #1232: a channel send waits for a repeater to be heard. No repeat in time leaves no
// mark rather than an X: nodes in direct range got it without echoing. A repeat heard
// later still ticks it. DMs are the tracker's business.
TEST(BadgeStore, ChannelSendsStopWaitingForARepeat) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "Public");
  const uint32_t old_send = s.addOutgoing(a, 100, "old", 0, BadgeSend::Sending);
  const uint32_t new_send = s.addOutgoing(a, 125, "new", 0, BadgeSend::Sending);
  const int d = s.convo(Store::Contact, Key(2).b, "Abend");
  const uint32_t dm = s.addOutgoing(d, 100, "dm", 7, BadgeSend::Sending);
  s.expireChannelSends(130, 30);
  EXPECT_EQ(BadgeSend::None, s.msg(old_send)->status);
  EXPECT_EQ(BadgeSend::Sending, s.msg(new_send)->status);
  EXPECT_EQ(BadgeSend::Sending, s.msg(dm)->status);
  EXPECT_TRUE(s.setStatus(old_send, BadgeSend::Delivered));
  EXPECT_EQ(BadgeSend::Delivered, s.msg(old_send)->status);
}

// #1233: the phone set the clock 19 hours forward. Messages stamped before that move
// with it, so a message that came a minute ago still reads a minute old.
TEST(BadgeStore, AClockCorrectionMovesTheStoredTimes) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "Public");
  const uint32_t in = incoming(s, a, "hi", 1000);
  const uint32_t out = s.addOutgoing(a, 1060, "yo", 0, BadgeSend::Sending);
  const int32_t jump = 19 * 3600;
  s.shiftTimes(jump);
  EXPECT_EQ(1000u + jump, s.msg(in)->time);
  EXPECT_EQ(1060u + jump, s.msg(out)->time);
  EXPECT_EQ(1060u + jump, s.convoAt(a)->last_time);
  // A channel send keeps its wait for a repeat: it isn't suddenly 19 hours old.
  s.expireChannelSends(1070 + jump, 30);
  EXPECT_EQ(BadgeSend::Sending, s.msg(out)->status);
}

TEST(BadgeStore, AClockCorrectionBackwardStopsAtZero) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "Public");
  const uint32_t in = incoming(s, a, "hi", 1000);
  s.shiftTimes(-5000);
  EXPECT_EQ(0u, s.msg(in)->time);
  EXPECT_EQ(0u, s.convoAt(a)->last_time);
}

// A conversation with nothing in it yet has no time to move.
TEST(BadgeStore, AClockCorrectionLeavesEmptyConversationsAlone) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "Public");
  s.shiftTimes(3600);
  EXPECT_EQ(0u, s.convoAt(a)->last_time);
}

TEST(BadgeStore, RemoveDropsOneMessage) {
  Store s;
  const int a = s.convo(Store::Contact, Key(1).b, "Abend");
  const uint32_t keep = incoming(s, a, "keep");
  const uint32_t gone = s.addOutgoing(a, 100, "gone", 3, BadgeSend::Failed);
  EXPECT_TRUE(s.remove(gone));
  EXPECT_EQ(nullptr, s.msg(gone));
  EXPECT_FALSE(s.remove(gone));
  uint32_t seqs[6];
  ASSERT_EQ(1, s.thread(a, seqs, 6));
  EXPECT_EQ(keep, seqs[0]);
}

TEST(BadgeStore, OwnMessagesDoNotCountUnreadButDoCountAsRecent) {
  Store s;
  const int a = s.convo(Store::Contact, Key(1).b, "a");
  const int b = s.convo(Store::Contact, Key(2).b, "b");
  incoming(s, a, "x");
  s.markRead(a);
  s.addOutgoing(b, 100, "y", 0, BadgeSend::None);
  EXPECT_EQ(0, s.totalUnread());
  uint8_t order[4];
  s.ordered(order, 4);
  EXPECT_EQ(b, order[0]);
}

// With the table full, the least recently active unpinned conversation goes, and its
// messages with it. Pinned conversations are never evicted.
TEST(BadgeStore, AFullTableEvictsTheLeastRecentUnpinnedConversation) {
  Store s;
  int c[4];
  for (int i = 0; i < 4; i++) {
    c[i] = s.convo(Store::Contact, Key((uint8_t)(i + 1)).b, "x");
    incoming(s, c[i], "x");
  }
  s.setPinned(c[0], true);   // the oldest, but pinned
  const uint32_t c1_msg = s.addOutgoing(c[1], 100, "c1", 0, BadgeSend::None);
  (void)c1_msg;
  const int fresh = s.convo(Store::Contact, Key(9).b, "fresh");
  ASSERT_GE(fresh, 0);
  EXPECT_EQ(c[2], fresh);                        // c[2] was the least recent unpinned
  EXPECT_EQ(-1, s.find(Store::Contact, Key(3).b));
  EXPECT_EQ(c[0], s.find(Store::Contact, Key(1).b));
  uint32_t seqs[6];
  EXPECT_EQ(0, s.thread(fresh, seqs, 6));        // the evicted conversation's messages went too
}

// The open conversation is on screen, so it is never the one evicted, even as the
// least recent; nor is a pinned one.
TEST(BadgeStore, EvictionSkipsPinnedAndOpenConversations) {
  Store s;
  int c[4];
  for (int i = 0; i < 4; i++) {
    c[i] = s.convo(Store::Contact, Key((uint8_t)(i + 1)).b, "x");
    incoming(s, c[i], "x");
  }
  s.setOpen(c[0]);          // least recent, but open
  s.setPinned(c[1], true);  // next, but pinned
  const int fresh = s.convo(Store::Contact, Key(9).b, "fresh");
  EXPECT_EQ(c[2], fresh);
  EXPECT_EQ(c[0], s.find(Store::Contact, Key(1).b));
  EXPECT_EQ(c[1], s.find(Store::Contact, Key(2).b));
}

TEST(BadgeStore, NoRoomWhenEveryConversationIsPinned) {
  Store s;
  for (int i = 0; i < 4; i++) s.setPinned(s.convo(Store::Contact, Key((uint8_t)(i + 1)).b, "x"), true);
  EXPECT_EQ(-1, s.convo(Store::Contact, Key(9).b, "fresh"));
}

TEST(BadgeStore, TextAndSenderAreCutToFit) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "a");
  const uint32_t m = s.addIncoming(a, 100, "a-very-long-sender-name", "0123456789abcdefXYZ", 2, -90);
  EXPECT_STREQ("0123456789abcdef", s.msg(m)->text);
  EXPECT_EQ(Store::kSenderLen - 1, (int)strlen(s.msg(m)->sender));
  EXPECT_EQ(2, s.msg(m)->hops);
  EXPECT_EQ(-90, s.msg(m)->rssi);
}

// MeshCore sends a channel message as "<name>: <text>".
TEST(BadgeStoreSplitSender, NameAndText) {
  char sender[16];
  const char* body = offband::splitSender("Abend: anyone at", sender, sizeof(sender));
  EXPECT_STREQ("Abend", sender);
  EXPECT_STREQ("anyone at", body);
}

TEST(BadgeStoreSplitSender, NoNameLeavesTheTextWhole) {
  char sender[16];
  const char* body = offband::splitSender("no colon here", sender, sizeof(sender));
  EXPECT_STREQ("", sender);
  EXPECT_STREQ("no colon here", body);
}

TEST(BadgeStoreSplitSender, AColonPastTheLongestNameIsText) {
  char sender[16];
  const std::string text = std::string(40, 'x') + ": later";
  const char* body = offband::splitSender(text.c_str(), sender, sizeof(sender));
  EXPECT_STREQ("", sender);
  EXPECT_EQ(text.c_str(), body);
}

// A cut never ends inside a UTF-8 character: 15 ASCII bytes then a 2-byte "é" is 17
// bytes, and 16 fit, so the "é" goes whole.
TEST(BadgeStore, CutTextEndsOnACharacterBoundary) {
  Store s;
  const int a = s.convo(Store::Channel, Key(1).b, "a");
  const uint32_t m = incoming(s, a, "0123456789abcde\xC3\xA9");
  EXPECT_STREQ("0123456789abcde", s.msg(m)->text);
}

TEST(BadgeStoreSplitSender, ACutNameEndsOnACharacterBoundary) {
  char sender[3];   // room for 2 bytes: "M" and half of "ü" would not be valid
  const char* body = offband::splitSender("M\xC3\xBCnchner: hi", sender, sizeof(sender));
  EXPECT_STREQ("M", sender);
  EXPECT_STREQ("hi", body);
}

// The wire format has no escape: the first ": " is the split.
TEST(BadgeStoreSplitSender, TheFirstColonSpaceSplits) {
  char sender[16];
  const char* body = offband::splitSender("Dr: Evil: hi", sender, sizeof(sender));
  EXPECT_STREQ("Dr", sender);
  EXPECT_STREQ("Evil: hi", body);
}

TEST(BadgeStoreSplitSender, ALongNameIsCutToTheBuffer) {
  char sender[6];
  const char* body = offband::splitSender("Strycher: hi", sender, sizeof(sender));
  EXPECT_STREQ("Stryc", sender);
  EXPECT_STREQ("hi", body);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
