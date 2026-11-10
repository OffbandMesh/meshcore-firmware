#pragma once

// Scrolling on e-paper (#2110, plan §9 choice 3). LVGL's momentum keeps a
// list moving after the finger lifts, and its elastic edge stretches past
// the end and springs back. Each is a run of frames, and on e-paper every
// frame is a refresh. With both off, a drag stops where the finger does.
//
// LVGL sets both flags in every object's constructor, after the theme has
// run, so neither a theme nor a sweep at build time catches objects made
// later. It reads them only once a drag has picked what it scrolls, after
// LV_EVENT_SCROLL_BEGIN, so this clears them on that object at that event,
// through the pointer driver's feedback hook.
//
// Header-only, as ScrollBody.h is: the native tests build src/helpers/gui
// without LVGL, and the LVGL tests (env:native_lvgl) include it directly.

#include <lvgl.h>

namespace offband {
namespace gui {

inline void einkScrollFeedback(lv_indev_drv_t* drv, uint8_t code) {
  (void)drv;
  if (code != LV_EVENT_SCROLL_BEGIN) return;
  lv_obj_t* obj = lv_indev_get_scroll_obj(lv_indev_get_act());
  if (obj) lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ELASTIC);
}

// Sets the driver's feedback_cb. Call before lv_indev_drv_register().
inline void einkScrollAttach(lv_indev_drv_t* drv) {
  if (drv) drv->feedback_cb = einkScrollFeedback;
}

}  // namespace gui
}  // namespace offband
