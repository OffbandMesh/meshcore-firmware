// Epic R #1743, R0 #1747: hashtag and Public channel keys, against the real
// sha256. A wrong key here means a radio that "joins" #name on a channel no
// phone or other radio is on, with nothing on screen to say so.
//
// Vectors from docs/companion_protocol.md, "Channel Types".

#include <gtest/gtest.h>

#include <string.h>

#include "helpers/ChannelKeys.h"

TEST(HashtagKey, MatchesTheProtocolsExample) {
  const uint8_t want[16] = {0x9c, 0xd8, 0xfc, 0xf2, 0x2a, 0x47, 0x33, 0x3b,
                            0x59, 0x1d, 0x96, 0xa2, 0xb8, 0x48, 0xb7, 0x3f};
  uint8_t key[16] = {};
  offband::hashtagChannelKey("#test", key);
  EXPECT_EQ(0, memcmp(want, key, 16));
}

// The '#' is part of what's hashed, and case is not folded here: the name is
// normalized before it gets this far (ChannelAdd.h).
TEST(HashtagKey, TheHashAndTheCaseAreBothPartOfTheKey) {
  uint8_t with[16], without[16], upper[16];
  offband::hashtagChannelKey("#test", with);
  offband::hashtagChannelKey("test", without);
  offband::hashtagChannelKey("#Test", upper);
  EXPECT_NE(0, memcmp(with, without, 16));
  EXPECT_NE(0, memcmp(with, upper, 16));
}

TEST(PublicKey, MatchesTheProtocol) {
  const uint8_t want[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                            0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};
  EXPECT_EQ(0, memcmp(want, offband::kPublicChannelKey, 16));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
