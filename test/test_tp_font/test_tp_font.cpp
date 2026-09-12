#include <gtest/gtest.h>
#include <set>
#include <utility>
#include <string.h>

#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/OLEDDisplayFonts.h>
#include <helpers/ui/TpFont.h>

using offband::tpfont::drawText;
using offband::tpfont::height;
using offband::tpfont::textWidth;
using offband::tpfont::charAdvance;

class PixelDisplay : public DisplayDriver {
public:
  std::set<std::pair<int, int>> px;
  PixelDisplay() : DisplayDriver(128, 64) {}
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
      for (int j = 0; j < h; j++) px.insert({x + i, y + j});
  }
  void drawRect(int, int, int, int) override {}
  void drawXbm(int, int, const uint8_t*, int, int) override {}
  uint16_t getTextWidth(const char* s) override { return (uint16_t)(strlen(s) * 6); }
  void endFrame() override {}
  int minX() const { int m = 999; for (auto& p : px) m = p.first < m ? p.first : m; return m; }
  int maxX() const { int m = -1; for (auto& p : px) m = p.first > m ? p.first : m; return m; }
  int minY() const { int m = 999; for (auto& p : px) m = p.second < m ? p.second : m; return m; }
  int maxY() const { int m = -1; for (auto& p : px) m = p.second > m ? p.second : m; return m; }
};

TEST(TpFont, HeightsComeFromTheFontHeader) {
  EXPECT_EQ(13, height(ArialMT_Plain_10));
  EXPECT_EQ(19, height(ArialMT_Plain_16));
}

TEST(TpFont, WidthsMatchTheJumpTable) {
  EXPECT_EQ(106, textWidth(ArialMT_Plain_16, "QueenCityCon"));
  EXPECT_EQ(114, textWidth(ArialMT_Plain_16, "Queen City Con"));
  EXPECT_EQ(38, textWidth(ArialMT_Plain_10, "Offband"));
  EXPECT_EQ(29, textWidth(ArialMT_Plain_10, "v1.5.0"));
  EXPECT_EQ(27, textWidth(ArialMT_Plain_10, "beta6"));
}

TEST(TpFont, SpaceAdvancesWithoutInk) {
  EXPECT_EQ(3, charAdvance(ArialMT_Plain_10, ' '));
  PixelDisplay d;
  EXPECT_EQ(3, drawText(d, 0, 0, ArialMT_Plain_10, " "));
  EXPECT_TRUE(d.px.empty());
}

TEST(TpFont, CharsOutsideTheFontDrawAsQuestionMark) {
  EXPECT_EQ(charAdvance(ArialMT_Plain_10, '?'), charAdvance(ArialMT_Plain_10, '\x01'));
}

TEST(TpFont, NullAndEmptyStringsAreHarmless) {
  PixelDisplay d;
  EXPECT_EQ(0, textWidth(ArialMT_Plain_10, nullptr));
  EXPECT_EQ(7, drawText(d, 7, 0, ArialMT_Plain_10, ""));
  EXPECT_TRUE(d.px.empty());
}

TEST(TpFont, QueenCityConRendersExactly) {
  PixelDisplay d;
  EXPECT_EQ(106, drawText(d, 0, 0, ArialMT_Plain_16, "QueenCityCon"));
  EXPECT_EQ(260u, d.px.size());
  EXPECT_EQ(1, d.minX());
  EXPECT_EQ(103, d.maxX());
  EXPECT_EQ(3, d.minY());
  EXPECT_EQ(17, d.maxY());
}

TEST(TpFont, OffbandRendersExactlyAtAnOffset) {
  PixelDisplay d;
  drawText(d, 10, 20, ArialMT_Plain_10, "Offband");
  EXPECT_EQ(88u, d.px.size());
  EXPECT_EQ(11, d.minX());
  EXPECT_EQ(46, d.maxX());
  EXPECT_EQ(23, d.minY());
  EXPECT_EQ(29, d.maxY());
}

// One glyph, 13 px high (two bytes per column). Its column has ink in row 0 and a stray
// bit in row 15, inside the byte padding but outside the 13-pixel cell.
static const uint8_t kPaddedFont[] = {
  1, 13, 'A', 1,   // max width, height, first char, char count
  0, 0, 2, 2,      // 'A': offset 0, 2 glyph bytes, advance 2
  0x01, 0x80,      // column 0: row 0, and row 15
};

TEST(TpFont, InkBelowTheCellIsNotDrawn) {
  PixelDisplay d;
  EXPECT_EQ(2, drawText(d, 0, 0, kPaddedFont, "A"));
  EXPECT_EQ(1u, d.px.size());
  EXPECT_EQ(1u, d.px.count({0, 0}));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
