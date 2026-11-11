// #1442: CHSC6x frame decoding and touch-to-panel mapping. A wrong nibble mask
// puts every tap below row 256 in the wrong place on a 320-row panel; a
// missing glitch check turns bus noise into phantom taps.

#include <gtest/gtest.h>
#include <string.h>

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

// ---- cst66xx (#2109) ----

struct HynEntry {
  uint16_t x, y;
  uint8_t id, event;
};

// Builds a cst66xx report as the chip lays it out, keys first, with a good
// checksum. Returns its length.
static size_t hynReport(uint8_t* b, uint8_t type, const HynEntry* keys, uint8_t nkeys, const HynEntry* fingers,
                        uint8_t nfingers) {
  memset(b, 0, kCst66xxMaxReportLen);
  b[2] = type;
  b[3] = uint8_t((nkeys << 4) | nfingers);
  uint8_t* e = b + kCst66xxHeadLen;
  auto put = [&](const HynEntry& h) {
    e[0] = uint8_t(h.x);
    e[1] = uint8_t(h.y);
    e[2] = 0x20;   // pressure
    e[3] = uint8_t(((h.x >> 8) & 0x0F) | (((h.y >> 8) & 0x0F) << 4));
    e[4] = uint8_t((h.event << 4) | (h.id & 0x0F));
    e += kCst66xxEntryLen;
  };
  for (uint8_t i = 0; i < nkeys; i++) put(keys[i]);
  for (uint8_t i = 0; i < nfingers; i++) put(fingers[i]);
  const size_t len = kCst66xxHeadLen + kCst66xxEntryLen * (nkeys + nfingers);
  uint16_t sum = 0x55;
  for (size_t i = kCst66xxHeadLen; i < len; i++) sum = uint16_t(sum + b[i]);
  b[0] = uint8_t(sum);
  b[1] = uint8_t(sum >> 8);
  return len;
}

TEST(Cst66xx, OneFingerDecodesTwelveBitCoordinates) {
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f{0x1EF, 0x13F, 0, 6};   // 495, 319: both high nibbles in use
  const size_t n = hynReport(b, 0xFF, nullptr, 0, &f, 1);
  EXPECT_EQ(kCst66xxFirstReadLen, n);
  TouchPoint p{};
  ASSERT_EQ(Cst66xxFrame::Pressed, parseCst66xx(b, n, &p));
  EXPECT_TRUE(p.pressed);
  EXPECT_EQ(0x1EF, p.x);
  EXPECT_EQ(0x13F, p.y);
}

TEST(Cst66xx, AFingerWithEventZeroIsALift) {
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f{100, 200, 0, 0};
  const size_t n = hynReport(b, 0xFF, nullptr, 0, &f, 1);
  TouchPoint p{true, 1, 1};
  ASSERT_EQ(Cst66xxFrame::Released, parseCst66xx(b, n, &p));
  EXPECT_FALSE(p.pressed);
}

TEST(Cst66xx, AReportWithNoEntriesIsARelease) {
  uint8_t b[kCst66xxMaxReportLen];
  const size_t n = hynReport(b, 0xFF, nullptr, 0, nullptr, 0);
  EXPECT_EQ(kCst66xxHeadLen, n);
  EXPECT_EQ(0x55, b[0]);   // the checksum of nothing is its seed
  TouchPoint p{true, 1, 1};
  // The driver always reads the head and one entry; the spare entry is ignored.
  ASSERT_EQ(Cst66xxFrame::Released, parseCst66xx(b, kCst66xxFirstReadLen, &p));
  EXPECT_FALSE(p.pressed);
}

TEST(Cst66xx, TheFirstFingerDownIsUsed) {
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f[3] = {{10, 20, 0, 0}, {30, 40, 1, 6}, {50, 60, 2, 6}};
  const size_t n = hynReport(b, 0xFF, nullptr, 0, f, 3);
  TouchPoint p{};
  ASSERT_EQ(Cst66xxFrame::Pressed, parseCst66xx(b, n, &p));
  EXPECT_EQ(30, p.x);
  EXPECT_EQ(40, p.y);
}

TEST(Cst66xx, KeyEntriesComeBeforeTheFingers) {
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry k{999, 999, 3, 1};
  const HynEntry f{77, 88, 0, 6};
  const size_t n = hynReport(b, 0xFF, &k, 1, &f, 1);
  TouchPoint p{};
  ASSERT_EQ(Cst66xxFrame::Pressed, parseCst66xx(b, n, &p));
  EXPECT_EQ(77, p.x);   // not the key entry's bytes
  EXPECT_EQ(88, p.y);
}

TEST(Cst66xx, OnlyAKeyIsNotATouch) {
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry k{0, 0, 1, 1};
  const size_t n = hynReport(b, 0xFF, &k, 1, nullptr, 0);
  TouchPoint p{true, 1, 1};
  EXPECT_EQ(Cst66xxFrame::Released, parseCst66xx(b, n, &p));
  EXPECT_FALSE(p.pressed);
}

TEST(Cst66xx, AKeyWithEveryFingerUpIsARelease) {
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry k{0, 0, 1, 1};
  const HynEntry f{100, 200, 0, 0};
  const size_t n = hynReport(b, 0xFF, &k, 1, &f, 1);
  TouchPoint p{true, 1, 1};
  EXPECT_EQ(Cst66xxFrame::Released, parseCst66xx(b, n, &p));
  EXPECT_FALSE(p.pressed);
}

TEST(Cst66xx, TheFarCornerPassesThroughForTheMappingToClamp) {
  // 12 bits reach 4095; mapTouch clamps it into the panel, the parser doesn't.
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f{0xFFF, 0xFFF, 0, 6};
  const size_t n = hynReport(b, 0xFF, nullptr, 0, &f, 1);
  TouchPoint p{};
  ASSERT_EQ(Cst66xxFrame::Pressed, parseCst66xx(b, n, &p));
  EXPECT_EQ(0xFFF, p.x);
  EXPECT_EQ(0xFFF, p.y);
}

TEST(Cst66xx, ABadChecksumIsAGlitch) {
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f{100, 200, 0, 6};
  const size_t n = hynReport(b, 0xFF, nullptr, 0, &f, 1);
  b[4] ^= 0x01;   // one bit flipped on the wire
  TouchPoint p{false, 5, 5};
  EXPECT_EQ(Cst66xxFrame::Invalid, parseCst66xx(b, n, &p));
  EXPECT_FALSE(p.pressed);   // untouched
  EXPECT_EQ(5, p.x);
}

TEST(Cst66xx, TheChecksumIsSixteenBits) {
  // Five entries of 0xFF-heavy bytes carry the sum past 0xFF.
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f[5] = {{0xFFF, 0xFFF, 15, 15}, {0xFFF, 0xFFF, 15, 15}, {0xFFF, 0xFFF, 15, 15},
                         {0xFFF, 0xFFF, 15, 15}, {0xFFF, 0xFFF, 15, 15}};
  const size_t n = hynReport(b, 0xFF, nullptr, 0, f, 5);
  EXPECT_NE(0, b[1]);
  TouchPoint p{};
  EXPECT_EQ(Cst66xxFrame::Pressed, parseCst66xx(b, n, &p));
}

TEST(Cst66xx, TooManyEntriesIsAGlitch) {
  uint8_t b[kCst66xxMaxReportLen] = {};
  b[0] = 0x55;
  b[2] = 0xFF;
  b[3] = 0x33;   // 3 keys + 3 fingers > 5
  TouchPoint p{};
  EXPECT_EQ(Cst66xxFrame::Invalid, parseCst66xx(b, sizeof b, &p));
  EXPECT_EQ(0u, cst66xxReportLen(0x33));
  EXPECT_EQ(0u, cst66xxReportLen(0x06));
  EXPECT_EQ(kCst66xxMaxReportLen, cst66xxReportLen(0x05));
  EXPECT_EQ(kCst66xxMaxReportLen, cst66xxReportLen(0x23));
}

TEST(Cst66xx, AShortReadIsAGlitch) {
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f[2] = {{1, 2, 0, 6}, {3, 4, 1, 6}};
  const size_t n = hynReport(b, 0xFF, nullptr, 0, f, 2);
  TouchPoint p{};
  EXPECT_EQ(Cst66xxFrame::Invalid, parseCst66xx(b, n - 1, &p));
  EXPECT_EQ(Cst66xxFrame::Invalid, parseCst66xx(b, kCst66xxHeadLen - 1, &p));
  EXPECT_EQ(Cst66xxFrame::Invalid, parseCst66xx(nullptr, n, &p));
  EXPECT_EQ(Cst66xxFrame::Invalid, parseCst66xx(b, n, nullptr));
}

TEST(Cst66xx, AnIdleBusIsAGlitch) {
  uint8_t ones[kCst66xxFirstReadLen];
  memset(ones, 0xFF, sizeof ones);   // nothing driving SDA
  uint8_t zeros[kCst66xxFirstReadLen] = {};
  TouchPoint p{};
  EXPECT_EQ(Cst66xxFrame::Invalid, parseCst66xx(ones, sizeof ones, &p));
  EXPECT_EQ(Cst66xxFrame::Invalid, parseCst66xx(zeros, sizeof zeros, &p));
}

TEST(Cst66xx, GestureAndProximityReportsAreNotTouches) {
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f{100, 200, 0, 6};
  for (uint8_t type : {uint8_t(0xF0), uint8_t(0xE0)}) {
    const size_t n = hynReport(b, type, nullptr, 0, &f, 1);
    TouchPoint p{false, 9, 9};
    EXPECT_EQ(Cst66xxFrame::Other, parseCst66xx(b, n, &p));
    EXPECT_FALSE(p.pressed);
    EXPECT_EQ(9, p.x);
  }
}

TEST(Cst66xxTracker, AHeldFingerStaysHeldBetweenReports) {
  Cst66xxTracker t;
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f{70, 80, 0, 6};
  const size_t n = hynReport(b, 0xFF, nullptr, 0, &f, 1);
  TouchPoint p{};
  ASSERT_TRUE(t.report(b, n, &p));
  for (uint8_t i = 0; i + 1 < kCst66xxStaleReads; i++) {
    t.idle(&p);
    EXPECT_TRUE(p.pressed);
    EXPECT_EQ(70, p.x);
  }
  ASSERT_TRUE(t.report(b, n, &p));   // a new report restarts the count
  for (uint8_t i = 0; i + 1 < kCst66xxStaleReads; i++) t.idle(&p);
  EXPECT_TRUE(p.pressed);
}

TEST(Cst66xxTracker, ALostReleaseReportDoesNotLeaveThePressStuck) {
  Cst66xxTracker t;
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f{70, 80, 0, 6};
  const size_t n = hynReport(b, 0xFF, nullptr, 0, &f, 1);
  TouchPoint p{};
  ASSERT_TRUE(t.report(b, n, &p));
  for (uint8_t i = 0; i < kCst66xxStaleReads; i++) t.idle(&p);
  EXPECT_FALSE(p.pressed);
  EXPECT_EQ(70, p.x);   // released where the finger was
  EXPECT_EQ(1u, t.staleReleases());
}

TEST(Cst66xxTracker, AFingerUpReportIsNotCountedAsAStaleRelease) {
  Cst66xxTracker t;
  uint8_t b[kCst66xxMaxReportLen];
  HynEntry f{70, 80, 0, 6};
  TouchPoint p{};
  ASSERT_TRUE(t.report(b, hynReport(b, 0xFF, nullptr, 0, &f, 1), &p));
  f.event = 0;
  ASSERT_TRUE(t.report(b, hynReport(b, 0xFF, nullptr, 0, &f, 1), &p));
  for (uint8_t i = 0; i < 2 * kCst66xxStaleReads; i++) t.idle(&p);   // idle with no finger
  EXPECT_FALSE(p.pressed);
  EXPECT_EQ(0u, t.staleReleases());
}

TEST(Cst66xxTracker, AReleaseReportReleasesAtOnce) {
  Cst66xxTracker t;
  uint8_t b[kCst66xxMaxReportLen];
  HynEntry f{70, 80, 0, 6};
  TouchPoint p{};
  ASSERT_TRUE(t.report(b, hynReport(b, 0xFF, nullptr, 0, &f, 1), &p));
  f.event = 0;
  ASSERT_TRUE(t.report(b, hynReport(b, 0xFF, nullptr, 0, &f, 1), &p));
  EXPECT_FALSE(p.pressed);
}

TEST(Cst66xxTracker, AnInvalidReportKeepsTheStateAndIsAFailedRead) {
  Cst66xxTracker t;
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f{70, 80, 0, 6};
  const size_t n = hynReport(b, 0xFF, nullptr, 0, &f, 1);
  TouchPoint p{};
  ASSERT_TRUE(t.report(b, n, &p));
  b[0] ^= 0xFF;
  TouchPoint q{};
  EXPECT_FALSE(t.report(b, n, &q));
  t.idle(&p);
  EXPECT_TRUE(p.pressed);
  EXPECT_FALSE(t.report(b, n, nullptr));
}

TEST(Cst66xxTracker, GestureAndProximityReportsKeepThePressAlive) {
  for (uint8_t type : {uint8_t(0xF0), uint8_t(0xE0)}) {
    Cst66xxTracker t;
    uint8_t b[kCst66xxMaxReportLen];
    const HynEntry f{70, 80, 0, 6};
    TouchPoint p{};
    ASSERT_TRUE(t.report(b, hynReport(b, 0xFF, nullptr, 0, &f, 1), &p));
    for (uint8_t i = 0; i + 1 < kCst66xxStaleReads; i++) t.idle(&p);
    ASSERT_TRUE(t.report(b, hynReport(b, type, nullptr, 0, &f, 1), &p));   // the chip is still reporting
    t.idle(&p);
    EXPECT_TRUE(p.pressed) << int(type);
    EXPECT_EQ(70, p.x);
  }
}

TEST(Cst66xxTracker, AResetDropsTheHeldPress) {
  Cst66xxTracker t;
  uint8_t b[kCst66xxMaxReportLen];
  const HynEntry f{70, 80, 0, 6};
  TouchPoint p{};
  ASSERT_TRUE(t.report(b, hynReport(b, 0xFF, nullptr, 0, &f, 1), &p));
  t.reset();
  t.idle(&p);
  EXPECT_FALSE(p.pressed);
}

// ---- the stuck-press rule shared by every driver (#2109) ----

TEST(FailedReadGuard, OneGlitchDoesNotDropADrag) {
  FailedReadGuard g;
  EXPECT_TRUE(g.update(true, true));
  for (uint8_t i = 0; i + 1 < kReleaseAfterFailedReads; i++) EXPECT_TRUE(g.update(false, false));
  EXPECT_TRUE(g.update(true, true));   // a good read resets the count
  for (uint8_t i = 0; i + 1 < kReleaseAfterFailedReads; i++) EXPECT_TRUE(g.update(false, false));
}

TEST(FailedReadGuard, ABusThatStaysDownReleasesThePress) {
  FailedReadGuard g;
  g.update(true, true);
  for (uint8_t i = 0; i < kReleaseAfterFailedReads; i++) g.update(false, true);   // `pressed` is ignored
  EXPECT_FALSE(g.pressed());
  for (int i = 0; i < 300; i++) EXPECT_FALSE(g.update(false, true));   // and stays released
}

TEST(FailedReadGuard, AGoodReadIsTakenAsIs) {
  FailedReadGuard g;
  EXPECT_TRUE(g.update(true, true));
  EXPECT_FALSE(g.update(true, false));
  EXPECT_FALSE(g.update(false, true));   // a failed read never presses
}

TEST(FailedReadGuard, ReleaseDropsAHeldPress) {
  FailedReadGuard g;
  g.update(true, true);
  g.release();
  EXPECT_FALSE(g.pressed());
}

// ---- reads queued during an e-paper refresh (#2110) ----

static TouchPoint up(uint16_t x = 0, uint16_t y = 0) { return TouchPoint{false, x, y}; }
static TouchPoint down(uint16_t x, uint16_t y) { return TouchPoint{true, x, y}; }

TEST(TouchQueue, ATapInsideARefreshIsKeptAsAPressAndARelease) {
  TouchQueue q;
  q.start(false);
  EXPECT_TRUE(q.offer(up(), 10));
  EXPECT_TRUE(q.offer(down(40, 50), 20));
  EXPECT_TRUE(q.offer(up(40, 50), 50));
  EXPECT_TRUE(q.offer(up(40, 50), 80));
  ASSERT_EQ(2, q.size());
  TouchPoint p{};
  uint32_t ms = 0;
  ASSERT_TRUE(q.take(&p, &ms));
  EXPECT_TRUE(p.pressed);
  EXPECT_EQ(40, p.x);
  EXPECT_EQ(50, p.y);
  EXPECT_EQ(20u, ms);
  ASSERT_TRUE(q.take(&p, &ms));
  EXPECT_FALSE(p.pressed);
  EXPECT_EQ(50u, ms);
  EXPECT_FALSE(q.take(&p, &ms));
}

TEST(TouchQueue, NothingTouchedQueuesNothing) {
  TouchQueue q;
  q.start(false);
  for (uint32_t i = 0; i < 100; i++) EXPECT_TRUE(q.offer(up(), i));
  EXPECT_TRUE(q.empty());
}

TEST(TouchQueue, ADragKeepsWhereItStartedAndWhereItGot) {
  TouchQueue q;
  q.start(false);
  q.offer(down(10, 10), 1);
  for (uint16_t i = 1; i <= 50; i++) q.offer(down(10, uint16_t(10 + i)), 1 + i);
  q.offer(up(10, 60), 99);
  ASSERT_EQ(3, q.size());
  TouchPoint p{};
  q.take(&p, nullptr);
  EXPECT_EQ(10, p.y);   // the press, where it started
  q.take(&p, nullptr);
  EXPECT_TRUE(p.pressed);
  EXPECT_EQ(60, p.y);   // the newest held point
  q.take(&p, nullptr);
  EXPECT_FALSE(p.pressed);
}

TEST(TouchQueue, AFingerAlreadyDownIsNotANewPress) {
  TouchQueue q;
  q.start(true);
  q.offer(down(30, 30), 1);
  q.offer(down(31, 30), 2);
  ASSERT_EQ(1, q.size());   // one held point, overwritten in place
  TouchPoint p{};
  q.take(&p, nullptr);
  EXPECT_TRUE(p.pressed);
  EXPECT_EQ(31, p.x);
}

TEST(TouchQueue, ALongHoldDoesNotFillTheQueue) {
  TouchQueue q;
  q.start(false);
  for (uint32_t i = 0; i < 1000; i++) EXPECT_TRUE(q.offer(down(5, 5), i));
  EXPECT_EQ(2, q.size());
  EXPECT_EQ(0u, q.dropped());
}

TEST(TouchQueue, TwoTapsStayTwoTaps) {
  TouchQueue q;
  q.start(false);
  q.offer(down(1, 1), 1);
  q.offer(up(1, 1), 2);
  q.offer(down(2, 2), 3);
  q.offer(up(2, 2), 4);
  ASSERT_EQ(4, q.size());
  bool want = true;
  TouchPoint p{};
  while (q.take(&p, nullptr)) {
    EXPECT_EQ(want, p.pressed);
    want = !want;
  }
}

TEST(TouchQueue, AFullQueueDropsAndCountsTheRest) {
  TouchQueue q;
  q.start(false);
  for (uint8_t i = 0; i < kTouchQueueLen; i++) EXPECT_TRUE(q.offer(i % 2 ? up() : down(1, 1), i));
  EXPECT_FALSE(q.offer(down(9, 9), 100));
  EXPECT_EQ(1u, q.dropped());
  EXPECT_EQ(kTouchQueueLen, q.size());
  // The oldest taps are the ones kept, in order.
  TouchPoint p{};
  q.take(&p, nullptr);
  EXPECT_TRUE(p.pressed);
  EXPECT_EQ(1, p.x);
}

TEST(TouchQueue, AHeldPointStillMovesWhenTheQueueIsFull) {
  TouchQueue q;
  q.start(false);
  for (uint8_t i = 0; i < kTouchQueueLen - 1; i++) q.offer(i % 2 ? up() : down(1, 1), i);
  q.offer(down(1, 1), 50);   // the first held point after the last press: the 16th entry
  ASSERT_EQ(kTouchQueueLen, q.size());
  EXPECT_TRUE(q.offer(down(7, 7), 51));   // moves it, drops nothing
  EXPECT_EQ(0u, q.dropped());
}

TEST(TouchQueue, AfterADroppedReleaseANewTapIsNotFoldedIntoTheOldPress) {
  TouchQueue q;
  q.start(false);
  // Fill it, ending on a press and its first held point.
  for (uint8_t i = 0; i < kTouchQueueLen - 2; i++) q.offer(i % 2 ? up() : down(1, 1), i);
  q.offer(down(1, 1), 20);
  q.offer(down(2, 2), 21);
  ASSERT_EQ(kTouchQueueLen, q.size());
  EXPECT_FALSE(q.offer(up(2, 2), 22));   // the lift is dropped
  // Room again (the oldest two taken), then a new tap somewhere else.
  TouchPoint p{};
  q.take(&p, nullptr);
  q.take(&p, nullptr);
  EXPECT_TRUE(q.offer(down(90, 90), 23));
  EXPECT_TRUE(q.offer(up(90, 90), 24));
  // The old press's held point is still its own, and the new tap is a press
  // of its own after it, not a move of the old one.
  for (uint8_t i = 0; i < kTouchQueueLen - 2; i++) q.take(&p, nullptr);
  EXPECT_TRUE(p.pressed);
  EXPECT_EQ(2, p.x);
  ASSERT_TRUE(q.take(&p, nullptr));
  EXPECT_TRUE(p.pressed);
  EXPECT_EQ(90, p.x);
  ASSERT_TRUE(q.take(&p, nullptr));
  EXPECT_FALSE(p.pressed);
  EXPECT_FALSE(q.take(&p, nullptr));
}

TEST(TouchQueue, AReleaseCarriesHowLongItsPressWasDown) {
  TouchQueue q;
  q.start(false);
  q.offer(down(5, 5), 100);
  q.offer(down(5, 6), 130);
  q.offer(up(5, 6), 700);
  TouchPoint p{};
  EXPECT_EQ(0u, q.nextReleaseHeldMs());   // the press is first
  q.take(&p, nullptr);
  q.take(&p, nullptr);
  EXPECT_EQ(600u, q.nextReleaseHeldMs());
}

TEST(TouchQueue, AFingerDownBeforeTheRefreshHasNoHoldTime) {
  // LVGL saw that press itself and is timing it already.
  TouchQueue q;
  q.start(true);
  q.offer(down(5, 5), 100);
  q.offer(up(5, 5), 900);
  TouchPoint p{};
  q.take(&p, nullptr);
  EXPECT_EQ(0u, q.nextReleaseHeldMs());
}

// ---- replaying what a refresh caught (#2110) ----

TEST(TouchReplay, ATapIsHandedOverInOnePass) {
  TouchQueue q;
  TouchReplay r(q, 400);
  q.start(false);
  q.offer(down(4, 4), 10);
  q.offer(up(4, 4), 60);
  TouchPoint p{};
  uint32_t ms = 0;
  bool more = false;
  ASSERT_TRUE(r.next(1000, &p, &ms, &more));
  EXPECT_TRUE(p.pressed);
  EXPECT_TRUE(more);
  ASSERT_TRUE(r.next(1000, &p, &ms, &more));
  EXPECT_FALSE(p.pressed);
  EXPECT_FALSE(more);
  EXPECT_FALSE(r.next(1000, &p, &ms, &more));   // read the chip
}

// A finger LVGL took before the refresh: its press stamp follows the reads,
// so LVGL's long-press clock counts the time the finger was down (#2114).
TEST(TouchReplay, AFingerDownBeforeTheRefreshMovesLvglsPressStamp) {
  TouchQueue q;
  TouchReplay r(q, 400);
  q.start(true, 1000);
  q.offer(down(4, 4), 1060);
  q.offer(down(4, 4), 1090);   // the held point moves on
  q.offer(up(4, 4), 1120);
  TouchPoint p{};
  bool more = false, restamp = false;
  uint32_t stamp = 0;
  ASSERT_TRUE(r.next(1800, &p, nullptr, &more, &restamp, &stamp));
  EXPECT_TRUE(p.pressed);
  EXPECT_TRUE(restamp);
  EXPECT_EQ(1800u - 90u, stamp);   // down 90 ms when that read was taken
  ASSERT_TRUE(r.next(1800, &p, nullptr, &more, &restamp, &stamp));
  EXPECT_FALSE(p.pressed);
  EXPECT_FALSE(restamp);   // a release isn't timed
}

TEST(TouchReplay, APressCaughtInTheRefreshLeavesTheStampAlone) {
  TouchQueue q;
  TouchReplay r(q, 400);
  q.start(false, 1000);
  q.offer(down(4, 4), 1060);
  q.offer(down(4, 4), 1090);
  q.offer(up(4, 4), 1120);
  TouchPoint p{};
  bool more = false, restamp = true;
  uint32_t stamp = 0;
  while (r.next(1800, &p, nullptr, &more, &restamp, &stamp)) EXPECT_FALSE(restamp);
}

TEST(TouchReplay, ASecondPressInTheSameRefreshIsNotTakenForTheFirst) {
  // The second press's stamp is LVGL's own, and the first keeps its time.
  TouchQueue q;
  TouchReplay r(q, 400);
  q.start(true, 1000);
  q.offer(down(4, 4), 1050);
  q.offer(up(4, 4), 1100);
  q.offer(down(9, 9), 1300);
  q.offer(down(9, 9), 1330);
  q.offer(up(9, 9), 1400);
  TouchPoint p{};
  bool more = false, restamp = false;
  uint32_t stamp = 0;
  ASSERT_TRUE(r.next(1800, &p, nullptr, &more, &restamp, &stamp));
  EXPECT_TRUE(restamp);
  EXPECT_EQ(1800u - 50u, stamp);
  int restamps = 0;
  while (r.next(1800, &p, nullptr, &more, &restamp, &stamp)) restamps += restamp;
  EXPECT_EQ(0, restamps);
}

TEST(TouchReplay, AReadBeforeLvglsStampIsTimedAsZero) {
  TouchQueue q;
  TouchReplay r(q, 400);
  q.start(true, 1000);
  q.offer(down(4, 4), 990);
  TouchPoint p{};
  bool more = false, restamp = false;
  uint32_t stamp = 0;
  ASSERT_TRUE(r.next(1800, &p, nullptr, &more, &restamp, &stamp));
  EXPECT_TRUE(restamp);
  EXPECT_EQ(1800u, stamp);
}

TEST(TouchReplay, ALongHoldIsHeldForTheLongPressTime) {
  TouchQueue q;
  TouchReplay r(q, 400);
  q.start(false);
  q.offer(down(4, 4), 10);
  q.offer(down(4, 5), 40);
  q.offer(up(4, 5), 900);
  TouchPoint p{};
  bool more = false;
  ASSERT_TRUE(r.next(1000, &p, nullptr, &more));   // the press
  EXPECT_TRUE(more);
  ASSERT_TRUE(r.next(1000, &p, nullptr, &more));   // its newest point: the hold begins
  EXPECT_TRUE(p.pressed);
  EXPECT_FALSE(more);
  EXPECT_TRUE(r.holding());
  const uint32_t past = 1000 + 400 + TouchReplay::kHoldMarginMs;
  for (uint32_t t = 1030; t <= past; t += 30) {
    ASSERT_TRUE(r.next(t, &p, nullptr, &more));
    EXPECT_TRUE(p.pressed) << t;
    EXPECT_EQ(5, p.y);
  }
  ASSERT_TRUE(r.next(past + 30, &p, nullptr, &more));
  EXPECT_FALSE(p.pressed);   // then the release
  EXPECT_FALSE(r.holding());
}

TEST(TouchReplay, ASlowLoopStillHandsOverAReadPastTheHoldTime) {
  // A read late enough to skip the hold time is still pressed, so LVGL's
  // long-press check sees it; the release comes on the read after.
  TouchQueue q;
  TouchReplay r(q, 400);
  q.start(false);
  q.offer(down(4, 4), 10);
  q.offer(up(4, 4), 900);
  TouchPoint p{};
  r.next(1000, &p, nullptr, nullptr);
  ASSERT_TRUE(r.holding());
  ASSERT_TRUE(r.next(1700, &p, nullptr, nullptr));
  EXPECT_TRUE(p.pressed);
  ASSERT_TRUE(r.next(1701, &p, nullptr, nullptr));
  EXPECT_FALSE(p.pressed);
}

TEST(TouchReplay, AShortPressIsNotHeld) {
  TouchQueue q;
  TouchReplay r(q, 400);
  q.start(false);
  q.offer(down(4, 4), 10);
  q.offer(up(4, 4), 390);   // just under the long-press time
  TouchPoint p{};
  bool more = false;
  r.next(1000, &p, nullptr, &more);
  EXPECT_TRUE(more);
  EXPECT_FALSE(r.holding());
}

TEST(TouchReplay, ReadsAfterTheHoldFollowIt) {
  TouchQueue q;
  TouchReplay r(q, 400);
  q.start(false);
  q.offer(down(1, 1), 0);
  q.offer(up(1, 1), 500);
  q.offer(down(2, 2), 600);
  q.offer(up(2, 2), 650);
  TouchPoint p{};
  bool more = false;
  r.next(1000, &p, nullptr, &more);   // held from here
  r.next(1500, &p, nullptr, &more);   // the read past the hold time
  EXPECT_TRUE(p.pressed);
  r.next(1530, &p, nullptr, &more);   // the release
  EXPECT_FALSE(p.pressed);
  EXPECT_TRUE(more);                  // the second tap, in the same pass
  r.next(1530, &p, nullptr, &more);
  EXPECT_TRUE(p.pressed);
  EXPECT_EQ(2, p.x);
}

TEST(TouchReplay, ClearEndsAHold) {
  TouchQueue q;
  TouchReplay r(q, 400);
  q.start(false);
  q.offer(down(1, 1), 0);
  q.offer(up(1, 1), 500);
  TouchPoint p{};
  r.next(1000, &p, nullptr, nullptr);
  ASSERT_TRUE(r.holding());
  r.clear();
  EXPECT_FALSE(r.holding());
  EXPECT_FALSE(r.next(1001, &p, nullptr, nullptr));
}

TEST(TouchQueue, AnEmptyQueueTakesNothing) {
  TouchQueue q;
  TouchPoint p{true, 9, 9};
  uint32_t ms = 7;
  EXPECT_FALSE(q.take(&p, &ms));
  EXPECT_TRUE(p.pressed);   // left alone
  EXPECT_EQ(7u, ms);
}

TEST(TouchQueue, ClearEmptiesItButKeepsTheDropCount) {
  TouchQueue q;
  q.start(false);
  for (uint8_t i = 0; i <= kTouchQueueLen; i++) q.offer(i % 2 ? up() : down(1, 1), i);
  q.clear();
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(1u, q.dropped());   // a running count: the log takes differences
  TouchPoint p{};
  EXPECT_FALSE(q.take(&p, nullptr));
  // After a clear a press is a press again.
  q.start(false);
  q.offer(down(3, 3), 1);
  ASSERT_TRUE(q.take(&p, nullptr));
  EXPECT_TRUE(p.pressed);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
