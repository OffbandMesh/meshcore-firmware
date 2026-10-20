#pragma once

// A scrolling page body: what every Settings page builds its rows in (Epic G),
// and what the native LVGL tests drag (#1671).
//
// It must be clickable as well as scrollable. LVGL only starts a drag on an
// object its hit test finds, and lv_obj_hit_test skips anything not clickable
// (lv_obj_pos.c), so a drag that began on a read-only row found nothing to
// scroll and the page stayed put (bench, Epic M, 2026-10-19).

#include <lvgl.h>

namespace offband {
namespace gui {

inline lv_obj_t* scrollBody(lv_obj_t* parent) {
  lv_obj_t* body = lv_obj_create(parent);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, LV_PCT(100), 0);
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  // Start from none, as the GUI's box() does (lv_obj_create makes an object
  // clickable by default, which would hide this rule from the tests), then
  // set both.
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  return body;
}

}  // namespace gui
}  // namespace offband
