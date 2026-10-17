#pragma once

// The small LVGL helpers every GuiShell screen file builds with (Screens.cpp,
// SetupScreens.cpp). Moved here from Screens.cpp when Setup (Epic G) took
// its own file; unchanged.

#include <lvgl.h>
#include <string.h>

#include <MeshLog.h>
#include <helpers/gui/GuiViews.h>
#include <helpers/gui/hw/GuiFonts.h>

namespace shell {

using offband::gui::FontRole;

constexpr int16_t kW = 240;
constexpr int16_t kStatusH = 20;
constexpr int16_t kTabH = 40;
constexpr int16_t kContentH = 320 - kStatusH - kTabH;

// The param of a CLICKED that a button (USR hold) sent rather than a touch.
inline const char* byButton() {
  static const char kByButton[] = "button";
  return kByButton;
}

// Names a step that held the loop past 100 ms (bench, #1534: taps lost to
// a 981 ms stall during search). One line, when it happens.
struct SlowStep {
  const char* what;
  int detail;
  uint32_t t0;
  SlowStep(const char* w, int d = 0) : what(w), detail(d), t0(lv_tick_get()) {}
  ~SlowStep() {
    const uint32_t ms = lv_tick_elaps(t0);
    if (ms > 100) mesh_log_line(MLOG_BOOT, "[gui] slow %s (%d): %lums\n", what, detail, (unsigned long)ms);
  }
};

inline lv_color_t c(uint32_t rgb) { return lv_color_hex(rgb & 0xFFFFFF); }

inline lv_obj_t* box(lv_obj_t* parent, lv_coord_t w, lv_coord_t h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return o;
}

inline lv_obj_t* label(lv_obj_t* parent, FontRole role, uint32_t rgb, const char* text = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, offband::gui::font(role), 0);
  lv_obj_set_style_text_color(l, c(rgb), 0);
  lv_label_set_text(l, text);
  return l;
}

// Text from the mesh, with emoji-steering characters dropped (displayText).
inline void userText(lv_obj_t* l, const char* s, size_t len) {
  static char buf[offband::gui::kMsgTextMax * 2 + 1];   // off the stack; the GUI is single-threaded
  offband::gui::displayText(s, len, buf, sizeof buf);
  lv_label_set_text(l, buf);
}
inline void userText(lv_obj_t* l, const char* s) { userText(l, s, strlen(s)); }

// A label held to one line, cut with "…". LVGL only cuts a label whose
// height is fixed; with a content height it wraps instead (bench, #1501).
inline void oneLine(lv_obj_t* l, FontRole role) {
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_height(l, lv_font_get_line_height(offband::gui::font(role)));
}

inline lv_obj_t* line(lv_obj_t* parent, const lv_point_t* pts, uint16_t n, uint32_t rgb) {
  lv_obj_t* l = lv_line_create(parent);
  lv_line_set_points(l, pts, n);
  lv_obj_set_style_line_width(l, 1, 0);
  lv_obj_set_style_line_color(l, c(rgb), 0);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}

// The "‹" back mark, 10 x 20.
inline const lv_point_t* chevronPoints() {
  static const lv_point_t k[] = {{7, 4}, {2, 10}, {7, 16}};
  return k;
}

}  // namespace shell
