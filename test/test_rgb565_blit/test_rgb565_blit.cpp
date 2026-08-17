// #749: unit tests for the RGB565 blit used by the native-resolution splash path.
//
// THE INVARIANT UNDER TEST
//   Blitting a w x h RGB565 image into a dst_w x dst_h framebuffer at (x, y) must
//   write EXACTLY the pixels that fall inside the destination, must read the
//   correspondingly-offset source pixels, and must never touch a byte outside the
//   destination buffer -- for every placement, including partly and wholly offscreen.
//
// WHY THIS IS A SEPARATE PURE HEADER
//   The only consumer is NV3001BDisplay, which is welded to SPI and board pins and
//   cannot be constructed natively. The geometry is where the bugs live: a clipped
//   image must advance its SOURCE pointer to match the clip, which a solid-colour
//   fill (fillPhysicalRect) never has to do. Extracting the arithmetic makes the
//   bug-prone part testable without mocking a display. Same rationale as
//   helpers/BleFrameSizing.h (#712).
//
// STORAGE CONVENTION
//   #747's back buffer holds pixels BYTE-SWAPPED, because endFrame() blits it with a
//   raw writeBytes() and the panel wants RGB565 most-significant-byte first. The blit
//   therefore swaps on write, exactly as fillPhysicalRect() does. Callers hand in
//   ordinary host-order RGB565 (same as ColorVal), so an asset is driver-agnostic.

#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "helpers/ui/Rgb565Blit.h"

namespace {

// Sentinel that is not a plausible real pixel and is not swap-symmetric, so a
// stray write or a missing swap both show up.
constexpr uint16_t kUntouched = 0x1234;

constexpr int kDstW = 8;
constexpr int kDstH = 4;

uint16_t swapped(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

std::vector<uint16_t> freshDest() {
  return std::vector<uint16_t>((size_t)kDstW * kDstH, kUntouched);
}

// A w x h image whose every pixel is a unique, position-encoding value, so a test
// can assert not just "something was written" but "the RIGHT source pixel landed
// here". 0x0100 * row + col + 1 keeps every value non-zero and swap-asymmetric.
std::vector<uint16_t> rampImage(int w, int h) {
  std::vector<uint16_t> img((size_t)w * h);
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      img[(size_t)row * w + col] = (uint16_t)(0x0100 * (row + 1) + col + 1);
    }
  }
  return img;
}

uint16_t at(const std::vector<uint16_t>& buf, int x, int y) {
  return buf[(size_t)y * kDstW + x];
}

}  // namespace

// --------------------------------------------------------------- placement ---

TEST(Rgb565Blit, CopiesEveryPixelOfAFullyVisibleImage) {
  auto dst = freshDest();
  auto img = rampImage(3, 2);

  rgb565::blitSwapped(dst.data(), kDstW, kDstH, 2, 1, img.data(), 3, 2);

  for (int row = 0; row < 2; row++) {
    for (int col = 0; col < 3; col++) {
      EXPECT_EQ(at(dst, 2 + col, 1 + row), swapped(img[(size_t)row * 3 + col]))
          << "source pixel (" << col << "," << row << ") did not land at its destination";
    }
  }
}

TEST(Rgb565Blit, LeavesEveryPixelOutsideTheImageUntouched) {
  auto dst = freshDest();
  auto img = rampImage(3, 2);

  rgb565::blitSwapped(dst.data(), kDstW, kDstH, 2, 1, img.data(), 3, 2);

  for (int y = 0; y < kDstH; y++) {
    for (int x = 0; x < kDstW; x++) {
      const bool inside = (x >= 2 && x < 5 && y >= 1 && y < 3);
      if (!inside) EXPECT_EQ(at(dst, x, y), kUntouched) << "wrote outside the image at (" << x << "," << y << ")";
    }
  }
}

TEST(Rgb565Blit, StoresBytesSwappedForThePanel) {
  auto dst = freshDest();
  const uint16_t one[] = {0xF800};  // pure red, deliberately not swap-symmetric

  rgb565::blitSwapped(dst.data(), kDstW, kDstH, 0, 0, one, 1, 1);

  EXPECT_EQ(at(dst, 0, 0), 0x00F8) << "pixel must be stored byte-swapped to match #747's buffer";
}

// ---------------------------------------------------------------- clipping ---
// The source-advance cases. A solid fill never has to do this, so these are the
// ones fillPhysicalRect's clamping logic does NOT already cover.

TEST(Rgb565Blit, ClipsAtTheRightEdgeAndKeepsRowsAligned) {
  auto dst = freshDest();
  auto img = rampImage(4, 2);

  // x=6 with a 4-wide image: columns 0,1 fit; columns 2,3 fall off the right edge.
  rgb565::blitSwapped(dst.data(), kDstW, kDstH, 6, 0, img.data(), 4, 2);

  EXPECT_EQ(at(dst, 6, 0), swapped(img[0]));
  EXPECT_EQ(at(dst, 7, 0), swapped(img[1]));
  // Row 1 must start from source column 0 again -- not continue mid-row. This is
  // the classic stride bug: using the CLIPPED width as the source stride makes
  // row 1 read img[2] here instead of img[4].
  EXPECT_EQ(at(dst, 6, 1), swapped(img[4]));
  EXPECT_EQ(at(dst, 7, 1), swapped(img[5]));
}

TEST(Rgb565Blit, ClipsANegativeOriginAndSkipsTheHiddenSourcePixels) {
  auto dst = freshDest();
  auto img = rampImage(4, 3);

  // x=-2, y=-1: source columns 0,1 and source row 0 are offscreen.
  rgb565::blitSwapped(dst.data(), kDstW, kDstH, -2, -1, img.data(), 4, 3);

  // Destination (0,0) must show the source pixel at (2,1), not (0,0).
  EXPECT_EQ(at(dst, 0, 0), swapped(img[(size_t)1 * 4 + 2]));
  EXPECT_EQ(at(dst, 1, 0), swapped(img[(size_t)1 * 4 + 3]));
  EXPECT_EQ(at(dst, 0, 1), swapped(img[(size_t)2 * 4 + 2]));
}

TEST(Rgb565Blit, ClipsAtTheBottomEdge) {
  auto dst = freshDest();
  auto img = rampImage(2, 4);

  rgb565::blitSwapped(dst.data(), kDstW, kDstH, 0, 3, img.data(), 2, 4);

  EXPECT_EQ(at(dst, 0, 3), swapped(img[0]));
  EXPECT_EQ(at(dst, 1, 3), swapped(img[1]));
  // Rows 1..3 of the image are below the panel and must simply not be written.
}

// ------------------------------------------------------------ degenerate ---

TEST(Rgb565Blit, WritesNothingWhenWhollyOffscreen) {
  const int placements[][2] = {{8, 0}, {-3, 0}, {0, 4}, {0, -2}, {100, 100}, {-100, -100}};

  for (const auto& p : placements) {
    auto dst = freshDest();
    auto img = rampImage(3, 2);

    rgb565::blitSwapped(dst.data(), kDstW, kDstH, p[0], p[1], img.data(), 3, 2);

    for (size_t i = 0; i < dst.size(); i++) {
      ASSERT_EQ(dst[i], kUntouched) << "placement (" << p[0] << "," << p[1] << ") wrote to the buffer";
    }
  }
}

TEST(Rgb565Blit, IgnoresNullAndNonPositiveDimensions) {
  auto dst = freshDest();
  auto img = rampImage(3, 2);

  rgb565::blitSwapped(nullptr, kDstW, kDstH, 0, 0, img.data(), 3, 2);
  rgb565::blitSwapped(dst.data(), kDstW, kDstH, 0, 0, nullptr, 3, 2);
  rgb565::blitSwapped(dst.data(), kDstW, kDstH, 0, 0, img.data(), 0, 2);
  rgb565::blitSwapped(dst.data(), kDstW, kDstH, 0, 0, img.data(), 3, -1);

  for (size_t i = 0; i < dst.size(); i++) ASSERT_EQ(dst[i], kUntouched);
}

// ------------------------------------------------------------------- clip ---
// clip() is the arithmetic blitSwapped() runs internally, exposed because the
// driver's UNBUFFERED fallback needs the identical result to build its SPI address
// window. #747's back buffer is allowed to be null (allocation failure degrades to
// direct-to-panel writes rather than losing the display), so that path is real and
// must not re-derive the clip by hand -- two copies of this arithmetic is how the
// fallback ends up shearing rows that the buffered path renders correctly.

TEST(Rgb565Clip, ReportsTheVisibleRegionOfAFullyVisibleImage) {
  const auto c = rgb565::clip(kDstW, kDstH, 2, 1, 3, 2);

  EXPECT_TRUE(c.visible);
  EXPECT_EQ(c.dx, 2);
  EXPECT_EQ(c.dy, 1);
  EXPECT_EQ(c.sx, 0);
  EXPECT_EQ(c.sy, 0);
  EXPECT_EQ(c.w, 3);
  EXPECT_EQ(c.h, 2);
}

TEST(Rgb565Clip, AdvancesTheSourceOriginForANegativePlacement) {
  const auto c = rgb565::clip(kDstW, kDstH, -2, -1, 4, 3);

  EXPECT_TRUE(c.visible);
  EXPECT_EQ(c.dx, 0);
  EXPECT_EQ(c.dy, 0);
  EXPECT_EQ(c.sx, 2) << "two source columns are off the left edge";
  EXPECT_EQ(c.sy, 1) << "one source row is off the top edge";
  EXPECT_EQ(c.w, 2);
  EXPECT_EQ(c.h, 2);
}

TEST(Rgb565Clip, ShrinksTheExtentAtTheFarEdges) {
  const auto c = rgb565::clip(kDstW, kDstH, 6, 3, 4, 4);

  EXPECT_TRUE(c.visible);
  EXPECT_EQ(c.w, 2) << "only two columns remain before the right edge";
  EXPECT_EQ(c.h, 1) << "only one row remains before the bottom edge";
  EXPECT_EQ(c.sx, 0) << "the far-edge clip must not move the source origin";
  EXPECT_EQ(c.sy, 0);
}

TEST(Rgb565Clip, SurvivesExtremeCoordinatesWithoutOverflowing) {
  // Hardening for the Gemini review's INT_MIN finding. clip() negates x and y for a
  // negative placement, and -INT_MIN is undefined behaviour, so the "cannot possibly
  // intersect" bail has to happen BEFORE that negation. No caller can reach this
  // today -- the splash passes generated constants -- but this is a shared
  // interface, and the guard doubles as the readable statement of intent.
  const int lo = std::numeric_limits<int>::min();
  const int hi = std::numeric_limits<int>::max();

  EXPECT_FALSE(rgb565::clip(kDstW, kDstH, lo, 0, 3, 2).visible);
  EXPECT_FALSE(rgb565::clip(kDstW, kDstH, 0, lo, 3, 2).visible);
  EXPECT_FALSE(rgb565::clip(kDstW, kDstH, lo, lo, 3, 2).visible);
  EXPECT_FALSE(rgb565::clip(kDstW, kDstH, hi, 0, 3, 2).visible);
  EXPECT_FALSE(rgb565::clip(kDstW, kDstH, 0, hi, 3, 2).visible);
  EXPECT_FALSE(rgb565::clip(kDstW, kDstH, hi, hi, 3, 2).visible);
}

TEST(Rgb565Blit, WritesNothingForExtremeCoordinates) {
  auto dst = freshDest();
  auto img = rampImage(3, 2);
  const int lo = std::numeric_limits<int>::min();

  rgb565::blitSwapped(dst.data(), kDstW, kDstH, lo, lo, img.data(), 3, 2);

  for (size_t i = 0; i < dst.size(); i++) ASSERT_EQ(dst[i], kUntouched);
}

TEST(Rgb565Clip, ReportsNotVisibleWhenWhollyOffscreen) {
  EXPECT_FALSE(rgb565::clip(kDstW, kDstH, 8, 0, 3, 2).visible);
  EXPECT_FALSE(rgb565::clip(kDstW, kDstH, -3, 0, 3, 2).visible);
  EXPECT_FALSE(rgb565::clip(kDstW, kDstH, 0, 4, 3, 2).visible);
  EXPECT_FALSE(rgb565::clip(kDstW, kDstH, 0, -2, 3, 2).visible);
  EXPECT_FALSE(rgb565::clip(kDstW, kDstH, 0, 0, 0, 2).visible) << "degenerate width";
  EXPECT_FALSE(rgb565::clip(0, 0, 0, 0, 3, 2).visible) << "degenerate destination";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
