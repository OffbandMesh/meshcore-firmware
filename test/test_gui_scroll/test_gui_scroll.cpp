// #1671 (Epic M): Settings pages scroll by touch. Runs the real LVGL 8.3 (the
// GUI env's version, with the device's lv_conf.h) headless: a 240x320 display
// whose flush does nothing, and a pointer driven by the test. A drag that
// starts on a read-only row must scroll the page body.

#include <gtest/gtest.h>

#include <lvgl.h>

#include "helpers/gui/ScrollBody.h"

using offband::gui::scrollBody;

namespace {

lv_coord_t g_x = 0, g_y = 0;
bool g_pressed = false;

void flushCb(lv_disp_drv_t* drv, const lv_area_t*, lv_color_t*) { lv_disp_flush_ready(drv); }

void readCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  data->point.x = g_x;
  data->point.y = g_y;
  data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// One LVGL pass, a read period on.
void pump(int passes = 1) {
  for (int i = 0; i < passes; i++) {
    lv_tick_inc(40);
    lv_timer_handler();
  }
}

class Scroll : public ::testing::Test {
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

  // A page as the Settings screens build it: a fixed-height column with a
  // body that grows into it, holding read-only rows taller than the page.
  lv_obj_t* page() {
    lv_obj_t* p = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, 240, 200);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return p;
  }
  static void rows(lv_obj_t* body) {
    for (int i = 0; i < 12; i++) {
      lv_obj_t* r = lv_obj_create(body);
      lv_obj_remove_style_all(r);
      lv_obj_set_size(r, LV_PCT(100), 40);
      lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);   // a reading: not a control
    }
  }

  // A finger down on a row, drawn upward, then lifted.
  static void dragUp() {
    g_x = 120;
    g_y = 160;
    g_pressed = true;
    pump(2);
    for (int i = 0; i < 12; i++) {
      g_y -= 10;
      pump();
    }
    g_pressed = false;
    pump(4);
  }
};

}  // namespace

TEST_F(Scroll, ADragOnAReadOnlyRowScrollsTheBody) {
  lv_obj_t* body = scrollBody(page());
  rows(body);
  pump(2);
  ASSERT_EQ(lv_obj_get_scroll_y(body), 0);
  dragUp();
  // The finger moved 120 px; the page must follow most of it, not twitch.
  EXPECT_GE(lv_obj_get_scroll_y(body), 50);
}

TEST_F(Scroll, AScrollableOnlyBodyDoesNotScroll) {
  // The bug: a body that is scrollable but not clickable is never found by
  // the hit test, so the same drag moves nothing. This is what keeps the test
  // above honest.
  lv_obj_t* body = scrollBody(page());
  lv_obj_clear_flag(body, LV_OBJ_FLAG_CLICKABLE);
  rows(body);
  pump(2);
  dragUp();
  EXPECT_EQ(lv_obj_get_scroll_y(body), 0);
}

TEST_F(Scroll, ContentThatFitsStaysPut) {
  lv_obj_t* body = scrollBody(page());
  lv_obj_t* r = lv_obj_create(body);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, LV_PCT(100), 40);
  pump(2);
  dragUp();
  EXPECT_EQ(lv_obj_get_scroll_y(body), 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
