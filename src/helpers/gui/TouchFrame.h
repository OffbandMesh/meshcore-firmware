#pragma once

// CHSC6x touch frames (#1442). The controller exposes a FocalTech-style
// register map: 0x02 holds the touch-point count in its low 3 bits, and the
// first point's X and Y are 12-bit values split across 0x03/0x04 and
// 0x05/0x06 (high nibble, low byte). A frame of all 0x00 or all 0xFF is a bus
// glitch, not a touch. Arduino-free so the native tests cover it.

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

// Bytes read starting at register 0x00.
constexpr size_t kChsc6xFrameLen = 7;

struct TouchPoint {
  bool pressed;
  uint16_t x, y;
};

// Decodes one frame. Returns false for a short or invalid frame (the caller
// keeps its previous state). A valid frame with no points is "released".
bool parseChsc6xFrame(const uint8_t* buf, size_t len, TouchPoint* out);

// How raw touch coordinates map onto the panel: optionally swap the axes,
// then mirror. `w`/`h` are the panel's size in the GUI's orientation.
struct TouchMapping {
  bool swap_xy;
  bool mirror_x;
  bool mirror_y;
  uint16_t w, h;
};

// Applies the mapping and clamps the result into the panel.
void mapTouch(const TouchMapping& m, uint16_t* x, uint16_t* y);

}  // namespace gui
}  // namespace offband
