#pragma once

// Diag builds: the log, copied to the SD card once a second while a card is
// working, so a tester with no serial console can hand the log over by
// pulling the card (T-Deck bench, #1730). It reads MeshLog's capture ring
// with its own cursor, so it takes nothing from the client's log download.
//
// /sd/offband-diag.log; at 4 MB it becomes offband-diag.old.log and a new
// one starts, so a card never fills with logs.

#include <stdint.h>

class SdDiagLog {
public:
  // Call from the loop while the card is mounted and working.
  void tick(uint32_t now_ms);

private:
  uint64_t _cursor = 0;
  uint32_t _last = 0;
  bool _started = false;
};
