// #1442: CHSC6x frame decoding and touch-to-panel mapping. A wrong nibble mask
// puts every tap below row 256 in the wrong place on a 320-row panel; a
// missing glitch check turns bus noise into phantom taps.

#include <gtest/gtest.h>

#include "helpers/gui/TouchFrame.h"

using namespace offband::gui;

namespace {

// Registers 0x00..0x06 with one point at (x, y).
void frame(uint8_t* b, uint8_t points, uint16_t x, uint16_t y) {
  b[0] = 0x00;
  b[1] = 0x00;
  b[2] = points;
  b[3] = uint8_t((x >> 8) & 0x0F) | 0x40;  // event flag bits above the nibble
  b[4] = uint8_t(x & 0xFF);
  b[5] = uint8_t((y >> 8) & 0x0F) | 0x10;  // touch-id bits above the nibble
  b[6] = uint8_t(y & 0xFF);
}

}  // namespace

TEST(Chsc6xFrame, OnePointDecodesTwelveBitCoordinates) {
  uint8_t b[kChsc6xFrameLen];
  frame(b, 1, 239, 319);
  TouchPoint p{};
  ASSERT_TRUE(parseChsc6xFrame(b, sizeof(b), &p));
  EXPECT_TRUE(p.pressed);
  EXPECT_EQ(p.x, 239);
  EXPECT_EQ(p.y, 319);
}

TEST(Chsc6xFrame, HighNibbleOnlyFlagBitsIgnored) {
  uint8_t b[kChsc6xFrameLen];
  frame(b, 1, 0x105, 0x0FA);
  TouchPoint p{};
  ASSERT_TRUE(parseChsc6xFrame(b, sizeof(b), &p));
  EXPECT_EQ(p.x, 0x105);
  EXPECT_EQ(p.y, 0x0FA);
}

TEST(Chsc6xFrame, NoPointsIsReleased) {
  uint8_t b[kChsc6xFrameLen];
  frame(b, 0, 10, 20);
  TouchPoint p{};
  ASSERT_TRUE(parseChsc6xFrame(b, sizeof(b), &p));
  EXPECT_FALSE(p.pressed);
}

TEST(Chsc6xFrame, PointCountUsesLowThreeBitsOnly) {
  uint8_t b[kChsc6xFrameLen];
  frame(b, 0xF8, 10, 20);  // count bits zero, upper bits set
  TouchPoint p{};
  ASSERT_TRUE(parseChsc6xFrame(b, sizeof(b), &p));
  EXPECT_FALSE(p.pressed);
}

TEST(Chsc6xFrame, AllZeroAndAllOnesFramesAreGlitches) {
  uint8_t zeros[kChsc6xFrameLen] = {0};
  uint8_t ones[kChsc6xFrameLen];
  for (uint8_t& v : ones) v = 0xFF;
  TouchPoint p{};
  EXPECT_FALSE(parseChsc6xFrame(zeros, sizeof(zeros), &p));
  EXPECT_FALSE(parseChsc6xFrame(ones, sizeof(ones), &p));

  // Only bytes 2, 3, 4 and 6 decide it; the others may hold anything.
  uint8_t partial_zeros[kChsc6xFrameLen] = {0xAA, 0xBB, 0x00, 0x00, 0x00, 0xCC, 0x00};
  uint8_t partial_ones[kChsc6xFrameLen] = {0xAA, 0xBB, 0xFF, 0xFF, 0xFF, 0xCC, 0xFF};
  EXPECT_FALSE(parseChsc6xFrame(partial_zeros, sizeof(partial_zeros), &p));
  EXPECT_FALSE(parseChsc6xFrame(partial_ones, sizeof(partial_ones), &p));
}

TEST(TouchMapping, ClampHappensBeforeMirror) {
  TouchMapping m{true, true, true, 320, 240};
  uint16_t x = 500, y = 30;  // swap -> (30, 500), clamp -> (30, 239), mirror -> (289, 0)
  mapTouch(m, &x, &y);
  EXPECT_EQ(x, 289);
  EXPECT_EQ(y, 0);
}

TEST(Chsc6xFrame, ShortOrNullFrameIsRejected) {
  uint8_t b[kChsc6xFrameLen];
  frame(b, 1, 5, 5);
  TouchPoint p{};
  EXPECT_FALSE(parseChsc6xFrame(b, kChsc6xFrameLen - 1, &p));
  EXPECT_FALSE(parseChsc6xFrame(nullptr, kChsc6xFrameLen, &p));
  EXPECT_FALSE(parseChsc6xFrame(b, kChsc6xFrameLen, nullptr));
}

TEST(TouchMapping, IdentityLeavesCoordinates) {
  TouchMapping m{false, false, false, 240, 320};
  uint16_t x = 12, y = 300;
  mapTouch(m, &x, &y);
  EXPECT_EQ(x, 12);
  EXPECT_EQ(y, 300);
}

TEST(TouchMapping, MirrorUsesTheLastPixel) {
  TouchMapping m{false, true, true, 240, 320};
  uint16_t x = 0, y = 0;
  mapTouch(m, &x, &y);
  EXPECT_EQ(x, 239);
  EXPECT_EQ(y, 319);
}

TEST(TouchMapping, SwapThenMirror) {
  TouchMapping m{true, true, false, 320, 240};  // landscape panel
  uint16_t x = 10, y = 50;                      // raw portrait reading
  mapTouch(m, &x, &y);
  EXPECT_EQ(x, 319 - 50);
  EXPECT_EQ(y, 10);
}

TEST(TouchMapping, OutOfRangeIsClamped) {
  TouchMapping m{false, false, false, 240, 320};
  uint16_t x = 400, y = 999;
  mapTouch(m, &x, &y);
  EXPECT_EQ(x, 239);
  EXPECT_EQ(y, 319);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
