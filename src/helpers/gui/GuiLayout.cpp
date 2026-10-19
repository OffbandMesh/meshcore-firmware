#include "GuiLayout.h"

namespace offband {
namespace gui {

namespace {
constexpr int16_t kPortraitW = 240, kPortraitH = 320;
constexpr uint8_t kMadctlMY = 0x80, kMadctlMX = 0x40, kMadctlMV = 0x20;
}  // namespace

LayoutClass layoutClassFor(uint16_t rotation) {
  return (rotation == 90 || rotation == 270) ? LayoutClass::B : LayoutClass::A;
}

Layout layoutFor(uint16_t rotation) {
  Layout l{};
  l.cls = layoutClassFor(rotation);
  l.status_h = 20;
  if (l.cls == LayoutClass::B) {
    l.rotation = rotation;
    l.w = kPortraitH;
    l.h = kPortraitW;
    l.tab_h = 0;
    l.rail_w = 48;
    l.row_h = 28;
    l.cat_row_h = 30;
    l.sheet_left = 56;
    l.sheet_row_h = 26;
    l.pick_rows = 5;
    l.node_rows = 5;
    l.key_w = 24;
    l.key_h = 25;
    l.logo = 40;
    l.hints = false;
    l.pad_beside = true;
  } else {
    l.rotation = 0;
    l.w = kPortraitW;
    l.h = kPortraitH;
    l.tab_h = 40;
    l.rail_w = 0;
    l.row_h = 34;
    l.cat_row_h = 34;
    l.sheet_left = 0;
    l.sheet_row_h = 30;
    l.pick_rows = 6;
    l.node_rows = 7;
    l.key_w = 20;
    l.key_h = 32;
    l.logo = 64;
    l.hints = true;
    l.pad_beside = false;
  }
  l.content_x = l.rail_w;
  l.content_y = l.status_h;
  l.content_w = int16_t(l.w - l.rail_w);
  l.content_h = int16_t(l.h - l.status_h - l.tab_h);
  return l;
}

const char* rotationText(uint16_t rotation) {
  switch (rotation) {
    case 90: return "Landscape 90°";
    case 270: return "Landscape 270°";
    default: return "Portrait 0°";
  }
}

int rotationIndex(uint16_t rotation) {
  for (size_t i = 0; i < kRotationCount; i++) {
    if (kRotations[i] == rotation) return int(i);
  }
  return 0;
}

uint8_t madctlFor(uint16_t rotation, uint8_t portrait) {
  // The board's colour-order and refresh bits stay; the three orientation
  // bits are replaced.
  const uint8_t keep = uint8_t(portrait & ~(kMadctlMY | kMadctlMX | kMadctlMV));
  switch (rotation) {
    case 90: return uint8_t(keep | kMadctlMV | kMadctlMX);
    case 270: return uint8_t(keep | kMadctlMV | kMadctlMY);
    default: return portrait;
  }
}

void rotateTouch(uint16_t rotation, uint16_t* x, uint16_t* y) {
  uint16_t px = *x, py = *y;
  if (px >= kPortraitW) px = kPortraitW - 1;
  if (py >= kPortraitH) py = kPortraitH - 1;
  switch (rotation) {
    case 90:   // MV|MX: LVGL (lx, ly) lands on portrait (239 - ly, lx)
      *x = py;
      *y = uint16_t(kPortraitW - 1 - px);
      return;
    case 270:  // MV|MY: LVGL (lx, ly) lands on portrait (ly, 319 - lx)
      *x = uint16_t(kPortraitH - 1 - py);
      *y = px;
      return;
    default:
      *x = px;
      *y = py;
      return;
  }
}

}  // namespace gui
}  // namespace offband
