#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Text for the diag self-test screen (#1185). Header-only so it is unit-tested natively.
namespace qcc {

// The panel is 128 px wide and the screen's lines use the 6 px default font.
constexpr size_t kSelfTestLineChars = 21;

// SafeBoot's own battery reading at this boot. SafeBoot also lets the boot through when
// its read comes back 0, so 0 reads "no reading": the gate did not check this boot.
inline void formatSafeBootLine(char* out, size_t n, uint16_t mv) {
  if (mv == 0) {
    snprintf(out, n, "SafeBoot: no reading");
  } else {
    snprintf(out, n, "SafeBoot: %u mV", (unsigned)mv);
  }
}

// Whether the CardKB-compatible keyboard answered at boot (#1207). TAB on Home opens the
// key-test screen when it did.
inline void formatKeyboardLine(char* out, size_t n, bool found) {
  snprintf(out, n, found ? "KB: found  TAB=keys" : "KB: none");
}

}  // namespace qcc
