#include "UITask.h"

#include <Arduino.h>
#include <MeshLog.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <helpers/AdvertDataHelpers.h>
#include <helpers/TimeZones.h>
#include <helpers/gui/ClockPolicy.h>
#include <helpers/gui/GuiViews.h>
#include <helpers/gui/SignalLevel.h>
#include <helpers/gui/hw/Chsc6xTouch.h>
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
// bench with the Epic A test screen's touch readout (A7).
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

const char* screenName(ScreenState s) {
  switch (s) {
    case ScreenState::On: return "on";
    case ScreenState::Dim: return "dim";
    case ScreenState::Off: return "off";
  }
  return "?";
}

Chsc6xTouch s_touch;

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

  GuiShell::Hooks hooks;
  hooks.send_advert = [](void* ctx) { static_cast<UITask*>(ctx)->sendAdvert(); };
  hooks.lora_popover = [](void* ctx) { static_cast<UITask*>(ctx)->openLoraPopover(); };
  hooks.ctx = this;
  _shell.begin(themeTokens(_theme, kDefaultAccent), _look, hooks);
  _next_status = _next_screen = 0;
  loop();   // fill the first frame before the backlight comes up
  lv_refr_now(nullptr);
  panel->setBrightness(100);
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
    } else if (_screen == ScreenState::Off || _shell.locked()) {
      // A dark or locked screen acts on the click itself, without waiting
      // out the multi-click window.
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
  if (a == Action::PocketLock) {
    _pocket_locked = true;
    applyScreen(ScreenState::Off);
    return;
  }
  _pocket_locked = false;
  applyScreen(ScreenState::On);   // a wake from off shows Lock

  switch (a) {
    case Action::Wake:
      return;
    case Action::ToggleBuzzer:
      toggleBuzzer();
      return;
    case Action::Open:
      // As on stock MeshCore: a USR long press in the first 8 s after boot
      // enters CLI rescue.
      if (ev == InputEvent::UsrHold && now - _started_at < 8000) {
        the_mesh.enterCLIRescue();
        return;
      }
      break;
    default:
      break;
  }
  _shell.action(a);
  _next_screen = 0;   // show the result now, not at the next 1 s refresh
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
      // A screen that went dark comes back locked (design: Lock).
      _shell.showLock();
      _next_screen = 0;
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

uint8_t UITask::battPercent() const {
  const uint16_t full = (_prefs && _prefs->batt_full_mv) ? _prefs->batt_full_mv : uint16_t(BATT_MAX_MILLIVOLTS);
  const uint16_t mv = _batt.seeded() ? _batt.value() : getBattMilliVolts();
  return uint8_t(offband::batteryPercent(mv, BATT_MIN_MILLIVOLTS, full));
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

  r.batt_pct = battPercent();
  return r;
}

void UITask::homeData(HomeData* d) {
  memset(d, 0, sizeof *d);
  snprintf(d->name, sizeof d->name, "%s", _prefs ? _prefs->node_name : "");
  keySummary(the_mesh.self_id.pub_key, d->key, sizeof d->key);

  HomeCell* cell = d->cells;
  formatFreq(_prefs ? _prefs->freq : 0, cell[0].value, sizeof cell[0].value);
  radioSub(_prefs ? _prefs->sf : 0, _prefs ? _prefs->bw : 0, _prefs ? _prefs->cr : 0, cell[0].sub,
           sizeof cell[0].sub);

  // Heard: contacts with a packet since boot, by type.
  MetricsHub& m = the_mesh.guiMetrics();
  uint16_t rpt = 0, room = 0, chat = 0;
  ContactInfo ci;
  for (int i = 0; i < the_mesh.getNumContacts(); i++) {
    if (!the_mesh.getContactByIdx(i, ci)) continue;
    const ContactStat* st = m.contacts.find(ci.id.pub_key);
    if (!st || !st->heard) continue;
    if (ci.type == ADV_TYPE_REPEATER) rpt++;
    else if (ci.type == ADV_TYPE_ROOM) room++;
    else if (ci.type == ADV_TYPE_CHAT) chat++;
  }
  snprintf(cell[1].value, sizeof cell[1].value, "%u", unsigned(rpt + room + chat));
  heardSub(rpt, room, chat, cell[1].sub, sizeof cell[1].sub);

  MsgStore& store = the_mesh.guiStore();
  snprintf(cell[2].value, sizeof cell[2].value, "%lu", (unsigned long)(store.ready() ? store.totalUnread() : 0));
  unreadSub(store, cell[2].sub, sizeof cell[2].sub);

  const bool gps_on = gpsEnabled();
  snprintf(cell[3].value, sizeof cell[3].value, "%s", gpsValue(gps_on, the_mesh.guiFixType()));
  uint8_t sats = 0;
  LocationProvider* loc = sensors.getLocationProvider();
  if (gps_on && loc) sats = uint8_t(loc->satellitesCount());
  gpsSub(sats, offband::tz::clockSet(rtc_clock.getCurrentTime()), cell[3].sub, sizeof cell[3].sub);
  if (!gps_on) cell[3].sub[0] = '\0';

  snprintf(cell[4].value, sizeof cell[4].value, "%u%%", unsigned(battPercent()));
  batterySub(_batt.seeded() ? _batt.value() : getBattMilliVolts(), millis() / 1000, cell[4].sub,
             sizeof cell[4].sub);

  formatAirtime(m.airtime.dutyPermille(millis()), cell[5].value, sizeof cell[5].value);
  snprintf(cell[5].sub, sizeof cell[5].sub, "tx, last hour");
}

void UITask::lockData(LockData* d) {
  memset(d, 0, sizeof *d);
  const int zone = _prefs ? _prefs->ui_tz : 0;
  const uint32_t now = rtc_clock.getCurrentTime();
  formatClock(zone, now, d->clock, sizeof d->clock);
  char date[24];
  formatDate(zone, now, date, sizeof date);
  if (date[0]) {
    snprintf(d->sub, sizeof d->sub, "%s · %s · %u%%", date, _prefs ? _prefs->node_name : "", unsigned(battPercent()));
  } else {
    snprintf(d->sub, sizeof d->sub, "%s · %u%%", _prefs ? _prefs->node_name : "", unsigned(battPercent()));
  }

  MsgStore& store = the_mesh.guiStore();
  uint32_t seqs[2];
  d->count = uint8_t(lockItems(store, seqs, 2, &d->more));
  for (uint8_t i = 0; i < d->count; i++) {
    const MsgRecord* msg = store.msg(seqs[i]);
    const Thread* th = msg ? store.threadAt(msg->thread) : nullptr;
    if (!msg || !th) continue;
    lockLabel(*th, d->items[i].label, sizeof d->items[i].label);
    formatClock(zone, msg->time, d->items[i].time, sizeof d->items[i].time);
    lockText(*msg, ThreadKind(th->kind), d->items[i].text, sizeof d->items[i].text);
  }
}

void UITask::openLoraPopover() {
  const SnrMedian& snr = the_mesh.guiMetrics().snr;
  LoraReadings r{};
  r.packets = snr.size();
  r.snr_db = snr.median();
  r.rssi = int16_t(radio_driver.getLastRSSI());
  r.noise = int16_t(radio_driver.getNoiseFloor());
  r.sf = _prefs ? _prefs->sf : 12;
  _shell.status().openPopover(r, millis());
  _last_input = millis();
}

// Home's Send advert: a flood advert, as the phone app's "Advert (flood)".
void UITask::sendAdvert() {
  const uint32_t now = millis();
  if (!_advert.press(now)) return;
  const bool ok = the_mesh.cmdSendAdvert(true) == CmdResult::Ok;
  _advert.result(ok, now);
  _shell.setAdvertState(_advert.state(now));
  mesh_log_line(MLOG_BOOT, "[gui] advert %s\n", ok ? "sent" : "not sent");
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

#ifdef OFFBAND_GUI_PERF
  static uint32_t s_t_input = 0;   // input handling, which includes screen rebuilds
  const uint32_t t_in = millis();
#endif
  pollButtons(now);
#ifdef OFFBAND_GUI_PERF
  if (millis() - t_in > s_t_input) s_t_input = millis() - t_in;
#endif

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

#ifdef OFFBAND_GUI_PERF
  // #1501: where a long loop gap comes from, piece by piece.
  static uint32_t s_t_status = 0, s_t_data = 0, s_t_update = 0, s_t_lvgl = 0;
  uint32_t t0 = millis();
  #define GUI_SPAN(acc) do { const uint32_t t1 = millis(); if (t1 - t0 > acc) acc = t1 - t0; t0 = t1; } while (0)
#else
  #define GUI_SPAN(acc) do {} while (0)
#endif

  // Four passes a second for the status bar; once a second for the screen.
  if ((int32_t)(now - _next_status) >= 0) {
    _next_status = now + 250;
    if (!_shell.locked()) {
      _shell.status().update(statusReadings(now), now);
      _shell.status().tick(now);
    }
    _shell.setAdvertState(_advert.state(now));
    GUI_SPAN(s_t_status);
  }
  if ((int32_t)(now - _next_screen) >= 0) {
    _next_screen = now + 1000;
    if (_shell.locked()) {
      LockData d;
      lockData(&d);
      GUI_SPAN(s_t_data);
      _shell.updateLock(d);
      GUI_SPAN(s_t_update);
    } else if (_shell.tab() == Tab::Home) {
      HomeData d;
      homeData(&d);
      GUI_SPAN(s_t_data);
      _shell.updateHome(d);
      GUI_SPAN(s_t_update);
    }
  }
  lvglLoop();
  GUI_SPAN(s_t_lvgl);
#undef GUI_SPAN

#ifdef OFFBAND_GUI_PERF
  // Diag builds: refresh timing for the frame-rate check (#1441).
  static uint32_t s_next_perf = 0;
  if (now >= s_next_perf) {
    s_next_perf = now + 30000;
    uint32_t frames, avg_ms, max_ms;
    lvglTakeFrameStats(&frames, &avg_ms, &max_ms);
    mesh_log_line(MLOG_BOOT, "[gui] frames=%lu avg=%lums max=%lums loop_gap_max=%lums cpu=%u\n",
                  (unsigned long)frames, (unsigned long)avg_ms, (unsigned long)max_ms,
                  (unsigned long)s_gap_max, (unsigned)getCpuFrequencyMhz());
    mesh_log_line(MLOG_BOOT, "[gui] spans max: input=%lums status=%lums data=%lums update=%lums lvgl=%lums\n",
                  (unsigned long)s_t_input, (unsigned long)s_t_status, (unsigned long)s_t_data,
                  (unsigned long)s_t_update, (unsigned long)s_t_lvgl);
    s_gap_max = s_t_input = s_t_status = s_t_data = s_t_update = s_t_lvgl = 0;
  }
#endif
}
