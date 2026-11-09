// #2009: the GUI on the T-Deck Pro's e-paper panel. Pixels to black and
// white, and when a refresh goes out.

#include <gtest/gtest.h>

#include "helpers/gui/EinkFrame.h"
#include "helpers/gui/GuiTheme.h"

using namespace offband::gui;

// LVGL 8.3's lv_color_brightness(), which the flush feeds to einkInk().
static uint8_t lvBrightness(uint32_t rgb) {
  const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  return uint8_t((3 * r + b + 4 * g) >> 3);
}

static int inkCount(uint8_t luma) {
  int n = 0;
  for (int16_t y = 0; y < 4; y++)
    for (int16_t x = 0; x < 4; x++) n += einkInk(luma, x, y);
  return n;
}

TEST(EinkInk, DarkStaysPaperAndBrightIsInk) {
  for (int16_t y = 0; y < 8; y++) {
    for (int16_t x = 0; x < 8; x++) {
      EXPECT_FALSE(einkInk(0, x, y));
      EXPECT_FALSE(einkInk(kEinkPaperLuma, x, y));
      EXPECT_TRUE(einkInk(255, x, y));
      EXPECT_TRUE(einkInk(kEinkInkLuma, x, y));
    }
  }
}

TEST(EinkInk, EveryThemeBackgroundIsCleanPaper) {
  // A background that speckled would grey the whole screen.
  for (ThemeId id : {ThemeId::Offband, ThemeId::Steel, ThemeId::MonoHC, ThemeId::Accent}) {
    const ThemeTokens t = themeTokens(id, 0xF2C14E);
    EXPECT_EQ(inkCount(lvBrightness(t.bg)), 0) << int(id);
    EXPECT_EQ(inkCount(lvBrightness(t.bg2)), 0) << int(id);
  }
}

TEST(EinkInk, EveryThemeInkIsSolidBlack) {
  for (ThemeId id : {ThemeId::Offband, ThemeId::Steel, ThemeId::MonoHC, ThemeId::Accent}) {
    const ThemeTokens t = themeTokens(id, 0xF2C14E);
    EXPECT_EQ(inkCount(lvBrightness(t.ink)), 16) << int(id);
  }
}

TEST(EinkInk, GreysDitherInOrder) {
  // More brightness never means less ink, and a mid grey is partly inked.
  int last = 0;
  for (int l = 0; l <= 255; l++) {
    const int n = inkCount(uint8_t(l));
    EXPECT_GE(n, last) << l;
    last = n;
  }
  const int mid = inkCount(uint8_t((kEinkPaperLuma + kEinkInkLuma) / 2));
  EXPECT_GT(mid, 4);
  EXPECT_LT(mid, 12);
}

TEST(EinkInk, NegativeCoordinatesDontCrash) {
  // The pattern indexes by the low bits only.
  EXPECT_EQ(einkInk(128, -1, -1), einkInk(128, 3, 3));
}

TEST(EinkArea, GrowsToCoverEverything) {
  EinkArea a;
  EXPECT_TRUE(a.empty());
  a.add(10, 20, 30, 40);
  a.add(0, 25, 15, 100);
  EXPECT_FALSE(a.empty());
  EXPECT_EQ(a.x1, 0);
  EXPECT_EQ(a.y1, 20);
  EXPECT_EQ(a.x2, 30);
  EXPECT_EQ(a.y2, 100);
}

TEST(EinkArea, AnEmptyRectangleAddsNothing) {
  EinkArea a;
  a.add(5, 5, 4, 10);
  EXPECT_TRUE(a.empty());
  a.add(1, 1, 2, 2);
  a.add(50, 50, 40, 60);
  EXPECT_EQ(a.x2, 2);
  EXPECT_EQ(a.y2, 2);
}

TEST(EinkPacer, NothingDrawnNothingPushed) {
  EinkPacer p(1000, 20);
  EXPECT_EQ(p.due(0), EinkPush::None);
  EXPECT_EQ(p.due(100000), EinkPush::None);
}

TEST(EinkPacer, FirstRefreshIsFullAndImmediate) {
  EinkPacer p(1000, 20);
  p.mark(0, 0, 239, 319);
  EXPECT_EQ(p.due(5), EinkPush::Full);
}

TEST(EinkPacer, WaitsOutTheGapThenGoesPartial) {
  EinkPacer p(1000, 20);
  p.mark(0, 0, 239, 319);
  p.pushed(p.due(0), 0);
  EXPECT_TRUE(p.dirty().empty());
  p.mark(10, 10, 20, 20);
  EXPECT_EQ(p.due(999), EinkPush::None);
  EXPECT_EQ(p.due(1000), EinkPush::Partial);
}

TEST(EinkPacer, FramesInsideTheGapMergeIntoOneRefresh) {
  EinkPacer p(1000, 20);
  p.mark(0, 0, 239, 319);
  p.pushed(EinkPush::Full, 0);
  p.mark(10, 10, 20, 20);
  p.mark(100, 200, 110, 210);
  ASSERT_EQ(p.due(1500), EinkPush::Partial);
  EXPECT_EQ(p.dirty().x1, 10);
  EXPECT_EQ(p.dirty().y2, 210);
}

TEST(EinkPacer, EveryNthRefreshIsFull) {
  EinkPacer p(1000, 4);
  uint32_t t = 0;
  p.mark(0, 0, 1, 1);
  p.pushed(p.due(t), t);   // the first, full
  for (int round = 0; round < 3; round++) {
    for (int i = 0; i < 3; i++) {
      t += 1000;
      p.mark(0, 0, 1, 1);
      EXPECT_EQ(p.due(t), EinkPush::Partial) << round << "/" << i;
      p.pushed(EinkPush::Partial, t);
    }
    t += 1000;
    p.mark(0, 0, 1, 1);
    EXPECT_EQ(p.due(t), EinkPush::Full) << round;
    p.pushed(EinkPush::Full, t);
  }
}

TEST(EinkPacer, RequestFullTakesTheNextRefreshOnly) {
  EinkPacer p(1000, 20);
  p.mark(0, 0, 1, 1);
  p.pushed(EinkPush::Full, 0);
  p.requestFull();
  EXPECT_EQ(p.due(5000), EinkPush::None);   // nothing drawn yet
  p.mark(0, 0, 1, 1);
  EXPECT_EQ(p.due(5000), EinkPush::Full);
  p.pushed(EinkPush::Full, 5000);
  p.mark(0, 0, 1, 1);
  EXPECT_EQ(p.due(6000), EinkPush::Partial);
}

TEST(EinkPacer, SurvivesMillisWrap) {
  EinkPacer p(1000, 20);
  p.mark(0, 0, 1, 1);
  p.pushed(EinkPush::Full, 0xFFFFFF00u);
  p.mark(0, 0, 1, 1);
  EXPECT_EQ(p.due(0xFFFFFF80u), EinkPush::None);
  EXPECT_EQ(p.due(0x00000400u), EinkPush::Partial);   // 1280 ms later, across the wrap
}

TEST(EinkPacer, NoneIsNotCountedAsAPush) {
  EinkPacer p(1000, 20);
  p.mark(0, 0, 1, 1);
  p.pushed(EinkPush::None, 0);
  EXPECT_EQ(p.due(0), EinkPush::Full);   // still the first refresh
}

TEST(EinkPacer, ZeroFullEveryMeansEveryRefreshIsFull) {
  EinkPacer p(1000, 0);
  p.mark(0, 0, 1, 1);
  p.pushed(EinkPush::Full, 0);
  p.mark(0, 0, 1, 1);
  EXPECT_EQ(p.due(1000), EinkPush::Full);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
