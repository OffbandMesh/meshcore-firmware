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

  // Reports read since begin(), valid or not: the diag log uses it to show
  // how often the chip reports while a finger is down (B3).
  uint32_t reportsRead() const { return _reports; }

private:
  bool readReport(offband::gui::TouchPoint* p);

  TwoWire& _wire;
  int _irq = -1;
  bool _present = false;
  uint32_t _reports = 0;
  offband::gui::HynInfo _info;
  offband::gui::Cst66xxTracker _tracker;
};
