#include "GuiGps.h"

namespace offband {
namespace gui {

// B1 stubs: the tests in test_gui_data pin the behavior; B5 implements it.
FixType fixType(bool, bool) { return FixType::None; }
void FixTracker::update(uint32_t, bool) {}
uint32_t FixTracker::secsSinceLost(uint32_t) const { return 0xFFFFFFFF; }

}  // namespace gui
}  // namespace offband
