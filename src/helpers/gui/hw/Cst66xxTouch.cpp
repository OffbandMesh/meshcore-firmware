#include "Cst66xxTouch.h"

#include <Arduino.h>

#include "HynBus.h"

using namespace offband::gui;

namespace {

// Set from the INT edge, cleared by the poll that takes the report. One
// controller per board, so one flag.
volatile bool s_report_ready = false;

void IRAM_ATTR onTouchIrq() { s_report_ready = true; }

}  // namespace

bool Cst66xxTouch::begin(int rst, int irq) {
  _present = false;
  _tracker.reset();
  hynReset(rst);
  uint8_t buf[kCst66xxInfoLen] = {};
  bool read_ok = false;
  if (!hyn66xxReadInfo(_wire, buf, &_info, &read_ok)) return false;
  // Normal mode once more after the info read, as the chip's own init does.
  hyn66xxNormalMode(_wire);

  _irq = irq;
  if (_irq >= 0) {
    pinMode(_irq, INPUT_PULLUP);
    s_report_ready = false;
    attachInterrupt(digitalPinToInterrupt(_irq), onTouchIrq, FALLING);
  }
  _present = true;
  return true;
}

bool Cst66xxTouch::readReport(TouchPoint* p) {
  uint8_t buf[kCst66xxMaxReportLen];
  bool ok = hynReadReg(_wire, kCst66xxReportReg, 4, buf, kCst66xxFirstReadLen);
  size_t len = kCst66xxFirstReadLen;
  if (ok) {
    // More than one entry: the rest follows in a read of its own. A count
    // too high for any report is left to the parser to reject.
    const size_t need = cst66xxReportLen(buf[3]);
    if (need > len) {
      ok = hynReadMore(_wire, buf + len, need - len);
      len = need;
    }
  }
  _reports++;
  return ok && _tracker.report(buf, len, p);
}

bool Cst66xxTouch::read(TouchPoint* p) {
  if (!_present || !p) return false;
  if (_irq >= 0) {
    // INT still low counts too: if the chip holds it low until it sees an
    // ack, a lost ack would otherwise mean no edge, and no touch, ever again.
    if (!s_report_ready && digitalRead(_irq) != LOW) {
      _tracker.idle(p);
      return true;
    }
    // Cleared before the read, so an edge that lands during it isn't lost.
    s_report_ready = false;
  }
  bool good = readReport(p) || readReport(p);
  // Tell the chip the report was taken, whether or not it read cleanly. A
  // failed ack is a bus error like any other.
  if (!hynSendReg(_wire, kCst66xxReportAck, 4)) good = false;
  return good;
}
