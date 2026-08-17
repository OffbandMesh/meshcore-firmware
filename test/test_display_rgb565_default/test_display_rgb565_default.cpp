// #749: the DisplayDriver::drawRGB565 default-implementation contract.
//
// THE INVARIANT UNDER TEST
//   A driver that does NOT override drawRGB565 must (a) still compile, and (b) do
//   NOTHING when it is called -- in particular it must not fall back to drawXbm()
//   or any other drawing primitive.
//
// WHY THIS IS WORTH A TEST
//   There are 12 DisplayDriver subclasses in the tree and ~115 mono-OLED envs, and
//   #749's whole "no changes needed to existing drivers" claim rests on this one
//   property. Two ways to break it, both silent at review time:
//
//     1. Making drawRGB565 PURE virtual. Then every one of the 12 subclasses needs
//        a stub. This suite stops compiling if that happens -- MonoStub below is
//        deliberately a minimal driver that overrides only the pre-existing pure
//        virtuals, so it stands in for "every existing driver, untouched".
//
//     2. Giving the default a threshold-to-1-bit body that forwards to drawXbm().
//        That was considered and rejected: 220x128 colour art dithered onto a
//        128x64 mono OLED reads worse than the purpose-made XBM the splash already
//        has. A mono panel must keep its XBM splash and ignore colour art entirely.
//        The call counters catch a fallback being added later.

#include <gtest/gtest.h>

#include "helpers/ui/DisplayDriver.h"

namespace {

// Minimal driver: overrides the pure virtuals that existed BEFORE #749 and nothing
// else. If this class ever fails to compile, drawRGB565 stopped being defaulted.
class MonoStub : public DisplayDriver {
public:
  int xbm_calls = 0;
  int fill_calls = 0;
  int rect_calls = 0;
  int print_calls = 0;

  MonoStub() : DisplayDriver(128, 64) {}

  bool isOn() override { return true; }
  void turnOn() override {}
  void turnOff() override {}
  void clear() override {}
  void startFrame(ColorVal bkg) override {}
  void setTextSize(int sz) override {}
  void setColor(ColorVal c) override {}
  void setCursor(int x, int y) override {}
  void print(const char* str) override { print_calls++; }
  void fillRect(int x, int y, int w, int h) override { fill_calls++; }
  void drawRect(int x, int y, int w, int h) override { rect_calls++; }
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override { xbm_calls++; }
  uint16_t getTextWidth(const char* str) override { return (uint16_t)(strlen(str) * 6); }
  void endFrame() override {}

  int totalDraws() const { return xbm_calls + fill_calls + rect_calls + print_calls; }
};

}  // namespace

TEST(DisplayDriverRgb565Default, IsCallableOnADriverThatDoesNotOverrideIt) {
  MonoStub d;
  const uint16_t art[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};

  // The point of the test is that this line compiles and runs at all.
  d.drawRGB565(0, 0, art, 2, 2);

  SUCCEED();
}

TEST(DisplayDriverRgb565Default, DrawsNothingRatherThanFallingBackToXbm) {
  MonoStub d;
  const uint16_t art[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};

  d.drawRGB565(0, 0, art, 2, 2);

  EXPECT_EQ(d.xbm_calls, 0) << "the default must not dither colour art down to drawXbm";
  EXPECT_EQ(d.totalDraws(), 0) << "the default must not draw anything at all";
}

TEST(DisplayDriverRgb565Default, ToleratesNullAndDegenerateInput) {
  MonoStub d;

  d.drawRGB565(0, 0, nullptr, 2, 2);
  d.drawRGB565(0, 0, nullptr, 0, 0);

  EXPECT_EQ(d.totalDraws(), 0);
}

TEST(DisplayDriverRgb565Default, AnOverridingDriverStillReceivesTheCall) {
  // Guards the other direction: the base must be VIRTUAL, not a non-virtual stub
  // that silently swallows a driver's override (which is how the RC32 would render
  // an empty splash while every test above still passed).
  class ColourStub : public MonoStub {
  public:
    int rgb_calls = 0;
    int last_w = 0;
    void drawRGB565(int x, int y, const uint16_t* px, int w, int h) override {
      rgb_calls++;
      last_w = w;
    }
  };

  ColourStub c;
  DisplayDriver& as_base = c;   // dispatch through the base reference, as UITask does
  const uint16_t art[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};

  as_base.drawRGB565(3, 4, art, 2, 2);

  EXPECT_EQ(c.rgb_calls, 1) << "override was not reached through a DisplayDriver&";
  EXPECT_EQ(c.last_w, 2);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
