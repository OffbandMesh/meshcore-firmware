#pragma once

// LVGL on the GUI panel (#1441): two partial draw buffers in internal DMA
// RAM, so LVGL renders into one while the other is on the wire.

#include <stddef.h>
#include <stdint.h>

#include "../TouchFrame.h"

class GuiPanel;

namespace offband {
namespace gui {

// Registers the touch controller (CHSC6x or GT911) as LVGL's pointer input.
// Call after lvglBegin(). The last touched point is kept so a release lands
// where the finger left.
void lvglAttachTouch(TouchSource& touch, const TouchMapping& mapping);

// The last mapped touch point, for the test screen's calibration readout.
bool lvglLastTouch(uint16_t* x, uint16_t* y);

// 1/10 of the 320-row portrait screen per buffer (PLAN.md A3).
constexpr uint16_t kDrawBufLines = 32;

// Allocates the buffers, brings up the panel and registers it with LVGL.
// Returns false (and leaves LVGL uninitialized) if either step fails.
bool lvglBegin(GuiPanel& panel);

// Runs LVGL's timers and rendering. Call from the main loop.
void lvglLoop();

// Epic H: turns the panel (MADCTL from the board's portrait value), sets
// LVGL's resolution, and maps touches to match. 90 and 270 are landscape;
// anything else is portrait.
void lvglSetRotation(uint16_t rotation, uint8_t portrait_madctl);

// Bytes held by the two draw buffers, for the memory telemetry.
size_t lvglDrawBufBytes();

// Screen refreshes since the last call, with their average and longest
// duration in ms (render plus transfer), then resets. For the frame-rate
// check on the bench (#1441).
void lvglTakeFrameStats(uint32_t* frames, uint32_t* avg_ms, uint32_t* max_ms);

}  // namespace gui
}  // namespace offband
