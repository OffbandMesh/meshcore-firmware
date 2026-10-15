#include "GuiGps.h"

namespace offband {
namespace gui {

FixType fixType(bool position_valid, bool three_d) {
  if (!position_valid) return FixType::None;
  return three_d ? FixType::Fix3D : FixType::Fix2D;
}

void FixTracker::update(uint32_t now_ms, bool has_fix) {
  if (has_fix) {
    _fix = true;
    _ever = true;
  } else if (_fix) {
    _fix = false;
    _lost_at = now_ms;  // the moment it went; later no-fix updates keep it
  }
}

uint32_t FixTracker::secsSinceLost(uint32_t now_ms) const {
  if (_fix || !_ever) return 0;
  return (now_ms - _lost_at) / 1000;  // unsigned difference: survives the millis() wrap
}

}  // namespace gui
}  // namespace offband
