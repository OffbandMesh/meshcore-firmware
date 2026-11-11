#pragma once

// A list that rests on its bottom edge while it fits, and scrolls through all
// of it once it doesn't: a chat thread, newest message on the compose bar
// (#2113, B6 bench).
//
// Bottom alignment alone (flex main place END) can't do the second part.
// When the children overflow, flex places them from max - content, which is
// negative, so the oldest sit above the container's top edge. LVGL has no
// scroll range up there: lv_obj_get_scroll_top() is just -scroll.y
// (lv_obj_scroll.c), and the bottom ends flush. An END-aligned list that
// overflows therefore can't scroll at all, by touch or by keys: the bench
// saw 14 drags on a long thread draw nothing.
//
// Header-only, as ScrollBody.h is: the LVGL tests include it directly.

#include <lvgl.h>

namespace offband {
namespace gui {

// Call once the list's children are added: lays it out bottom-aligned, and
// if that puts its first child above the top, top-aligned instead, so the
// whole list is in the scroll range. Scroll to the end afterwards to show
// the newest. True when it overflows.
inline bool bottomStack(lv_obj_t* list) {
  if (!list) return false;
  lv_obj_set_style_flex_main_place(list, LV_FLEX_ALIGN_END, 0);
  lv_obj_update_layout(list);
  lv_obj_t* first = lv_obj_get_child(list, 0);
  if (!first) return false;
  const lv_coord_t top = list->coords.y1 + lv_obj_get_style_pad_top(list, LV_PART_MAIN);
  if (first->coords.y1 >= top) return false;
  lv_obj_set_style_flex_main_place(list, LV_FLEX_ALIGN_START, 0);
  lv_obj_update_layout(list);
  return true;
}

// More than this above the end is reading history: a redraw keeps the
// reader's place (Epic D), and the thread offers a way back to the newest
// (#2113 B6 bench).
constexpr lv_coord_t kReadingHistoryPx = 8;

inline bool readingHistory(lv_obj_t* list) {
  return list && lv_obj_get_scroll_bottom(list) > kReadingHistoryPx;
}

}  // namespace gui
}  // namespace offband
