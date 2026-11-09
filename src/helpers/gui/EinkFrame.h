#pragma once

// The GUI on a black-and-white e-paper panel (T-Deck Pro, #2009). Free of
// LVGL and the panel so it runs in the native tests.
//
// Two jobs:
// - Each pixel LVGL draws becomes black or white. The GUI's themes are light
//   ink on a dark background, so a bright pixel becomes black ink and a dark
//   one stays white paper: every theme reads as black on white. Greys between
//   are ordered-dithered, so a hairline or a selected row's wash still shows.
// - When to refresh. A refresh blocks for most of a second (772 ms partial,
//   measured on tdeck-pro-1, #2037), so frames are gathered and pushed
//   together, and every so often as a full refresh to clear ghosting. The gap
//   runs from the END of one refresh, so the main loop always gets that long
//   free between two of them.

#include <stdint.h>

namespace offband {
namespace gui {

// Brightness at or below this stays white; at or above kEinkInkLuma it is
// black. The themes' backgrounds (0x0F1412, 0x1D2D3D, 0x000000) all sit at
// or below the first, so a background never speckles.
constexpr uint8_t kEinkPaperLuma = 48;
constexpr uint8_t kEinkInkLuma = 208;

// Whether the pixel at (x, y), with brightness `luma` (0-255), is black ink.
bool einkInk(uint8_t luma, int16_t x, int16_t y);

// A rectangle of the screen, inclusive, in the coordinates LVGL draws in.
struct EinkArea {
  int16_t x1 = 0, y1 = 0, x2 = -1, y2 = -1;
  bool empty() const { return x2 < x1 || y2 < y1; }
  // Grows to cover the given rectangle too.
  void add(int16_t ax1, int16_t ay1, int16_t ax2, int16_t ay2);
};

enum class EinkPush : uint8_t { None, Partial, Full };

// Defaults: a second free after each refresh, and every 20th refresh full.
// Both are bench values to tune, not measurements.
#ifndef EINK_MIN_PUSH_GAP_MS
  #define EINK_MIN_PUSH_GAP_MS 1000
#endif
#ifndef EINK_FULL_EVERY
  #define EINK_FULL_EVERY 20
#endif

class EinkPacer {
public:
  explicit EinkPacer(uint32_t min_gap_ms = EINK_MIN_PUSH_GAP_MS, uint8_t full_every = EINK_FULL_EVERY)
      : _min_gap_ms(min_gap_ms), _full_every(full_every ? full_every : 1) {}

  // LVGL drew this rectangle into the frame buffer.
  void mark(int16_t x1, int16_t y1, int16_t x2, int16_t y2) { _dirty.add(x1, y1, x2, y2); }

  // The next refresh is full: the panel woke, turned, or lost its image.
  void requestFull() { _force_full = true; }

  // What to refresh now, if anything. The first refresh is always full.
  EinkPush due(uint32_t now_ms) const;

  // The refresh `due()` returned has been sent.
  void pushed(EinkPush kind, uint32_t now_ms);

  const EinkArea& dirty() const { return _dirty; }

private:
  uint32_t _min_gap_ms;
  uint8_t _full_every;
  EinkArea _dirty;
  uint32_t _last_push_ms = 0;
  uint8_t _partials = 0;   // partial refreshes since the last full one
  bool _pushed_once = false;
  bool _force_full = false;
};

}  // namespace gui
}  // namespace offband
