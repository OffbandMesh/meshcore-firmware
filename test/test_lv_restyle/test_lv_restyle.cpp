// #2110: LvRestyle.h against the real LVGL 8.3 with the device's lv_conf.h,
// headless. LVGL redraws an object on every style set, same value or not;
// the helpers set only a new value, so a timer that restyles with nothing
// changed costs e-paper no refresh.

#include <gtest/gtest.h>

#include <lvgl.h>

#include "helpers/gui/LvRestyle.h"

using namespace offband::gui;

namespace {

void flushCb(lv_disp_drv_t* drv, const lv_area_t*, lv_color_t*) { lv_disp_flush_ready(drv); }

class Restyle : public ::testing::Test {
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
  }

  void SetUp() override {
    lv_obj_clean(lv_scr_act());
    o = lv_obj_create(lv_scr_act());
    lv_obj_set_size(o, 40, 20);
    settle();
  }

  // Draws whatever is pending, so the next check sees only new invalidation.
  static void settle() { lv_refr_now(nullptr); }
  static bool redrawPending() { return lv_disp_get_default()->inv_p > 0; }

  lv_obj_t* o = nullptr;
};

}  // namespace

TEST_F(Restyle, LvglRedrawsOnASetOfTheSameColor) {
  // The cause: nothing changed, and LVGL still marks the object for redraw.
  lv_obj_set_style_bg_color(o, lv_color_hex(0x123456), 0);
  settle();
  lv_obj_set_style_bg_color(o, lv_color_hex(0x123456), 0);
  EXPECT_TRUE(redrawPending());
}

TEST_F(Restyle, TheSameBgColorIsNotSetAgain) {
  EXPECT_TRUE(setBgColorIfNew(o, lv_color_hex(0x123456)));
  settle();
  EXPECT_FALSE(setBgColorIfNew(o, lv_color_hex(0x123456)));
  EXPECT_FALSE(redrawPending());
}

TEST_F(Restyle, ANewBgColorIsSetAndRedrawn) {
  setBgColorIfNew(o, lv_color_hex(0x123456));
  settle();
  EXPECT_TRUE(setBgColorIfNew(o, lv_color_hex(0x654321)));
  EXPECT_TRUE(redrawPending());
  EXPECT_EQ(lv_color_to32(lv_color_hex(0x654321)), lv_color_to32(lv_obj_get_style_bg_color(o, LV_PART_MAIN)));
}

TEST_F(Restyle, AFirstSetAlwaysHappens) {
  // No local value yet, even if the theme draws that color already.
  const lv_color_t drawn = lv_obj_get_style_bg_color(o, LV_PART_MAIN);
  EXPECT_TRUE(setBgColorIfNew(o, drawn));
  lv_style_value_t v;
  EXPECT_EQ(LV_STYLE_RES_FOUND, lv_obj_get_local_style_prop(o, LV_STYLE_BG_COLOR, &v, 0));
}

TEST_F(Restyle, AStateStyleDoesNotHideANeededSet) {
  // Focused, the object draws a local focused color. The default-state value
  // is still unset, so setting the default to that same color must happen.
  lv_obj_set_style_bg_color(o, lv_color_hex(0x00FF00), LV_STATE_FOCUSED);
  lv_obj_add_state(o, LV_STATE_FOCUSED);
  ASSERT_EQ(lv_color_to32(lv_color_hex(0x00FF00)), lv_color_to32(lv_obj_get_style_bg_color(o, LV_PART_MAIN)));
  EXPECT_TRUE(setBgColorIfNew(o, lv_color_hex(0x00FF00)));
  lv_obj_clear_state(o, LV_STATE_FOCUSED);
  EXPECT_EQ(lv_color_to32(lv_color_hex(0x00FF00)), lv_color_to32(lv_obj_get_style_bg_color(o, LV_PART_MAIN)));
}

TEST_F(Restyle, OpacityBorderAndTextFollowTheSameRule) {
  lv_obj_t* l = lv_label_create(o);
  setBgOpaIfNew(o, LV_OPA_COVER);
  setBorderColorIfNew(o, lv_color_hex(0x111111));
  setTextColorIfNew(l, lv_color_hex(0x222222));
  settle();
  EXPECT_FALSE(setBgOpaIfNew(o, LV_OPA_COVER));
  EXPECT_FALSE(setBorderColorIfNew(o, lv_color_hex(0x111111)));
  EXPECT_FALSE(setTextColorIfNew(l, lv_color_hex(0x222222)));
  EXPECT_FALSE(redrawPending());
  EXPECT_TRUE(setBgOpaIfNew(o, LV_OPA_TRANSP));
  EXPECT_TRUE(setBorderColorIfNew(o, lv_color_hex(0x333333)));
  EXPECT_TRUE(setTextColorIfNew(l, lv_color_hex(0x444444)));
  EXPECT_TRUE(redrawPending());
}

TEST_F(Restyle, ANullObjectIsIgnored) {
  EXPECT_FALSE(setBgColorIfNew(nullptr, lv_color_hex(0x123456)));
}

// #2113: the thread's "Newest" is shown or hidden after every scroll.
TEST_F(Restyle, LvglRedrawsOnShowingAnObjectAlreadyShown) {
  lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  EXPECT_TRUE(redrawPending());
}

TEST_F(Restyle, HidingWhatIsHiddenDoesNothing) {
  EXPECT_TRUE(setHiddenIfNew(o, true));
  settle();
  EXPECT_FALSE(setHiddenIfNew(o, true));
  EXPECT_FALSE(redrawPending());
  EXPECT_TRUE(lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN));
}

TEST_F(Restyle, ShowingWhatIsShownDoesNothing) {
  EXPECT_FALSE(setHiddenIfNew(o, false));
  EXPECT_FALSE(redrawPending());
}

TEST_F(Restyle, ShowingWhatIsHiddenShowsIt) {
  setHiddenIfNew(o, true);
  settle();
  EXPECT_TRUE(setHiddenIfNew(o, false));
  EXPECT_FALSE(lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN));
  EXPECT_TRUE(redrawPending());
  EXPECT_FALSE(setHiddenIfNew(nullptr, true));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
