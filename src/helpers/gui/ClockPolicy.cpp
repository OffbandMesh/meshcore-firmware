#include "ClockPolicy.h"

namespace offband {
namespace gui {

uint32_t cpuMhzFor(bool screen_on) {
  (void)screen_on;
  return 0;
}

}  // namespace gui
}  // namespace offband
