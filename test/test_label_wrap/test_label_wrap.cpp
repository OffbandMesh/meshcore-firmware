// #2113: LabelWrap.h against the real LVGL 8.3 with the device's lv_conf.h,
// headless. A chat bubble's text must wrap at the bubble's width cap; set up
// with max_width alone, LVGL draws it as one clipped line.

#include <gtest/gtest.h>

#include <lvgl.h>

#include "helpers/gui/LabelWrap.h"

using offband::gui::wrapAt;

namespace {

void flushCb(lv_disp_drv_t* drv, const lv_area_t*, lv_color_t*) { lv_disp_flush_ready(drv); }

// The Public message the bench showed cut short.
const char* kLong = "For anyone awake, here's a glass of water. You probably need it, and sleep. Night mesh";
const char* kShort = "Night mesh";
constexpr lv_coord_t kMaxW = 160;

class Wrap : public ::testing::Test {
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

  void SetUp() override { lv_obj_clean(lv_scr_act()); }

  // A bubble as Screens.cpp builds one: a content-sized box holding the label.
  static lv_obj_t* bubbleLabel(const char* text) {
    lv_obj_t* bub = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(bub);
    lv_obj_set_size(bub, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_t* l = lv_label_create(bub);
    lv_label_set_text(l, text);
    return l;
  }

  static lv_coord_t lineHeight(lv_obj_t* l) {
    return lv_font_get_line_height(lv_obj_get_style_text_font(l, LV_PART_MAIN));
  }
};

}  // namespace

TEST_F(Wrap, MaxWidthAloneDrawsALongMessageOnOneClippedLine) {
  // The cause, as the bubble used to be set up.
  lv_obj_t* l = bubbleLabel(kLong);
  lv_obj_set_style_max_width(l, kMaxW, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_update_layout(lv_scr_act());
  EXPECT_EQ(kMaxW, lv_obj_get_width(l));
  EXPECT_EQ(lineHeight(l), lv_obj_get_height(l));   // one line: the rest is cut off
}

TEST_F(Wrap, ALongMessageWrapsAtTheCap) {
  lv_obj_t* l = bubbleLabel(kLong);
  wrapAt(l, kMaxW);
  lv_obj_update_layout(lv_scr_act());
  EXPECT_EQ(kMaxW, lv_obj_get_width(l));
  EXPECT_GE(lv_obj_get_height(l), 3 * lineHeight(l));
  EXPECT_EQ(kMaxW, lv_obj_get_width(lv_obj_get_parent(l)));   // the bubble follows its text
}

TEST_F(Wrap, AShortMessageKeepsItsOwnWidth) {
  lv_obj_t* l = bubbleLabel(kShort);
  wrapAt(l, kMaxW);
  lv_obj_update_layout(lv_scr_act());
  EXPECT_LT(lv_obj_get_width(l), kMaxW);
  EXPECT_EQ(lineHeight(l), lv_obj_get_height(l));
}

TEST_F(Wrap, AMessageExactlyAtTheCapStaysOneLine) {
  lv_obj_t* l = bubbleLabel(kShort);
  lv_point_t sz;
  lv_txt_get_size(&sz, kShort, lv_obj_get_style_text_font(l, LV_PART_MAIN), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  wrapAt(l, sz.x);
  lv_obj_update_layout(lv_scr_act());
  EXPECT_EQ(lineHeight(l), lv_obj_get_height(l));
}

TEST_F(Wrap, ANullLabelIsIgnored) { wrapAt(nullptr, kMaxW); }

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
