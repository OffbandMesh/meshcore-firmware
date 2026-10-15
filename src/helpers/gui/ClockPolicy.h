#pragma once

// CPU clock policy for GUI builds (owner decision, #1326, 2026-10-07):
// 240 MHz while the screen is on, 80 MHz while it is off. At 80 MHz the
// mesh, radio and BLE work is unaffected; drawing is what needs the speed
// (LVGL scroll 9.3 fps at 80 MHz vs 16.9 at 240, #1326).

#include <stdint.h>

namespace offband {
namespace gui {

constexpr uint32_t kCpuMhzScreenOn = 240;
constexpr uint32_t kCpuMhzScreenOff = 80;

uint32_t cpuMhzFor(bool screen_on);

}  // namespace gui
}  // namespace offband
