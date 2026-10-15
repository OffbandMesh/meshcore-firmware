#include "ClockPolicy.h"

namespace offband {
namespace gui {

uint32_t cpuMhzFor(bool screen_on) { return screen_on ? kCpuMhzScreenOn : kCpuMhzScreenOff; }

}  // namespace gui
}  // namespace offband
