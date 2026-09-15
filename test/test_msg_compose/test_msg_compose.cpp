// Native unit tests for composing a message on the badge's keyboard (#1228): how many
// characters a message may carry, a line that stops at that budget, and the picker's
// list of channels and contacts.

#include <gtest/gtest.h>
#include <string>
#include "helpers/ui/MsgCompose.h"

using compose::Target;

// MeshCore caps a text payload at MAX_TEXT_LEN (160) bytes. A DM gets all of it; a
// channel message spends name + 2 of them on "<name>: ".
TEST(ComposeBudget, DirectMessageGetsTheWholePayload) {
  EXPECT_EQ(160, compose::budget(false, 12, 160));
}

TEST(ComposeBudget, ChannelMessageLosesTheNamePrefix) {
  EXPECT_EQ(160 - 12 - 2, compose::budget(true, 12, 160));
}

TEST(ComposeBudget, NameTooLongLeavesNothing) {
  EXPECT_EQ(0, compose::budget(true, 158, 160));
  EXPECT_EQ(0, compose::budget(true, 200, 160));
}

TEST(Composer, StopsAtTheBudget) {
  compose::Composer<160> c;
  c.start(3);
  EXPECT_TRUE(c.apply('a'));
  EXPECT_TRUE(c.apply('b'));
  EXPECT_TRUE(c.apply('c'));
  EXPECT_FALSE(c.apply('d'));
  EXPECT_STREQ("abc", c.text());
  EXPECT_EQ(0, c.remaining());
}

TEST(Composer, BackspaceStillWorksAtTheBudget) {
  compose::Composer<160> c;
  c.start(2);
  c.apply('a');
  c.apply('b');
  EXPECT_TRUE(c.apply(KEY_BACKSPACE));
  EXPECT_STREQ("a", c.text());
  EXPECT_EQ(1, c.remaining());
}

TEST(Composer, TabCountsAsASpace) {
  compose::Composer<160> c;
  c.start(1);
  EXPECT_TRUE(c.apply(KEY_TAB));
  EXPECT_STREQ(" ", c.text());
  EXPECT_FALSE(c.apply('x'));
}

TEST(Composer, NavigationKeysDoNotType) {
  compose::Composer<160> c;
  c.start(10);
  EXPECT_FALSE(c.apply(KEY_LEFT));
  EXPECT_FALSE(c.apply(KEY_ENTER));
  EXPECT_FALSE(c.apply(KEY_CANCEL));
  EXPECT_EQ(0u, c.length());
}

TEST(Composer, BudgetNeverExceedsTheBuffer) {
  compose::Composer<4> c;
  c.start(160);
  EXPECT_EQ(4, c.remaining());
  for (char ch : std::string("abcdef")) c.apply((uint8_t)ch);
  EXPECT_STREQ("abcd", c.text());
}

TEST(Composer, StartClearsTheLine) {
  compose::Composer<160> c;
  c.start(10);
  c.apply('a');
  c.start(10);
  EXPECT_EQ(0u, c.length());
  EXPECT_EQ(10, c.remaining());
}

namespace {
// A key prefix whose every byte is v, as wide as Target::kKeyLen.
struct Prefix {
  uint8_t b[Target::kKeyLen];
  explicit Prefix(uint8_t v) { memset(b, v, sizeof(b)); }
};

// A contact callback for tests: contact i's key prefix is Prefix(i + 1).
bool anyContact(int i, uint8_t* key) {
  memcpy(key, Prefix((uint8_t)(i + 1)).b, Target::kKeyLen);
  return true;
}
bool noContact(int, uint8_t*) { return false; }
}  // namespace

// The picker's list: named channels first, then chat contacts, in the mesh's order.
TEST(TargetList, NamedChannelsFirstThenChatContacts) {
  compose::TargetList<8> list;
  const bool channel_named[4] = {true, false, true, false};
  const bool contact_chat[3] = {false, true, true};   // contact 0 is, say, a repeater
  compose::buildTargets(list, 4, [&](int i) { return channel_named[i]; },
                        3, [&](int i, uint8_t* key) { return contact_chat[i] && anyContact(i, key); });
  ASSERT_EQ(4u, list.count());
  EXPECT_EQ(Target::Channel, list.at(0).kind);
  EXPECT_EQ(0, list.at(0).index);
  EXPECT_EQ(Target::Channel, list.at(1).kind);
  EXPECT_EQ(2, list.at(1).index);
  EXPECT_EQ(Target::Contact, list.at(2).kind);
  EXPECT_EQ(1, list.at(2).index);
  EXPECT_EQ(Target::Contact, list.at(3).kind);
  EXPECT_EQ(2, list.at(3).index);
}

// A contact is identified by its key prefix, so a list that changes under the picker
// can't redirect a message. Channels carry no key.
TEST(TargetList, ContactsCarryTheirKeyChannelsCarryNone) {
  compose::TargetList<8> list;
  compose::buildTargets(list, 1, [](int) { return true; }, 2, anyContact);
  ASSERT_EQ(3u, list.count());
  EXPECT_EQ(0, memcmp(Prefix(0).b, list.at(0).key, Target::kKeyLen));
  EXPECT_EQ(0, memcmp(Prefix(1).b, list.at(1).key, Target::kKeyLen));
  EXPECT_EQ(0, memcmp(Prefix(2).b, list.at(2).key, Target::kKeyLen));
}

TEST(TargetList, StopsAtCapacity) {
  compose::TargetList<2> list;
  compose::buildTargets(list, 3, [](int) { return true; }, 2, anyContact);
  EXPECT_EQ(2u, list.count());
  EXPECT_EQ(Target::Channel, list.at(1).kind);
}

TEST(TargetList, RebuildStartsOver) {
  compose::TargetList<8> list;
  compose::buildTargets(list, 2, [](int) { return true; }, 0, noContact);
  compose::buildTargets(list, 1, [](int) { return true; }, 0, noContact);
  EXPECT_EQ(1u, list.count());
}

// findContact turns a picked prefix back into a contact, wherever it sits now.
TEST(FindContact, FindsTheContactWhereverItMoved) {
  EXPECT_EQ(2, compose::findContact(Prefix(3).b, 4, anyContact));   // contact 2 is Prefix(3)
}

TEST(FindContact, NoneWhenTheContactIsGone) {
  EXPECT_EQ(-1, compose::findContact(Prefix(9).b, 4, anyContact));
  EXPECT_EQ(-1, compose::findContact(Prefix(9).b, 0, anyContact));
}

// Two contacts share the prefix. Neither is picked, and the other contacts are still
// found.
TEST(FindContact, NoneWhenTwoContactsShareThePrefix) {
  const Prefix shared(7);
  auto twins = [&](int i, uint8_t* key) {   // contacts 1 and 3 share a prefix
    if (i == 1 || i == 3) {
      memcpy(key, shared.b, Target::kKeyLen);
      return true;
    }
    return anyContact(i, key);
  };
  EXPECT_EQ(-1, compose::findContact(shared.b, 4, twins));
  EXPECT_EQ(2, compose::findContact(Prefix(3).b, 4, twins));
}

// The whole prefix counts: a key that matches all but its last byte is someone else.
TEST(FindContact, EveryByteOfThePrefixCounts) {
  Prefix almost(3);
  almost.b[Target::kKeyLen - 1] = 4;
  EXPECT_EQ(-1, compose::findContact(almost.b, 4, anyContact));
}

// A repeater or room with the same prefix isn't a chat contact, so it doesn't count.
TEST(FindContact, OnlyChatContactsCount) {
  const Prefix shared(7);
  auto one_chat = [&](int i, uint8_t* key) {
    memcpy(key, shared.b, Target::kKeyLen);
    return i == 2;   // contacts 0, 1 and 3 have the key but aren't chat contacts
  };
  EXPECT_EQ(2, compose::findContact(shared.b, 4, one_chat));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
