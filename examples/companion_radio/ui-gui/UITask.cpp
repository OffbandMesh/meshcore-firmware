#include "UITask.h"

#include <Arduino.h>
#include <MeshLog.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <helpers/gui/hw/GuiPanel.h>
#include <helpers/gui/hw/LvglPort.h>

#include "../MyMesh.h"

extern MyMesh the_mesh;

namespace {

// Test-screen colors: the Offband theme's bg, ink and signal tokens. Epic A6
// replaces these with the theme tables.
constexpr uint32_t kBg = 0x0F1412;
constexpr uint32_t kInk = 0xEFF3E8;
constexpr uint32_t kDim = 0x7C8B82;
constexpr uint32_t kSig = 0x7BEFA8;

lv_obj_t* s_name = nullptr;
lv_obj_t* s_pin = nullptr;
lv_obj_t* s_unread = nullptr;

}  // namespace

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _prefs = node_prefs;

  GuiPanel* panel = static_cast<GuiPanel*>(display);
  _gui_ok = panel && offband::gui::lvglBegin(*panel);
  mesh_log_line(MLOG_BOOT, "[gui] lvgl %s %dx%d bufs=%uB int free=%u largest=%u\n",
                _gui_ok ? "up" : "FAILED", display ? display->width() : 0,
                display ? display->height() : 0, (unsigned)offband::gui::lvglDrawBufBytes(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  if (!_gui_ok) return;

  buildTestScreen();
  refreshTestScreen();
}

void UITask::buildTestScreen() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);

  lv_obj_t* brand = lv_label_create(scr);
  lv_obj_set_style_text_font(brand, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(brand, lv_color_hex(kSig), 0);
  lv_label_set_text(brand, "offband");
  lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 40);

  s_name = lv_label_create(scr);
  lv_obj_set_style_text_color(s_name, lv_color_hex(kInk), 0);
  lv_obj_align(s_name, LV_ALIGN_TOP_MID, 0, 90);

  s_pin = lv_label_create(scr);
  lv_obj_set_style_text_font(s_pin, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(s_pin, lv_color_hex(kInk), 0);
  lv_obj_align(s_pin, LV_ALIGN_CENTER, 0, 0);

  s_unread = lv_label_create(scr);
  lv_obj_set_style_text_color(s_unread, lv_color_hex(kDim), 0);
  lv_obj_align(s_unread, LV_ALIGN_CENTER, 0, 40);

  lv_obj_t* note = lv_label_create(scr);
  lv_obj_set_style_text_color(note, lv_color_hex(kDim), 0);
  lv_label_set_text(note, "GUI foundation test screen");
  lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -20);
}

void UITask::refreshTestScreen() {
  if (!_gui_ok) return;
  lv_label_set_text(s_name, _prefs ? _prefs->node_name : "");

  uint32_t pin = the_mesh.getBLEPin();
  if (pin != _shown_pin) {
    _shown_pin = pin;
    if (pin) lv_label_set_text_fmt(s_pin, "BLE PIN %06lu", (unsigned long)pin);
    else lv_label_set_text(s_pin, "BLE PIN off");
  }
  lv_label_set_text_fmt(s_unread, "%s  -  unread %d", hasConnection() ? "phone connected" : "no phone",
                        _unread);
}

void UITask::msgRead(int msgcount) { _unread = msgcount; }

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  (void)path_len;
  (void)from_name;
  (void)text;
  _unread = msgcount;
}

void UITask::notify(UIEventType t) { (void)t; }

void UITask::loop() {
  if (!_gui_ok) return;
  if (millis() >= _next_refresh) {
    _next_refresh = millis() + 1000;
    refreshTestScreen();
  }
  offband::gui::lvglLoop();
}
