// Native unit tests for the Nearby list (#1234): every node heard lately, newest first,
// one row per node even when it is both a contact and in the advert table.

#include <gtest/gtest.h>
#include <string>
#include "helpers/ui/NearbyList.h"

using badgeui::NearbyNode;
using List = badgeui::NearbyList<3>;

namespace {
NearbyNode contact(uint8_t id, uint32_t heard, uint8_t type, uint8_t hops = 0xFF) {
  NearbyNode n = {};
  n.heard = heard;
  memset(n.key, id, sizeof(n.key));
  n.key_len = 32;
  n.type = type;
  n.hops = hops;
  n.contact = true;
  snprintf(n.name, sizeof(n.name), "contact%u", id);
  return n;
}
NearbyNode advert(uint8_t id, uint32_t heard, uint8_t hops) {
  NearbyNode n = {};
  n.heard = heard;
  memset(n.key, id, 7);   // the advert table keeps a 7-byte prefix
  n.key_len = 7;
  n.hops = hops;
  snprintf(n.name, sizeof(n.name), "advert%u", id);
  return n;
}
}  // namespace

TEST(NearbyList, NewestFirst) {
  List l;
  l.offer(contact(1, 100, 1));
  l.offer(contact(2, 300, 1));
  l.offer(contact(3, 200, 1));
  ASSERT_EQ(3, l.count());
  EXPECT_EQ(300u, l.at(0).heard);
  EXPECT_EQ(200u, l.at(1).heard);
  EXPECT_EQ(100u, l.at(2).heard);
}

// A contact heard through an advert is one node: its whole key and type from the
// contact, its hops from the advert as heard, and the newer of the two times.
TEST(NearbyList, AContactAndItsAdvertAreOneRow) {
  List l;
  l.offer(contact(1, 100, 2 /* repeater */));
  l.offer(advert(1, 150, 3));
  ASSERT_EQ(1, l.count());
  const NearbyNode& n = l.at(0);
  EXPECT_TRUE(n.contact);
  EXPECT_EQ(32, n.key_len);
  EXPECT_EQ(2, n.type);
  EXPECT_EQ(3, n.hops);
  EXPECT_EQ(150u, n.heard);
  EXPECT_STREQ("contact1", n.name);
}

TEST(NearbyList, TheSameInEitherOrder) {
  List l;
  l.offer(advert(1, 150, 3));
  l.offer(contact(1, 100, 2));
  ASSERT_EQ(1, l.count());
  EXPECT_TRUE(l.at(0).contact);
  EXPECT_EQ(3, l.at(0).hops);
  EXPECT_EQ(150u, l.at(0).heard);
}

TEST(NearbyList, AStrangerStaysAStranger) {
  List l;
  l.offer(advert(9, 100, 1));
  ASSERT_EQ(1, l.count());
  EXPECT_FALSE(l.at(0).contact);
  EXPECT_EQ(0, l.at(0).type);
}

// Full: the oldest goes, and a node older than all of them isn't taken.
TEST(NearbyList, KeepsTheNewestWhenFull) {
  List l;
  l.offer(contact(1, 100, 1));
  l.offer(contact(2, 200, 1));
  l.offer(contact(3, 300, 1));
  l.offer(contact(4, 400, 1));
  l.offer(contact(5, 50, 1));
  ASSERT_EQ(3, l.count());
  EXPECT_EQ(400u, l.at(0).heard);
  EXPECT_EQ(200u, l.at(2).heard);
}

// A contact whose route isn't known yet still gets the hops its advert was heard over.
TEST(NearbyList, TheHeardHopsFillAContactsUnknownRoute) {
  List l;
  l.offer(contact(1, 100, 1, 0xFF));
  l.offer(advert(1, 90, 2));
  ASSERT_EQ(1, l.count());
  EXPECT_EQ(2, l.at(0).hops);
  EXPECT_EQ(100u, l.at(0).heard);   // the newer time stays
}

// Hearing a node again in a full list moves its row; nobody else goes.
TEST(NearbyList, HearingAgainInAFullListEvictsNobody) {
  List l;
  l.offer(contact(1, 100, 1));
  l.offer(contact(2, 200, 1));
  l.offer(contact(3, 300, 1));
  l.offer(advert(1, 400, 1));
  ASSERT_EQ(3, l.count());
  EXPECT_EQ(400u, l.at(0).heard);
  EXPECT_STREQ("contact1", l.at(0).name);
  EXPECT_EQ(200u, l.at(2).heard);
}

TEST(NearbyList, ClearStartsOver) {
  List l;
  l.offer(contact(1, 100, 1));
  l.clear();
  EXPECT_EQ(0, l.count());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
