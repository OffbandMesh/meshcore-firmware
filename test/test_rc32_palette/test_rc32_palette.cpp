// #757: invariants for the RC32 dark palette.
//
// WHY A PALETTE HAS TESTS AT ALL
//   The failure mode here is not a crash. It is a screen nobody can read, on one
//   board, discovered by a human squinting at hardware. Two of these invariants
//   (contrast, and title_bkg != window_bkg) encode acceptance criteria straight off
//   the issue; the third encodes the owner's instruction that no blue survives.
//
//   These are cheap to check and would otherwise be checked by eye, once, and then
//   silently broken by the next person who adjusts a colour.

#include <gtest/gtest.h>

#include "helpers/ui/Rc32Palette.h"

namespace {

using namespace rc32_palette;

int r5(uint16_t c) { return (c >> 11) & 0x1F; }
int g6(uint16_t c) { return (c >> 5) & 0x3F; }
int b5(uint16_t c) { return c & 0x1F; }

// Rec.601 luma on 0..255-normalised channels. Good enough to separate "readable"
// from "invisible", which is all this needs to do.
double luma(uint16_t c) {
  const double r = r5(c) * 255.0 / 31.0, g = g6(c) * 255.0 / 63.0, b = b5(c) * 255.0 / 31.0;
  return 0.299 * r + 0.587 * g + 0.114 * b;
}

// A colour is "blue-dominant" if blue clearly leads both other channels. This is
// what 0x001F (the old title_bkg) and 0x001A (the old corp_blue) look like.
bool blueDominant(uint16_t c) {
  const double r = r5(c) / 31.0, g = g6(c) / 63.0, b = b5(c) / 31.0;
  return b > r + 0.15 && b > g + 0.15;
}

struct Pair { const char* name; uint16_t fg, bg; };

const Pair kTextOnBackground[] = {
  {"primary_txt on window_bkg",   PRIMARY_TXT,   WINDOW_BKG},
  {"secondary_txt on window_bkg", SECONDARY_TXT, WINDOW_BKG},
  {"warning_txt on window_bkg",   WARNING_TXT,   WINDOW_BKG},
  {"title_txt on title_bkg",      TITLE_TXT,     TITLE_BKG},
  {"popup_txt on popup_bkg",      POPUP_TXT,     POPUP_BKG},
};

}  // namespace

TEST(Rc32Palette, EveryTextRoleIsReadableOnItsOwnBackground) {
  // The issue's acceptance criterion: "no screen left with black-on-dark or
  // otherwise unreadable text". Inverting a palette one constant at a time is
  // exactly how a pair gets missed.
  for (const auto& p : kTextOnBackground) {
    const double delta = luma(p.fg) - luma(p.bg);
    EXPECT_GT(std::abs(delta), 60.0) << p.name << " has too little contrast (delta " << delta << ")";
  }
}

TEST(Rc32Palette, NoRoleIsBlueAnyMore) {
  // Owner instruction: the blue banner goes, and so does corp_blue.
  const std::pair<const char*, uint16_t> roles[] = {
    {"window_bkg", WINDOW_BKG}, {"primary_txt", PRIMARY_TXT}, {"secondary_txt", SECONDARY_TXT},
    {"title_bkg", TITLE_BKG}, {"title_txt", TITLE_TXT}, {"popup_bkg", POPUP_BKG},
    {"popup_txt", POPUP_TXT}, {"warning_txt", WARNING_TXT}, {"corp_blue", CORP_ACCENT},
  };
  for (const auto& r : roles) {
    EXPECT_FALSE(blueDominant(r.second)) << r.first << " is still blue (0x"
                                         << std::hex << r.second << ")";
  }
}

TEST(Rc32Palette, TitleBackgroundStaysDistinctFromTheWindowBackground) {
  // ui-new/UITask.cpp:324 branches on title_bkg == window_bkg to decide how to draw
  // the page indicator -- that equality is the MONO panel path. If a palette edit
  // ever collapses these two, a colour panel silently takes the mono branch.
  EXPECT_NE(TITLE_BKG, WINDOW_BKG);
}

TEST(Rc32Palette, TheThemeIsActuallyDark) {
  // Guards against a future edit quietly reverting to a light scheme while leaving
  // the rest of the theme's assumptions (and the baked splash asset) in place.
  EXPECT_LT(luma(WINDOW_BKG), 40.0) << "window background is not dark";
  EXPECT_GT(luma(PRIMARY_TXT), 200.0) << "primary text is not light";
}

TEST(Rc32Palette, TheSplashAssetBackgroundMatchesTheWindowBackground) {
  // The splash asset has its background FLATTENED IN at generation time (#749), so
  // if window_bkg and the asset's baked background diverge, the artwork renders on
  // a visible mismatched rectangle. SPLASH_BAKED_BKG is what the asset was
  // generated against; it must track window_bkg.
  EXPECT_EQ(SPLASH_BAKED_BKG, WINDOW_BKG)
      << "regenerate the splash asset with --rgb565-bg to match the new background";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
