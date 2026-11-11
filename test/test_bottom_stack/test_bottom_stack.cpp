// #2113: BottomStack.h against the real LVGL 8.3 with the device's lv_conf.h,
// headless, with a pointer driven by the test. A chat thread rests on its
// compose bar while it fits, and the whole history scrolls by drag once it
// doesn't.

#include <gtest/gtest.h>

#include <lvgl.h>

#include "helpers/gui/BottomStack.h"

using offband::gui::bottomStack;

namespace {

lv_coord_t g_x = 0, g_y = 0;
bool g_pressed = false;

void flushCb(lv_disp_drv_t* drv, const lv_area_t*, lv_color_t*) { lv_disp_flush_ready(drv); }

void readCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  data->point.x = g_x;
  data->point.y = g_y;
  data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void pump(int passes = 1) {
  for (int i = 0; i < passes; i++) {
    lv_tick_inc(40);
    lv_timer_handler();
  }
}

constexpr lv_coord_t kListH = 200, kRowH = 40;

class Stack : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    lv_init();
    static lv_disp_draw_buf_t buf;
    static lv_color_t px[240 * 20];
    lv_disp_draw_buf_init(&buf, px, nullptr, 240 * 20);
    static lv_disp_drv_t disp;
    lv_disp_drv_init(&disp);
    disp.hor_res = 240;
    disp.ver_res = 320;
    disp.flush_cb = flushCb;
    disp.draw_buf = &buf;
    lv_disp_drv_register(&disp);
    static lv_indev_drv_t indev;
    lv_indev_drv_init(&indev);
    indev.type = LV_INDEV_TYPE_POINTER;
    indev.read_cb = readCb;
    lv_indev_drv_register(&indev);
  }

  void SetUp() override {
    lv_obj_clean(lv_scr_act());
    g_pressed = false;
    pump(2);
  }

  // The thread's message list as Screens.cpp builds it: bottom-aligned,
  // scrollable and clickable, holding rows that aren't clickable.
  static lv_obj_t* thread(int rows) {
    lv_obj_t* l = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, 240, kListH);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(l, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_ver(l, 6, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    for (int i = 0; i < rows; i++) {
      lv_obj_t* r = lv_obj_create(l);
      lv_obj_remove_style_all(r);
      lv_obj_set_size(r, LV_PCT(100), kRowH);
      lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    return l;
  }

  // A finger down mid-list, drawn down 120 px (toward older messages), lifted.
  static void dragDown() {
    g_x = 120;
    g_y = 40;
    g_pressed = true;
    pump(2);
    for (int i = 0; i < 12; i++) {
      g_y += 10;
      pump();
    }
    g_pressed = false;
    pump(4);
  }

  static lv_obj_t* last(lv_obj_t* l) { return lv_obj_get_child(l, lv_obj_get_child_cnt(l) - 1); }
};

}  // namespace

TEST_F(Stack, BottomAlignedAloneAnOverflowingThreadCannotScroll) {
  // The cause: the oldest rows sit above the top, out of the scroll range.
  lv_obj_t* l = thread(20);
  lv_obj_update_layout(l);
  lv_obj_scroll_to_y(l, LV_COORD_MAX, LV_ANIM_OFF);
  EXPECT_LT(lv_obj_get_child(l, 0)->coords.y1, l->coords.y1);
  EXPECT_EQ(0, lv_obj_get_scroll_bottom(l));
  EXPECT_EQ(0, lv_obj_get_scroll_top(l));
  dragDown();
  EXPECT_EQ(0, lv_obj_get_scroll_y(l));
}

TEST_F(Stack, AnOverflowingThreadShowsTheNewestAndScrollsBack) {
  lv_obj_t* l = thread(20);
  EXPECT_TRUE(bottomStack(l));
  lv_obj_scroll_to_y(l, LV_COORD_MAX, LV_ANIM_OFF);
  lv_obj_update_layout(l);
  // The newest rests on the bottom edge, as before.
  EXPECT_EQ(l->coords.y2 - lv_obj_get_style_pad_bottom(l, LV_PART_MAIN), last(l)->coords.y2);
  const lv_coord_t at_newest = lv_obj_get_scroll_y(l);
  EXPECT_GT(at_newest, 0);
  dragDown();
  EXPECT_LT(lv_obj_get_scroll_y(l), at_newest - 50);   // the history came into view
}

TEST_F(Stack, TheWholeHistoryIsInReach) {
  lv_obj_t* l = thread(20);
  bottomStack(l);
  lv_obj_scroll_to_y(l, 0, LV_ANIM_OFF);
  lv_obj_update_layout(l);
  EXPECT_EQ(l->coords.y1 + lv_obj_get_style_pad_top(l, LV_PART_MAIN), lv_obj_get_child(l, 0)->coords.y1);
}

TEST_F(Stack, AThreadThatFitsRestsOnTheBottom) {
  lv_obj_t* l = thread(2);
  EXPECT_FALSE(bottomStack(l));
  EXPECT_EQ(l->coords.y2 - lv_obj_get_style_pad_bottom(l, LV_PART_MAIN), last(l)->coords.y2);
  EXPECT_EQ(0, lv_obj_get_scroll_bottom(l));
}

TEST_F(Stack, AThreadThatStopsFittingIsRestackedOnTheNextBuild) {
  lv_obj_t* l = thread(2);
  bottomStack(l);
  for (int i = 0; i < 18; i++) {
    lv_obj_t* r = lv_obj_create(l);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), kRowH);
  }
  EXPECT_TRUE(bottomStack(l));
  lv_obj_scroll_to_y(l, 0, LV_ANIM_OFF);
  lv_obj_update_layout(l);
  EXPECT_GE(lv_obj_get_child(l, 0)->coords.y1, l->coords.y1);
}

TEST_F(Stack, AnEmptyListIsLeftAlone) {
  lv_obj_t* l = lv_obj_create(lv_scr_act());
  EXPECT_FALSE(bottomStack(l));
  EXPECT_FALSE(bottomStack(nullptr));
}

// The rule for keeping a reader's place and for showing "Newest".
TEST_F(Stack, AtTheNewestIsNotReadingHistory) {
  lv_obj_t* l = thread(20);
  bottomStack(l);
  lv_obj_scroll_to_y(l, LV_COORD_MAX, LV_ANIM_OFF);
  EXPECT_FALSE(offband::gui::readingHistory(l));
}

TEST_F(Stack, ADragUpIntoTheHistoryIsReadingIt) {
  lv_obj_t* l = thread(20);
  bottomStack(l);
  lv_obj_scroll_to_y(l, LV_COORD_MAX, LV_ANIM_OFF);
  dragDown();
  EXPECT_TRUE(offband::gui::readingHistory(l));
}

TEST_F(Stack, AFewPixelsShortIsStillTheNewest) {
  lv_obj_t* l = thread(20);
  bottomStack(l);
  lv_obj_scroll_to_y(l, LV_COORD_MAX, LV_ANIM_OFF);
  lv_obj_scroll_by(l, 0, offband::gui::kReadingHistoryPx, LV_ANIM_OFF);
  EXPECT_FALSE(offband::gui::readingHistory(l));
  lv_obj_scroll_by(l, 0, 1, LV_ANIM_OFF);
  EXPECT_TRUE(offband::gui::readingHistory(l));
}

TEST_F(Stack, AThreadThatFitsIsNeverReadingHistory) {
  lv_obj_t* l = thread(2);
  bottomStack(l);
  EXPECT_FALSE(offband::gui::readingHistory(l));
  EXPECT_FALSE(offband::gui::readingHistory(nullptr));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
