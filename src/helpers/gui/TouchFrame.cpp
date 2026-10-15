#include "TouchFrame.h"

namespace offband {
namespace gui {

bool parseChsc6xFrame(const uint8_t* buf, size_t len, TouchPoint* out) {
  if (!buf || !out || len < kChsc6xFrameLen) return false;

  const bool all_zero = buf[2] == 0x00 && buf[3] == 0x00 && buf[4] == 0x00 && buf[6] == 0x00;
  const bool all_ones = buf[2] == 0xFF && buf[3] == 0xFF && buf[4] == 0xFF && buf[6] == 0xFF;
  if (all_zero || all_ones) return false;

  const uint8_t points = buf[2] & 0x07;
  out->pressed = points > 0;
  out->x = uint16_t((buf[3] & 0x0F) << 8) | buf[4];
  out->y = uint16_t((buf[5] & 0x0F) << 8) | buf[6];
  return true;
}

void mapTouch(const TouchMapping& m, uint16_t* x, uint16_t* y) {
  uint16_t tx = *x, ty = *y;
  if (m.swap_xy) {
    uint16_t t = tx;
    tx = ty;
    ty = t;
  }
  if (tx >= m.w) tx = m.w - 1;
  if (ty >= m.h) ty = m.h - 1;
  if (m.mirror_x) tx = uint16_t(m.w - 1 - tx);
  if (m.mirror_y) ty = uint16_t(m.h - 1 - ty);
  *x = tx;
  *y = ty;
}

}  // namespace gui
}  // namespace offband
