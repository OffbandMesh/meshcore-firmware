#include <gtest/gtest.h>
#include <set>
#include <utility>
#include <string.h>
#include <stdlib.h>

#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/OffbandSplash.h>
#include <helpers/ui/OLEDDisplayFonts.h>
#include <helpers/ui/TpFont.h>
#include "../../variants/qcc_badge/QccSplashLayout.h"
#include "../../variants/qcc_badge/QccSplashArt.h"

class SplashDisplay : public DisplayDriver {
public:
  std::set<std::pair<int, int>> text_px;   // everything drawn with fillRect (the fonts)
  int xbm_calls = 0, xbm_x = -1, xbm_y = -1, xbm_w = 0, xbm_h = 0;
  SplashDisplay() : DisplayDriver(128, 64) {}
  bool isOn() override { return true; }
  void turnOn() override {}
  void turnOff() override {}
  void clear() override {}
  void startFrame(ColorVal) override {}
  void setTextSize(int) override {}
  void setColor(ColorVal) override {}
  void setCursor(int, int) override {}
  void print(const char*) override {}
  void fillRect(int x, int y, int w, int h) override {
    for (int i = 0; i < w; i++)
      for (int j = 0; j < h; j++) text_px.insert({x + i, y + j});
  }
  void drawRect(int, int, int, int) override {}
  void drawXbm(int x, int y, const uint8_t*, int w, int h) override {
    xbm_calls++; xbm_x = x; xbm_y = y; xbm_w = w; xbm_h = h;
  }
  uint16_t getTextWidth(const char* s) override { return (uint16_t)(strlen(s) * 6); }
  void endFrame() override {}
};

static std::set<std::pair<int, int>> eyeInk() {
  std::set<std::pair<int, int>> ink;
  const int rb = (QCC_EYE_W + 7) / 8;
  for (int r = 0; r < QCC_EYE_H; r++)
    for (int c = 0; c < QCC_EYE_W; c++)
      if (qcc_eye[r * rb + c / 8] & (0x80 >> (c % 8))) ink.insert({qcc::kEyeX + c, qcc::kEyeY + r});
  return ink;
}

static void drawWith(SplashDisplay& d, const char* ver) {
  offband::SplashInfo si(ver, "v1.17.0", "12 Sep 2026");
  offband::drawEventSplash(d, si);
}

TEST(QccSplash, DrawsTheEyeCenteredAtTheTop) {
  SplashDisplay d;
  drawWith(d, "v1.5.0-beta6");
  EXPECT_EQ(1, d.xbm_calls);
  EXPECT_EQ(39, d.xbm_x);
  EXPECT_EQ(0, d.xbm_y);
  EXPECT_EQ(50, d.xbm_w);
  EXPECT_EQ(50, d.xbm_h);
}

TEST(QccSplash, NothingIsDrawnOffTheCanvas) {
  SplashDisplay d;
  drawWith(d, "v1.5.0-beta6");
  for (auto& p : d.text_px) {
    EXPECT_GE(p.first, 0);   EXPECT_LE(p.first, 127);
    EXPECT_GE(p.second, 0);  EXPECT_LE(p.second, 63);
  }
}

TEST(QccSplash, TextNeverTouchesTheEye) {
  const auto ink = eyeInk();
  // The second version yields the widest corner lines the splitter can produce:
  // "v123.45" (38 px) and "beta6+1" (39 px), against the 40 px kCornerMaxW.
  for (const char* ver : {"v1.5.0-beta6+17*", "v123.456.789-beta6+1"}) {
    SplashDisplay d;
    drawWith(d, ver);
    for (auto& t : d.text_px)
      for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
          EXPECT_EQ(0u, ink.count({t.first + dx, t.second + dy}))
              << ver << ": text pixel at " << t.first << "," << t.second << " touches the eye";
  }
}

TEST(QccSplash, TheWidestAllowedCornerTextClearsTheEyeInk) {
  // No version reaches exactly kCornerMaxW, so pin the bound itself: right-aligned text
  // of that width starts here, and must not touch the eye's ink in the corner rows.
  // (drawXbm is transparent, so the eye's blank columns do not count.)
  const int text_left = qcc::kRightEdgeX + 1 - qcc::kCornerMaxW;
  const int top = qcc::kCornerLine1Y;
  const int bottom = qcc::kCornerLine2Y + offband::tpfont::height(ArialMT_Plain_10) - 1;
  int eye_right = -1;
  for (auto& p : eyeInk())
    if (p.second >= top && p.second <= bottom && p.first > eye_right) eye_right = p.first;
  EXPECT_GE(text_left - eye_right, 2);   // the same no-touch margin as TextNeverTouchesTheEye
}

TEST(QccSplash, TheConNameIsCenteredBelowTheEye) {
  SplashDisplay d;
  drawWith(d, "v1.6.0");
  int minx = 999, maxx = -1;
  for (auto& p : d.text_px) {
    if (p.second < 46) continue;
    minx = p.first < minx ? p.first : minx;
    maxx = p.first > maxx ? p.first : maxx;
  }
  EXPECT_NEAR(64, (minx + maxx + 1) / 2, 1);
}

struct Split { const char* in; const char* l1; const char* l2; };

class CornerVersion : public ::testing::TestWithParam<Split> {};

TEST_P(CornerVersion, SplitsForTheTopRightCorner) {
  char l1[24], l2[24];
  qcc::splitCornerVersion(GetParam().in, l1, sizeof(l1), l2, sizeof(l2), qcc::kCornerMaxW);
  EXPECT_STREQ(GetParam().l1, l1);
  EXPECT_STREQ(GetParam().l2, l2);
}

INSTANTIATE_TEST_SUITE_P(Qcc, CornerVersion, ::testing::Values(
  Split{"v1.5.0-beta6", "v1.5.0", "beta6"},
  Split{"v1.5.0-beta6+17", "v1.5.0", "beta6"},     // "beta6+17" is 45 px, over 40
  Split{"v1.5.0-beta6+17*", "v1.5.0", "beta6"},
  Split{"v1.5.0-rc1", "v1.5.0", "rc1"},
  Split{"v1.6.0", "v1.6.0", ""},
  Split{"v1.6.0+3*", "v1.6.0", "+3*"},
  Split{"v0.14.0 bench", "v0.14.0", ""},           // a build tag is left to the About screen
  Split{"v1.5.0-rc1+3* qcc", "v1.5.0", "rc1+3*"},
  Split{"", "", ""}
));

TEST(CornerVersionEdges, NullIsEmpty) {
  char l1[8] = "x", l2[8] = "y";
  qcc::splitCornerVersion(nullptr, l1, sizeof(l1), l2, sizeof(l2), qcc::kCornerMaxW);
  EXPECT_STREQ("", l1);
  EXPECT_STREQ("", l2);
}

TEST(CornerVersionEdges, AnOverlongReleaseIsShortenedToFit) {
  char l1[24], l2[24];
  qcc::splitCornerVersion("v123.456.789", l1, sizeof(l1), l2, sizeof(l2), qcc::kCornerMaxW);
  EXPECT_LE(offband::tpfont::textWidth(ArialMT_Plain_10, l1), qcc::kCornerMaxW);
  EXPECT_EQ(0, strncmp("v123", l1, 4));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
