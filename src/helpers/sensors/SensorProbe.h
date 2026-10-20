#pragma once

// Which entries of the sensor table EnvironmentSensorManager::begin() starts
// (#1671, Epic M). Pure and header-only, so the native tests run the same
// rules the device does:
// - an entry is tried only if its address answered the I2C scan;
// - primary entries are all tried before any fallback entry, wherever the
//   fallbacks sit in the table (the Bosch parts' 0x77 alternates are
//   fallbacks, so a BMP085 at 0x77 is started by its own driver first);
// - an address a driver already started is never probed by another driver
//   (another driver's begin() can reset a chip it doesn't own);
// - one sensor per driver, since each driver is a single instance;
// - within each pass table order is kept, so the channels a node already
//   reports don't move, and a part with several channels (INA3221) gets one
//   hit per channel.

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace sensors {

struct ProbeEntry {
  uint8_t address;
  uintptr_t driver;   // the same value for every entry of one driver
  bool fallback;      // an alternate address, tried after every primary entry
};

struct ProbeHit {
  uint16_t entry;   // index into the table
  uint8_t sub;      // channel within that part
};

// Tries the table, primaries then fallbacks. init(i) starts entry i on the
// hardware and returns its channel count (0: it isn't that part, or it
// failed). Returns the hits, at most max.
template <typename InitFn>
size_t runProbe(const ProbeEntry* table, size_t n, const bool detected[128], InitFn init, ProbeHit* out,
                size_t max) {
  bool claimed[128] = {};
  size_t hits = 0;
  for (int pass = 0; pass < 2; pass++) {
    for (size_t i = 0; i < n && hits < max; i++) {
      const ProbeEntry& e = table[i];
      if (e.fallback != (pass == 1)) continue;
      if (e.address >= 128 || !detected[e.address] || claimed[e.address]) continue;
      bool running = false;
      for (size_t h = 0; h < hits && !running; h++) running = table[out[h].entry].driver == e.driver;
      if (running) continue;
      const uint8_t subs = init(i);
      if (subs == 0) continue;
      claimed[e.address] = true;
      for (uint8_t s = 0; s < subs && hits < max; s++) out[hits++] = ProbeHit{uint16_t(i), s};
    }
  }
  return hits;
}

}  // namespace sensors
}  // namespace offband
