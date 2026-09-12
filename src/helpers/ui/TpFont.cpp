#include "TpFont.h"

namespace offband {
namespace tpfont {

static const int kHeaderBytes = 4;
static const int kJumpBytes = 4;
static const int kNoInk = 0xFFFF;

static int glyphIndex(const uint8_t* font, char c) {
  const int first = font[2];
  const int count = font[3];
  int i = (int)(uint8_t)c - first;
  if (i < 0 || i >= count) i = '?' - first;
  return (i >= 0 && i < count) ? i : -1;
}

uint8_t height(const uint8_t* font) {
  return font[1];
}

int charAdvance(const uint8_t* font, char c) {
  const int i = glyphIndex(font, c);
  return i < 0 ? 0 : font[kHeaderBytes + i * kJumpBytes + 3];
}

int textWidth(const uint8_t* font, const char* s) {
  int w = 0;
  for (; s && *s; s++) w += charAdvance(font, *s);
  return w;
}

int drawText(DisplayDriver& display, int x, int y, const uint8_t* font, const char* s) {
  const int h = font[1];
  const int count = font[3];
  const int raster = (h + 7) / 8;
  const uint8_t* data = font + kHeaderBytes + count * kJumpBytes;

  for (; s && *s; s++) {
    const int i = glyphIndex(font, *s);
    if (i < 0) continue;
    const uint8_t* jump = font + kHeaderBytes + i * kJumpBytes;
    const int offset = (jump[0] << 8) | jump[1];
    const int bytes = jump[2];

    if (offset != kNoInk) {
      const uint8_t* glyph = data + offset;
      const int cols = (bytes + raster - 1) / raster;
      for (int col = 0; col < cols; col++) {
        int run_start = -1;
        // Stop at the font's height, not the byte padding below it.
        for (int row = 0; row <= h; row++) {
          const int k = col * raster + row / 8;
          const bool on = row < h && k < bytes && ((glyph[k] >> (row % 8)) & 1);
          if (on && run_start < 0) run_start = row;
          if (!on && run_start >= 0) {
            display.fillRect(x + col, y + run_start, 1, row - run_start);
            run_start = -1;
          }
        }
      }
    }
    x += jump[3];
  }
  return x;
}

}  // namespace tpfont
}  // namespace offband
