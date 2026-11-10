#pragma once

// Our own driver for Hynitron's cst66xx touch family (#2109): the T-Deck
// Pro's chip, found by the B1 probe (#2108). LilyGo's Hynitron sources carry
// no license, so nothing is taken from them; the register protocol is the
// chip's (../TouchFrame.h, ../HynInfo.h).
//
// The chip pulls INT low when a report is ready. With INT wired, a poll reads
// after an edge or while INT is still low, and any other poll keeps the last
// state (Cst66xxTracker lets a stale press go). Without INT, every poll reads.

#include <stdint.h>

#include <Wire.h>

#include "../HynInfo.h"
#include "../TouchFrame.h"

class Cst66xxTouch : public offband::gui::TouchSource {
public:
  // The bus is the board's: it must already be started, and the driver never
  // starts it again (a second begin on the same pins breaks every device on
  // them, UITask.cpp's s_touch comment).
  explicit Cst66xxTouch(TwoWire& wire = Wire) : _wire(wire) {}

  // Resets the chip on `rst`, puts it in normal mode and checks its info
  // block. `irq` is the INT line, or -1 to read on every poll. False when the
  // chip doesn't answer as a cst66xx; read() then reports no touch.
  bool begin(int rst, int irq);

  bool present() const { return _present; }
  const offband::gui::HynInfo& info() const { return _info; }

  // False on a bus error or a report that fails its checks, after one retry.
  bool read(offband::gui::TouchPoint* p) override;

  // What the driver saw since the last takeStats(), for the diag log (B3,
  // #2110). max_held_gap_ms is the longest wait between two reports while a
  // finger was down: it settles whether the chip keeps reporting a resting
  // finger, which the stale rule (kCst66xxStaleReads) depends on.
  struct Stats {
    uint32_t reads = 0;        // polls that read the chip
    uint32_t bus_errors = 0;   // reads that failed after the retry
    uint32_t reports = 0;      // reports read, valid or not
    uint32_t presses = 0;
    uint32_t stale_releases = 0;
    uint32_t max_held_gap_ms = 0;
  };
  Stats takeStats();

private:
  bool readReport(offband::gui::TouchPoint* p);
  void noteState(const offband::gui::TouchPoint& p, bool reported);

  TwoWire& _wire;
  int _irq = -1;
  bool _present = false;
  Stats _stats;
  uint32_t _stale_seen = 0;
  bool _held = false;
  uint32_t _last_report_ms = 0;
  offband::gui::HynInfo _info;
  offband::gui::Cst66xxTracker _tracker;
};
