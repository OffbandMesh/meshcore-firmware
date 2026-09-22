#pragma once

#include <stddef.h>

// Splash A for the QCC 0x4 badge (#1172), in 128x64 logical pixels. Values were
// derived from the art and the ArialMT tables; test_qcc_splash checks every one
// against the rendered output rather than trusting these comments.
namespace qcc {

constexpr int kEyeX = 39;           // (128 - 50) / 2
constexpr int kEyeY = 0;
constexpr int kCornerLine1Y = 0;
constexpr int kCornerLine2Y = 11;
constexpr int kRightEdgeX = 127;
constexpr int kCornerMaxW = 40;     // widest ArialMT 10 corner text that clears the eye
constexpr int kNameY = 45;          // ArialMT 16 box; ink rows 48..62

// Splits a short version ("v1.5.0-beta6+17*") for the top-right corner.
//   l1 = up to the first '-', '+' or ' '.
//   l2 = the text after '-'; if that is wider than max_w, only the pre-release label
//        before any '+' ("beta6"). With no '-' but a '+', l2 is the "+N" part.
//   A build tag (after a space) is left out.
// Each line is then shortened from the right until it fits max_w in ArialMT 10.
// The full version lives on the About screen; the splash is branding.
void splitCornerVersion(const char* ver, char* l1, size_t n1, char* l2, size_t n2, int max_w);

}  // namespace qcc
