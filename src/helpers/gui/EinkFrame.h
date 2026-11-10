#pragma once

// The GUI on a black-and-white e-paper panel (T-Deck Pro, #2009). Free of
// LVGL and the panel so it runs in the native tests.
//
// Two jobs:
// - Each pixel LVGL draws becomes black or white, at one cutoff and with no
//   dithering, so text and lines stay crisp (owner, 2026-11-09). The GUI's
//   themes are light ink on a dark background, so a bright pixel becomes
//   black ink and a dark one stays white paper. The panel always shows the
//   high-contrast theme, which is black and white by design; the cutoff is
//   set for its colors.
// - When to refresh. A refresh blocks for most of a second (772 ms partial,
//   measured on tdeck-pro-1, #2037), so frames are gathered and pushed
//   together, and every so often as a full refresh to clear ghosting. The gap
//   runs from the END of one refresh, so the main loop always gets that long
//   free between two of them.

#include <stdint.h>

#include "GuiTheme.h"

namespace offband {
namespace gui {

// The theme the panel shows, whatever theme is stored.
constexpr ThemeId kEinkTheme = ThemeId::MonoHC;

// Brightness above this is black ink; at or below it, white paper. Set
// between the high-contrast theme's backgrounds (26 at most, bg3 0x1A1A1A)
// and its secondary hairline (106 after RGB565 rounding, line2 0x6B6B6B),
// with room on both sides. Text needs none of this: the e-paper image builds
// 1-bit fonts (fonts_eink/). The cutoff decides what LVGL still blends: the
// edges of rounded corners, arcs and images, and anything drawn below full
// opacity. A blend reads as ink once it is past about 31% of white, so a
// half-opacity disabled row, a scrim or a "sending" bubble prints the same
// as full ink: those cues are lost on this panel (test_gui_eink pins it).
constexpr uint8_t kEinkInkCutoff = 80;

// Whether a pixel with brightness `luma` (0-255) is black ink.
inline bool einkInk(uint8_t luma) { return luma > kEinkInkCutoff; }

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
