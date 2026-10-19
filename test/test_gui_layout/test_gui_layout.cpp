// Epic H (#1432) H1: layout selection by rotation, and the panel and touch
// mapping that go with it.

#include <gtest/gtest.h>

#include <string.h>

#include "helpers/gui/GuiLayout.h"
#include "helpers/gui/GuiViews.h"

using namespace offband::gui;

TEST(LayoutClass, PortraitIsAAndLandscapeIsB) {
  EXPECT_EQ(layoutClassFor(0), LayoutClass::A);
  EXPECT_EQ(layoutClassFor(90), LayoutClass::B);
  EXPECT_EQ(layoutClassFor(270), LayoutClass::B);
}

TEST(LayoutClass, AnyOtherRotationDrawsPortrait) {
  // A corrupt or unsupported pref must still draw: 180 isn't offered.
  EXPECT_EQ(layoutClassFor(180), LayoutClass::A);
  EXPECT_EQ(layoutClassFor(45), LayoutClass::A);
  EXPECT_EQ(layoutClassFor(65535), LayoutClass::A);
  EXPECT_EQ(layoutFor(180).rotation, 0);
}

TEST(Layout, ClassAAsDesigned) {
  const Layout l = layoutFor(0);
  EXPECT_EQ(l.w, 240);
  EXPECT_EQ(l.h, 320);
  EXPECT_EQ(l.status_h, 20);
  EXPECT_EQ(l.tab_h, 40);
  EXPECT_EQ(l.rail_w, 0);
  EXPECT_EQ(l.content_x, 0);
  EXPECT_EQ(l.content_y, 20);
  EXPECT_EQ(l.content_w, 240);
  EXPECT_EQ(l.content_h, 260);
  EXPECT_TRUE(l.hints);
  EXPECT_FALSE(l.pad_beside);
}

TEST(Layout, ClassBAsDesigned) {
  for (uint16_t r : {uint16_t(90), uint16_t(270)}) {
    const Layout l = layoutFor(r);
    EXPECT_EQ(l.rotation, r);
    EXPECT_EQ(l.w, 320);
    EXPECT_EQ(l.h, 240);
    EXPECT_EQ(l.status_h, 20);
    EXPECT_EQ(l.tab_h, 0);
    EXPECT_EQ(l.rail_w, 48);
    EXPECT_EQ(l.content_x, 48);
    EXPECT_EQ(l.content_y, 20);
    EXPECT_EQ(l.content_w, 272);
    EXPECT_EQ(l.content_h, 220);
  }
}

TEST(Layout, ClassBTakesTheDesignsLandscapeValues) {
  const Layout a = layoutFor(0), b = layoutFor(90);
  EXPECT_EQ(b.row_h, 28);
  EXPECT_EQ(b.cat_row_h, 30);
  EXPECT_EQ(b.sheet_left, 56);   // a sheet starts right of the rail
  EXPECT_EQ(b.node_rows, 5);
  EXPECT_EQ(b.key_w, 24);
  EXPECT_EQ(b.key_h, 25);
  EXPECT_EQ(b.logo, 40);
  EXPECT_FALSE(b.hints);
  EXPECT_TRUE(b.pad_beside);
  EXPECT_EQ(a.row_h, 34);
  EXPECT_EQ(a.node_rows, 7);
  EXPECT_EQ(a.logo, 64);
}

TEST(Layout, ContentFitsBesideTheChrome) {
  for (uint16_t r : kRotations) {
    const Layout l = layoutFor(r);
    EXPECT_EQ(l.content_x + l.content_w, l.w);
    EXPECT_EQ(l.content_y + l.content_h + l.tab_h, l.h);
    // A picker's window and its title fit in the content height.
    EXPECT_LE(l.pick_rows * l.sheet_row_h + 40, l.content_h);
  }
}

TEST(Rotation, SettingTextAndOrder) {
  ASSERT_EQ(kRotationCount, 3u);
  EXPECT_STREQ(rotationText(0), "Portrait 0°");
  EXPECT_STREQ(rotationText(90), "Landscape 90°");
  EXPECT_STREQ(rotationText(270), "Landscape 270°");
  EXPECT_STREQ(rotationText(180), "Portrait 0°");
  EXPECT_EQ(rotationIndex(270), 2);
  EXPECT_EQ(rotationIndex(180), 0);   // not offered: reads as portrait
}

TEST(Madctl, OrientationBitsOnly) {
  EXPECT_EQ(madctlFor(0, 0x00), 0x00);
  EXPECT_EQ(madctlFor(90, 0x00), 0x60);    // MV|MX
  EXPECT_EQ(madctlFor(270, 0x00), 0xA0);   // MV|MY
  // The board's other bits (BGR 0x08, refresh order 0x04) are kept.
  EXPECT_EQ(madctlFor(90, 0x08), 0x68);
  EXPECT_EQ(madctlFor(270, 0x0C), 0xAC);
  EXPECT_EQ(madctlFor(180, 0x08), 0x08);   // not offered: the portrait value
}

TEST(Touch, PortraitUnchanged) {
  uint16_t x = 10, y = 300;
  rotateTouch(0, &x, &y);
  EXPECT_EQ(x, 10);
  EXPECT_EQ(y, 300);
}

TEST(Touch, CornersFollowTheRotation) {
  // 90 (MV|MX): LVGL (lx, ly) lands on portrait (239 - ly, lx).
  uint16_t x = 239, y = 0;   // portrait top right
  rotateTouch(90, &x, &y);
  EXPECT_EQ(x, 0);
  EXPECT_EQ(y, 0);           // LVGL top left
  x = 0, y = 319;            // portrait bottom left
  rotateTouch(90, &x, &y);
  EXPECT_EQ(x, 319);
  EXPECT_EQ(y, 239);         // LVGL bottom right
  // 270 (MV|MY): LVGL (lx, ly) lands on portrait (ly, 319 - lx).
  x = 0, y = 319;
  rotateTouch(270, &x, &y);
  EXPECT_EQ(x, 0);
  EXPECT_EQ(y, 0);
  x = 239, y = 0;
  rotateTouch(270, &x, &y);
  EXPECT_EQ(x, 319);
  EXPECT_EQ(y, 239);
}

TEST(Touch, OutOfRangeIsClampedInside) {
  uint16_t x = 900, y = 900;
  rotateTouch(90, &x, &y);
  EXPECT_LT(x, 320);
  EXPECT_LT(y, 240);
  x = 900, y = 900;
  rotateTouch(0, &x, &y);
  EXPECT_EQ(x, 239);
  EXPECT_EQ(y, 319);
}

TEST(Keyboard, ClassBKeysFitTheContent) {
  const Layout b = layoutFor(90);
  KeyRect k[40];
  for (KeyPage p : {KeyPage::Letters, KeyPage::Symbols, KeyPage::Symbols2}) {
    const size_t n = pageLayout(p, b.content_w, 100, k, 40, b.key_w, b.key_h);
    ASSERT_GT(n, 30u);
    for (size_t i = 0; i < n; i++) {
      EXPECT_GE(k[i].x, 0) << k[i].label;
      EXPECT_LE(k[i].x + k[i].w, b.content_w) << k[i].label;
      EXPECT_EQ(k[i].h, b.key_h) << k[i].label;
      EXPECT_LE(k[i].y + k[i].h, 100 + keyboardHeight(b.key_h));
    }
  }
}

TEST(Keyboard, PortraitIsUnchanged) {
  KeyRect a[40], b[40];
  const size_t na = pageLayout(KeyPage::Letters, 240, 100, a, 40);
  const size_t nb = pageLayout(KeyPage::Letters, 240, 100, b, 40, 21, kKeyH);
  ASSERT_EQ(na, nb);
  for (size_t i = 0; i < na; i++) {
    EXPECT_EQ(a[i].x, b[i].x);
    EXPECT_EQ(a[i].w, b[i].w);
    EXPECT_EQ(a[i].h, b[i].h);
  }
  EXPECT_EQ(keyboardHeight(kKeyH), 4 * 32 + 3 * 3);
}

TEST(Keyboard, ClassBKeyboardLeavesRoomForTheDraft) {
  // Compose in class B: status, header, one draft line and the keyboard
  // fit in 240 px (mockup 5).
  const Layout b = layoutFor(270);
  EXPECT_LE(b.status_h + 28 + 36 + keyboardHeight(b.key_h) + 4, b.h);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
