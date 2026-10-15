#include "UITask.h"

#include <Arduino.h>
#include <MeshLog.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <helpers/gui/ClockPolicy.h>
#include <helpers/gui/hw/Chsc6xTouch.h>
#include <helpers/gui/hw/GuiPanel.h>
#include <helpers/gui/hw/LvglPort.h>
#include <helpers/ui/ScreenOff.h>

#include "../MyMesh.h"

extern MyMesh the_mesh;

using namespace offband::gui;

// Touch axes relative to the panel's portrait orientation. Confirmed on the
// bench with the test screen's touch readout (A7).
#ifndef GUI_TOUCH_SWAP_XY
  #define GUI_TOUCH_SWAP_XY 0
#endif
#ifndef GUI_TOUCH_MIRROR_X
  #define GUI_TOUCH_MIRROR_X 0
#endif
#ifndef GUI_TOUCH_MIRROR_Y
  #define GUI_TOUCH_MIRROR_Y 0
#endif
#ifndef PIN_TOUCH_RST
  #define PIN_TOUCH_RST -1
#endif

// Screen timing until Display settings land (B6, G3): dim after 30 s; off
// after the existing screen-off pref, whose 0 means this GUI default.
#ifndef GUI_DIM_AFTER_MS
  #define GUI_DIM_AFTER_MS 30000
#endif
#ifndef GUI_OFF_AFTER_MS
  #define GUI_OFF_AFTER_MS 120000
#endif
#ifndef GUI_DIM_PERCENT
  #define GUI_DIM_PERCENT 20
#endif

namespace {

// Test-screen colors: the Offband theme's bg, ink, dim and signal tokens.
// A6 replaces these with the theme tables.
constexpr uint32_t kBg = 0x0F1412;
constexpr uint32_t kInk = 0xEFF3E8;
constexpr uint32_t kDim = 0x7C8B82;
constexpr uint32_t kSig = 0x7BEFA8;

lv_obj_t* s_name = nullptr;
lv_obj_t* s_pin = nullptr;
lv_obj_t* s_unread = nullptr;
lv_obj_t* s_touch_label = nullptr;
lv_obj_t* s_action_label = nullptr;
Chsc6xTouch s_touch;

const char* actionName(Action a) {
  switch (a) {
    case Action::NextFocus: return "next";
    case Action::Open: return "open";
    case Action::Back: return "back";
    case Action::Canned: return "canned";
    case Action::Wake: return "wake";
    case Action::Sleep: return "sleep";
    case Action::None: break;
  }
  return "-";
}

const char* screenName(ScreenState s) {
  switch (s) {
    case ScreenState::On: return "on";
    case ScreenState::Dim: return "dim";
    case ScreenState::Off: return "off";
  }
  return "?";
}

}  // namespace

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _prefs = node_prefs;

  GuiPanel* panel = static_cast<GuiPanel*>(display);
  _gui_ok = panel && lvglBegin(*panel);
  mesh_log_line(MLOG_BOOT, "[gui] lvgl %s %dx%d bufs=%uB int free=%u largest=%u\n",
                _gui_ok ? "up" : "FAILED", display ? display->width() : 0,
                display ? display->height() : 0, (unsigned)lvglDrawBufBytes(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  if (!_gui_ok) return;

#if defined(PIN_TOUCH_SDA) && defined(PIN_TOUCH_SCL)
  _touch_ok = s_touch.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, PIN_TOUCH_RST);
  if (_touch_ok) {
    TouchMapping m = {GUI_TOUCH_SWAP_XY != 0, GUI_TOUCH_MIRROR_X != 0, GUI_TOUCH_MIRROR_Y != 0,
                      uint16_t(display->width()), uint16_t(display->height())};
    lvglAttachTouch(s_touch, m);
  }
  // No answer from the controller: the GUI runs on buttons only (PLAN.md §6).
  mesh_log_line(MLOG_BOOT, "[gui] touch %s\n", _touch_ok ? "chsc6x ok" : "none (buttons only)");
#endif

  // Both buttons pull to ground when pressed.
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#ifdef PIN_USER_BTN_2
  pinMode(PIN_USER_BTN_2, INPUT_PULLUP);
#endif

  _last_input = millis();
  setCpuFrequencyMhz(cpuMhzFor(true));
  mesh_log_line(MLOG_BOOT, "[gui] screen on cpu=%u\n", (unsigned)getCpuFrequencyMhz());

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

  s_touch_label = lv_label_create(scr);
  lv_obj_set_style_text_color(s_touch_label, lv_color_hex(kDim), 0);
  lv_obj_align(s_touch_label, LV_ALIGN_CENTER, 0, 70);

  s_action_label = lv_label_create(scr);
  lv_obj_set_style_text_color(s_action_label, lv_color_hex(kDim), 0);
  lv_obj_align(s_action_label, LV_ALIGN_CENTER, 0, 100);

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

  uint16_t tx = 0, ty = 0;
  if (!_touch_ok) lv_label_set_text(s_touch_label, "touch: none (buttons only)");
  else if (lvglLastTouch(&tx, &ty)) lv_label_set_text_fmt(s_touch_label, "touch %u, %u", tx, ty);
  else lv_label_set_text(s_touch_label, "touch: tap anywhere");

  lv_label_set_text_fmt(s_action_label, "last action: %s  -  cpu %u MHz", actionName(_last_action),
                        (unsigned)getCpuFrequencyMhz());
}

// Idle time is the shorter of "since the last button" and LVGL's own "since
// the last touch", so either kind of input keeps the screen lit.
uint32_t UITask::idleMs(uint32_t now) const {
  uint32_t idle = now - _last_input;
  if (_touch_ok && _screen != ScreenState::Off) {
    uint32_t touch_idle = lv_disp_get_inactive_time(nullptr);
    if (touch_idle < idle) idle = touch_idle;
  }
  return idle;
}

void UITask::pollButtons(uint32_t now) {
  PressKind kind;
  if (_prg.update(digitalRead(PIN_USER_BTN) == LOW, now, &kind)) {
    handle(kind == PressKind::Press  ? InputEvent::PrgPress
           : kind == PressKind::Hold ? InputEvent::PrgHold
                                     : InputEvent::PrgLongHold,
           now);
  }
#ifdef PIN_USER_BTN_2
  if (_user.update(digitalRead(PIN_USER_BTN_2) == LOW, now, &kind)) {
    handle(kind == PressKind::Press ? InputEvent::UserPress : InputEvent::UserHold, now);
  }
#endif
}

void UITask::handle(InputEvent ev, uint32_t now) {
  const Action a = mapInput(ev, _screen != ScreenState::Off);
  _last_input = now;
  _last_action = a;
  if (a == Action::Sleep) {
    _manual_off = true;
    applyScreen(ScreenState::Off);
  } else {
    _manual_off = false;
    applyScreen(ScreenState::On);
  }
  // NextFocus / Open / Back / Canned drive the screens from Epic D onward.
}

void UITask::applyScreen(ScreenState s) {
  if (s == _screen) return;
  GuiPanel* panel = static_cast<GuiPanel*>(_display);
  if (s == ScreenState::Off) {
    panel->turnOff();
  } else {
    if (_screen == ScreenState::Off) {
      panel->turnOn();
      lv_obj_invalidate(lv_scr_act());  // redraw what the sleeping panel lost
    }
    panel->setBrightness(s == ScreenState::Dim ? GUI_DIM_PERCENT : 100);
  }
  const bool lit = s != ScreenState::Off;
  if ((_screen == ScreenState::Off) != !lit) setCpuFrequencyMhz(cpuMhzFor(lit));
  _screen = s;
  mesh_log_line(MLOG_BOOT, "[gui] screen %s cpu=%u\n", screenName(s), (unsigned)getCpuFrequencyMhz());
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
  const uint32_t now = millis();

  pollButtons(now);

  // With the panel off LVGL isn't running, so a touch is read here to wake it.
  if (_screen == ScreenState::Off && _touch_ok) {
    TouchPoint p{};
    if (s_touch.read(&p) && p.pressed) handle(InputEvent::TouchTap, now);
  }

  if (!_manual_off) {
    const uint32_t off_ms = offband::screenOffMillis(_prefs ? _prefs->ui_screen_secs : 0, GUI_OFF_AFTER_MS);
    applyScreen(screenStateFor(idleMs(now), GUI_DIM_AFTER_MS, off_ms, _always_on));
  }

  if (_screen == ScreenState::Off) return;

  if (now >= _next_refresh) {
    _next_refresh = now + 1000;
    refreshTestScreen();
  }
  lvglLoop();
}
