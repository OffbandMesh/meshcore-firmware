// Epic R #1743, R0 #1747: the rules for adding a channel on the device.
// What the screens can't show at a glance: a hashtag typed two ways must be
// one channel, a key typed in groups must parse, a 41st channel must be
// refused, and a channel the radio already holds must be found, not doubled.

#include <gtest/gtest.h>

#include <string.h>

#include "helpers/gui/ChannelAdd.h"

using namespace offband::gui;

// ------------------------------------------------------------ hashtag names

// The Offband client keys "#QueenCityCon" exactly as typed; a radio that
// folded case would be on a different channel from a phone with the same name.
TEST(HashtagName, CaseIsKeptToMatchTheClient) {
  char a[32], b[32];
  ASSERT_TRUE(hashtagName("QueenCityCon", a, sizeof a));
  ASSERT_TRUE(hashtagName("#queencitycon", b, sizeof b));
  EXPECT_STREQ("#QueenCityCon", a);
  EXPECT_STREQ("#queencitycon", b);
}

TEST(HashtagName, ExtraHashesAndOuterSpacesAreDropped) {
  char out[32];
  ASSERT_TRUE(hashtagName("  ##Mesh-Net_2 ", out, sizeof out));
  EXPECT_STREQ("#Mesh-Net_2", out);
}

// UTF-8 passes through byte for byte, as the client hashes it.
TEST(HashtagName, NonAsciiIsKeptAsTyped) {
  char out[32];
  ASSERT_TRUE(hashtagName("Caf\xC3\xA9", out, sizeof out));
  EXPECT_STREQ("#Caf\xC3\xA9", out);
}

// The exact size a name needs: 31 characters and the NUL.
TEST(HashtagName, AFullLengthNameFitsItsDocumentedBuffer) {
  char out[kChannelNameMax + 1];
  ASSERT_TRUE(hashtagName("abcdefghijabcdefghijabcdefghij", out, sizeof out));
  EXPECT_EQ(kChannelNameMax, strlen(out));
}

TEST(HashtagName, NothingLeftIsRefused) {
  char out[32] = "x";
  EXPECT_FALSE(hashtagName("", out, sizeof out));
  EXPECT_STREQ("", out);
  EXPECT_FALSE(hashtagName("###  ", out, sizeof out));
  EXPECT_FALSE(hashtagName(nullptr, out, sizeof out));
}

TEST(HashtagName, ASpaceInsideIsRefused) {
  char out[32] = "x";
  EXPECT_FALSE(hashtagName("queen city", out, sizeof out));
  EXPECT_STREQ("", out);
}

TEST(HashtagName, TooLongForTheChannelTableIsRefused) {
  char out[64];
  const char thirty[] = "abcdefghijabcdefghijabcdefghij";   // + '#' = 31: fits
  EXPECT_TRUE(hashtagName(thirty, out, sizeof out));
  EXPECT_EQ(kChannelNameMax, strlen(out));
  const char thirty_one[] = "abcdefghijabcdefghijabcdefghijk";
  EXPECT_FALSE(hashtagName(thirty_one, out, sizeof out));
}

TEST(HashtagName, ATooSmallBufferIsRefused) {
  char out[4];
  EXPECT_FALSE(hashtagName("mesh", out, sizeof out));
  EXPECT_FALSE(hashtagName("mesh", nullptr, 32));
}

// --------------------------------------------------------------- hex keys

TEST(ChannelKey, ParsesAKeyTypedInGroups) {
  uint8_t key[kChannelKeyLen] = {};
  EXPECT_EQ(kChannelKeyHexLen, parseChannelKey("9cd8 fcf2 2a47 333b 591d 96a2 b848 b73f", key));
  const uint8_t want[kChannelKeyLen] = {0x9c, 0xd8, 0xfc, 0xf2, 0x2a, 0x47, 0x33, 0x3b,
                                        0x59, 0x1d, 0x96, 0xa2, 0xb8, 0x48, 0xb7, 0x3f};
  EXPECT_EQ(0, memcmp(want, key, kChannelKeyLen));
}

TEST(ChannelKey, UppercaseAndDashesAreFine) {
  uint8_t a[kChannelKeyLen], b[kChannelKeyLen];
  ASSERT_EQ(kChannelKeyHexLen, parseChannelKey("9CD8FCF2-2A47333B-591D96A2-B848B73F", a));
  ASSERT_EQ(kChannelKeyHexLen, parseChannelKey("9cd8fcf22a47333b591d96a2b848b73f", b));
  EXPECT_EQ(0, memcmp(a, b, kChannelKeyLen));
}

TEST(ChannelKey, AShortKeyCountsWhatIsThereAndWritesNothing) {
  uint8_t key[kChannelKeyLen];
  memset(key, 0xEE, sizeof key);
  EXPECT_EQ(20u, parseChannelKey("9cd8 fcf2 2a47 333b 591d", key));
  for (uint8_t b : key) EXPECT_EQ(0xEE, b);
  EXPECT_EQ(0u, parseChannelKey("", key));
  EXPECT_EQ(0u, parseChannelKey(nullptr, key));
}

// A typo must not be skipped: dropping it would silently change the key.
TEST(ChannelKey, ACharacterThatIsNeitherHexNorASeparatorIsNotAKey) {
  uint8_t key[kChannelKeyLen];
  memset(key, 0xEE, sizeof key);
  EXPECT_EQ(kChannelKeyBad, parseChannelKey("9cd8fcf22a47333b591d96a2b848b73f-invalid", key));
  EXPECT_EQ(kChannelKeyBad, parseChannelKey("9cd8 fcf2 2a47 333b 591d 96a2 b848 b73o", key));
  for (uint8_t b : key) EXPECT_EQ(0xEE, b);
  uint8_t ok[kChannelKeyLen];
  EXPECT_EQ(kChannelKeyHexLen, parseChannelKey("9c:d8:fc:f2:2a:47:33:3b:59:1d:96:a2:b8:48:b7:3f", ok));
}

TEST(ChannelKey, ALongKeyIsNotAKey) {
  uint8_t key[kChannelKeyLen];
  memset(key, 0xEE, sizeof key);
  EXPECT_GT(parseChannelKey("9cd8fcf22a47333b591d96a2b848b73f0", key), kChannelKeyHexLen);
  for (uint8_t b : key) EXPECT_EQ(0xEE, b);
}

TEST(ChannelKey, FormatsInGroupsOfFourAndRoundTrips) {
  const uint8_t key[kChannelKeyLen] = {0x4f, 0x1c, 0x8a, 0x20, 0xd3, 0x7e, 0x6b, 0x95,
                                       0x0c, 0x4e, 0xa1, 0xf7, 0x2d, 0x68, 0xb3, 0x9a};
  char text[40];
  formatChannelKey(key, text, sizeof text);
  EXPECT_STREQ("4f1c 8a20 d37e 6b95 0c4e a1f7 2d68 b39a", text);
  uint8_t back[kChannelKeyLen];
  ASSERT_EQ(kChannelKeyHexLen, parseChannelKey(text, back));
  EXPECT_EQ(0, memcmp(key, back, kChannelKeyLen));
}

TEST(ChannelKey, AShortBufferIsCutNotOverrun) {
  const uint8_t key[kChannelKeyLen] = {};
  char text[10];
  memset(text, 'X', sizeof text);
  formatChannelKey(key, text, sizeof text);
  EXPECT_LT(strlen(text), sizeof text);
}

// -------------------------------------------------------------- slots

namespace {
ChannelSlot slot(bool used, uint8_t fill) {
  ChannelSlot s{};
  s.used = used;
  memset(s.key, fill, sizeof s.key);
  return s;
}
}  // namespace

TEST(ChannelSlots, TheFirstFreeSlotIsUsed) {
  ChannelSlot s[4] = {slot(true, 1), slot(true, 2), slot(false, 0), slot(false, 0)};
  EXPECT_EQ(2, firstFreeChannelSlot(s, 4));
  EXPECT_EQ(2, usedChannelSlots(s, 4));
}

TEST(ChannelSlots, AllFullHasNoFreeSlot) {
  ChannelSlot s[40];
  for (int i = 0; i < 40; i++) s[i] = slot(true, uint8_t(i));
  EXPECT_EQ(-1, firstFreeChannelSlot(s, 40));
  EXPECT_EQ(40, usedChannelSlots(s, 40));
  EXPECT_EQ(-1, firstFreeChannelSlot(nullptr, 40));
}

TEST(ChannelSlots, AChannelAlreadyHeldIsFoundByItsKey) {
  ChannelSlot s[3] = {slot(true, 0x11), slot(false, 0x22), slot(true, 0x33)};
  uint8_t key[kChannelKeyLen];
  memset(key, 0x33, sizeof key);
  EXPECT_EQ(2, findChannelByKey(s, 3, key));
}

// A freed slot keeps its old bytes until reused; it must not count as held.
TEST(ChannelSlots, AnUnusedSlotIsNeverAMatch) {
  ChannelSlot s[3] = {slot(true, 0x11), slot(false, 0x22), slot(true, 0x33)};
  uint8_t key[kChannelKeyLen];
  memset(key, 0x22, sizeof key);
  EXPECT_EQ(-1, findChannelByKey(s, 3, key));
  memset(key, 0x44, sizeof key);
  EXPECT_EQ(-1, findChannelByKey(s, 3, key));
  EXPECT_EQ(-1, findChannelByKey(s, 3, nullptr));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
