#pragma once

// Style setters that leave an object alone when the value is unchanged.
//
// LVGL redraws an object on every local style set, same value or not
// (lv_obj_set_local_style_prop calls lv_obj_refresh_style, which
// invalidates it). On a TFT a redraw costs a few milliseconds. On e-paper
// each one is a panel refresh: the Nodes tab recolored every row's bars
// once a second and kept the T-Deck Pro refreshing every 1.7 s, with a full
// refresh's black flash every 34 s (#2110 bench, 2026-11-11). Code that
// restyles on a timer uses these.
//
// The comparison is against the object's own local value for the selector,
// not the style it draws with now, so a theme's focused or pressed style
// can't make a needed set look unneeded. Header-only, as ScrollBody.h is:
// the LVGL tests (env:native_lvgl) include it directly.

#include <lvgl.h>

namespace offband {
namespace gui {

namespace restyle_detail {

inline bool sameValue(lv_style_prop_t prop, const lv_style_value_t& a, const lv_style_value_t& b) {
  switch (prop) {
    case LV_STYLE_BG_COLOR:
    case LV_STYLE_BORDER_COLOR:
    case LV_STYLE_TEXT_COLOR:
      return lv_color_to32(a.color) == lv_color_to32(b.color);
    default:
      return a.num == b.num;
  }
}

// Sets `prop` at `selector` unless the object already holds that value
// there. True when it set it.
inline bool setIfNew(lv_obj_t* obj, lv_style_prop_t prop, lv_style_value_t v, lv_style_selector_t selector) {
  if (!obj) return false;
  lv_style_value_t cur;
  if (lv_obj_get_local_style_prop(obj, prop, &cur, selector) == LV_STYLE_RES_FOUND && sameValue(prop, cur, v)) {
    return false;
  }
  lv_obj_set_local_style_prop(obj, prop, v, selector);
  return true;
}

}  // namespace restyle_detail

inline bool setBgColorIfNew(lv_obj_t* obj, lv_color_t c, lv_style_selector_t selector = 0) {
  lv_style_value_t v;
  v.color = c;
  return restyle_detail::setIfNew(obj, LV_STYLE_BG_COLOR, v, selector);
}

inline bool setBgOpaIfNew(lv_obj_t* obj, lv_opa_t opa, lv_style_selector_t selector = 0) {
  lv_style_value_t v;
  v.num = opa;
  return restyle_detail::setIfNew(obj, LV_STYLE_BG_OPA, v, selector);
}

inline bool setBorderColorIfNew(lv_obj_t* obj, lv_color_t c, lv_style_selector_t selector = 0) {
  lv_style_value_t v;
  v.color = c;
  return restyle_detail::setIfNew(obj, LV_STYLE_BORDER_COLOR, v, selector);
}

inline bool setTextColorIfNew(lv_obj_t* obj, lv_color_t c, lv_style_selector_t selector = 0) {
  lv_style_value_t v;
  v.color = c;
  return restyle_detail::setIfNew(obj, LV_STYLE_TEXT_COLOR, v, selector);
}

}  // namespace gui
}  // namespace offband
