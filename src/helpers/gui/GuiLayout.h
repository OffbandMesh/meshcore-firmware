#pragma once

// Layout classes (Epic H, #1432): the screen's geometry and the values the
// design varies by class, chosen from the rotation. Free of LVGL and the
// panel so it runs in the native tests.
//
// Design of record (ObScreen.dc.html, spec v0.3):
// - Class A, portrait 240x320: status 20 px, content 240x260, tab bar 40 px.
// - Class B, landscape 320x240: status 20 px across the top, a 48 px rail on
//   the left, content 272x220.
// Gap screens follow the owner-approved epic-h-landscape-mockups.html
// (2026-10-19).

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

enum class LayoutClass : uint8_t { A, B };

struct Layout {
  LayoutClass cls;
  uint16_t rotation;   // 0, 90 or 270
  int16_t w, h;        // the screen as LVGL draws it
  int16_t status_h;    // the status strip, full width
  int16_t tab_h;       // class A's tab bar; 0 in class B
  int16_t rail_w;      // class B's rail; 0 in class A
  int16_t content_x, content_y, content_w, content_h;
  int16_t row_h;        // a Settings page row
  int16_t cat_row_h;    // a Settings list (category) row
  int16_t sheet_left;   // where a bottom sheet starts: right of the rail in class B
  int16_t sheet_row_h;  // a sheet's option row
  int16_t pick_rows;    // a picker's window
  int16_t node_rows;    // Nodes rows the design draws on one screen
  int16_t key_w, key_h; // an on-screen keyboard key
  int16_t logo;         // the boot and lock mark
  bool hints;           // row hints show (the design hides them in class B)
  bool pad_beside;      // number pads sit beside their field, not under it
};

// The rotations the setting offers, in its order.
constexpr uint16_t kRotations[] = {0, 90, 270};
constexpr size_t kRotationCount = sizeof(kRotations) / sizeof(kRotations[0]);

// 90 and 270 are landscape (class B); anything else is portrait (class A),
// so a corrupt pref still draws.
LayoutClass layoutClassFor(uint16_t rotation);
Layout layoutFor(uint16_t rotation);

// The Rotation row and picker: "Portrait 0°", "Landscape 90°", "Landscape 270°".
const char* rotationText(uint16_t rotation);
// Where a rotation sits in kRotations; 0 (portrait) if it isn't one.
int rotationIndex(uint16_t rotation);

// ST7789 MADCTL for a rotation, from the portrait value the board uses
// (0x00 on the Heltec V4 TFT): 90 exchanges rows and columns and mirrors
// columns (MV|MX), 270 exchanges them and mirrors rows (MV|MY).
uint8_t madctlFor(uint16_t rotation, uint8_t portrait);

// A touch point in portrait panel coordinates (240x320, after the board's
// own mapping) to where LVGL draws it at this rotation, clamped inside.
void rotateTouch(uint16_t rotation, uint16_t* x, uint16_t* y);

}  // namespace gui
}  // namespace offband
