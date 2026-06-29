#include <gtest/gtest.h>
#include <cstring>
#include "helpers/BlockStore.h"

static void mkKey(uint8_t* out, uint8_t seed) { memset(out, seed, BLOCK_KEY_SIZE); }

TEST(BlockStore, AddContainsRemove) {
    BlockStore bs;
    uint8_t a[BLOCK_KEY_SIZE], b[BLOCK_KEY_SIZE];
    mkKey(a, 0xAA); mkKey(b, 0xBB);

    EXPECT_EQ(bs.count(), 0);
    EXPECT_FALSE(bs.contains(a));

    EXPECT_TRUE(bs.add(a));
    EXPECT_TRUE(bs.contains(a));
    EXPECT_FALSE(bs.contains(b));
    EXPECT_EQ(bs.count(), 1);

    EXPECT_TRUE(bs.remove(a));
    EXPECT_FALSE(bs.contains(a));
    EXPECT_EQ(bs.count(), 0);
    EXPECT_FALSE(bs.remove(a));   // not present
}

TEST(BlockStore, DedupAndCapacity) {
    BlockStore bs;
    uint8_t k[BLOCK_KEY_SIZE];
    mkKey(k, 0x01);
    EXPECT_TRUE(bs.add(k));
    EXPECT_TRUE(bs.add(k));        // dedup -> still true, count stays 1
    EXPECT_EQ(bs.count(), 1);

    for (int i = 2; i <= MAX_BLOCKED_KEYS; i++) {
        uint8_t kk[BLOCK_KEY_SIZE]; mkKey(kk, (uint8_t)i); EXPECT_TRUE(bs.add(kk));
    }
    EXPECT_EQ(bs.count(), MAX_BLOCKED_KEYS);

    uint8_t overflow[BLOCK_KEY_SIZE]; mkKey(overflow, 0xFF);
    EXPECT_FALSE(bs.add(overflow)); // full
    EXPECT_EQ(bs.count(), MAX_BLOCKED_KEYS);
}

TEST(BlockStore, RemoveMiddleKeepsOthers) {
    BlockStore bs;
    uint8_t a[BLOCK_KEY_SIZE], b[BLOCK_KEY_SIZE], c[BLOCK_KEY_SIZE];
    mkKey(a, 1); mkKey(b, 2); mkKey(c, 3);
    bs.add(a); bs.add(b); bs.add(c);
    EXPECT_TRUE(bs.remove(b));
    EXPECT_EQ(bs.count(), 2);
    EXPECT_TRUE(bs.contains(a));
    EXPECT_TRUE(bs.contains(c));
    EXPECT_FALSE(bs.contains(b));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
