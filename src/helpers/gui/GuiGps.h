#pragma once

// GPS fix type and time since the fix was lost (#1456, PLAN.md §3 B5).
// 3D comes from the receiver's GSA fix mode (LocationProvider::fixMode()).
// The plan named MicroNMEA's altitude-valid flag, but that flag turns true on
// the first GGA sentence and never clears, and GGA can't tell 2D from 3D.

#include <stdint.h>

namespace offband {
namespace gui {

enum class FixType : uint8_t { None, Fix2D, Fix3D };

// A position fix the receiver reports as 3D is 3D; any other fix is 2D.
FixType fixType(bool position_valid, bool three_d);

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
