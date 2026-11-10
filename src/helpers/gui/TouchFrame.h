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

// A touch controller the GUI polls: the CHSC6x (Heltec), the GT911 (T-Deck,
// Epic J #1434) or the cst66xx (T-Deck Pro, #2109). read() is false on a bus
// error; a controller with nothing new
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

// Hynitron cst66xx point reports (#2109): the T-Deck Pro's touch chip (B1,
// #2108). 4-byte big-endian register addresses. A report is read from
// 0xD0070000, and a write of 0xD00002AB after each read tells the chip it was
// taken. The report is a 4-byte head, then 5 bytes per key and per finger,
// keys first:
//   [0..1] checksum, little-endian: 0x55 plus every entry byte, 16-bit
//   [2]    report type: 0xFF points, 0xF0 a gesture, 0xE0 proximity
//   [3]    low nibble fingers, high nibble keys; at most 5 in all
//   entry  x low, y low, pressure, x high (low nibble) | y high (high nibble),
//          id (low nibble) | event (high nibble; 0 means the finger is up)
// The chip pulls its INT line low when a report is ready. The layout is the
// chip's, read from LilyGo's V1.1 sources; nothing is taken from them.
constexpr uint32_t kCst66xxReportReg = 0xD0070000;
constexpr uint32_t kCst66xxReportAck = 0xD00002AB;
constexpr size_t kCst66xxHeadLen = 4;
constexpr size_t kCst66xxEntryLen = 5;
constexpr uint8_t kCst66xxMaxEntries = 5;
// The first read: the head and one entry. More entries are read after it.
constexpr size_t kCst66xxFirstReadLen = kCst66xxHeadLen + kCst66xxEntryLen;
constexpr size_t kCst66xxMaxReportLen = kCst66xxHeadLen + kCst66xxEntryLen * kCst66xxMaxEntries;

// The whole report's length from its count byte ([3]); 0 when the counts
// can't be a report.
size_t cst66xxReportLen(uint8_t counts);

enum class Cst66xxFrame : uint8_t {
  Released,   // a point report with no finger down
  Pressed,    // the first finger down is in *out
  Other,      // a valid gesture or proximity report: no change to touch
  Invalid,    // too many entries, short, or a bad checksum
};

Cst66xxFrame parseCst66xx(const uint8_t* buf, size_t len, TouchPoint* out);

// Polls in a row with no report before a held press is let go, so a lost
// release report can't leave the GUI pressed. At LVGL's 30 ms input period
// this is about 300 ms. [hypothesis] the chip reports at least that often
// while a finger is down, as LilyGo's V1.1 UI needs: it takes any poll with
// no report as a release. B3 measures the rate on the bench.
constexpr uint8_t kCst66xxStaleReads = 10;

// The cst66xx's touch state across polls, the same rule as Gt911Tracker.
class Cst66xxTracker {
public:
  // A poll that read a report: folds it in and writes the state to *out.
  // False for an invalid report, which leaves the state alone.
  bool report(const uint8_t* buf, size_t len, TouchPoint* out);
  // A poll with no report ready: keeps the state, or lets a stale press go.
  void idle(TouchPoint* out);
  // Drops a held press (the chip was reset).
  void reset();
  // Presses let go by the stale rule rather than by a finger-up report. The
  // diag log counts them: a long hold that ends this way means the chip
  // doesn't keep reporting while a finger rests (B3, #2110).
  uint32_t staleReleases() const { return _stale_releases; }

private:
  TouchPoint _last{false, 0, 0};
  uint8_t _stale = 0;
  uint32_t _stale_releases = 0;
};

// Failed reads in a row before a held press is let go (moved here from
// LvglPort.cpp, #2109): one glitch frame must not drop a drag, but a bus that
// stays down must not leave a press stuck. 3 reads at LVGL's 30 ms input
// period is about 90 ms.
constexpr uint8_t kReleaseAfterFailedReads = 3;

class FailedReadGuard {
public:
  // One poll. `ok` is whether the driver's read succeeded; `pressed` is its
  // state then, ignored when !ok. Returns the press state to report.
  bool update(bool ok, bool pressed);
  bool pressed() const { return _pressed; }
  // Drops a held press (the screen turned under it).
  void release() { _pressed = false; }

private:
  bool _pressed = false;
  uint8_t _failed = 0;
};

// Touch read while an e-paper refresh blocks the UI loop (#2110, plan row 5),
// for LVGL to take once the refresh is done. A tap that starts and ends
// inside one refresh would otherwise never be seen.
//
// Each press and each release is kept, so a tap stays a tap. While a finger
// stays down only its newest point is kept after the press, so a drag keeps
// where it started and where it got to, and a long hold doesn't fill the
// queue.
constexpr uint8_t kTouchQueueLen = 16;

class TouchQueue {
public:
  // A refresh begins. `pressed` is the state LVGL last saw, so a finger
  // already down isn't taken for a new press.
  void start(bool pressed);
  // One read taken during the refresh, at `ms`. False when the queue is
  // full and the read was dropped.
  bool offer(const TouchPoint& p, uint32_t ms);
  // The oldest read, and when it was taken. False when empty.
  bool take(TouchPoint* p, uint32_t* ms);
  bool empty() const { return _count == 0; }
  uint8_t size() const { return _count; }
  // When the oldest read is a release whose press was also caught, how long
  // the finger was down; otherwise 0. TouchReplay uses it to keep a long
  // hold long.
  uint32_t nextReleaseHeldMs() const;
  // Empties it (the screen turned under the reads it holds).
  void clear();
  // Reads dropped because the queue was full, ever: a running count the
  // diag log takes differences of, so clear() leaves it.
  uint32_t dropped() const { return _dropped; }

private:
  void append(const TouchPoint& p, uint32_t ms, uint32_t held_ms);

  TouchPoint _q[kTouchQueueLen] = {};
  uint32_t _ms[kTouchQueueLen] = {};
  uint32_t _held_ms[kTouchQueueLen] = {};   // set on a release entry
  uint8_t _head = 0;
  uint8_t _count = 0;
  bool _last_pressed = false;
  bool _tail_is_held = false;   // the newest entry is a held point to overwrite
  bool _press_caught = false;   // the current press's edge is in the queue
  uint32_t _press_ms = 0;
  uint32_t _dropped = 0;
};

// Hands LVGL what a refresh caught, one read per call. LVGL takes the whole
// queue in one pass, at one tick, so a hold that began and ended inside the
// refresh would arrive as a click. A hold at least `long_press_ms` long is
// replayed as a hold instead: its press, then pressed on every read until
// LVGL has been handed one read past that long, then its release. Ending on
// a read rather than on the clock means a slow UI loop can't skip LVGL's
// long-press check.
class TouchReplay {
public:
  // Past LVGL's long-press time, so its timer has fired by that read.
  static constexpr uint32_t kHoldMarginMs = 50;

  TouchReplay(TouchQueue& q, uint32_t long_press_ms) : _q(q), _long_ms(long_press_ms) {}

  // One LVGL read at `now`. True when the replay supplies it: *p, when it was
  // taken (*taken_ms), and whether LVGL should read again in this pass
  // (*more). False when there is nothing to replay: read the chip.
  bool next(uint32_t now, TouchPoint* p, uint32_t* taken_ms, bool* more);
  bool holding() const { return _holding; }
  void clear();

private:
  TouchQueue& _q;
  uint32_t _long_ms;
  bool _holding = false;
  bool _hold_done = false;   // a read past the hold time has been handed over
  uint32_t _hold_from = 0;
  TouchPoint _held{false, 0, 0};
  uint32_t _held_taken_ms = 0;
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
