// #2110 (B3): touch on e-paper, against the real LVGL 8.3 with the device's
// lv_conf.h, headless, as test_gui_scroll runs it.
//
// - einkScrollAttach: a drag stops where the finger does. No momentum after
//   the lift, no stretch past the end. Each case runs again with the hook
//   off, to show LVGL would have done otherwise.
// - TouchQueue drained through continue_reading: a tap caught during a
//   refresh reaches LVGL as a click, in the one pass after the refresh.

#include <gtest/gtest.h>

#include <lvgl.h>

#include "helpers/gui/EinkScroll.h"
#include "helpers/gui/TouchFrame.h"

using offband::gui::einkScrollAttach;
using offband::gui::TouchPoint;
using offband::gui::TouchQueue;
using offband::gui::TouchReplay;

namespace {

lv_coord_t g_x = 0, g_y = 0;
bool g_pressed = false;
TouchQueue g_queue;
lv_indev_drv_t g_indev;
void (*g_hook)(lv_indev_drv_t*, uint8_t) = nullptr;   // what the attach set

void flushCb(lv_disp_drv_t* drv, const lv_area_t*, lv_color_t*) { lv_disp_flush_ready(drv); }

TouchReplay g_replay(g_queue, LV_INDEV_DEF_LONG_PRESS_TIME);
bool g_raw_drain = false;   // the negative case: the queue taken as is

// As LvglPort's touchReadCb: what the refresh caught first, then the live
// state.
void readCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  TouchPoint p{};
  bool got, more = false;
  if (g_raw_drain) {
    got = g_queue.take(&p, nullptr);
    more = !g_queue.empty();
  } else {
    got = g_replay.next(lv_tick_get(), &p, nullptr, &more);
  }
  if (got) {
    data->continue_reading = more;
    if (p.pressed) {
      data->point.x = lv_coord_t(p.x);
      data->point.y = lv_coord_t(p.y);
    }
    data->state = p.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    return;
  }
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

class Eink : public ::testing::Test {
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
    lv_indev_drv_init(&g_indev);
    g_indev.type = LV_INDEV_TYPE_POINTER;
    g_indev.read_cb = readCb;
    einkScrollAttach(&g_indev);
    g_hook = g_indev.feedback_cb;
    lv_indev_drv_register(&g_indev);
  }

  void SetUp() override {
    lv_obj_clean(lv_scr_act());
    g_replay.clear();
    g_raw_drain = false;
    g_pressed = false;
    hook(true);
    pump(2);
  }

  // The negative cases run LVGL as it is without the attach.
  static void hook(bool on) { g_indev.feedback_cb = on ? g_hook : nullptr; }

  // A finger down near the top, drawn downward past the list's start, then
  // lifted. Returns the lowest scroll position seen (below 0 is a stretch).
  static lv_coord_t dragPastTheTop(lv_obj_t* l) {
    g_x = 120;
    g_y = 40;
    g_pressed = true;
    pump(2);
    lv_coord_t lowest = 0;
    for (int i = 0; i < 8; i++) {
      g_y += 15;
      pump();
      if (lv_obj_get_scroll_y(l) < lowest) lowest = lv_obj_get_scroll_y(l);
    }
    g_pressed = false;
    pump(20);
    return lowest;
  }

  // A list taller than the screen, as the Nodes list is.
  static lv_obj_t* list() {
    lv_obj_t* l = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, 240, 200);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < 20; i++) {
      lv_obj_t* r = lv_obj_create(l);
      lv_obj_remove_style_all(r);
      lv_obj_set_size(r, LV_PCT(100), 40);
      lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    pump(2);
    return l;
  }

  // A fast drag upward, lifted while still moving.
  static void fling() {
    g_x = 120;
    g_y = 180;
    g_pressed = true;
    pump(2);
    for (int i = 0; i < 6; i++) {
      g_y -= 20;
      pump();
    }
    g_pressed = false;
    pump();
  }
};

int s_clicks = 0;
int s_long = 0;
void onClick(lv_event_t*) { s_clicks++; }
void onLong(lv_event_t*) { s_long++; }

lv_obj_t* button() {
  lv_obj_t* b = lv_btn_create(lv_scr_act());
  lv_obj_set_pos(b, 20, 20);
  lv_obj_set_size(b, 100, 60);
  lv_obj_add_event_cb(b, onClick, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(b, onLong, LV_EVENT_LONG_PRESSED, nullptr);
  s_clicks = 0;
  s_long = 0;
  pump(2);
  return b;
}

// A finger held 900 ms on the button, all inside one refresh.
void holdCaughtInARefresh() {
  g_queue.start(false);
  g_queue.offer(TouchPoint{true, 50, 40}, 100);
  g_queue.offer(TouchPoint{true, 51, 40}, 130);
  g_queue.offer(TouchPoint{false, 51, 40}, 1000);
}

}  // namespace

TEST_F(Eink, LvglTurnsBothOnForEveryObject) {
  // Why a hook is needed at all: nothing the GUI builds clears them.
  lv_obj_t* l = list();
  EXPECT_TRUE(lv_obj_has_flag(l, LV_OBJ_FLAG_SCROLL_MOMENTUM));
  EXPECT_TRUE(lv_obj_has_flag(l, LV_OBJ_FLAG_SCROLL_ELASTIC));
}

TEST_F(Eink, TheObjectADragScrollsLosesBoth) {
  lv_obj_t* l = list();
  fling();
  EXPECT_FALSE(lv_obj_has_flag(l, LV_OBJ_FLAG_SCROLL_MOMENTUM));
  EXPECT_FALSE(lv_obj_has_flag(l, LV_OBJ_FLAG_SCROLL_ELASTIC));
}

TEST_F(Eink, AFlingStopsWhenTheFingerLifts) {
  lv_obj_t* l = list();
  fling();
  const lv_coord_t at_lift = lv_obj_get_scroll_y(l);
  EXPECT_GT(at_lift, 0);
  pump(20);
  EXPECT_EQ(at_lift, lv_obj_get_scroll_y(l));
}

TEST_F(Eink, WithoutTheHookTheSameFlingKeepsGoing) {
  hook(false);
  lv_obj_t* l = list();
  fling();
  const lv_coord_t at_lift = lv_obj_get_scroll_y(l);
  pump(20);
  EXPECT_GT(lv_obj_get_scroll_y(l), at_lift);
}

TEST_F(Eink, ADragPastTheTopDoesNotStretch) {
  lv_obj_t* l = list();
  EXPECT_EQ(0, dragPastTheTop(l));
  EXPECT_EQ(0, lv_obj_get_scroll_y(l));
}

TEST_F(Eink, WithoutTheHookTheSameDragStretches) {
  hook(false);
  lv_obj_t* l = list();
  EXPECT_LT(dragPastTheTop(l), 0);
}

TEST_F(Eink, ATapCaughtInARefreshIsAClick) {
  button();
  g_queue.start(false);
  g_queue.offer(TouchPoint{true, 50, 40}, 1);
  g_queue.offer(TouchPoint{false, 50, 40}, 2);
  pump();   // the one pass after the refresh
  EXPECT_EQ(1, s_clicks);
  EXPECT_TRUE(g_queue.empty());
}

TEST_F(Eink, WithoutTheQueueThatTapIsLost) {
  // The old path: after the refresh the finger is already up, so the live
  // read says released and nothing ever pressed.
  button();
  pump(3);
  EXPECT_EQ(0, s_clicks);
}

TEST_F(Eink, TwoTapsCaughtInOneRefreshAreTwoClicks) {
  button();
  g_queue.start(false);
  g_queue.offer(TouchPoint{true, 50, 40}, 1);
  g_queue.offer(TouchPoint{false, 50, 40}, 2);
  g_queue.offer(TouchPoint{true, 60, 50}, 3);
  g_queue.offer(TouchPoint{false, 60, 50}, 4);
  pump();
  EXPECT_EQ(2, s_clicks);
}

TEST_F(Eink, AHoldCaughtInARefreshIsALongPress) {
  // Holding a failed DM resends it; holding the signal bars opens LoRa.
  // The same hold done live, for what LVGL makes of it: one long press, and
  // the click LVGL 8.3 sends on any release that didn't scroll.
  button();
  g_x = 50;
  g_y = 40;
  g_pressed = true;
  pump(23);   // 920 ms
  g_pressed = false;
  pump(2);
  const int live_long = s_long, live_clicks = s_clicks;
  ASSERT_EQ(1, live_long);

  lv_obj_clean(lv_scr_act());
  button();
  holdCaughtInARefresh();
  pump(16);   // 640 ms of passes after the refresh
  EXPECT_EQ(live_long, s_long);
  EXPECT_EQ(live_clicks, s_clicks);
  EXPECT_TRUE(g_queue.empty());
}

TEST_F(Eink, TakenAllAtOnceThatHoldIsOnlyAClick) {
  g_raw_drain = true;
  button();
  holdCaughtInARefresh();
  pump(16);
  EXPECT_EQ(0, s_long);
  EXPECT_EQ(1, s_clicks);
}

TEST_F(Eink, ATapCaughtInARefreshIsNotALongPress) {
  button();
  g_queue.start(false);
  g_queue.offer(TouchPoint{true, 50, 40}, 100);
  g_queue.offer(TouchPoint{false, 50, 40}, 180);
  pump(16);
  EXPECT_EQ(0, s_long);
  EXPECT_EQ(1, s_clicks);
}

// A button as a row of a list taller than the screen, at y 160-200.
static lv_obj_t* buttonInAList(lv_obj_t** list_out) {
  lv_obj_t* l = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(l);
  lv_obj_set_size(l, 240, 200);
  lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
  for (int i = 0; i < 20; i++) {
    lv_obj_t* r = i == 4 ? lv_btn_create(l) : lv_obj_create(l);
    if (i != 4) {
      lv_obj_remove_style_all(r);
      lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    } else {
      lv_obj_add_event_cb(r, onClick, LV_EVENT_CLICKED, nullptr);
    }
    lv_obj_set_size(r, LV_PCT(100), 40);
  }
  s_clicks = 0;
  pump(2);
  *list_out = l;
  return l;
}

TEST_F(Eink, ADragCaughtInARefreshScrollsAsTheSameDragLive) {
  // Live: down on the button, up the list, lifted. It scrolls, no click.
  lv_obj_t* l = nullptr;
  buttonInAList(&l);
  g_x = 120;
  g_y = 180;
  g_pressed = true;
  pump(2);
  g_y = 30;
  pump();
  g_pressed = false;
  pump(2);
  const lv_coord_t live = lv_obj_get_scroll_y(l);
  EXPECT_GT(live, 0);
  EXPECT_EQ(0, s_clicks);

  // The same drag, caught in a refresh: its press, its newest point, its lift.
  lv_obj_clean(lv_scr_act());
  buttonInAList(&l);
  g_queue.start(false);
  g_queue.offer(TouchPoint{true, 120, 180}, 1);
  g_queue.offer(TouchPoint{true, 120, 100}, 2);
  g_queue.offer(TouchPoint{true, 120, 30}, 3);   // overwrites the point before
  g_queue.offer(TouchPoint{false, 120, 30}, 4);
  pump(3);
  EXPECT_EQ(live, lv_obj_get_scroll_y(l));
  EXPECT_EQ(0, s_clicks);
}

TEST_F(Eink, ADragBegunLiveAndFinishedInARefreshScrollsTheWholeWay) {
  // The B6 bench (#2113): the first frame of a swipe starts the refresh, and
  // the rest of the swipe is read while the panel is busy.
  lv_obj_t* l = list();
  g_x = 120;
  g_y = 180;
  g_pressed = true;
  pump(2);
  for (int i = 0; i < 3; i++) {
    g_y -= 10;
    pump();
  }
  const lv_coord_t before = lv_obj_get_scroll_y(l);
  EXPECT_GT(before, 0);   // the scroll began live
  g_queue.start(true);
  for (int i = 0; i < 10; i++) g_queue.offer(TouchPoint{true, 120, uint16_t(150 - 10 * (i + 1))}, 100 + 30 * i);
  g_queue.offer(TouchPoint{false, 120, 50}, 500);
  g_pressed = false;   // up by the time the refresh ends
  g_y = 50;
  pump(3);
  EXPECT_EQ(before + 100, lv_obj_get_scroll_y(l));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
