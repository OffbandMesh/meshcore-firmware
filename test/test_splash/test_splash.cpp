// #822 / #705: the shared splash component's artwork-selection contract.
//
// WHY THIS IS THE THING UNDER TEST
//   Six independent splash implementations existed, five of them drawing MeshCore
//   artwork, and colour art was enabled by a per-VARIANT build flag. That flag was
//   silent when omitted, and boards were missed with it twice -- the RC32 got the
//   colour splash and every other colour panel quietly fell back to mono, with
//   nothing failing.
//
//   So the invariant that matters is not "does it draw" but "does the RIGHT panel
//   get the RIGHT artwork, decided by the DRIVER rather than by remembering to set
//   a flag". These tests encode that decision, so a future board cannot be missed
//   silently -- it either declares colour capability and gets colour art, or does
//   not and gets the 1-bit art. There is no third outcome where branding vanishes.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "helpers/ui/DisplayDriver.h"
#include "helpers/ui/OffbandSplash.h"

// The UIColor statics come from src/helpers/ui/NativeUIColorDefs.cpp, which the
// native env compiles in place of the display driver that defines them in firmware.

namespace {

// Records what a splash actually asked the driver to draw.
struct DrawnText { int x, y; std::string s; };
struct DrawnArt  { int x, y, w, h; };

class RecordingDisplay : public DisplayDriver {
public:
  int xbm_calls = 0;
  int rgb_calls = 0;
  std::vector<DrawnText> texts;
  std::vector<DrawnArt> xbms;
  bool colour_capable;
  int phys_w, phys_h;
  int cur_x = 0, cur_y = 0;

  RecordingDisplay(bool colour, int pw = 128, int ph = 64)
      : DisplayDriver(128, 64), colour_capable(colour), phys_w(pw), phys_h(ph) {}

  // Logical-geometry ctor, for panels that are not 128x64 (the 72x40 techo card).
  RecordingDisplay(int lw, int lh)
      : DisplayDriver(lw, lh), colour_capable(false), phys_w(lw), phys_h(lh) {}

  // Capability IS the asset: a driver declares colour art by returning it, so it
  // cannot claim the capability and then have nothing to draw.
  static const uint16_t kPixels[4];
  const ColourArt* colourSplashArt() const override {
    static const ColourArt art{kPixels, 2, 2, 0, 0};
    return colour_capable ? &art : nullptr;
  }
  int physicalWidth() const override { return phys_w; }
  int physicalHeight() const override { return phys_h; }

  bool isOn() override { return true; }
  void turnOn() override {}
  void turnOff() override {}
  void clear() override {}
  void startFrame(ColorVal bkg) override {}
  void setTextSize(int sz) override {}
  void setColor(ColorVal c) override {}
  void setCursor(int x, int y) override { cur_x = x; cur_y = y; }
  void print(const char* s) override { texts.push_back({cur_x, cur_y, s ? s : ""}); }
  void fillRect(int, int, int, int) override {}
  void drawRect(int, int, int, int) override {}
  void drawXbm(int x, int y, const uint8_t*, int w, int h) override {
    xbm_calls++; xbms.push_back({x, y, w, h});
  }
  void drawRGB565(int, int, const uint16_t*, int, int) override { rgb_calls++; }
  uint16_t getTextWidth(const char* s) override { return (uint16_t)(strlen(s) * 6); }
  void drawTextCentered(int x, int y, const char* s) override {
    texts.push_back({x, y, s ? s : ""});
  }
  void endFrame() override {}

  bool drewText(const char* needle) const {
    for (const auto& t : texts) if (t.s.find(needle) != std::string::npos) return true;
    return false;
  }
};

const uint16_t RecordingDisplay::kPixels[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};

const offband::SplashInfo kInfo{"1.5.0", "1.16.0", "Aug 18 2026"};

}  // namespace

TEST(Splash, MonoPanelGetsTheOneBitOffbandArt) {
  RecordingDisplay d(/*colour=*/false);
  offband::drawSplash(d, kInfo);
  EXPECT_GT(d.xbm_calls, 0) << "a mono panel must still get Offband artwork";
  EXPECT_EQ(d.rgb_calls, 0) << "a mono panel must never be handed colour art";
}

TEST(Splash, ColourPanelGetsColourArtWithoutAnyPerBoardFlag) {
  // The whole point: capability comes from the driver. No variant opts in.
  RecordingDisplay d(/*colour=*/true, 220, 128);
  offband::drawSplash(d, kInfo);
  EXPECT_GT(d.rgb_calls, 0) << "a colour-capable panel must get colour art";
}

TEST(Splash, EveryPanelGetsOffbandBrandingOfSomeKind) {
  // The five-implementations bug in one assertion: no display may end up with no
  // Offband artwork at all.
  for (bool colour : {false, true}) {
    RecordingDisplay d(colour);
    offband::drawSplash(d, kInfo);
    EXPECT_GT(d.xbm_calls + d.rgb_calls, 0)
        << (colour ? "colour" : "mono") << " panel drew no branding";
  }
}

TEST(Splash, MeshCoreAttributionSurvivesAsText) {
  // #153 / MIT obligation. It must remain, but as attribution text -- not as
  // MeshCore artwork, which is what five implementations were shipping.
  RecordingDisplay d(false);
  offband::drawSplash(d, kInfo);
  EXPECT_TRUE(d.drewText("MeshCore")) << "MeshCore attribution was dropped";
}

TEST(Splash, ShowsBothVersionsAndTheBuildDate) {
  RecordingDisplay d(false);
  offband::drawSplash(d, kInfo);
  EXPECT_TRUE(d.drewText("1.5.0"));
  EXPECT_TRUE(d.drewText("1.16.0"));
  EXPECT_TRUE(d.drewText("Aug 18 2026"));
}

TEST(Splash, NothingIsDrawnOffTheBottomOfTheLogicalCanvas) {
  // The #758 clipping trap, generalised: the shared component must not assume one
  // panel's line spacing. Nothing may be placed below the 64-row logical canvas.
  RecordingDisplay d(false);
  offband::drawSplash(d, kInfo);
  EXPECT_LE(offband::splashLastLineY(), 64 - 8)
      << "the last splash line would clip on a 128x64 logical canvas";
}


// ---- #222 version derivation -------------------------------------------------
// These fixtures were previously asserted only against a PYTHON RE-IMPLEMENTATION
// of the algorithm (scripts/test_offband_version_splash.py), because the real C++
// lived inside a UIScreen welded to a display. They now run the shipping code.
static std::string shortVer(const char* describe, const char* build_tag = nullptr) {
  char buf[40];
  offband::shortVersionInto(buf, sizeof(buf), describe, build_tag);
  return std::string(buf);
}

TEST(OffbandVersion, ExactReleaseTag)     { EXPECT_EQ("v0.14.0",     shortVer("offband-v0.14.0")); }
TEST(OffbandVersion, ExactPreRelease)     { EXPECT_EQ("v0.14.0-rc1", shortVer("offband-v0.14.0-rc1")); }
TEST(OffbandVersion, Rc2ExactPreRelease)  { EXPECT_EQ("v1.2.3-rc2",  shortVer("offband-v1.2.3-rc2")); }
TEST(OffbandVersion, CommitsPastRelease)  { EXPECT_EQ("v0.14.0+4",   shortVer("offband-v0.14.0-4-g369714e")); }
TEST(OffbandVersion, CommitsPastPreRel)   { EXPECT_EQ("v0.14.0-rc1+4", shortVer("offband-v0.14.0-rc1-4-g369714e")); }
TEST(OffbandVersion, DirtyExactPreRel)    { EXPECT_EQ("v0.14.0-rc1*", shortVer("offband-v0.14.0-rc1-dirty")); }
TEST(OffbandVersion, DirtyCommitsPastTag) { EXPECT_EQ("v0.14.0+4*",  shortVer("offband-v0.14.0-4-g369714e-dirty")); }
TEST(OffbandVersion, UntaggedShaFallback) { EXPECT_EQ("369714e",     shortVer("369714e")); }

// #33 regression: splitting on the FIRST dash mis-read "-rc1" as the commits
// field and rendered a spurious "+0".
TEST(OffbandVersion, PreReleaseHasNoSpuriousPlusZero) {
  EXPECT_EQ(std::string::npos, shortVer("offband-v0.14.0-rc1").find("+0"));
}

TEST(OffbandVersion, AppendsBuildTag) {
  EXPECT_EQ("v0.14.0 bench", shortVer("offband-v0.14.0", "bench"));
}

TEST(OffbandVersion, EmptyBuildTagAddsNothing) {
  EXPECT_EQ("v0.14.0", shortVer("offband-v0.14.0", ""));
}

// The caller's buffer bounds the result, never the input.
TEST(OffbandVersion, TruncatesIntoSmallBuffer) {
  char buf[8];
  offband::shortVersionInto(buf, sizeof(buf), "offband-v0.14.0-rc1-4-g369714e", nullptr);
  EXPECT_LT(strlen(buf), sizeof(buf));
}

TEST(OffbandVersion, NullDescribeYieldsEmpty) {
  char buf[8];
  offband::shortVersionInto(buf, sizeof(buf), nullptr, nullptr);
  EXPECT_STREQ("", buf);
}

// ---- compact panels ----------------------------------------------------------
// The 72x40 techo card: the full lockup does not fit, so it must still get the
// mark and a version line rather than three lines running off the bottom.
TEST(OffbandSplash, CompactPanelDrawsNothingBelowItsHeight) {
  RecordingDisplay d(72, 40);
  offband::drawSplash(d, offband::SplashInfo{"v1.5.0", "1.16.0", "2026-08-18"});
  for (const auto& t : d.texts) EXPECT_LT(t.y + 8, 41) << "text at y=" << t.y << " overflows a 40px panel";
  for (const auto& x : d.xbms)  EXPECT_LE(x.x + x.w, 72) << "art overflows a 72px panel";
}

TEST(OffbandSplash, CompactPanelStillShowsOffbandVersion) {
  RecordingDisplay d(72, 40);
  offband::drawSplash(d, offband::SplashInfo{"v1.5.0", "1.16.0", "2026-08-18"});
  bool found = false;
  for (const auto& t : d.texts) if (t.s == "v1.5.0") found = true;
  EXPECT_TRUE(found) << "a small panel lost its Offband identity";
}

TEST(OffbandSplash, TrimsCommitHashFromMeshCoreVersion) {
  RecordingDisplay d(false);
  offband::drawSplash(d, offband::SplashInfo("v1.5.0", "v1.16.0-abcdef", "2026-08-18"));
  EXPECT_TRUE(d.drewText("on MeshCore 1.16.0"));
  EXPECT_FALSE(d.drewText("abcdef")) << "the commit hash reached the splash";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
