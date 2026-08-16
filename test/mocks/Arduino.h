#pragma once

#include <cstdint>
#include <cmath>
#include "Stream.h"

inline uint32_t g_mock_millis = 0;

using std::isnan;

inline uint32_t millis() {
  return g_mock_millis;
}

inline void delay(uint32_t ms) {
  g_mock_millis += ms;
}

// --- Offband host-build additions (#628) ------------------------------------
// MeshLog.cpp is linked into the native env because upstream 1.17.0 added
// Packet.cpp there, and in Offband MESH_DEBUG routes through MeshLog. These are
// the only Arduino primitives MeshLog needs. No-ops are correct here: the host
// test binary is single-threaded, so the critical section MLOG_ENTER/EXIT
// guards against ISR re-entrancy has nothing to guard.
inline void noInterrupts() {}
inline void interrupts() {}

// Sink for MeshLog's console mirror. Discards output -- the tests assert on the
// capture ring, never on what reached the console.
//
// #718: derives from Stream, not Print. ArduinoSerialInterface compares its
// target against `&Serial` (isConsoleSharedWithProtocol, #411) via a
// static_cast<Stream*>, which does not compile if the mock Serial is not a
// Stream -- and that comparison is on the path of anything that unit-tests the
// framed serial transport natively.
class MockSerial : public Stream {
public:
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t size) override { return size; }
  void begin(unsigned long) {}
  // MeshLog throttles its console mirror on this; a large constant means
  // "never backpressure", which is what a discarding sink should report.
  int availableForWrite() override { return 1024; }
  void flush() override {}
  operator bool() const { return true; }
};
inline MockSerial Serial;
