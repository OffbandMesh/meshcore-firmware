#pragma once

#include <cstdint>
#include <cmath>
#include <string>
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

// --- Pin I/O for host tests of button logic (#1206) ---------------------------
// Tests set g_mock_pin_level[] to what the pin reads, and can check the mode the
// code under test set. Values match the Arduino cores' own constants.
#define LOW            0x0
#define HIGH           0x1
#define INPUT          0x0
#define OUTPUT         0x1
#define INPUT_PULLUP   0x2
#define INPUT_PULLDOWN 0x3
inline int g_mock_pin_level[64] = {};
inline int g_mock_pin_mode[64] = {};
inline int g_mock_analog[64] = {};
inline void pinMode(uint8_t pin, uint8_t mode) { if (pin < 64) g_mock_pin_mode[pin] = mode; }
inline int digitalRead(uint8_t pin) { return pin < 64 ? g_mock_pin_level[pin] : LOW; }
inline int analogRead(uint8_t pin) { return pin < 64 ? g_mock_analog[pin] : 0; }

// Sink for MeshLog's console mirror. Discards output unless a test sets `record`,
// in which case everything written lands in `captured` (#1211: the console must
// see a mesh_log_print line exactly once). Off by default, so every other test
// keeps the discarding sink.
//
// #718: derives from Stream, not Print. ArduinoSerialInterface compares its
// target against `&Serial` (isConsoleSharedWithProtocol, #411) via a
// static_cast<Stream*>, which does not compile if the mock Serial is not a
// Stream -- and that comparison is on the path of anything that unit-tests the
// framed serial transport natively.
class MockSerial : public Stream {
public:
  bool record = false;
  std::string captured;

  size_t write(uint8_t c) override {
    if (record) captured += static_cast<char>(c);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    if (record) captured.append(reinterpret_cast<const char*>(buf), size);
    return size;
  }
  void begin(unsigned long) {}
  // MeshLog throttles its console mirror on this; a large constant means
  // "never backpressure", which is what a discarding sink should report.
  int availableForWrite() override { return 1024; }
  void flush() override {}
  operator bool() const { return true; }
};
inline MockSerial Serial;
