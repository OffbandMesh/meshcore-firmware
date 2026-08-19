// #757/#785: invariants for every Offband colour palette in the tree.
//
// WHY A PALETTE HAS TESTS AT ALL
//   The failure mode is not a crash. It is a screen nobody can read, on one board,
//   discovered by a human squinting at hardware. Contrast and "no blue survives"
//   are acceptance criteria off the issues; they are cheap to check here and
//   otherwise get checked by eye once and silently broken by the next edit.
//
// WHY TABLE-DRIVEN
//   This started as test_rc32_palette with the assertions written against one set
//   of constants. #785 adds a second palette and #793 will add a third, so copying
//   the assertions would mean three drifting copies of the same intent. Each
//   invariant loops kPalettes instead, and a new board is ONE ROW.
//
//   Every failure message names the palette. A red test that does not say which
//   board broke is barely better than no test.

#include <gtest/gtest.h>

#include <cmath>
#include <utility>

#include "helpers/ui/OffbandBrand.h"
#include "helpers/ui/Rc32Palette.h"
#include "helpers/ui/St7735Palette.h"

namespace {

struct Palette {
  const char* name;
  uint16_t window_bkg, primary_txt, secondary_txt;
  uint16_t title_bkg, title_txt;
  uint16_t popup_bkg, popup_txt;
  uint16_t warning_txt, corp_accent;
  // Only the RC32 blits an asset whose background was flattened in at generation
  // time (#749). ST7735 has no such asset -- OFFBAND_COLOUR_SPLASH appears zero
  // times across its four boards -- so that invariant must not be asserted there,
  // where it could only ever pass vacuously.
  bool has_baked_splash;
  uint16_t splash_baked_bkg;
};

const Palette kPalettes[] = {
  { "RC32/NV3001B",
    rc32_palette::WINDOW_BKG, rc32_palette::PRIMARY_TXT, rc32_palette::SECONDARY_TXT,
    rc32_palette::TITLE_BKG,  rc32_palette::TITLE_TXT,
    rc32_palette::POPUP_BKG,  rc32_palette::POPUP_TXT,
    rc32_palette::WARNING_TXT, rc32_palette::CORP_ACCENT,
    true, rc32_palette::SPLASH_BAKED_BKG },

  { "ST7735",
    st7735_palette::WINDOW_BKG, st7735_palette::PRIMARY_TXT, st7735_palette::SECONDARY_TXT,
    st7735_palette::TITLE_BKG,  st7735_palette::TITLE_TXT,
    st7735_palette::POPUP_BKG,  st7735_palette::POPUP_TXT,
    st7735_palette::WARNING_TXT, st7735_palette::CORP_ACCENT,
    false, 0 },
};

int r5(uint16_t c) { return (c >> 11) & 0x1F; }
int g6(uint16_t c) { return (c >> 5) & 0x3F; }
int b5(uint16_t c) { return c & 0x1F; }

double luma(uint16_t c) {
  const double r = r5(c) * 255.0 / 31.0, g = g6(c) * 255.0 / 63.0, b = b5(c) * 255.0 / 31.0;
  return 0.299 * r + 0.587 * g + 0.114 * b;
}

// What 0x001F (the old title_bkg) and 0x001A (the old corp_blue) look like.
bool blueDominant(uint16_t c) {
  const double r = r5(c) / 31.0, g = g6(c) / 63.0, b = b5(c) / 31.0;
  return b > r + 0.15 && b > g + 0.15;
}

}  // namespace

TEST(Palettes, EveryTextRoleIsReadableOnItsOwnBackground) {
  for (const auto& p : kPalettes) {
    const std::pair<const char*, std::pair<uint16_t, uint16_t>> pairs[] = {
      {"primary_txt on window_bkg",   {p.primary_txt,   p.window_bkg}},
      {"secondary_txt on window_bkg", {p.secondary_txt, p.window_bkg}},
      {"warning_txt on window_bkg",   {p.warning_txt,   p.window_bkg}},
      {"title_txt on title_bkg",      {p.title_txt,     p.title_bkg}},
      {"popup_txt on popup_bkg",      {p.popup_txt,     p.popup_bkg}},
    };
    for (const auto& c : pairs) {
      const double delta = std::abs(luma(c.second.first) - luma(c.second.second));
      EXPECT_GT(delta, 60.0) << p.name << ": " << c.first
                             << " has too little contrast (delta " << delta << ")";
    }
  }
}

TEST(Palettes, NoRoleIsBlue) {
  for (const auto& p : kPalettes) {
    const std::pair<const char*, uint16_t> roles[] = {
      {"window_bkg", p.window_bkg},   {"primary_txt", p.primary_txt},
      {"secondary_txt", p.secondary_txt}, {"title_bkg", p.title_bkg},
      {"title_txt", p.title_txt},     {"popup_bkg", p.popup_bkg},
      {"popup_txt", p.popup_txt},     {"warning_txt", p.warning_txt},
      {"corp_accent", p.corp_accent},
    };
    for (const auto& r : roles) {
      EXPECT_FALSE(blueDominant(r.second))
          << p.name << ": " << r.first << " is still blue (0x" << std::hex << r.second << ")";
    }
  }
}

TEST(Palettes, TitleBackgroundStaysDistinctFromTheWindowBackground) {
  // ui-new/UITask.cpp branches on title_bkg == window_bkg to decide how to draw the
  // page indicator -- that equality is the MONO path. If a palette edit collapses
  // the two, a colour panel silently takes the mono branch.
  for (const auto& p : kPalettes) {
    EXPECT_NE(p.title_bkg, p.window_bkg) << p.name;
  }
}

TEST(Palettes, TheThemeIsActuallyDark) {
  for (const auto& p : kPalettes) {
    EXPECT_LT(luma(p.window_bkg), 40.0) << p.name << ": background is not dark";
    EXPECT_GT(luma(p.primary_txt), 200.0) << p.name << ": primary text is not light";
  }
}

TEST(Palettes, BakedSplashBackgroundTracksTheWindowBackground) {
  // Applies ONLY where an asset has a background flattened into it (#749). Asserting
  // it on a palette without one would pass vacuously and teach nobody anything.
  int checked = 0;
  for (const auto& p : kPalettes) {
    if (!p.has_baked_splash) continue;
    checked++;
    EXPECT_EQ(p.splash_baked_bkg, p.window_bkg)
        << p.name << ": regenerate the splash asset with --rgb565-bg to match";
  }
  EXPECT_GT(checked, 0) << "no palette declares a baked splash -- has the flag been lost?";
}

TEST(Palettes, BrandPrimitivesAreTheDocumentedBrandColours) {
  // Guards the shared header itself. Every palette above draws from these, so an
  // edit here silently repaints every colour board at once.
  EXPECT_EQ(offband_brand::INK,   0x08A2) << "#0F1412";
  EXPECT_EQ(offband_brand::PAPER, 0xEF9D) << "#EFF3E8";
  EXPECT_EQ(offband_brand::DEEP,  0x1BC8) << "#1A7A44";
  EXPECT_EQ(offband_brand::MINT,  0x7F75) << "#7BEFA8";
}

TEST(Palettes, TheColourBoardsShareOneDefinition) {
  // The point of extracting OffbandBrand.h: two drivers cannot drift apart on a
  // colour they are both meant to be showing.
  EXPECT_EQ(kPalettes[0].window_bkg,  kPalettes[1].window_bkg);
  EXPECT_EQ(kPalettes[0].primary_txt, kPalettes[1].primary_txt);
  EXPECT_EQ(kPalettes[0].title_bkg,   kPalettes[1].title_bkg);
  EXPECT_EQ(kPalettes[0].warning_txt, kPalettes[1].warning_txt)
      << "#790 agreed to unify warning_txt across drivers";
  EXPECT_EQ(kPalettes[0].secondary_txt, kPalettes[1].secondary_txt)
      << "#790 agreed to unify secondary_txt across drivers";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
