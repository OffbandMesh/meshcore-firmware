#pragma once

// LVGL on the GUI panel (#1441): two partial draw buffers in internal DMA
// RAM, so LVGL renders into one while the other is on the wire.

#include <stddef.h>
#include <stdint.h>

class GuiPanel;

namespace offband {
namespace gui {

// 1/10 of the 320-row portrait screen per buffer (PLAN.md A3).
constexpr uint16_t kDrawBufLines = 32;

// Allocates the buffers, brings up the panel and registers it with LVGL.
// Returns false (and leaves LVGL uninitialized) if either step fails.
bool lvglBegin(GuiPanel& panel);

// Runs LVGL's timers and rendering. Call from the main loop.
void lvglLoop();

// Bytes held by the two draw buffers, for the memory telemetry.
size_t lvglDrawBufBytes();

}  // namespace gui
}  // namespace offband
