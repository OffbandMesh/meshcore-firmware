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

// With an AUX button USR clicks aren't grouped into double and triple:
// AUX does "previous", and AUX held 3 s the buzzer (#1534).
#ifdef PIN_USER_BTN_2
constexpr bool kUsrClicksAtOnce = true;
#else
constexpr bool kUsrClicksAtOnce = false;
#endif

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

  _shell.begin(themeTokens(_theme, kDefaultAccent), _look, this);
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
    } else if (kUsrClicksAtOnce || _screen == ScreenState::Off || _shell.locked() || _shell.stepping()) {
      // A dark or locked screen acts on the click itself, without waiting
      // out the multi-click window. So does a screen where USR steps, and
      // every screen on a board with AUX (owner, 2026-10-17, #1534): fast
      // clicks are fast steps, never a double ("previous", AUX's job) or a
      // triple (the buzzer, AUX held 3 s there).
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
    _prefs_dirty = true;   // saved when idle, like every GUI pref (Epic D gate)
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
  const char* node = _prefs ? _prefs->node_name : "";
  displayText(node, strlen(node), d->name, sizeof d->name);
  keySummary(the_mesh.self_id.pub_key, d->key, sizeof d->key);

  HomeCell* cell = d->cells;
  formatFreq(_prefs ? _prefs->freq : 0, cell[0].value, sizeof cell[0].value);
  radioSub(_prefs ? _prefs->sf : 0, _prefs ? _prefs->bw : 0, _prefs ? _prefs->cr : 0, cell[0].sub,
           sizeof cell[0].sub);

  // Heard: contacts with a packet since boot, by type.
  MetricsHub& m = the_mesh.guiMetrics();
  uint16_t rpt = 0, room = 0, chat = 0;
  ContactInfo ci;
  // The iterator skips the anonymous-request slots at the front of the
  // table; getContactByIdx doesn't, so a 0..getNumContacts() walk read those
  // and missed the newest contacts (#1550).
  ContactsIterator it = the_mesh.startContactsIterator();
  while (it.hasNext(&the_mesh, ci)) {
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
    char raw[sizeof d->items[i].text];
    lockLabel(*th, raw, sizeof d->items[i].label);
    displayText(raw, strlen(raw), d->items[i].label, sizeof d->items[i].label);
    formatClock(zone, msg->time, d->items[i].time, sizeof d->items[i].time);
    lockText(*msg, ThreadKind(th->kind), raw, sizeof raw);
    displayText(raw, strlen(raw), d->items[i].text, sizeof d->items[i].text);
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

const MsgStore& UITask::store() { return the_mesh.guiStore(); }

int UITask::channelSlot(int thread) {
  const Thread* th = the_mesh.guiStore().threadAt(thread);
  if (!th || ThreadKind(th->kind) != ThreadKind::Channel) return -1;
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (the_mesh.getChannel(i, ch) && memcmp(ch.channel.secret, th->key, sizeof ch.channel.secret) == 0) return i;
  }
  return -1;
}

// Channels have a per-channel mode (GuiPrefs, B6); DMs and rooms always alert.
NotifyMode UITask::threadMode(int thread) {
  const int slot = channelSlot(thread);
  return (slot >= 0 && _prefs) ? _prefs->gui.channelMode(uint8_t(slot)) : NotifyMode::All;
}

// The bell's sheet (D6): the channel's mode, kept in the GUI prefs (B6).
void UITask::setThreadMode(int thread, NotifyMode m) {
  const int slot = channelSlot(thread);
  if (slot < 0 || !_prefs || _prefs->gui.channelMode(uint8_t(slot)) == m) return;
  _prefs->gui.setChannelMode(uint8_t(slot), m);
  _prefs_dirty = true;   // saved when idle (#1472's rule for internal flash)
  mesh_log_line(MLOG_BOOT, "[gui] channel %d notify %u\n", slot, unsigned(m));
}

void UITask::threadOpened(int thread) {
  MsgStore& store = the_mesh.guiStore();
  const Thread* th = store.threadAt(thread);
  if (!th || th->unread == 0) return;
  if (ThreadKind(th->kind) != ThreadKind::Channel) the_mesh.guiMetrics().contacts.markRead(th->key);
  store.markRead(thread);
  the_mesh.guiStoreFlash().indexChanged(millis());   // written when idle (#1472)
}

// The conversation header's route line: a channel shows how many repeats of
// your last message were heard (plan §9 #13); a DM or room the contact's path.
void UITask::routeMeta(int thread, char* out, size_t n) {
  const MsgStore& store = the_mesh.guiStore();
  const Thread* th = store.threadAt(thread);
  if (!th || !n) {
    if (n) out[0] = '\0';
    return;
  }
  if (ThreadKind(th->kind) == ThreadKind::Channel) {
    uint32_t seqs[16];
    const size_t k = store.threadMessages(thread, seqs, 16);
    for (size_t i = k; i > 0; i--) {
      const MsgRecord* m = store.msg(seqs[i - 1]);
      if (m && m->own) {
        if (m->echoes) {
          snprintf(out, n, "flood · %u", unsigned(m->echoes));
          return;
        }
        break;
      }
    }
    snprintf(out, n, "flood");
    return;
  }
  const ContactInfo* ci = the_mesh.lookupContactByPubKey(th->key, PUB_KEY_SIZE);
  if (!ci || ci->out_path_len == OUT_PATH_UNKNOWN) {
    snprintf(out, n, "flood");
    return;
  }
  const unsigned hops = ci->out_path_len & 63;
  if (hops == 0) snprintf(out, n, "direct");
  else snprintf(out, n, "direct · %u hop%s", hops, hops == 1 ? "" : "s");
}

// ------------------------------------------------------------- Compose (D5)

// A channel message goes out as "<name>: <text>" inside the 160-byte text
// payload, so the name and ": " come off the top (as compose::budget).
size_t UITask::composeBudget(int thread) {
  const Thread* th = the_mesh.guiStore().threadAt(thread);
  if (!th || ThreadKind(th->kind) != ThreadKind::Channel) return kMsgTextMax;
  const size_t name = _prefs ? strlen(_prefs->node_name) : 0;
  return name + 2 < kMsgTextMax ? kMsgTextMax - name - 2 : 0;
}

const char* UITask::cannedText(int i) {
  return (_prefs && i >= 0 && i < int(kCannedCount)) ? _prefs->gui.canned[i] : "";
}

bool UITask::sendDirect(int thread, const char* text, uint32_t timestamp, uint8_t attempt, uint32_t* timeout_ms) {
  const Thread* th = the_mesh.guiStore().threadAt(thread);
  if (!th) return false;
  ContactInfo* to = the_mesh.lookupContactByPubKey(th->key, PUB_KEY_SIZE);
  if (!to) return false;
  bool flood = false;
  uint32_t ack = 0, est = 0;
  const CmdResult r = the_mesh.cmdSendDirect(*to, timestamp, attempt, text, false, &flood, &ack, &est);
  *timeout_ms = est ? est : 10000;
  return r == CmdResult::Ok;
}

bool UITask::sendText(int thread, const char* text, size_t len) {
  MsgStore& store = the_mesh.guiStore();
  const Thread* th = store.threadAt(thread);
  if (!th || len == 0 || len > kMsgTextMax) return false;
  char buf[kMsgTextMax + 1];
  memcpy(buf, text, len);
  buf[len] = '\0';
  const uint32_t now = rtc_clock.getCurrentTime();

  if (ThreadKind(th->kind) == ThreadKind::Channel) {
    const int slot = channelSlot(thread);
    const bool ok = slot >= 0 && the_mesh.cmdSendChannel(uint8_t(slot), now, buf, len) == CmdResult::Ok;
    mesh_log_line(MLOG_BOOT, "[gui] channel send %s\n", ok ? "ok" : "failed");
    return ok;
  }
  // Every device DM is tracked to delivered or failed, so with no free slot
  // it isn't sent (Compose says "Not sent") rather than left "sending…".
  DmRetry* slot = nullptr;
  for (DmRetry& r : _dm) {
    if (!r.active()) {
      slot = &r;
      break;
    }
  }
  uint32_t timeout = 0;
  if (!slot || !sendDirect(thread, buf, now, 0, &timeout)) {
    mesh_log_line(MLOG_BOOT, "[gui] dm send failed%s\n", slot ? "" : " (8 awaiting ACK)");
    return false;
  }
  // The store kept it as Sending (#1464); watch for its ACK.
  const uint32_t seq = store.findOwn(thread, buf, len);
  if (seq) slot->start(seq, millis(), timeout);
  mesh_log_line(MLOG_BOOT, "[gui] dm send ok, ack timeout %lums\n", (unsigned long)timeout);
  return true;
}

uint8_t UITask::dmAttempt(uint32_t seq) {
  for (const DmRetry& r : _dm) {
    if (r.active() && r.seq() == seq) return r.attempt();
  }
  return 0;
}

// Held on a DM that wasn't delivered: a fresh round of tries, sent as a
// resend (same timestamp) so a recipient that did get it sees a repeat.
void UITask::resend(uint32_t seq) {
  MsgStore& store = the_mesh.guiStore();
  const MsgRecord* m = store.msg(seq);
  if (!m || !m->own || Delivery(m->delivery) != Delivery::Failed || dmAttempt(seq)) return;
  char buf[kMsgTextMax + 1];
  memcpy(buf, m->text, m->text_len);
  buf[m->text_len] = '\0';
  uint32_t timeout = 0;
  if (!sendDirect(m->thread, buf, m->time, 1, &timeout)) return;
  for (DmRetry& r : _dm) {
    if (!r.active()) {
      r.start(seq, millis(), timeout);
      break;
    }
  }
  mesh_log_line(MLOG_BOOT, "[gui] dm resend\n");
}

void UITask::pollDmRetries(uint32_t now) {
  MsgStore& store = the_mesh.guiStore();
  for (DmRetry& r : _dm) {
    if (!r.active()) continue;
    const uint32_t seq = r.seq();   // poll() clears it on Fail
    const MsgRecord* m = store.msg(seq);
    const bool delivered = !m || Delivery(m->delivery) == Delivery::Delivered;   // gone counts as done
    switch (r.poll(now, delivered)) {
      case DmRetry::Step::Resend: {
        char buf[kMsgTextMax + 1];
        memcpy(buf, m->text, m->text_len);
        buf[m->text_len] = '\0';
        uint32_t timeout = 10000;
        sendDirect(m->thread, buf, m->time, r.attempt(), &timeout);
        r.resent(now, timeout);
        mesh_log_line(MLOG_BOOT, "[gui] dm retry %u/%u\n", unsigned(r.attempt()), unsigned(kDmAttempts));
        break;
      }
      case DmRetry::Step::Fail:
        store.setDelivery(seq, Delivery::Failed);
        the_mesh.guiStoreFlash().ringChanged(millis());
        mesh_log_line(MLOG_BOOT, "[gui] dm not delivered\n");
        break;
      default:
        break;
    }
  }
}

// ------------------------------------------------------------- Nodes (E)

// Every contact as a list row. Last heard is the contact's advert time on
// our clock (ContactInfo::lastmod, kept across reboots); SNR and unread come
// from the GUI's per-contact stats, which start empty at boot.
const NodeRow* UITask::nodeRows(size_t* total) {
  *total = 0;
  if (!_node_rows || !_node_keys) {
    // Both or neither: a half-made pair is freed so the next call tries again.
    if (!_node_rows) _node_rows = static_cast<NodeRow*>(heap_caps_malloc(sizeof(NodeRow) * MAX_CONTACTS, MALLOC_CAP_SPIRAM));
    if (!_node_keys) _node_keys = static_cast<uint8_t(*)[32]>(heap_caps_malloc(32 * MAX_CONTACTS, MALLOC_CAP_SPIRAM));
    if (!_node_rows || !_node_keys) {
      heap_caps_free(_node_rows);
      heap_caps_free(_node_keys);
      _node_rows = nullptr;
      _node_keys = nullptr;
      return nullptr;
    }
  }
  const uint32_t now = rtc_clock.getCurrentTime();
  const bool clock_ok = offband::tz::clockSet(now);
  ContactInfo ci;
  size_t n = 0;
  // Every contact, as the phone's list walks them: past the anonymous-request
  // slots (see homeData, #1550).
  ContactsIterator it = the_mesh.startContactsIterator();
  while (n < MAX_CONTACTS && it.hasNext(&the_mesh, ci)) {
    if (ci.type == ADV_TYPE_NONE) continue;   // an empty slot
    fillNodeRow(ci, _node_rows[n], _node_keys[n], now, clock_ok);
    n++;
  }
  *total = n;
  return _node_rows;
}

void UITask::fillNodeRow(const ContactInfo& ci, NodeRow& r, uint8_t* key_copy, uint32_t now, bool clock_ok) {
  memcpy(key_copy, ci.id.pub_key, 32);
  snprintf(r.name, sizeof r.name, "%s", ci.name);
  r.key = key_copy;
  r.kind = nodeKind(ci.type);
  r.out_path_len = ci.out_path_len;
  // A clock stepped back (a GPS or phone sync) reads as "now", not "never".
  r.heard = clock_ok && ci.lastmod != 0;
  r.heard_age_s = (r.heard && now >= ci.lastmod) ? now - ci.lastmod : 0;
  const ContactStat* st = the_mesh.guiMetrics().contacts.find(ci.id.pub_key);
  r.snr_known = st && st->heard;
  r.snr_db = r.snr_known ? st->snr_db : 0;
  // Unread from the store, as Chats counts it (it survives a reboot).
  const MsgStore& store = the_mesh.guiStore();
  const int t = store.find(ci.type == ADV_TYPE_ROOM ? ThreadKind::Room : ThreadKind::Direct, ci.id.pub_key,
                           PUB_KEY_SIZE);
  const Thread* th = store.threadAt(t);
  r.unread = th ? th->unread : 0;
}

// ------------------------------------------------------- Node detail (E4)

namespace {

// A hop's hash as the hash size gives it: a hop's bytes in out_path, which
// are the first bytes of that repeater's key. The trace flags carry the size
// as a power of two; a 3-byte hash can't be traced.
int traceSizeCode(uint8_t hash_size) {
  return hash_size == 1 ? 0 : hash_size == 2 ? 1 : hash_size == 4 ? 2 : -1;
}

// "Hilltop-2" for a hop whose hash matches a contact, else its hash in hex.
void hopName(const uint8_t* hash, uint8_t size, char* out, size_t n) {
  ContactInfo* c = the_mesh.lookupContactByPubKey(hash, size);
  if (c && c->name[0]) {
    snprintf(out, n, "%s", c->name);
    return;
  }
  size_t o = 0;
  for (uint8_t i = 0; i < size && o + 2 < n; i++) o += snprintf(out + o, n - o, "%02x", hash[i]);
}

}  // namespace

bool UITask::nodeDetail(const uint8_t* key, NodeDetail* out) {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(key, PUB_KEY_SIZE);
  if (!ci) return false;
  const uint32_t now = rtc_clock.getCurrentTime();
  fillNodeRow(*ci, out->row, out->key, now, offband::tz::clockSet(now));
  const ContactStat* st = the_mesh.guiMetrics().contacts.find(ci->id.pub_key);
  out->rssi = st && st->heard ? st->rssi : 0;
  out->path[0] = '\0';
  out->can_trace = false;
  if (ci->out_path_len == OUT_PATH_UNKNOWN) return true;   // flood: no route yet

  // "path: You › Hilltop-2 › Wren": each hop, then the node.
  const uint8_t hops = ci->out_path_len & 63, size = uint8_t((ci->out_path_len >> 6) + 1);
  textf(out->path, sizeof out->path, "path: You");
  char name[32];
  for (uint8_t h = 0; h < hops && size_t(h + 1) * size <= MAX_PATH_SIZE; h++) {
    hopName(&ci->out_path[h * size], size, name, sizeof name);
    const size_t o = strlen(out->path);
    textf(out->path + o, sizeof out->path - o, " › %s", name);   // cut whole characters at the end
  }
  const size_t o = strlen(out->path);
  textf(out->path + o, sizeof out->path - o, " › %s", ci->name);
  // Trace: out along the route and back. A repeater turns it around itself;
  // anything else, at the last repeater before it.
  const bool turns = ci->type == ADV_TYPE_REPEATER;
  const size_t legs = size_t(hops) * 2 + (turns ? 1 : 0) - (turns || hops == 0 ? 0 : 1);
  out->can_trace = traceSizeCode(size) >= 0 && (hops > 0 || turns) && legs * size <= MAX_PATH_SIZE;
  return true;
}

void UITask::detailOpened(const uint8_t* key) {
  memcpy(_act_key, key, PUB_KEY_SIZE);
  for (NodeActRun& a : _acts) a = NodeActRun{};
  // A reply meant for an earlier detail mustn't land on this one.
  Reply r;
  ReplyBox& box = the_mesh.guiReplies();
  box.take(ReplyKind::Login, &r);
  box.take(ReplyKind::Status, &r);
  box.take(ReplyKind::Trace, &r);
}

bool UITask::startNodeAct(const uint8_t* key, NodeAct a, const char* password) {
  if (a == NodeAct::Count) return false;
  if (memcmp(key, _act_key, PUB_KEY_SIZE) != 0) detailOpened(key);
  NodeActRun& run = _acts[size_t(a)];
  if (run.view.state == ActView::Busy) return false;
  // Ping and Login share the mesh's one request slot (#1454): one at a time.
  if ((a == NodeAct::Ping && _acts[size_t(NodeAct::Login)].view.state == ActView::Busy) ||
      (a == NodeAct::Login && _acts[size_t(NodeAct::Ping)].view.state == ActView::Busy)) {
    return false;
  }
  ContactInfo* ci = the_mesh.lookupContactByPubKey(key, PUB_KEY_SIZE);
  if (!ci) return false;
  const uint32_t now = millis();
  bool flood = false;
  uint32_t tag = 0, est = 0;
  CmdResult res = CmdResult::Failed;
  Reply stale;
  switch (a) {
    case NodeAct::Ping:
      the_mesh.guiReplies().take(ReplyKind::Status, &stale);
      res = the_mesh.cmdStatusReq(*ci, &flood, &tag, &est);
      break;
    case NodeAct::Login:
      the_mesh.guiReplies().take(ReplyKind::Login, &stale);
      // An empty field tries the guest password (mockup 5).
      snprintf(_login_pw, sizeof _login_pw, "%s", password ? password : "");
      res = the_mesh.cmdLogin(*ci, _login_pw, &flood, &tag, &est);
      break;
    case NodeAct::Trace: {
      NodeDetail d;
      if (!nodeDetail(key, &d) || !d.can_trace) return false;
      const uint8_t hops = ci->out_path_len & 63, size = uint8_t((ci->out_path_len >> 6) + 1);
      uint8_t path[MAX_PATH_SIZE];
      size_t len = 0;
      for (uint8_t h = 0; h < hops; h++, len += size) memcpy(&path[len], &ci->out_path[h * size], size);
      if (ci->type == ADV_TYPE_REPEATER) {
        memcpy(&path[len], ci->id.pub_key, size);
        len += size;
      }
      const int back_from = ci->type == ADV_TYPE_REPEATER ? int(hops) - 1 : int(hops) - 2;
      for (int h = back_from; h >= 0; h--, len += size) memcpy(&path[len], &ci->out_path[h * size], size);
      tag = esp_random();
      the_mesh.guiReplies().take(ReplyKind::Trace, &stale);
      res = the_mesh.cmdTrace(tag, 0, uint8_t(traceSizeCode(size)), path, uint8_t(len), &est);
      break;
    }
    case NodeAct::Share:
      res = the_mesh.cmdShareContact(*ci);
      run.view = ActView{};
      run.view.state = res == CmdResult::Ok ? ActView::Ok : ActView::Failed;
      run.view.done_ms = now;
      mesh_log_line(MLOG_BOOT, "[gui] node share: %s\n", res == CmdResult::Ok ? "sent" : "not sent");
      return res == CmdResult::Ok;
    default:
      return false;
  }
  if (res != CmdResult::Ok) {
    mesh_log_line(MLOG_BOOT, "[gui] node act %u not sent (%u)\n", unsigned(a), unsigned(res));
    return false;
  }
  run.view = ActView{};
  run.view.state = ActView::Busy;
  run.started = now;
  run.deadline = now + (est ? est : 10000) + 2000;   // the send's own estimate, and a margin
  run.tag = tag;
  mesh_log_line(MLOG_BOOT, "[gui] node act %u sent (%s, %lums)\n", unsigned(a), flood ? "flood" : "direct",
                (unsigned long)est);
  return true;
}

void UITask::nodeActState(NodeAct a, ActView* out) {
  *out = a < NodeAct::Count ? _acts[size_t(a)].view : ActView{};
}

const char* UITask::savedPassword(const uint8_t* key) {
  loadPasswords();
  return _passwords.get(key);
}

int UITask::openDm(const uint8_t* key) {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(key, PUB_KEY_SIZE);
  return ci ? the_mesh.guiThreadForContact(*ci) : -1;
}

// Replies for the open detail's actions, and their timeouts.
void UITask::pollNodeActs(uint32_t now) {
  ReplyBox& box = the_mesh.guiReplies();
  Reply r;
  auto finish = [&](NodeActRun& run, bool ok, const Reply* rep) {
    run.view.state = ok ? ActView::Ok : ActView::Failed;
    run.view.done_ms = now;
    if (rep) {
      run.view.rtt_ms = rep->at_ms - run.started;
      run.view.len = rep->len < sizeof run.view.body ? rep->len : uint8_t(sizeof run.view.body);
      memcpy(run.view.body, rep->data, run.view.len);
    }
  };

  NodeActRun& ping = _acts[size_t(NodeAct::Ping)];
  if (ping.view.state == ActView::Busy) {
    if (box.take(ReplyKind::Status, &r) && memcmp(r.peer, _act_key, 4) == 0) {
      finish(ping, true, &r);
      mesh_log_line(MLOG_BOOT, "[gui] ping reply %lums\n", (unsigned long)ping.view.rtt_ms);
    } else if (int32_t(now - ping.deadline) >= 0) {
      finish(ping, false, nullptr);
      mesh_log_line(MLOG_BOOT, "[gui] ping: no reply\n");
    }
  }

  NodeActRun& login = _acts[size_t(NodeAct::Login)];
  if (login.view.state == ActView::Busy) {
    if (box.take(ReplyKind::Login, &r) && memcmp(r.peer, _act_key, 4) == 0) {
      finish(login, r.ok, &r);
      login.view.login_refused = !r.ok;
      if (r.ok) {   // remembered for next time (owner pick a)
        loadPasswords();
        _passwords.set(_act_key, _login_pw);
        _passwords_dirty = true;
      }
      mesh_log_line(MLOG_BOOT, "[gui] login %s\n", r.ok ? "ok" : "refused");
    } else if (int32_t(now - login.deadline) >= 0) {
      finish(login, false, nullptr);
      mesh_log_line(MLOG_BOOT, "[gui] login: no reply\n");
    }
  }

  NodeActRun& trace = _acts[size_t(NodeAct::Trace)];
  if (trace.view.state == ActView::Busy) {
    if (box.take(ReplyKind::Trace, &r) && r.tag == trace.tag) {
      finish(trace, true, &r);
      mesh_log_line(MLOG_BOOT, "[gui] trace reply %lums\n", (unsigned long)trace.view.rtt_ms);
    } else if (int32_t(now - trace.deadline) >= 0) {
      finish(trace, false, nullptr);
      mesh_log_line(MLOG_BOOT, "[gui] trace: no reply\n");
    }
  }
}

namespace {
const char* const kPasswordPath = "/gui_pw";
const char* const kPasswordTmp = "/gui_pw.tmp";

FILESYSTEM* guiFs() {
  DataStore* ds = the_mesh.guiDataStore();
  if (!ds) return nullptr;
  return ds->getSecondaryFS() ? ds->getSecondaryFS() : ds->getPrimaryFS();
}

class PwFileSource : public ByteSource {
public:
  explicit PwFileSource(File& f) : _f(f) {}
  size_t read(void* data, size_t len) override { return _f.read(static_cast<uint8_t*>(data), len); }

private:
  File& _f;
};
class PwFileSink : public ByteSink {
public:
  explicit PwFileSink(File& f) : _f(f) {}
  bool write(const void* data, size_t len) override {
    return _f.write(static_cast<const uint8_t*>(data), len) == len;
  }

private:
  File& _f;
};
}  // namespace

// Read once, on first use: the book is small, and nothing else writes it.
void UITask::loadPasswords() {
  if (_passwords_loaded) return;
  _passwords_loaded = true;
  FILESYSTEM* fs = guiFs();
  if (!fs) return;
  // Cut off between the remove and the rename: the temp file is the book.
  if (!fs->exists(kPasswordPath) && fs->exists(kPasswordTmp)) fs->rename(kPasswordTmp, kPasswordPath);
  if (!fs->exists(kPasswordPath)) return;
  File f = the_mesh.guiDataStore()->openRead(fs, kPasswordPath);
  if (!f) return;
  PwFileSource in(f);
  const LoadResult res = _passwords.load(in);
  f.close();
  mesh_log_line(MLOG_BOOT, "[gui] passwords: %u (load %u)\n", unsigned(_passwords.size()), unsigned(res));
}

// Written whole to a temp file, then swapped in, so a power cut mid-write
// leaves the old book (or the new one), never a cut-short file.
void UITask::savePasswords() {
  FILESYSTEM* fs = guiFs();
  if (!fs) return;
  File f = the_mesh.guiDataStore()->openWriteFile(fs, kPasswordTmp);
  if (!f) return;
  PwFileSink out(f);
  const bool ok = _passwords.save(out);
  f.close();
  if (ok) {
    fs->remove(kPasswordPath);
    fs->rename(kPasswordTmp, kPasswordPath);
  } else {
    fs->remove(kPasswordTmp);
  }
  mesh_log_line(MLOG_BOOT, "[gui] passwords saved: %s\n", ok ? "ok" : "failed");
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
  const bool idle = _screen != ScreenState::On || idleMs(now) >= GUI_DIM_AFTER_MS;
  the_mesh.setGuiScreenIdle(idle);
  if (idle && _prefs_dirty) {   // a channel's notify mode (D6)
    _prefs_dirty = false;
    the_mesh.savePrefs();
  }
  if (idle && _passwords_dirty) {   // a login that worked (E4)
    _passwords_dirty = false;
    savePasswords();
  }

  // DMs sent from here retry and fail on their own, screen lit or not; so
  // do a node detail's Ping, Trace and login.
  pollDmRetries(now);
  pollNodeActs(now);

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
      static LockData d;   // off the stack; the GUI is single-threaded
      lockData(&d);
      GUI_SPAN(s_t_data);
      _shell.updateLock(d);
      GUI_SPAN(s_t_update);
    } else if (_shell.tab() == Tab::Home) {
      static HomeData d;
      homeData(&d);
      GUI_SPAN(s_t_data);
      _shell.updateHome(d);
      GUI_SPAN(s_t_update);
    }
  }
  // Chats redraw only when the store's revision moved, which is a compare on
  // every other pass.
  _shell.updateChats();
  _shell.updateNodes(now);   // once a second while the Nodes tab is up
  _shell.updateDetail(now);  // a node detail's actions as they progress
  GUI_SPAN(s_t_update);
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
