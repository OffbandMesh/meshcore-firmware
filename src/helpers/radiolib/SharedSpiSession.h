#pragma once

// The bus lock around a radio's SPI traffic on a bus it shares (Epic J #1434,
// J2 #1725). On the T-Deck the display, the micro-SD and the SX1262 are three
// devices on one SPI2 bus (owner, 2026-10-21, option A); ESP-IDF's bus lock
// keeps them apart. RadioLib frames every command as begin / transfer... /
// end, with the radio's chip select asserted in between, so the bus must stay
// held from begin to end: a display flush landing between two transfers of
// one command would clock the radio's bytes into the panel.
//
// Pure: the lock and the wire are behind SpiBusOps, so the host tests can
// check the pairing without ESP-IDF.

#include <stddef.h>
#include <stdint.h>

namespace offband {

class SpiBusOps {
public:
  virtual bool acquire() = 0;   // blocks until the bus is ours
  virtual void release() = 0;
  virtual bool transfer(const uint8_t* out, uint8_t* in, size_t len) = 0;
  virtual ~SpiBusOps() {}
};

class SharedSpiSession {
public:
  explicit SharedSpiSession(SpiBusOps& ops) : _ops(ops) {}

  // RadioLib's spiBeginTransaction. Nested begins hold the one acquisition.
  void begin() {
    if (_depth++ == 0) _held = _ops.acquire();
  }

  // RadioLib's spiEndTransaction. Releases only what begin() acquired.
  void end() {
    if (_depth == 0) return;   // an end without a begin changes nothing
    if (--_depth == 0 && _held) {
      _ops.release();
      _held = false;
    }
  }

  // RadioLib's spiTransfer. Outside a begin/end pair it holds the bus for
  // itself, so a stray transfer can never reach the wire unlocked.
  bool transfer(const uint8_t* out, uint8_t* in, size_t len) {
    if (len == 0) return true;
    const bool own = _depth == 0;
    if (own) begin();
    const bool ok = _held && _ops.transfer(out, in, len);
    if (own) end();
    return ok;
  }

  bool held() const { return _held; }
  uint8_t depth() const { return _depth; }

private:
  SpiBusOps& _ops;
  uint8_t _depth = 0;
  bool _held = false;
};

}  // namespace offband
