#pragma once

// A label that wraps at a maximum width (#2113, B6 bench).
//
// LVGL 8.3 measures a content-width label as one line however long it is
// (lv_label.c, LV_EVENT_GET_SELF_SIZE uses LV_COORD_MAX), then clamps the
// object to max_width and draws the text with LV_TEXT_FLAG_FIT, which does
// not wrap. So a long message showed as one clipped line: the bench saw a
// Public message stop after its first 48 bytes, short of the bubble's edge.
//
// Text wider than max_w gets max_w as a fixed width, which LVGL wraps; text
// that fits keeps its content width, so a short bubble stays short.
// Header-only, as ScrollBody.h is: the LVGL tests include it directly.

#include <lvgl.h>

namespace offband {
namespace gui {

// Call after the label's text and font are set.
inline void wrapAt(lv_obj_t* label, lv_coord_t max_w) {
  if (!label) return;
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_point_t one_line;
  lv_txt_get_size(&one_line, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN),
                  lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                  lv_obj_get_style_text_line_space(label, LV_PART_MAIN), LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_obj_set_width(label, one_line.x > max_w ? max_w : LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(label, max_w, 0);
}

}  // namespace gui
}  // namespace offband
