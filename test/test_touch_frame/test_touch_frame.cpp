// #1442: CHSC6x frame decoding and touch-to-panel mapping. A wrong nibble mask
// puts every tap below row 256 in the wrong place on a 320-row panel; a
// missing glitch check turns bus noise into phantom taps.

#include <gtest/gtest.h>

#include "helpers/gui/GuiLayout.h"
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

// ------------------------------------------------- GT911 (Epic J, J3 #1726)

namespace {
// Point 1 at 0x8150: track id, X lo/hi, Y lo/hi, size lo/hi, reserved.
void gtPoint(uint8_t* b, uint16_t x, uint16_t y) {
  b[0] = 0;
  b[1] = uint8_t(x & 0xFF);
  b[2] = uint8_t(x >> 8);
  b[3] = uint8_t(y & 0xFF);
  b[4] = uint8_t(y >> 8);
  b[5] = 20;
  b[6] = 0;
  b[7] = 0;
}
}  // namespace

TEST(Gt911, APointIsReadAcrossBothBytes) {
  uint8_t b[kGt911PointLen];
  gtPoint(b, 300, 200);   // past 255 in X: the high byte matters
  TouchPoint p{};
  EXPECT_EQ(Gt911Frame::Pressed, parseGt911(0x81, b, sizeof b, &p));
  EXPECT_TRUE(p.pressed);
  EXPECT_EQ(300, p.x);
  EXPECT_EQ(200, p.y);
}

TEST(Gt911, NoNewReportKeepsTheLastState) {
  TouchPoint p{true, 10, 20};
  EXPECT_EQ(Gt911Frame::NotReady, parseGt911(0x01, nullptr, 0, &p)) << "bit 7 clear: nothing new";
  EXPECT_TRUE(p.pressed);
  EXPECT_EQ(10, p.x);
}

TEST(Gt911, AReportWithNoPointsIsARelease) {
  TouchPoint p{true, 10, 20};
  EXPECT_EQ(Gt911Frame::Released, parseGt911(0x80, nullptr, 0, &p));
  EXPECT_FALSE(p.pressed);
}

TEST(Gt911, TooManyPointsIsAGlitch) {
  uint8_t b[kGt911PointLen];
  gtPoint(b, 1, 1);
  TouchPoint p{};
  EXPECT_EQ(Gt911Frame::Invalid, parseGt911(0x8F, b, sizeof b, &p)) << "15 points: the GT911 reports 5 at most";
  EXPECT_EQ(Gt911Frame::Invalid, parseGt911(0xFF, b, sizeof b, &p)) << "an all-ones bus read";
}

TEST(Gt911, AShortPointReadIsAGlitch) {
  uint8_t b[kGt911PointLen];
  gtPoint(b, 1, 1);
  TouchPoint p{};
  EXPECT_EQ(Gt911Frame::Invalid, parseGt911(0x81, b, 4, &p));
  EXPECT_EQ(Gt911Frame::Invalid, parseGt911(0x81, nullptr, 0, &p));
}

TEST(Gt911, ACoordinateBeyondTheChipsRangeIsAGlitch) {
  uint8_t b[kGt911PointLen];
  gtPoint(b, 0xFFFF, 100);
  TouchPoint p{};
  EXPECT_EQ(Gt911Frame::Invalid, parseGt911(0x81, b, sizeof b, &p));
}

TEST(Gt911, TheFirstPointIsUsedWhenSeveralAreDown) {
  uint8_t b[kGt911PointLen];
  gtPoint(b, 50, 60);
  TouchPoint p{};
  EXPECT_EQ(Gt911Frame::Pressed, parseGt911(0x83, b, sizeof b, &p));
  EXPECT_EQ(50, p.x);
}

// The first point's block starts with its track id, right after the status
// register; X low is at 0x8150. Reading the block from 0x8150 shifted every
// field by a byte and made every press an out-of-range glitch (T-Deck bench,
// #1730: 43 reports, 0 presses, 25 invalid).
TEST(Gt911Registers, TheFirstPointStartsRightAfterTheStatus) {
  EXPECT_EQ(0x814E, kGt911Status);
  EXPECT_EQ(0x814F, kGt911Point1);
  EXPECT_EQ(kGt911Status + 1, kGt911Point1);
}

// The bench's failure, as bytes: a block read one byte late (no track id
// first) can't come out as the press it was.
TEST(Gt911Registers, ABlockReadOneByteLateIsNotThePress) {
  uint8_t chip[1 + kGt911PointLen + 1];   // 0x814F..0x8157: the point, then point 2's id
  chip[0] = 0;                            // track id
  chip[1] = 0xC8; chip[2] = 0x00;         // X = 200
  chip[3] = 0x96; chip[4] = 0x00;         // Y = 150
  chip[5] = 0x20; chip[6] = 0x00;         // size
  chip[7] = 0;                            // reserved
  chip[8] = 1;
  TouchPoint right{}, late{};
  EXPECT_EQ(Gt911Frame::Pressed, parseGt911(0x81, chip, kGt911PointLen, &right));
  EXPECT_EQ(200, right.x);
  EXPECT_EQ(150, right.y);
  const Gt911Frame f = parseGt911(0x81, chip + 1, kGt911PointLen, &late);
  EXPECT_TRUE(f == Gt911Frame::Invalid || late.x != 200 || late.y != 150);
}

TEST(Gt911Id, TheProductIdIsRecognized) {
  const uint8_t id[] = {'9', '1', '1', 0};
  EXPECT_TRUE(isGt911ProductId(id, sizeof id));
}

TEST(Gt911Id, AnotherChipIsNotTakenForTheTouchPanel) {
  const uint8_t other[] = {0x00, 0x00, 0x00, 0x00};
  const uint8_t ff[] = {0xFF, 0xFF, 0xFF, 0xFF};
  const uint8_t close[] = {'9', '1', '7', 0};
  EXPECT_FALSE(isGt911ProductId(other, sizeof other));
  EXPECT_FALSE(isGt911ProductId(ff, sizeof ff));
  EXPECT_FALSE(isGt911ProductId(close, sizeof close));
  EXPECT_FALSE(isGt911ProductId(nullptr, 4));
  EXPECT_FALSE(isGt911ProductId(other, 2));
}

TEST(Gt911Tracker, AHeldFingerStaysHeldBetweenReports) {
  Gt911Tracker t;
  uint8_t b[kGt911PointLen];
  gtPoint(b, 70, 80);
  TouchPoint p{};
  ASSERT_TRUE(t.update(0x81, b, sizeof b, &p));
  for (uint8_t i = 0; i + 1 < kGt911StaleReads; i++) {
    ASSERT_TRUE(t.update(0x00, nullptr, 0, &p));
    EXPECT_TRUE(p.pressed);
    EXPECT_EQ(70, p.x);
  }
  ASSERT_TRUE(t.update(0x81, b, sizeof b, &p));   // a new report restarts the count
  for (uint8_t i = 0; i + 1 < kGt911StaleReads; i++) ASSERT_TRUE(t.update(0x00, nullptr, 0, &p));
  EXPECT_TRUE(p.pressed);
}

TEST(Gt911Tracker, ALostReleaseReportDoesNotLeaveThePressStuck) {
  Gt911Tracker t;
  uint8_t b[kGt911PointLen];
  gtPoint(b, 70, 80);
  TouchPoint p{};
  ASSERT_TRUE(t.update(0x81, b, sizeof b, &p));
  for (uint8_t i = 0; i < kGt911StaleReads; i++) ASSERT_TRUE(t.update(0x00, nullptr, 0, &p));
  EXPECT_FALSE(p.pressed);
  EXPECT_EQ(70, p.x);   // released where the finger was
  ASSERT_TRUE(t.update(0x00, nullptr, 0, &p));
  EXPECT_FALSE(p.pressed);
}

TEST(Gt911Tracker, AReleaseReportReleasesAtOnce) {
  Gt911Tracker t;
  uint8_t b[kGt911PointLen];
  gtPoint(b, 70, 80);
  TouchPoint p{};
  ASSERT_TRUE(t.update(0x81, b, sizeof b, &p));
  ASSERT_TRUE(t.update(0x80, nullptr, 0, &p));
  EXPECT_FALSE(p.pressed);
  EXPECT_EQ(80, p.y);
}

TEST(Gt911Tracker, AnInvalidReportIsAFailedReadAndKeepsTheState) {
  Gt911Tracker t;
  uint8_t b[kGt911PointLen];
  gtPoint(b, 70, 80);
  TouchPoint p{};
  ASSERT_TRUE(t.update(0x81, b, sizeof b, &p));
  TouchPoint q{};
  EXPECT_FALSE(t.update(0x87, b, sizeof b, &q));   // 7 points: a glitch
  ASSERT_TRUE(t.update(0x00, nullptr, 0, &p));
  EXPECT_TRUE(p.pressed);
  EXPECT_FALSE(t.update(0x81, b, sizeof b, nullptr));
}

// The T-Deck's GT911 reports in the panel's own portrait frame (240 x 320),
// so the GUI applies no swap and the landscape turn alone maps it (T-Deck
// bench, #1730: with a swap, the right of the screen collapsed onto one
// edge). Every corner of the touch surface reaches its own corner of the
// 320 x 240 landscape screen.
TEST(TDeckTouch, ThePortraitFrameReachesTheWholeLandscapeScreen) {
  const TouchMapping m{false, false, false, 240, 320};
  const uint16_t raw[4][2] = {{0, 0}, {239, 0}, {0, 319}, {239, 319}};
  bool seen[2][2] = {};
  for (auto& r : raw) {
    uint16_t x = r[0], y = r[1];
    mapTouch(m, &x, &y);
    rotateTouch(90, &x, &y);
    ASSERT_TRUE((x == 0 || x == 319) && (y == 0 || y == 239)) << r[0] << "," << r[1] << " -> " << x << "," << y;
    seen[x == 319][y == 239] = true;
  }
  EXPECT_TRUE(seen[0][0] && seen[0][1] && seen[1][0] && seen[1][1]);
}

// The bench's failure: swapping a portrait frame first leaves most of the
// landscape screen unreachable.
TEST(TDeckTouch, ASwapOnThePortraitFrameLeavesTheScreenUnreachable) {
  const TouchMapping m{true, false, false, 240, 320};
  uint16_t max_x = 0;
  for (uint16_t rx = 0; rx < 240; rx += 7) {
    for (uint16_t ry = 0; ry < 320; ry += 7) {
      uint16_t x = rx, y = ry;
      mapTouch(m, &x, &y);
      rotateTouch(90, &x, &y);
      if (x > max_x) max_x = x;
    }
  }
  EXPECT_LT(max_x, 240);   // the right 80 columns can't be touched
}

// A controller whose axes run the other way round from the panel (320 x
// 240 against a 240 x 320 portrait panel): swapping the axes puts its touch
// on the portrait panel.
TEST(TouchMapping, ALandscapeControllerOnAPortraitPanelSwaps) {
  TouchMapping m{true, false, false, 240, 320};
  uint16_t x = 300, y = 10;   // near the long edge's far end
  mapTouch(m, &x, &y);
  EXPECT_EQ(10, x);
  EXPECT_EQ(300, y);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
