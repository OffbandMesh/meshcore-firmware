// #2009: the GUI on the T-Deck Pro's e-paper panel. Pixels to black and
// white, and when a refresh goes out.

#include <gtest/gtest.h>

#include "helpers/gui/EinkFrame.h"
#include "helpers/gui/GuiTheme.h"

using namespace offband::gui;

// What the flush feeds einkInk(): LVGL 8.3's lv_color_brightness() on the
// color as the RGB565 build stores it (lv_color_to32's 565 expansion).
static uint8_t lvBrightness(uint32_t rgb) {
  const uint32_t r5 = ((rgb >> 16) & 0xFF) >> 3, g6 = ((rgb >> 8) & 0xFF) >> 2, b5 = (rgb & 0xFF) >> 3;
  const uint32_t r = (r5 * 263 + 7) >> 5, g = (g6 * 259 + 3) >> 6, b = (b5 * 263 + 7) >> 5;
  return uint8_t((3 * r + b + 4 * g) >> 3);
}

static bool ink(uint32_t rgb) { return einkInk(lvBrightness(rgb)); }

TEST(EinkInk, OneCutoffNoGreys) {
  EXPECT_FALSE(einkInk(0));
  EXPECT_FALSE(einkInk(kEinkInkCutoff));
  EXPECT_TRUE(einkInk(kEinkInkCutoff + 1));
  EXPECT_TRUE(einkInk(255));
  // Every brightness is one or the other, and the change happens once.
  int flips = 0;
  for (int l = 1; l <= 255; l++) flips += einkInk(uint8_t(l)) != einkInk(uint8_t(l - 1));
  EXPECT_EQ(flips, 1);
}

TEST(EinkInk, PanelShowsTheHighContrastTheme) {
  EXPECT_EQ(kEinkTheme, ThemeId::MonoHC);
  EXPECT_TRUE(themeTokens(kEinkTheme, 0xF2C14E).flat);
}

TEST(EinkInk, HighContrastBackgroundsArePaper) {
  const ThemeTokens t = themeTokens(kEinkTheme, 0xF2C14E);
  EXPECT_FALSE(ink(t.bg));
  EXPECT_FALSE(ink(t.bg2));
  EXPECT_FALSE(ink(t.bg3));
  EXPECT_FALSE(ink(t.wash & 0xFFFFFF));   // opaque in this theme
  EXPECT_FALSE(ink(t.sigInk));            // text on a selected row
}

TEST(EinkInk, HighContrastTextAndLinesAreInk) {
  const ThemeTokens t = themeTokens(kEinkTheme, 0xF2C14E);
  for (uint32_t c : {t.line, t.line2, t.ink, t.ink2, t.dim, t.sig, t.warn, t.ok, t.bad}) EXPECT_TRUE(ink(c)) << std::hex << c;
  for (uint32_t c : t.nodeType) EXPECT_TRUE(ink(c)) << std::hex << c;
}

TEST(EinkInk, CutoffHasMarginBothSides) {
  // RGB565 rounding moves a brightness by a few steps; 16 either side keeps a
  // background from tipping into ink, or a hairline out of it.
  const ThemeTokens t = themeTokens(kEinkTheme, 0xF2C14E);
  EXPECT_LE(lvBrightness(t.bg3) + 16, kEinkInkCutoff);
  EXPECT_GE(lvBrightness(t.line2), kEinkInkCutoff + 16);
}

// LVGL 8.3's lv_color_mix() per channel: (fg * opa + bg * (255 - opa) + 128) / 255,
// near enough at 8 bits for a cutoff test.
static uint32_t mix(uint32_t fg, uint32_t bg, uint8_t opa) {
  uint32_t out = 0;
  for (int s = 0; s <= 16; s += 8) {
    const uint32_t f = (fg >> s) & 0xFF, b = (bg >> s) & 0xFF;
    out |= ((f * opa + b * (255 - opa) + 128) / 255) << s;
  }
  return out;
}

TEST(EinkInk, OpacityCuesAreLostOnThePanel) {
  // A known cost of one cutoff (owner's call, 2026-11-09: crisp over grey). In
  // the high-contrast theme, ink at 50% (a disabled row) or 60% (a message
  // still sending) over the background prints as full ink, the same as an
  // enabled row. If this starts failing, the e-paper cue for those states has
  // changed: check the screens that rely on it.
  const ThemeTokens t = themeTokens(kEinkTheme, 0xF2C14E);
  EXPECT_TRUE(ink(mix(t.ink, t.bg, 128)));   // LV_OPA_50
  EXPECT_TRUE(ink(mix(t.ink, t.bg, 153)));   // LV_OPA_60
  EXPECT_TRUE(ink(mix(t.dim, t.bg, 128)));   // dim text, disabled
}

TEST(EinkInk, FaintBlendsStayPaper) {
  // The other side: a blend under about 31% of white stays paper, so a faint
  // wash or the outer edge of an anti-aliased corner doesn't speckle.
  const ThemeTokens t = themeTokens(kEinkTheme, 0xF2C14E);
  EXPECT_FALSE(ink(mix(t.ink, t.bg, 64)));   // LV_OPA_20, about 25%
  EXPECT_TRUE(ink(mix(t.ink, t.bg, 102)));   // LV_OPA_40
}

TEST(EinkInk, ColorThemesWouldLoseLines) {
  // Why the panel doesn't show the stored theme: at this cutoff the default
  // theme's dividers vanish into the paper. If this ever passes as ink, the
  // forced theme can be revisited.
  const ThemeTokens t = themeTokens(ThemeId::Offband, 0xF2C14E);
  EXPECT_FALSE(ink(t.line));
  EXPECT_FALSE(ink(t.line2));
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
