#include "UITask.h"

#include <Arduino.h>
#include <MeshLog.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <helpers/TimeZones.h>
#include <helpers/gui/ClockPolicy.h>
#include <helpers/gui/GuiViews.h>
#include <helpers/gui/SignalLevel.h>
#include <helpers/gui/hw/Chsc6xTouch.h>
#include <helpers/gui/hw/GuiFonts.h>
#include <helpers/gui/hw/GuiPanel.h>
#include <helpers/gui/hw/LvglPort.h>
#include <helpers/ui/ScreenOff.h>

#include "../MyMesh.h"
#include "target.h"

extern MyMesh the_mesh;

// The battery scale's ends, as in ui-new: empty here, full from the learned or
// pinned prefs value (#1254) or this.
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif

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

// The Accent theme's signal color until Display settings let the user pick
// one (the prototype's default, ObScreen.dc.html:330).
constexpr uint32_t kDefaultAccent = 0xF2C14E;

const char* themeName(ThemeId id) {
  switch (id) {
    case ThemeId::Offband: return "Offband";
    case ThemeId::Steel: return "Steel";
    case ThemeId::MonoHC: return "Mono HC";
    case ThemeId::Accent: return "Accent";
  }
  return "?";
}

ThemeId nextTheme(ThemeId id) {
  switch (id) {
    case ThemeId::Offband: return ThemeId::Steel;
    case ThemeId::Steel: return ThemeId::MonoHC;
    case ThemeId::MonoHC: return ThemeId::Accent;
    case ThemeId::Accent: break;
  }
  return ThemeId::Offband;
}

ThemeId prevTheme(ThemeId id) {
  switch (id) {
    case ThemeId::Offband: return ThemeId::Accent;
    case ThemeId::Steel: return ThemeId::Offband;
    case ThemeId::MonoHC: return ThemeId::Steel;
    case ThemeId::Accent: break;
  }
  return ThemeId::MonoHC;
}

lv_color_t c(uint32_t rgb) { return lv_color_hex(rgb & 0xFFFFFF); }
lv_opa_t alpha(uint32_t argb) { return lv_opa_t(argb >> 24); }

lv_obj_t* s_name = nullptr;
lv_obj_t* s_pin = nullptr;
lv_obj_t* s_unread = nullptr;
lv_obj_t* s_touch_label = nullptr;
lv_obj_t* s_action_label = nullptr;
Chsc6xTouch s_touch;

const char* actionName(Action a) {
  switch (a) {
    case Action::Next: return "next";
    case Action::Prev: return "prev";
    case Action::Open: return "open";
    case Action::Back: return "back";
    case Action::Canned: return "canned";
    case Action::Wake: return "wake";
    case Action::PocketLock: return "pocket lock";
    case Action::ToggleBuzzer: return "buzzer";
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

  // USR and AUX both pull to ground when pressed.
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#ifdef PIN_USER_BTN_2
  pinMode(PIN_USER_BTN_2, INPUT_PULLUP);
#endif

#ifdef PIN_BUZZER
  _buzzer.begin();
  _buzzer.quiet(_prefs ? _prefs->buzzer_quiet : false);
  _buzzer.startup();
#endif

  _started_at = millis();
  _last_input = _started_at;
  setCpuFrequencyMhz(cpuMhzFor(true));
  mesh_log_line(MLOG_BOOT, "[gui] screen on cpu=%u\n", (unsigned)getCpuFrequencyMhz());

  buildTestScreen();
  refreshTestScreen();
  lv_refr_now(nullptr);
  panel->setBrightness(100);
}

// A test screen built only from the theme tables, the Look and the font
// table, so the bench sees every token and font role on the real panel.
void UITask::buildTestScreen() {
  const ThemeTokens t = themeTokens(_theme, kDefaultAccent);
  const LookTokens look = lookTokens(_look);

  lv_obj_t* scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, c(t.bg), 0);
  _status.build(scr, t, [](void* ctx) { static_cast<UITask*>(ctx)->openLoraPopover(); }, this);
  _next_status = 0;

  lv_obj_t* brand = lv_label_create(scr);
  lv_obj_set_style_text_font(brand, font(FontRole::Title), 0);
  lv_obj_set_style_text_color(brand, c(t.sig), 0);
  lv_label_set_text(brand, "offband");
  lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 28);

  lv_obj_t* theme_label = lv_label_create(scr);
  lv_obj_set_style_text_font(theme_label, font(FontRole::Label), 0);
  lv_obj_set_style_text_color(theme_label, c(t.dim), 0);
  lv_label_set_text_fmt(theme_label, "THEME %s  /  LOOK %s", themeName(_theme),
                        _look == Look::Modern ? "MODERN" : "CLASSIC");
  lv_obj_align(theme_label, LV_ALIGN_TOP_MID, 0, 56);

  s_name = lv_label_create(scr);
  lv_obj_set_style_text_font(s_name, font(FontRole::Body), 0);
  lv_obj_set_style_text_color(s_name, c(t.ink), 0);
  lv_obj_align(s_name, LV_ALIGN_TOP_MID, 0, 80);

  // The PIN sits on a card: bg3 fill, line border, the Look's radius.
  lv_obj_t* card = lv_obj_create(scr);
  lv_obj_set_size(card, 200, 52);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, -20);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, c(t.flat ? t.bg : t.bg3), 0);
  lv_obj_set_style_border_color(card, c(t.line), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, look.radius, 0);
  lv_obj_set_style_pad_all(card, 4, 0);

  s_pin = lv_label_create(card);
  lv_obj_set_style_text_font(s_pin, font(FontRole::Data), 0);
  lv_obj_set_style_text_color(s_pin, c(t.ink), 0);
  lv_obj_center(s_pin);
  _shown_pin = 0;  // force the label to refill

  // An "own message" sample (ObScreen.dc.html:379): Modern is a solid signal
  // fill with sigInk text in every theme; Classic is a signal outline over
  // the wash.
  lv_obj_t* bubble = lv_obj_create(scr);
  lv_obj_set_size(bubble, 140, 30);
  lv_obj_align(bubble, LV_ALIGN_CENTER, 30, 26);
  lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(bubble, look.bubble_radius, 0);
  if (look.own_msg_outline) {
    lv_obj_set_style_bg_color(bubble, c(t.wash), 0);
    lv_obj_set_style_bg_opa(bubble, alpha(t.wash), 0);
    lv_obj_set_style_border_color(bubble, c(t.sig), 0);
    lv_obj_set_style_border_width(bubble, 1, 0);
  } else {
    lv_obj_set_style_bg_color(bubble, c(t.sig), 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
  }
  lv_obj_t* bubble_text = lv_label_create(bubble);
  lv_obj_set_style_text_font(bubble_text, font(FontRole::BodySmall), 0);
  lv_obj_set_style_text_color(bubble_text, c(look.own_msg_outline ? t.ink : t.sigInk), 0);
  lv_label_set_text(bubble_text, "Copy that");
  lv_obj_center(bubble_text);

  s_unread = lv_label_create(scr);
  lv_obj_set_style_text_font(s_unread, font(FontRole::Meta), 0);
  lv_obj_set_style_text_color(s_unread, c(t.dim), 0);
  lv_obj_align(s_unread, LV_ALIGN_CENTER, 0, 60);

  s_touch_label = lv_label_create(scr);
  lv_obj_set_style_text_font(s_touch_label, font(FontRole::Meta), 0);
  lv_obj_set_style_text_color(s_touch_label, c(t.dim), 0);
  lv_obj_align(s_touch_label, LV_ALIGN_CENTER, 0, 80);

  s_action_label = lv_label_create(scr);
  lv_obj_set_style_text_font(s_action_label, font(FontRole::Meta), 0);
  lv_obj_set_style_text_color(s_action_label, c(t.dim), 0);
  lv_obj_align(s_action_label, LV_ALIGN_CENTER, 0, 100);

  lv_obj_t* note = lv_label_create(scr);
  lv_obj_set_style_text_font(note, font(FontRole::BodySmall), 0);
  lv_obj_set_style_text_color(note, c(t.dim), 0);
  lv_label_set_text(note, "USR: theme  hold: look  3x: buzzer");
  lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -16);
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

  lv_label_set_text_fmt(s_action_label, "last: %s  -  buzzer %s  -  %u MHz", actionName(_last_action),
                        buzzerQuiet() ? "off" : "on", (unsigned)getCpuFrequencyMhz());
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

static InputEvent clicksEvent(uint8_t n) {
  return n >= 3 ? InputEvent::UsrTripleClick : n == 2 ? InputEvent::UsrDoubleClick : InputEvent::UsrClick;
}

void UITask::pollButtons(uint32_t now) {
  PressKind kind;
  if (_usr.update(digitalRead(PIN_USER_BTN) == LOW, now, &kind)) {
    if (kind != PressKind::Press) {
      // Clicks just before this hold go first, in the order they happened.
      if (uint8_t n = _usr_clicks.take()) handle(clicksEvent(n), now);
      handle(kind == PressKind::Hold ? InputEvent::UsrHold : InputEvent::UsrLongHold, now);
    } else if (_screen == ScreenState::Off) {
      // A dark screen wakes on the click itself, without waiting out the
      // multi-click window.
      handle(InputEvent::UsrClick, now);
    } else {
      _usr_clicks.click(now);
    }
  }
  if (uint8_t n = _usr_clicks.poll(now, _usr.isDown())) handle(clicksEvent(n), now);
#ifdef PIN_USER_BTN_2
  if (_aux.update(digitalRead(PIN_USER_BTN_2) == LOW, now, &kind)) {
    handle(kind == PressKind::Press  ? InputEvent::AuxPress
           : kind == PressKind::Hold ? InputEvent::AuxHold
                                     : InputEvent::AuxLongHold,
           now);
  }
#endif
}

void UITask::handle(InputEvent ev, uint32_t now) {
  const Action a = mapInput(ev, _screen != ScreenState::Off, _pocket_locked);
  if (a == Action::None) return;
  _last_input = now;
  _last_action = a;
  if (a == Action::PocketLock) {
    _pocket_locked = true;
    applyScreen(ScreenState::Off);
    return;
  }
  _pocket_locked = false;
  applyScreen(ScreenState::On);

  if (a == Action::Back && _status.popoverOpen()) {
    _status.closePopover();
    return;
  }

  // On the test screen, Next and Prev cycle the theme and Open the Look so
  // the bench can see each. From Epic D these actions drive real screens.
  switch (a) {
    case Action::Open:
      // As on stock MeshCore: a USR long press in the first 8 s after boot
      // enters CLI rescue.
      if (ev == InputEvent::UsrHold && now - _started_at < 8000) {
        the_mesh.enterCLIRescue();
        return;
      }
      _look = _look == Look::Modern ? Look::Classic : Look::Modern;
      break;
    case Action::Next:
      _theme = nextTheme(_theme);
      break;
    case Action::Prev:
      _theme = prevTheme(_theme);
      break;
    case Action::ToggleBuzzer:
      toggleBuzzer();
      break;
    default:
      return;
  }
  buildTestScreen();
  refreshTestScreen();
}

bool UITask::buzzerQuiet() {
#ifdef PIN_BUZZER
  return _buzzer.isQuiet();
#else
  return true;
#endif
}

void UITask::toggleBuzzer() {
#ifdef PIN_BUZZER
  _buzzer.quiet(!_buzzer.isQuiet());
  if (!_buzzer.isQuiet()) notify(UIEventType::ack);
  if (_prefs) {
    _prefs->buzzer_quiet = _buzzer.isQuiet();
    the_mesh.savePrefs();
  }
#endif
}

void UITask::applyBuzzerMute(bool quiet) {
#ifdef PIN_BUZZER
  _buzzer.quiet(quiet);
#else
  (void)quiet;
#endif
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

bool UITask::gpsEnabled() const {
  if (!_sensors) return false;
  const int num = _sensors->getNumSettings();
  for (int i = 0; i < num; i++) {
    if (strcmp(_sensors->getSettingName(i), "gps") == 0) return strcmp(_sensors->getSettingValue(i), "1") == 0;
  }
  return false;   // no GPS setting: no module
}

StatusReadings UITask::statusReadings(uint32_t now) {
  StatusReadings r{};
  formatClock(_prefs ? _prefs->ui_tz : 0, rtc_clock.getCurrentTime(), r.clock, sizeof r.clock);

  MsgStore& store = the_mesh.guiStore();
  r.unread = store.ready() ? store.totalUnread() : uint32_t(_unread > 0 ? _unread : 0);

  const SnrMedian& snr = the_mesh.guiMetrics().snr;
  r.lora_heard = !snr.empty();
  r.bars = r.lora_heard ? signalBars(snr.median(), _prefs ? _prefs->sf : 12) : 0;

  r.ble = bleIcon(isBluetoothEnabled(), hasConnection());

  // Time without a fix counts from when GPS was turned on, so enabling it
  // late doesn't start it red.
  const bool gps_on = gpsEnabled();
  if (gps_on && !_gps_was_on) _gps_on_at = now;
  _gps_was_on = gps_on;
  const FixTracker& fix = the_mesh.guiFix();
  const uint32_t since_on = (now - _gps_on_at) / 1000;
  uint32_t without = since_on;
  if (fix.everFixed() && fix.secsSinceLost(now) < since_on) without = fix.secsSinceLost(now);
  r.gps = gpsIcon(gps_on, the_mesh.guiFixType(), without);

  const uint16_t full = (_prefs && _prefs->batt_full_mv) ? _prefs->batt_full_mv : uint16_t(BATT_MAX_MILLIVOLTS);
  const uint16_t mv = _batt.seeded() ? _batt.value() : getBattMilliVolts();
  r.batt_pct = uint8_t(offband::batteryPercent(mv, BATT_MIN_MILLIVOLTS, full));
  return r;
}

void UITask::openLoraPopover() {
  const SnrMedian& snr = the_mesh.guiMetrics().snr;
  LoraReadings r{};
  r.packets = snr.size();
  r.snr_db = snr.median();
  r.rssi = int16_t(radio_driver.getLastRSSI());
  r.noise = int16_t(radio_driver.getNoiseFloor());
  r.sf = _prefs ? _prefs->sf : 12;
  _status.openPopover(r, millis());
  _last_input = millis();
}

void UITask::msgRead(int msgcount) { _unread = msgcount; }

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  (void)path_len;
  (void)from_name;
  (void)text;
  _unread = msgcount;
}

// The stock ui-new sounds, so a GUI companion sounds like any other.
void UITask::notify(UIEventType t) {
#ifdef PIN_BUZZER
  switch (t) {
    case UIEventType::contactMessage:
      _buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
      break;
    case UIEventType::channelMessage:
      _buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
      break;
    case UIEventType::ack:
      _buzzer.play("ack:d=32,o=8,b=120:c");
      break;
    default:
      break;
  }
#else
  (void)t;
#endif
}

void UITask::loop() {
  if (!_gui_ok) return;
  const uint32_t now = millis();

#ifdef OFFBAND_GUI_PERF
  // #1472: the longest gap between two loop passes. Frame time can't see a
  // stall elsewhere in the loop (a flash write), but this does: the screen
  // can't redraw or take input while the loop is held.
  static uint32_t s_last_loop = 0, s_gap_max = 0;
  if (s_last_loop && now - s_last_loop > s_gap_max) s_gap_max = now - s_last_loop;
  s_last_loop = now;
#endif

#ifdef PIN_BUZZER
  if (_buzzer.isPlaying()) _buzzer.loop();
#endif

  pollButtons(now);

  // With the panel off LVGL isn't running, so a touch is read here to wake it
  // (ignored while pocket-locked).
  if (_screen == ScreenState::Off && _touch_ok) {
    TouchPoint p{};
    if (s_touch.read(&p) && p.pressed) handle(InputEvent::TouchTap, now);
  }

  if (!_pocket_locked) {
    const uint32_t off_ms = offband::screenOffMillis(_prefs ? _prefs->ui_screen_secs : 0, GUI_OFF_AFTER_MS);
    applyScreen(screenStateFor(idleMs(now), GUI_DIM_AFTER_MS, off_ms, _always_on));
  }

  // #1472: internal-flash writes stall both cores, so they wait until nobody
  // is using the device: the screen dim or off, or no input for the dim time
  // (which covers "always on", where the screen never dims).
  the_mesh.setGuiScreenIdle(_screen != ScreenState::On || idleMs(now) >= GUI_DIM_AFTER_MS);

  // One battery reading a second into the average (#1246), lit or not.
  if ((int32_t)(now - _next_batt_read) >= 0) {
    _batt.feed(getBattMilliVolts());
    _next_batt_read = now + 1000;
  }

  if (_screen == ScreenState::Off) return;

  // Four passes a second: enough for the GPS seeking blink at 1 Hz.
  if ((int32_t)(now - _next_status) >= 0) {
    _next_status = now + 250;
    _status.update(statusReadings(now), now);
    _status.tick(now);
  }

  if (now >= _next_refresh) {
    _next_refresh = now + 1000;
    refreshTestScreen();
  }
  lvglLoop();

#ifdef OFFBAND_GUI_PERF
  // Diag builds: refresh timing for the frame-rate check (#1441). A theme
  // switch on the test screen is a full-screen redraw, the worst case.
  static uint32_t s_next_perf = 0;
  if (now >= s_next_perf) {
    s_next_perf = now + 30000;
    uint32_t frames, avg_ms, max_ms;
    lvglTakeFrameStats(&frames, &avg_ms, &max_ms);
    mesh_log_line(MLOG_BOOT, "[gui] frames=%lu avg=%lums max=%lums loop_gap_max=%lums cpu=%u\n",
                  (unsigned long)frames, (unsigned long)avg_ms, (unsigned long)max_ms,
                  (unsigned long)s_gap_max, (unsigned)getCpuFrequencyMhz());
    s_gap_max = 0;
  }
#endif
}
