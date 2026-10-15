#pragma once

// GSA sentence fix mode (#1456): field 2 is 1 = no fix, 2 = 2D, 3 = 3D.
// MicroNMEA parses only GGA and RMC, and GGA can't tell 2D from 3D (its fix
// quality is the same for both), so the location provider reads GSA itself
// through MicroNMEA's unknown-sentence hook. Arduino-free so it is tested
// natively.

#include <string.h>

namespace offband {

// The fix mode (1..3) from a full sentence such as "$GNGSA,A,3,...*hh", or 0
// when it isn't a GSA sentence or the mode field is missing or out of range.
inline unsigned char parseGsaMode(const char* sentence) {
  if (!sentence || sentence[0] != '$') return 0;
  const char* comma = strchr(sentence, ',');
  // "$" + 2-char talker + "GSA" puts the first comma at offset 6.
  if (!comma || comma - sentence != 6 || strncmp(sentence + 3, "GSA", 3) != 0) return 0;
  const char* mode = strchr(comma + 1, ',');  // skip field 1 (A/M selection)
  if (!mode) return 0;
  const char m = mode[1];
  if (m < '1' || m > '3') return 0;
  if (mode[2] != ',' && mode[2] != '*' && mode[2] != '\0') return 0;
  return (unsigned char)(m - '0');
}

}  // namespace offband
