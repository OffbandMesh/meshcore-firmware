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

// A touch controller the GUI polls: the CHSC6x (Heltec) or the GT911 (T-Deck,
// Epic J #1434). read() is false on a bus error; a controller with nothing new
// to report returns its last state.
class TouchSource {
public:
  virtual bool read(TouchPoint* p) = 0;
  virtual ~TouchSource() {}
};

// GT911 (J3 #1726). 16-bit big-endian registers: 0x814E is the status (bit 7:
// a new report is ready; low nibble: points touched, at most 5), and the first
// point follows it at 0x814F: track id, X low/high (0x8150/51), Y low/high,
// size low/high, reserved. The host writes 0 to the status after reading, or
// no new report comes. Reading from 0x8150 instead (one byte late) made every
// press read as an out-of-range glitch on the T-Deck bench (#1730).
constexpr uint16_t kGt911Status = 0x814E;
constexpr uint16_t kGt911Point1 = 0x814F;
constexpr size_t kGt911PointLen = 8;

enum class Gt911Frame : uint8_t {
  NotReady,   // nothing new since the last report: keep the last state
  Released,   // a report with no points
  Pressed,    // the first point is in *out
  Invalid,    // a status or point that can't be a real report
};

// `point` may be null when the status reports no points.
Gt911Frame parseGt911(uint8_t status, const uint8_t* point, size_t len, TouchPoint* out);

// 0x8140 holds the product id as ASCII, "911" then a NUL. Checked at begin()
// so another chip answering at 0x5D or 0x14 is not taken for the touch panel.
constexpr uint16_t kGt911ProductId = 0x8140;
constexpr size_t kGt911ProductIdLen = 4;
bool isGt911ProductId(const uint8_t* id, size_t len);

// Polls in a row with no new report before a held press is let go. The GT911
// reports about every 10 ms while touched and latches the release report
// until the host clears it, so a real hold never goes this long without one;
// at LVGL's 30 ms input period this is about 300 ms.
constexpr uint8_t kGt911StaleReads = 10;

// The GT911's touch state across polls: a report replaces it, "not ready"
// keeps it, and a press with no report for kGt911StaleReads polls is released
// so a lost release report can't leave the GUI pressed.
class Gt911Tracker {
public:
  // Folds one status (and the first point, when the status has points) into
  // the state and writes it to *out. False for an invalid report, which
  // leaves the state alone.
  bool update(uint8_t status, const uint8_t* point, size_t len, TouchPoint* out);

private:
  TouchPoint _last{false, 0, 0};
  uint8_t _stale = 0;
};

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
