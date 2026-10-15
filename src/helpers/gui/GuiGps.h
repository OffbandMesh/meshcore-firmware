#pragma once

// GPS fix type and time since the fix was lost (#1456, PLAN.md §3 B5).
// MicroNMEA reports whether the altitude is valid; getAltitude() in the
// location provider throws that away today, so 2D and 3D look the same.

#include <stdint.h>

namespace offband {
namespace gui {

enum class FixType : uint8_t { None, Fix2D, Fix3D };

// A position fix with a valid altitude is 3D; without one, 2D.
FixType fixType(bool position_valid, bool altitude_valid);

// Tracks fix/no-fix over time.
class FixTracker {
public:
  void update(uint32_t now_ms, bool has_fix);
  bool hasFix() const { return _fix; }
  bool everFixed() const { return _ever; }
  // Whole seconds since the fix was lost; 0 while fixed or if never fixed.
  uint32_t secsSinceLost(uint32_t now_ms) const;

private:
  bool _fix = false;
  bool _ever = false;
  uint32_t _lost_at = 0;
};

}  // namespace gui
}  // namespace offband
