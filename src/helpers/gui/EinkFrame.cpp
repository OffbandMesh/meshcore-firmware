#include "EinkFrame.h"

namespace offband {
namespace gui {

namespace {

// 4x4 Bayer order: a grey's black dots spread evenly instead of clumping.
constexpr uint8_t kBayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

}  // namespace

bool einkInk(uint8_t luma, int16_t x, int16_t y) {
  if (luma <= kEinkPaperLuma) return false;
  if (luma >= kEinkInkLuma) return true;
  // How much ink, 0-255, across the band between the two ends.
  const uint16_t level = uint16_t(luma - kEinkPaperLuma) * 255u / uint16_t(kEinkInkLuma - kEinkPaperLuma);
  const uint8_t threshold = uint8_t(kBayer4[y & 3][x & 3] * 16 + 8);   // 8..248
  return level > threshold;
}

void EinkArea::add(int16_t ax1, int16_t ay1, int16_t ax2, int16_t ay2) {
  if (ax2 < ax1 || ay2 < ay1) return;
  if (empty()) {
    x1 = ax1;
    y1 = ay1;
    x2 = ax2;
    y2 = ay2;
    return;
  }
  if (ax1 < x1) x1 = ax1;
  if (ay1 < y1) y1 = ay1;
  if (ax2 > x2) x2 = ax2;
  if (ay2 > y2) y2 = ay2;
}

EinkPush EinkPacer::due(uint32_t now_ms) const {
  if (_dirty.empty()) return EinkPush::None;
  if (!_pushed_once) return EinkPush::Full;
  // Unsigned subtraction, so millis() wrapping doesn't stall the panel.
  if (now_ms - _last_push_ms < _min_gap_ms) return EinkPush::None;
  if (_force_full || _partials >= _full_every - 1) return EinkPush::Full;
  return EinkPush::Partial;
}

void EinkPacer::pushed(EinkPush kind, uint32_t now_ms) {
  if (kind == EinkPush::None) return;
  _dirty = EinkArea();
  _last_push_ms = now_ms;
  _pushed_once = true;
  if (kind == EinkPush::Full) {
    _partials = 0;
    _force_full = false;
  } else {
    _partials++;
  }
}

}  // namespace gui
}  // namespace offband
