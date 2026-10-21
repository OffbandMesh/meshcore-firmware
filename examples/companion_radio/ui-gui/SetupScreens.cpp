// Setup (Epic G, #1431): the Settings list, its pages, and the edit sheets.
// Design of record: ObScreen.dc.html (cSetList, cSetSub) and the
// owner-approved gap mockups (epic-g-gap-mockups.html, 2026-10-17).
//
// Buttons as everywhere: USR / AUX move, USR hold opens or edits, AUX hold
// goes back. A sheet takes the buttons while it's up.

#include <string.h>

#include <helpers/TimeZones.h>
#include <helpers/gui/ScrollBody.h>

#include "Screens.h"
#include "ShellWidgets.h"

using namespace offband::gui;
using namespace shell;

namespace {

// kInfo reads as kRo but takes focus, so buttons can scroll a long page.
enum RowKind : uint8_t { kVal, kSel, kNav, kTog, kRo, kInfo };
enum RadioRow : uint8_t { rPreset, rFreq, rBw, rSf, rCr, rTx, rRepeat, rPath, rSave };
enum MsgRow : uint8_t { mOn, mDm, mCh, mDefault, mNode, mAlert, mClear, mJump };
enum DisplayRow : uint8_t { dTheme, dLook, dClock, dZone, dRotation, dBright, dDim, dOff };
enum NodeRowId : uint8_t { nName, nKey, nGps, nPos, nGpsSecs };
enum PrivacyRow : uint8_t { pLoc, pAcks, pBase, pLocTel, pEnv };
enum ContactsRow : uint8_t { cAuto, cChat, cRpt, cRoom, cSens, cHops, cCount };
enum ConnRow : uint8_t { bOn, bPin, bPhone };
enum PowerRow : uint8_t { wNow, wFull, wLearn, wShut, wOff, wTouch };
enum ActRow : uint8_t { aAdvert, aTime, aReboot, aPaths };
enum DebugRow : uint8_t { gLog, gBoots, gReset };
// The auto-add bits (MyMesh AUTO_ADD_*), in the rows' order.
const uint8_t kTypeBit[4] = {0x02, 0x04, 0x08, 0x10};
const char* const kTypeName[4] = {"Companions", "Repeaters", "Room servers", "Sensors"};

const char* const kModeName[] = {"All messages", "Mentions only", "Off"};   // as the notification sheet
const char* const kLookName[] = {"Modern", "Classic"};

const char* const kPageName[] = {"Node",    "Radio",        "Radio stats", "Sensors", "SD card", "Privacy", "Contacts",
                                 "Messages & alerts", "Display", "Connectivity", "Power", "Actions", "Debug",
                                 "About device"};
// The list's icon tiles: the font's emoji nearest the drawn icons.
const char* const kPageIcon[] = {"👤", "📡", "📈", "🌡", "💾", "🛡", "👥", "🔔", "🖥", "📶", "🔋", "⚡", "🔧", "📋"};
// SD card (Epic I, mockup 3): the one action row.
constexpr uint8_t kSdFormat = 0;
// Sensors (Epic M): the Units row's id, past any reading's.
constexpr uint8_t kSensUnits = 200;
constexpr size_t kPages = sizeof(kPageName) / sizeof(kPageName[0]);
static_assert(kPages == size_t(SetPage::Count), "one name per page");

// The design's hints (cSetSub) and the mockups' sheet lines.
const char* const kPresetsUpdated = "Updated Thu, Oct 1";   // the preset table's date (2026-10-01)
const char* const kMustMatch = "Every node on a mesh must match";

const lv_point_t kDown[] = {{0, 0}, {3, 3}, {6, 0}};      // ▾, a picker
const lv_point_t kRight[] = {{1, 1}, {5, 6}, {1, 11}};    // ›, a page

// The pad's keys (mockup 5), then Cancel and Set.
const char* const kPadKeys[13] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "⌫", "±"};
constexpr int kPadCancel = 13, kPadSet = 14;   // ± (12) only on the Position pad
// The Position pad is four across, so its 13 keys fit four rows under the
// status bar (bench: five rows of three ran into it): the slots' keys, in
// order, as 1 2 3 ⌫ / 4 5 6 ± / 7 8 9 . / a wide 0.
const int kPosOrder[13] = {0, 1, 2, 11, 3, 4, 5, 12, 6, 7, 8, 9, 10};

}  // namespace

// ---------------------------------------------------------------- state

void GuiShell::resetSetup() {
  _set_page = SetPage::List;
  _set_focus = SetPage::Node;
  _set_focus_row = 0xFF;
  _set_note[0] = '\0';
  _note_row = 0xFF;
}

void GuiShell::setOpen(SetPage p) {
  if (p < SetPage::Count) _set_focus = p;   // the list row to come back to
  _set_page = p;
  _set_focus_row = 0;
  _set_note[0] = '\0';
  _note_row = 0xFF;
  if (p == SetPage::Radio) _radio_was = _radio_now = _model->radioValues();
  rebuild();
}

// Back from a page: with unsaved radio changes, ask first (mockup 6).
void GuiShell::setBack() {
  char lines[1][48];
  if (_set_page == SetPage::Radio && radioChanges(_radio_was, _radio_now, lines, 0) > 0) {
    openSetSheet(SetSheet::Confirm, 0);
    return;
  }
  _model->savePrefsNow();   // leaving a page keeps what changed on it
  if (_set_page == SetPage::Theme) {   // back to Display, on its Theme row
    _set_page = SetPage::Display;
    _set_focus_row = 0;
  } else {
    _set_page = SetPage::List;
  }
  rebuild();
}

void GuiShell::setFlag(uint8_t& flag) {
  flag = flag ? 0 : 1;
  _model->prefsChanged();
  rebuild();
}

void GuiShell::setNote(const char* text) {
  textf(_set_note, sizeof _set_note, "%s", text);
  _set_note_at = lv_tick_get();
  if (_set_save) lv_label_set_text(lv_obj_get_child(lv_obj_get_child(_set_save, 0), 0), _set_note);
}

void GuiShell::updateSetup(uint32_t now_ms) {
  // Radio stats: live, once a second while open (no sheet on it). Sensors
  // (Epic M): every 5 s, the rate the readings are taken at.
  // SD card refreshes too, so a pulled or returned card shows without leaving.
  const bool live = _set_page == SetPage::Stats || _set_page == SetPage::Sensors || _set_page == SetPage::Sd;
  if (_tab == Tab::Setup && !_locked && live && _ss == SetSheet::None && _sheet == Sheet::None &&
      int32_t(now_ms - _stats_at) >= 0) {
    _stats_at = now_ms + (_set_page == SetPage::Sensors ? 5000 : _set_page == SetPage::Sd ? 2000 : 1000);
    _stats_scroll = _set_body ? lv_obj_get_scroll_y(_set_body) : 0;
    // Keep the row the buttons are on: only a tap records it otherwise, so a
    // refresh would drop focus back to the top (SD card's Format card is the
    // last row). Rows carry their id + 1 as user data.
    if (lv_obj_t* f = lv_group_get_focused(_group)) {
      const uintptr_t ud = uintptr_t(lv_obj_get_user_data(f));
      if (ud) _set_focus_row = uint8_t(ud - 1);
    }
    rebuild();
  }
  if (!_set_note[0] || lv_tick_elaps(_set_note_at) < 2000) return;
  _set_note[0] = '\0';
  if (_note_row != 0xFF) {   // an action's result: its row's hint comes back
    _note_row = 0xFF;
    if (_tab == Tab::Setup && !_locked && (_set_page == SetPage::Actions || _set_page == SetPage::Sd) &&
        _ss == SetSheet::None && _sheet == Sheet::None) {
      rebuild();
    }
    return;
  }
  if (_set_save) lv_label_set_text(lv_obj_get_child(lv_obj_get_child(_set_save, 0), 0), "Save · radio restarts");
}

// ------------------------------------------------------------ building

void GuiShell::buildSetup(lv_obj_t* content) {
  SlowStep slow("setup", int(_set_page));
  _set_body = nullptr;
  if (_set_page != SetPage::Stats && _set_page != SetPage::Sensors && _set_page != SetPage::Sd) {
    _stats_scroll = 0;   // a fresh visit starts at the top
  }
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  if (_set_page == SetPage::List) buildSetupList(content);
  else if (_set_page == SetPage::Radio) buildRadioPage(content);
  else if (_set_page == SetPage::Msg) buildMsgPage(content);
  else if (_set_page == SetPage::Node) buildNodePage(content);
  else if (_set_page == SetPage::Stats) buildStatsPage(content);
  else if (_set_page == SetPage::Sensors) buildSensorsPage(content);
  else if (_set_page == SetPage::Sd) buildSdPage(content);
  else if (_set_page == SetPage::Privacy) buildPrivacyPage(content);
  else if (_set_page == SetPage::Contacts) buildContactsPage(content);
  else if (_set_page == SetPage::Display) buildDisplayPage(content);
  else if (_set_page == SetPage::Theme) buildThemePage(content);
  else if (_set_page == SetPage::Conn) buildConnPage(content);
  else if (_set_page == SetPage::Power) buildPowerPage(content);
  else if (_set_page == SetPage::Actions) buildActionsPage(content);
  else if (_set_page == SetPage::Debug) buildDebugPage(content);
  else buildAboutPage(content);
}

// A page's header: ‹ and its name; a tap goes back.
lv_obj_t* GuiShell::setHeader(lv_obj_t* content, const char* title) {
  lv_obj_t* h = box(content, LV_PCT(100), 32);
  lv_obj_set_style_pad_hor(h, 8, 0);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(h, 1, 0);
  lv_obj_set_style_border_color(h, c(_t.line), 0);
  lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(h, 6, 0);
  lv_obj_t* back = box(h, 10, 20);
  line(back, chevronPoints(), 3, _t.sig);
  label(h, FontRole::Title, _t.ink, title);
  lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      h, [](lv_event_t* e) { static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->setBack(); }); },
      LV_EVENT_CLICKED, this);
  return h;
}

lv_obj_t* GuiShell::setSection(lv_obj_t* parent, const char* h) {
  char caps[32];
  textf(caps, sizeof caps, "%s", h);
  for (char* p = caps; *p; p++) {
    if (*p >= 'a' && *p <= 'z') *p = char(*p - 'a' + 'A');
  }
  lv_obj_t* l = label(parent, FontRole::Label, _t.dim, caps);
  lv_obj_set_style_pad_left(l, 8, 0);
  lv_obj_set_style_pad_top(l, 6, 0);
  lv_obj_set_style_pad_bottom(l, 2, 0);
  return l;
}

// A row as drawn (cSetSub): label and hint, the value, and ▾ (a picker),
// › (a page) or a toggle. A changed, unsaved value shows in amber (mockup 2).
lv_obj_t* GuiShell::setRow(lv_obj_t* parent, uint8_t id, const char* k, const char* hint, const char* v, uint8_t kind,
                           bool on, bool changed, bool off) {
  lv_obj_t* row = box(parent, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(row, _lay.row_h, 0);   // 34, or 28 in class B (ObScreen landscape)
  lv_obj_set_style_pad_hor(row, 8, 0);
  lv_obj_set_style_pad_ver(row, landscape() ? 1 : 3, 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, c(_t.line2), 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, 0);
  if (off) lv_obj_set_style_opa(row, LV_OPA_50, 0);

  lv_obj_t* kk = box(row, 0, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(kk, 1);
  lv_obj_set_flex_flow(kk, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* kl = label(kk, FontRole::Body, _t.ink, k);
  lv_obj_set_width(kl, LV_PCT(100));
  oneLine(kl, FontRole::Body);
  if (hint && hint[0] && _lay.hints) {   // the design hides hints in class B
    lv_obj_t* hl = label(kk, FontRole::Label, _t.dim, hint);
    lv_obj_set_width(hl, LV_PCT(100));
    oneLine(hl, FontRole::Label);
  }
  if (v && v[0]) {
    const uint32_t ink = changed ? _t.warn : _t.ink2;
    label(row, kind == kVal || kind == kRo || kind == kInfo ? FontRole::Meta : FontRole::BodySmall, ink, v);
  }
  if (kind == kSel) {
    lv_obj_t* d = box(row, 7, 4);
    line(d, kDown, 3, _t.dim);
  } else if (kind == kNav) {
    lv_obj_t* d = box(row, 6, 12);
    line(d, kRight, 3, _t.dim);
  } else if (kind == kTog) {
    lv_obj_t* tr = box(row, 28, 16);
    const bool pill = _look.pill_chips;
    lv_obj_set_style_radius(tr, pill ? LV_RADIUS_CIRCLE : 0, 0);
    if (on) {
      lv_obj_set_style_bg_opa(tr, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(tr, c(changed ? _t.warn : _t.sig), 0);
    } else {
      lv_obj_set_style_border_width(tr, 2, 0);
      lv_obj_set_style_border_color(tr, c(changed ? _t.warn : _t.line), 0);
    }
    lv_obj_t* kn = box(tr, on ? 12 : 9, on ? 12 : 9);
    lv_obj_align(kn, on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, on ? -2 : 2, 0);
    lv_obj_set_style_radius(kn, pill ? LV_RADIUS_CIRCLE : 0, 0);
    lv_obj_set_style_bg_opa(kn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(kn, c(on ? _t.sigInk : _t.dim), 0);
  }

  if (kind != kRo && !off) {
    focusable(row);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(uintptr_t(id) + 1));
    lv_obj_add_event_cb(
        row,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_set_focus_row = uint8_t(uintptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))) - 1);
          self->later([](GuiShell* g) { g->setRowPressed(g->_set_focus_row); });
        },
        LV_EVENT_CLICKED, this);
    if (id == _set_focus_row) _focus_row = row;
  }
  return row;
}

// The list (cSetList): twelve categories, icon tile, name, current value.
void GuiShell::buildSetupList(lv_obj_t* content) {
  lv_obj_t* h = box(content, LV_PCT(100), 32);
  lv_obj_set_style_pad_hor(h, 8, 0);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(h, 1, 0);
  lv_obj_set_style_border_color(h, c(_t.line), 0);
  lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(h, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  label(h, FontRole::Title, _t.ink, "Settings");
  lv_obj_t* who = label(h, FontRole::Meta, _t.dim);
  userText(who, _model->nodeName());

  lv_obj_t* list = box(content, LV_PCT(100), 0);
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  char v[48];
  for (size_t i = 0; i < kPages; i++) {
    const SetPage p = SetPage(i);
    if (p == SetPage::Sd && !_model->sdSlot()) continue;   // only boards with a card slot
    lv_obj_t* row = box(list, LV_PCT(100), _lay.cat_row_h);   // 34, or 30 in class B
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, c(_t.line2), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 7, 0);
    lv_obj_t* ic = box(row, 20, 20);
    lv_obj_set_style_radius(ic, _look.radius, 0);
    if (!_t.flat) {
      lv_obj_set_style_bg_opa(ic, lv_opa_t(_t.wash >> 24), 0);
      lv_obj_set_style_bg_color(ic, c(_t.wash), 0);
    }
    lv_obj_center(label(ic, FontRole::BodySmall, p == SetPage::Actions ? _t.warn : _t.sig, kPageIcon[i]));
    label(row, FontRole::Body, _t.ink, kPageName[i]);
    _model->setupSummary(p, v, sizeof v);
    uint32_t value_ink = _t.ink2;
    if (p == SetPage::Sd) {   // a card that's gone or won't mount reads red (mockup 2)
      SdView sd;
      _model->sdView(&sd);
      bool alarm = false;
      sdListValue(sd.state, sd.usage_ok, sd.free_bytes, v, sizeof v, &alarm);
      if (alarm) value_ink = _t.bad;
    }
    lv_obj_t* vl = label(row, FontRole::BodySmall, value_ink);
    userText(vl, v);
    lv_obj_set_flex_grow(vl, 1);
    lv_obj_set_style_text_align(vl, LV_TEXT_ALIGN_RIGHT, 0);
    oneLine(vl, FontRole::BodySmall);
    lv_obj_t* d = box(row, 6, 12);
    line(d, kRight, 3, _t.dim);
    focusable(row);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(uintptr_t(i) + 1));
    lv_obj_add_event_cb(
        row,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_set_focus = SetPage(uintptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))) - 1);
          self->later([](GuiShell* g) { g->setOpen(g->_set_focus); });
        },
        LV_EVENT_CLICKED, this);
    if (p == _set_focus) _focus_row = row;
  }
}

// Radio (cSetSub, mockup 2): LoRa and Mesh, and Save pinned where the tabs
// were. Values change here; Save applies them.
void GuiShell::buildRadioPage(lv_obj_t* content) {
  setHeader(content, "Radio");
  lv_obj_t* body = box(content, LV_PCT(100), 0);
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);

  const RadioValues& w = _radio_was;
  const RadioValues& n = _radio_now;
  char v[32], hint[24];
  setSection(body, "LoRa");
  const int preset = presetFor(n, _preset_hint);
  setRow(body, rPreset, "Preset", kPresetsUpdated, preset >= 0 ? radioPreset(size_t(preset)).title : "Custom", kSel,
         false, presetFor(w, _preset_hint) != preset, false);
  freqText(n.freq_khz, v, sizeof v);
  setRow(body, rFreq, "Frequency", "150.0–2500.0 MHz", v, kVal, false, w.freq_khz != n.freq_khz, false);
  bwText(n.bw_hz, v, sizeof v);
  setRow(body, rBw, "Bandwidth", nullptr, v, kSel, false, w.bw_hz != n.bw_hz, false);
  sfText(n.sf, v, sizeof v);
  setRow(body, rSf, "Spreading factor", nullptr, v, kSel, false, w.sf != n.sf, false);
  crText(n.cr, v, sizeof v);
  setRow(body, rCr, "Coding rate", nullptr, v, kSel, false, w.cr != n.cr, false);
  dbmText(n.tx_dbm, v, sizeof v);
  textf(hint, sizeof hint, "0–%d dBm", int(_model->txMax()));
  setRow(body, rTx, "TX power", hint, v, kVal, false, w.tx_dbm != n.tx_dbm, false);
  setSection(body, "Mesh");
  setRow(body, rRepeat, "Off-grid repeat", "Repeat mesh packets for others", nullptr, kTog, n.repeat,
         w.repeat != n.repeat, false);
  pathHashText(n.path_hash_mode, v, sizeof v);
  setRow(body, rPath, "Path hash mode", "Needs v1.14+ across the mesh", v, kSel, false,
         w.path_hash_mode != n.path_hash_mode, false);

  // Save, pinned (mockup 2): in the focus order after the rows.
  _set_save = box(content, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(_set_save, 6, 0);
  lv_obj_set_style_pad_ver(_set_save, 4, 0);
  lv_obj_set_style_border_side(_set_save, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(_set_save, 1, 0);
  lv_obj_set_style_border_color(_set_save, c(_t.line), 0);
  lv_obj_t* btn = box(_set_save, LV_PCT(100), 30);
  lv_obj_set_style_radius(btn, _look.radius, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(btn, c(_t.sig), 0);
  lv_obj_center(label(btn, FontRole::Body, _t.sigInk, _set_note[0] ? _set_note : "Save · radio restarts"));
  focusable(btn);
  lv_obj_set_user_data(btn, reinterpret_cast<void*>(uintptr_t(rSave) + 1));
  lv_obj_add_event_cb(
      btn,
      [](lv_event_t* e) {
        GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
        self->_set_focus_row = rSave;
        self->later([](GuiShell* g) { g->setRowPressed(rSave); });
      },
      LV_EVENT_CLICKED, this);
  if (_set_focus_row == rSave) _focus_row = btn;
}

// A scrolling page body under the header: ScrollBody.h, which the native
// LVGL tests drag (#1671).
static lv_obj_t* pageBody(lv_obj_t* content) { return scrollBody(content); }

// Messages & alerts (cSetSub, mockup 7): every change applies and is kept
// at once. With notifications off, the rows under it grey out.
void GuiShell::buildMsgPage(lv_obj_t* content) {
  setHeader(content, "Messages & alerts");
  lv_obj_t* body = pageBody(content);
  const GuiPrefs& g = _model->guiPrefs();
  const bool on = g.notify_on;
  setSection(body, "Notifications");
  setRow(body, mOn, "Enable notifications", "Messages and adverts", nullptr, kTog, on, false, false);
  setRow(body, mDm, "Direct messages", "New DM from a contact", nullptr, kTog, g.notify_dm, false, !on);
  setRow(body, mCh, "Channel messages", "Per channel: All / Mentions / Off", nullptr, kTog, g.notify_channels,
         false, !on);
  setRow(body, mDefault, "New channels default", nullptr, kModeName[g.notify_default < 3 ? g.notify_default : 0],
         kSel, false, false, false);
  setRow(body, mNode, "New node discovered", "Repeaters or contacts heard", nullptr, kTog, g.notify_new_node, false,
         !on);
  setRow(body, mAlert, "Alert with", "Device only", _model->buzzerOn() ? "Buzzer" : "Silent", kSel, false, false,
         false);
  setSection(body, "Messaging");
  setRow(body, mClear, "Clear path on max retry", "After 3 failed sends", nullptr, kTog, g.clear_path_on_fail, false,
         false);
  setRow(body, mJump, "Jump to oldest unread", "Open chats at first unread", nullptr, kTog, g.jump_to_unread, false,
         false);
}

// Display (cSetSub, mockup 8): Appearance and Screen, applied at once.
void GuiShell::buildDisplayPage(lv_obj_t* content) {
  setHeader(content, "Display");
  lv_obj_t* body = pageBody(content);
  const GuiPrefs& g = _model->guiPrefs();
  char v[24];
  setSection(body, "Appearance");
  setRow(body, dTheme, "Theme", nullptr, themeName(g.theme), kNav, false, false, false);
  setRow(body, dLook, "Look", "Rounded or square", kLookName[g.look & 1], kSel, false, false, false);
  setRow(body, dClock, "Clock", nullptr, g.clock_12h ? "12-hour" : "24-hour", kSel, false, false, false);
  // Epic L (#1610, mockup 1): amber while not set, as the clock is UTC then.
  char hint[48];
  zoneHint(_model->zone(), _model->gpsZone(), _model->gpsLiveFix(), hint, sizeof hint);
  zoneRowText(_model->zone(), v, sizeof v);
  setRow(body, dZone, "Time zone", hint, v, kSel, false, !offband::tz::isSet(_model->zone()), false);
  setRow(body, dRotation, "Rotation", nullptr, rotationText(g.rotation), kSel, false, false, false);   // Epic H, H3
  setSection(body, "Screen");
  pctText(g.brightness, v, sizeof v);
  setRow(body, dBright, "Brightness", nullptr, v, kVal, false, false, false);
  durationText(g.dim_secs, v, sizeof v);
  setRow(body, dDim, "Dim after", nullptr, v, kSel, false, false, false);
  durationText(_model->screenOffSecs(), v, sizeof v);
  setRow(body, dOff, "Screen off", nullptr, v, kSel, false, false, false);
}

// Display › Theme: the four themes as tiles (as drawn on the wide layout),
// and with Accent, its eight colors (owner choice 12B). A pick applies at once.
void GuiShell::buildThemePage(lv_obj_t* content) {
  setHeader(content, "Theme");
  lv_obj_t* body = pageBody(content);
  lv_obj_set_style_pad_all(body, 8, 0);
  lv_obj_set_style_pad_row(body, 8, 0);
  const GuiPrefs& g = _model->guiPrefs();
  lv_obj_t* grid = box(body, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 8, 0);
  lv_obj_set_style_pad_column(grid, 8, 0);
  for (uint8_t i = 0; i < 4; i++) {
    const ThemeTokens tt = themeTokens(ThemeId(i), g.accent);
    const bool on = g.theme == i;
    lv_obj_t* tile = box(grid, (_lay.content_w - 16 - 8) / 2, landscape() ? 52 : 62);
    lv_obj_set_style_radius(tile, _look.radius, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tile, c(tt.bg), 0);
    lv_obj_set_style_border_width(tile, on ? 2 : 1, 0);
    lv_obj_set_style_border_color(tile, c(on ? _t.sig : _t.line), 0);
    lv_obj_set_style_pad_all(tile, 6, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 6, 0);
    label(tile, FontRole::Body, tt.ink, themeName(i));
    lv_obj_t* sw = box(tile, LV_PCT(100), 14);
    lv_obj_set_flex_flow(sw, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(sw, 4, 0);
    for (uint32_t rgb : {tt.sig, tt.ink2, tt.bg3, tt.warn}) {
      lv_obj_t* d = box(sw, 14, 14);
      lv_obj_set_style_radius(d, _look.pill_chips ? LV_RADIUS_CIRCLE : 2, 0);
      lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(d, c(rgb), 0);
      lv_obj_set_style_border_width(d, 1, 0);
      lv_obj_set_style_border_color(d, c(tt.line), 0);
    }
    focusable(tile);
    lv_obj_set_user_data(tile, reinterpret_cast<void*>(uintptr_t(i) + 1));
    lv_obj_add_event_cb(
        tile,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_set_focus_row = uint8_t(uintptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))) - 1);
          self->later([](GuiShell* g) {
            g->_model->guiPrefs().theme = g->_set_focus_row;
            g->_model->prefsChanged();   // redraws in the new theme (a same-theme tap changes nothing)
          });
        },
        LV_EVENT_CLICKED, this);
    if (i == _set_focus_row) _focus_row = tile;
  }
  if (g.theme != uint8_t(ThemeId::Accent)) return;
  setSection(body, "Accent");
  lv_obj_t* row = box(body, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_set_style_pad_row(row, 8, 0);
  for (size_t i = 0; i < kAccentCount; i++) {
    const bool on = (g.accent & 0xFFFFFF) == kAccents[i];
    lv_obj_t* d = box(row, 40, 40);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, c(kAccents[i]), 0);
    lv_obj_set_style_border_width(d, on ? 3 : 0, 0);
    lv_obj_set_style_border_color(d, c(_t.ink), 0);
    focusable(d);
    lv_obj_set_user_data(d, reinterpret_cast<void*>(uintptr_t(4 + i) + 1));
    lv_obj_add_event_cb(
        d,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_set_focus_row = uint8_t(uintptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))) - 1);
          self->later([](GuiShell* g) {
            g->_model->guiPrefs().accent = kAccents[g->_set_focus_row - 4];
            g->_model->prefsChanged();   // redraws in the new accent
          });
        },
        LV_EVENT_CLICKED, this);
    if (4 + i == _set_focus_row) _focus_row = d;
  }
}

// Node (mockup 9): name, key, and where this device is.
void GuiShell::buildNodePage(lv_obj_t* content) {
  setHeader(content, "Node");
  lv_obj_t* body = pageBody(content);
  const NodeSettings s = _model->nodeSettings();
  char v[40];
  setSection(body, "Identity");
  setRow(body, nName, "Name", "Shown to others in adverts", s.name, kNav, false, false, false);
  keySummary(_model->selfKey(), v, sizeof v);
  setRow(body, nKey, "Public key", nullptr, v, kRo, false, false, false);
  setSection(body, "Location");
  setRow(body, nGps, "GPS", "Position from the receiver", nullptr, kTog, s.gps_on, false, false);
  positionText(s.lat_e6, s.lon_e6, v, sizeof v);
  // With GPS on, the position comes from the fix; set here only with it off.
  setRow(body, nPos, "Position", s.gps_on ? "From GPS" : "Set here", v, s.gps_on ? kRo : kNav, false, false, false);
  durationText(s.gps_secs, v, sizeof v);
  setRow(body, nGpsSecs, "GPS interval", "How often it reads", v, kSel, false, false, !s.gps_on);
}

// Radio stats (mockup 10): read-only, refreshed every second while open.
void GuiShell::buildStatsPage(lv_obj_t* content) {
  setHeader(content, "Radio stats");
  lv_obj_t* body = pageBody(content);
  _set_body = body;
  RadioStats st{};
  _model->radioStats(&st);
  char v[32];
  setSection(body, "Since boot");
  airtimeText(st.tx_air_secs, st.uptime_secs, v, sizeof v);
  setRow(body, 0, "Airtime", "Transmit share of uptime", v, kRo, false, false, false);
  textf(v, sizeof v, "%d dBm", int(st.noise_floor));
  setRow(body, 1, "Noise floor", nullptr, v, kRo, false, false, false);
  thousands(st.recv, v, sizeof v);
  setRow(body, 2, "Received", "Packets", v, kRo, false, false, false);
  sentText(st.sent_flood, st.sent_direct, v, sizeof v);
  setRow(body, 3, "Sent", "Flood · direct", v, kRo, false, false, false);
  lastPacketText(st.snr_x4, st.rssi, v, sizeof v);
  setRow(body, 4, "Last packet", "SNR · RSSI", v, kRo, false, false, false);
  setSection(body, "Radio");
  formatUptime(st.uptime_secs, v, sizeof v);
  setRow(body, 5, "Uptime", nullptr, v, kRo, false, false, false);
  // The 1 s refresh keeps where the reader scrolled to.
  lv_obj_update_layout(body);
  lv_obj_scroll_to_y(body, _stats_scroll, LV_ANIM_OFF);
}

// Sensors (Epic M, #1661): the radio's own sensors as the phone's telemetry
// request reads them, refreshed every 5 s while open. One section per sensor
// (two across in class B), the GPS, then Units (°F by default). Mockups 2-4
// and the owner's decisions on #1660 (2026-10-19).
void GuiShell::buildSensorsPage(lv_obj_t* content) {
  lv_obj_t* head = setHeader(content, "Sensors");
  lv_obj_set_flex_grow(box(head, 1, 1), 1);
  label(head, FontRole::Meta, _t.dim, "every 5 s");
  lv_obj_t* body = pageBody(content);
  _set_body = body;
  const bool f = _model->guiPrefs().temp_f != 0;
  static SensorReading r[24];   // off the stack; the GUI is single-threaded
  const size_t n = _model->sensorReadings(r, 24);
  const int found = _model->sensorCount();
  char a[32], b[24], v[16], u[8], line[40];
  uint8_t id = 0;

  if (_model->gpsLines(a, sizeof a, b, sizeof b)) {   // the GPS module, without the position
    setSection(body, "GPS");
    // kInfo: read-only, but USR steps through them, so buttons scroll too.
    setRow(body, id++, "Fix", nullptr, a, kInfo, false, false, false);
    setRow(body, id++, "Altitude", nullptr, b, kInfo, false, false, false);
  }

  if (found <= 0) {   // mockup 3
    lv_obj_t* none = box(body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(none, 12, 0);
    lv_obj_set_flex_flow(none, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(none, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(none, 4, 0);
    label(none, FontRole::Body, _t.ink, "No sensors found");
    lv_obj_t* l1 = label(none, FontRole::Label, _t.dim,
                         "Checked at boot on the I2C bus for AHT, BME/BMP, SHT, LPS22, INA2xx/INA3221, MLX90614 and "
                         "VL53L0X.");
    lv_obj_t* l2 = label(none, FontRole::Label, _t.dim, "Fit one and restart to read it here.");
    for (lv_obj_t* l : {l1, l2}) {
      lv_obj_set_width(l, LV_PCT(100));
      lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
  } else {
    // A section per sensor, in channel order; class B sets them two across.
    lv_obj_t* grid = body;
    if (landscape()) {
      grid = box(body, LV_PCT(100), LV_SIZE_CONTENT);
      lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    }
    for (size_t i = 0; i < n;) {
      const uint8_t ch = r[i].channel;
      const char* name = _model->sensorName(ch);
      lv_obj_t* sec = grid;
      if (landscape()) {
        sec = box(grid, LV_PCT(50), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);
      }
      setSection(sec, name[0] ? name : "Sensor");
      for (; i < n && r[i].channel == ch; i++) {
        readingText(r[i], name, f, v, sizeof v, u, sizeof u);
        textf(line, sizeof line, "%s%s%s", v, u[0] ? " " : "", u);
        setRow(sec, id++, readingName(r[i].type, name), nullptr, line, kInfo, false, false, false);
      }
    }
    if (n == 0) setRow(body, id++, "Reading", "The sensors didn't answer this time", "—", kRo, false, false, false);
  }

  setRow(body, kSensUnits, "Units", nullptr, f ? "°F" : "°C", kSel, false, false, false);
  char foot_text[80];
  textf(foot_text, sizeof foot_text, "Shared over the mesh per Privacy › Environment telemetry (%s).",
        telemetryText(_model->nodeSettings().tel_env));
  lv_obj_t* foot = label(body, FontRole::Label, _t.dim, foot_text);
  lv_obj_set_width(foot, LV_PCT(100));
  lv_label_set_long_mode(foot, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_all(foot, 8, 0);
  // The 5 s refresh keeps where the reader scrolled to.
  lv_obj_update_layout(body);
  lv_obj_scroll_to_y(body, _stats_scroll, LV_ANIM_OFF);
}

// SD card (Epic I #1705; owner-approved mockups 3, 4 and 7, 2026-10-21):
// what the card is doing, how full it is, and Format card. Refreshed every
// 2 s, so a card pulled or put back shows without leaving the page.
void GuiShell::buildSdPage(lv_obj_t* content) {
  setHeader(content, "SD card");
  lv_obj_t* body = pageBody(content);
  _set_body = body;
  SdView sd;
  _model->sdView(&sd);
  const bool working = sd.state == SdState::Working;
  char v[24];
  uint8_t id = 1;   // 0 is Format card

  // Class B sets the details and the space side by side (mockup 7).
  lv_obj_t* left = body;
  lv_obj_t* right = body;
  if (landscape() && working) {
    lv_obj_t* cols = box(body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cols, LV_FLEX_FLOW_ROW);
    left = box(cols, LV_PCT(50), LV_SIZE_CONTENT);
    right = box(cols, LV_PCT(50), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
  }

  lv_obj_t* st = setRow(left, id++, "Status", nullptr, sdStatusText(sd.state), kInfo, false, false, false);
  const uint32_t st_ink = working ? _t.ok : sd.state == SdState::NeverSeen ? _t.ink2 : _t.bad;
  lv_obj_set_style_text_color(lv_obj_get_child(st, -1), c(st_ink), 0);

  if (working) {
    if (sd.card[0]) setRow(left, id++, "Card", nullptr, sd.card, kInfo, false, false, false);
    textf(v, sizeof v, "%lu", (unsigned long)sd.on_card);
    setRow(left, id++, landscape() ? "Messages" : "Messages on card", nullptr, v, kInfo, false, false, false);
    if (sd.usage_ok && sd.total > 0) {
      if (!landscape()) setSection(right, "Space");
      lv_obj_t* hold = box(right, LV_PCT(100), LV_SIZE_CONTENT);   // LVGL 8 has no margins
      lv_obj_set_style_pad_hor(hold, 8, 0);
      lv_obj_set_style_pad_ver(hold, 4, 0);
      lv_obj_t* bar = box(hold, LV_PCT(100), 8);
      lv_obj_set_style_border_width(bar, 1, 0);
      lv_obj_set_style_border_color(bar, c(_t.line), 0);
      lv_obj_set_style_radius(bar, 3, 0);
      const uint64_t used = sd.total > sd.free_bytes ? sd.total - sd.free_bytes : 0;
      lv_obj_t* fill = box(bar, LV_PCT(int16_t(used * 100 / sd.total)), LV_PCT(100));
      lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(fill, c(_t.sig), 0);
      sdSizeText(used, v, sizeof v);
      setRow(right, id++, "Used", nullptr, v, kInfo, false, false, false);
      sdSizeText(sd.free_bytes, v, sizeof v);
      setRow(right, id++, "Free", nullptr, v, kInfo, false, false, false);
      sdSizeText(sd.total, v, sizeof v);
      setRow(right, id++, "Total", nullptr, v, kInfo, false, false, false);
    }
  } else if (sd.state != SdState::NeverSeen) {   // mockup 4: the card is out, or won't mount
    setRow(body, id++, "Saving to", nullptr, "Flash", kInfo, false, false, false);
    if (sd.waiting) {
      textf(v, sizeof v, "%u message%s", unsigned(sd.waiting), sd.waiting == 1 ? "" : "s");
      setRow(body, id++, "Waiting to copy", nullptr, v, kInfo, false, false, false);
    }
    lv_obj_t* foot = label(body, FontRole::Label, _t.dim,
                           "Messages are kept in flash until the card is back, then copied to it. "
                           "Flash holds up to 1,000.");
    lv_obj_set_width(foot, LV_PCT(100));
    lv_label_set_long_mode(foot, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_all(foot, 8, 0);
  }

  // Format card: on a card that works, and on one that won't mount (that's how
  // it gets fixed from here). With no card in, there's nothing to format.
  const bool can_format = working || sd.state == SdState::Failed;
  const bool note = _note_row == kSdFormat && _set_note[0];
  lv_obj_t* fmt = setRow(body, kSdFormat, "Format card", note ? _set_note : nullptr, nullptr, kVal, false,
                         false, !can_format);
  lv_obj_t* kk = lv_obj_get_child(fmt, 0);
  lv_obj_set_style_text_color(lv_obj_get_child(kk, 0), c(can_format ? _t.bad : _t.dim), 0);

  lv_obj_update_layout(body);
  lv_obj_scroll_to_y(body, _stats_scroll, LV_ANIM_OFF);
}

// Privacy (mockup 11): location in adverts, extra ACKs, telemetry.
void GuiShell::buildPrivacyPage(lv_obj_t* content) {
  setHeader(content, "Privacy");
  lv_obj_t* body = pageBody(content);
  const NodeSettings s = _model->nodeSettings();
  setSection(body, "Adverts");
  setRow(body, pLoc, "Share location", "In your adverts", s.share_location ? "On" : "Off", kSel, false, false, false);
  setSection(body, "Messaging");
  setRow(body, pAcks, "Extra ACKs", "Send each ACK more than once", nullptr, kTog, s.extra_acks, false, false);
  setSection(body, "Telemetry");
  setRow(body, pBase, "Base", "Battery, uptime to others", telemetryText(s.tel_base), kSel, false, false, false);
  setRow(body, pLocTel, "Location", nullptr, telemetryText(s.tel_loc), kSel, false, false, false);
  setRow(body, pEnv, "Environment", "Sensors", telemetryText(s.tel_env), kSel, false, false, false);
}

// Contacts (mockup 12, owner pick a): the auto-add rules. All on is
// "every type"; the type rows then pick which.
void GuiShell::buildContactsPage(lv_obj_t* content) {
  setHeader(content, "Contacts");
  lv_obj_t* body = pageBody(content);
  const NodeSettings s = _model->nodeSettings();
  const bool all = !s.manual_add;
  const bool on = all || (s.auto_bits & 0x1E);
  char v[24];
  setSection(body, "Adding");
  setRow(body, cAuto, "Add automatically", "New nodes heard become contacts", nullptr, kTog, on, false, false);
  for (uint8_t i = 0; i < 4; i++) {
    setRow(body, uint8_t(cChat + i), kTypeName[i], nullptr, nullptr, kTog, all || (s.auto_bits & kTypeBit[i]), false,
           !on);
  }
  hopLimitText(s.auto_hops, v, sizeof v);
  setRow(body, cHops, "Up to", "Hops away", v, kSel, false, false, !on);
  setSection(body, "Stored");
  textf(v, sizeof v, "%u", unsigned(s.contacts));
  char hint[16];
  textf(hint, sizeof hint, "Of %u", unsigned(s.contacts_max));
  setRow(body, cCount, "Contacts", hint, v, kRo, false, false, false);
}

// ------------------------------------------------------------ Node › Name

void GuiShell::openSetText() {
  _set_text = true;
  _set_buf.start(31);
  const NodeSettings s = _model->nodeSettings();
  _set_buf.insert(s.name, strlen(s.name));
  _page.select(KeyPage::Letters);
  _shift = ShiftState();
  _hot = -1;
  rebuild();
}

void GuiShell::closeSetText(bool keep) {
  if (keep && _set_buf.length()) {   // an empty name isn't kept
    NodeSettings s = _model->nodeSettings();
    textf(s.name, sizeof s.name, "%s", _set_buf.text());
    _model->setNodeSettings(s);
  }
  _set_text = false;
  rebuild();
}

// As Admin login's sheet: a title, the field, a hint, the keyboard.
void GuiShell::buildSetText(lv_obj_t* scr) {
  lv_obj_t* h = box(scr, _lay.content_w, 28);
  lv_obj_set_pos(h, 0, kStatusH);
  lv_obj_set_style_pad_hor(h, 8, 0);
  lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(h, 6, 0);
  lv_obj_t* back = box(h, 10, 20);
  line(back, chevronPoints(), 3, _t.sig);
  label(h, FontRole::Body, _t.ink, "Name");
  lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      h, [](lv_event_t* e) { static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->closeSetText(false); }); },
      LV_EVENT_CLICKED, this);
  lv_obj_t* field = box(scr, _lay.content_w - 16, 34);
  lv_obj_set_pos(field, 8, kStatusH + 32);
  lv_obj_set_style_pad_hor(field, 8, 0);
  lv_obj_set_style_border_width(field, 1, 0);
  lv_obj_set_style_border_color(field, c(_t.sig), 0);
  lv_obj_set_style_bg_opa(field, lv_opa_t(_t.wash >> 24), 0);
  lv_obj_set_style_bg_color(field, c(_t.wash), 0);
  lv_obj_set_style_radius(field, _look.radius, 0);
  lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  _set_text_label = label(field, FontRole::Body, _t.ink);
  lv_obj_t* caret = box(field, 1, 14);
  lv_obj_set_style_bg_opa(caret, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(caret, c(_t.sig), 0);
  // The limit is 31 bytes (accents and emoji take more than one), so it
  // isn't stated as characters.
  lv_obj_t* hint = label(scr, FontRole::Label, _t.dim, "Shown to others in adverts");
  lv_obj_set_pos(hint, 10, kStatusH + 32 + 38);
  buildKeyboard(scr);
  refreshSetText();
}

void GuiShell::refreshSetText() {
  if (_set_text_label) userText(_set_text_label, _set_buf.text());
}

// Connectivity (mockup 13): Bluetooth only; this build has no WiFi.
void GuiShell::buildConnPage(lv_obj_t* content) {
  setHeader(content, "Connectivity");
  lv_obj_t* body = pageBody(content);
  DeviceInfo d{};
  _model->deviceInfo(&d);
  char v[24];
  setSection(body, "Bluetooth");
  // Off holds until the next boot: the interface comes back on with it.
  setRow(body, bOn, "Bluetooth", d.ble_on ? "For the phone app" : "On again after a reboot", nullptr, kTog, d.ble_on,
         false, false);
  // A boot's own PIN is shown: it's the one a phone has to type.
  pinText(d.pin_stored, d.pin_active, v, sizeof v);
  setRow(body, bPin, "PIN", d.pin_stored ? "Asked when pairing" : "New each boot", v, kNav, false, false, false);
  setRow(body, bPhone, "Phone", nullptr, d.phone ? "Connected" : "Not connected", kRo, false, false, false);
}

// Power (mockup 14). Shut down below is shown, not set: it is one policy
// with SafeBoot's gate.
void GuiShell::buildPowerPage(lv_obj_t* content) {
  setHeader(content, "Power");
  lv_obj_t* body = pageBody(content);
  DeviceInfo d{};
  _model->deviceInfo(&d);
  const GuiPrefs& g = _model->guiPrefs();
  char v[24];
  setSection(body, "Battery");
  batteryNowText(d.batt_mv, d.batt_pct, v, sizeof v);
  setRow(body, wNow, "Now", nullptr, v, kRo, false, false, false);
  voltsText(d.full_mv, v, sizeof v);
  setRow(body, wFull, "Full charge at", d.full_pinned ? "Set here" : "Learned after a full charge", v, kVal, false,
         false, false);
  if (d.full_pinned) {
    setRow(body, wLearn, "Learn again", "The next full charge sets it", nullptr, kVal, false, false, false);
  }
  voltsText(d.shutdown_mv, v, sizeof v);
  setRow(body, wShut, "Shut down below", "Low-voltage protection", v, kRo, false, false, false);
  setSection(body, "Screen");
  durationText(_model->screenOffSecs(), v, sizeof v);
  setRow(body, wOff, "Screen off after", "Same as Display", v, kSel, false, false, false);
  setRow(body, wTouch, "Wake on touch", "Else USR only", nullptr, kTog, g.wake_touch, false, false);
}

// Actions (mockup 15): Reboot and Delete all paths ask first, Cancel the
// default; Send advert and Sync time act and say how it went on the row.
void GuiShell::buildActionsPage(lv_obj_t* content) {
  setHeader(content, "Actions");
  lv_obj_t* body = pageBody(content);
  struct Act {
    const char* k;
    const char* hint;
    uint32_t ink;
  };
  const Act acts[] = {{"Send advert", "Zero-hop now", _t.ink},
                      {"Sync time", "From GPS; the phone sets it on connect", _t.ink},
                      {"Reboot", "Saves first", _t.warn},
                      {"Delete all paths", "Every contact floods until a route returns", _t.bad}};
  for (uint8_t i = 0; i < 4; i++) {
    const bool note = _note_row == i && _set_note[0];
    lv_obj_t* row = setRow(body, i, acts[i].k, note ? _set_note : acts[i].hint, nullptr, kVal, false, false, false);
    lv_obj_set_style_min_height(row, 38, 0);
    lv_obj_t* kk = lv_obj_get_child(row, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(kk, 0), c(acts[i].ink), 0);
    if (note) lv_obj_set_style_text_color(lv_obj_get_child(kk, 1), c(_t.sig), 0);
  }
}

void GuiShell::actionDone(uint8_t row, const char* note) {
  _note_row = row;
  setNote(note);
  rebuild();
}

// Debug (mockup 16): the capture level, and SafeBoot, read-only.
void GuiShell::buildDebugPage(lv_obj_t* content) {
  setHeader(content, "Debug");
  lv_obj_t* body = pageBody(content);
  DeviceInfo d{};
  _model->deviceInfo(&d);
  char v[24];
  setSection(body, "Logs");
  setRow(body, gLog, "Serial log", "USB serial, capture level", logName(d.log_index), kSel, false, false, false);
  setSection(body, "SafeBoot");
  if (d.boot_limit) ofText(d.boot_attempt, d.boot_limit, v, sizeof v);
  else textf(v, sizeof v, "—");
  setRow(body, gBoots, "Boots", "Since the last clean start", v, kRo, false, false, false);
  setRow(body, gReset, "Last reset", nullptr, resetName(d.reset_code), kRo, false, false, false);
}

// About device (mockup 17): all read-only; the rows take focus so the
// buttons can scroll to the end.
void GuiShell::buildAboutPage(lv_obj_t* content) {
  setHeader(content, "About device");
  lv_obj_t* body = pageBody(content);
  DeviceInfo d{};
  _model->deviceInfo(&d);
  char v[40];
  setSection(body, "Firmware");
  setRow(body, 0, "Offband", nullptr, d.offband, kInfo, false, false, false);
  setRow(body, 1, "MeshCore", nullptr, d.meshcore, kInfo, false, false, false);
  setRow(body, 2, "Build", nullptr, d.build, kInfo, false, false, false);
  setSection(body, "Device");
  setRow(body, 3, "Model", nullptr, d.model, kInfo, false, false, false);
  voltsText(d.batt_mv, v, sizeof v);
  setRow(body, 4, "Battery", nullptr, v, kInfo, false, false, false);
  keySummary(_model->selfKey(), v, sizeof v);
  setRow(body, 5, "Public key", nullptr, v, kInfo, false, false, false);
  setSection(body, "Data");
  ofText(d.msgs, d.msgs_cap, v, sizeof v);
  setRow(body, 6, "Messages", "On this device", v, kInfo, false, false, false);
  ofText(d.contacts, d.contacts_max, v, sizeof v);
  setRow(body, 7, "Contacts", nullptr, v, kInfo, false, false, false);
}

// ------------------------------------------------------------- actions

void GuiShell::setRowPressed(uint8_t id) {
  _set_focus_row = id;
  GuiPrefs& g = _model->guiPrefs();
  if (_set_page == SetPage::Msg) {
    switch (id) {
      case mOn: setFlag(g.notify_on); break;
      case mDm: setFlag(g.notify_dm); break;
      case mCh: setFlag(g.notify_channels); break;
      case mNode: setFlag(g.notify_new_node); break;
      case mClear: setFlag(g.clear_path_on_fail); break;
      case mJump: setFlag(g.jump_to_unread); break;
      case mDefault:
      case mAlert: openSetSheet(SetSheet::Pick, id); break;
      default: break;
    }
    return;
  }
  if (_set_page == SetPage::Node) {
    NodeSettings s = _model->nodeSettings();
    switch (id) {
      case nName: openSetText(); break;
      case nGps:
        s.gps_on = !s.gps_on;
        _model->setNodeSettings(s);
        rebuild();
        break;
      case nPos:
        _pad_stage = 0;
        openSetSheet(SetSheet::Pad, id);
        break;
      case nGpsSecs: openSetSheet(SetSheet::Pick, id); break;
      default: break;
    }
    return;
  }
  if (_set_page == SetPage::Sensors) {   // the readings are read-only; Units picks
    if (id == kSensUnits) openSetSheet(SetSheet::Pick, id);
    return;
  }
  if (_set_page == SetPage::Privacy) {
    if (id == pAcks) {
      NodeSettings s = _model->nodeSettings();
      s.extra_acks = !s.extra_acks;
      _model->setNodeSettings(s);
      rebuild();
    } else {
      openSetSheet(SetSheet::Pick, id);
    }
    return;
  }
  if (_set_page == SetPage::Contacts) {
    NodeSettings s = _model->nodeSettings();
    const bool all = !s.manual_add;
    if (id == cAuto) {   // on: every type; off: none
      const bool on = all || (s.auto_bits & 0x1E);
      s.manual_add = on;
      s.auto_bits = uint8_t(s.auto_bits & ~0x1E);
    } else if (id >= cChat && id <= cSens) {   // one type, from what is in effect now
      uint8_t bits = all ? uint8_t(s.auto_bits | 0x1E) : s.auto_bits;
      bits ^= kTypeBit[id - cChat];
      s.manual_add = true;
      s.auto_bits = bits;
    } else if (id == cHops) {
      openSetSheet(SetSheet::Pick, id);
      return;
    } else {
      return;
    }
    _model->setNodeSettings(s);
    rebuild();
    return;
  }
  if (_set_page == SetPage::Display) {
    switch (id) {
      case dTheme: setOpen(SetPage::Theme); break;
      case dBright: openSetSheet(SetSheet::Step, id); break;
      case dLook:
      case dClock:
      case dZone:
      case dRotation:
      case dDim:
      case dOff: openSetSheet(SetSheet::Pick, id); break;
      default: break;
    }
    return;
  }
  if (_set_page == SetPage::Conn) {
    if (id == bOn) {
      DeviceInfo d{};
      _model->deviceInfo(&d);
      _model->setBleOn(!d.ble_on);
      rebuild();
    } else if (id == bPin) {
      openSetSheet(SetSheet::Pad, id);
    }
    return;
  }
  if (_set_page == SetPage::Power) {
    switch (id) {
      case wFull: openSetSheet(SetSheet::Step, id); break;
      case wLearn:
        _model->learnFullAgain();
        _set_focus_row = wFull;   // its row goes
        rebuild();
        break;
      case wOff: openSetSheet(SetSheet::Pick, id); break;
      case wTouch: setFlag(g.wake_touch); break;
      default: break;
    }
    return;
  }
  if (_set_page == SetPage::Sd) {
    if (id != kSdFormat) return;   // the rest are read-only
    SdView sd;
    _model->sdView(&sd);
    if (sd.state == SdState::Working || sd.state == SdState::Failed) openSetSheet(SetSheet::Confirm, id);
    return;
  }
  if (_set_page == SetPage::Actions) {
    switch (id) {
      case aAdvert: actionDone(id, _model->advertNow() ? "Sent ✓" : "Not sent"); break;
      case aTime: actionDone(id, _model->syncTimeFromGps() ? "Time from GPS ✓" : "No GPS time"); break;
      case aReboot:
      case aPaths: openSetSheet(SetSheet::Confirm, id); break;
      default: break;
    }
    return;
  }
  if (_set_page == SetPage::Debug) {
    if (id == gLog) openSetSheet(SetSheet::Pick, id);
    return;
  }
  if (_set_page != SetPage::Radio) return;
  switch (id) {
    case rPreset:
    case rBw:
    case rSf:
    case rCr:
    case rPath:
      openSetSheet(SetSheet::Pick, id);
      break;
    case rFreq:
      openSetSheet(SetSheet::Pad, id);
      break;
    case rTx:
      openSetSheet(SetSheet::Step, id);
      break;
    case rRepeat:
      _radio_now.repeat = !_radio_now.repeat;
      rebuild();
      break;
    case rSave: {
      char lines[1][48];
      if (radioChanges(_radio_was, _radio_now, lines, 0) == 0) break;
      const bool ok = _model->saveRadio(_radio_now);
      // What the device now holds: a save can land part-way (the radio, then
      // TX power refused), and amber must mark only what isn't saved.
      _radio_was = _model->radioValues();
      if (ok) {
        _radio_now = _radio_was;
        rebuild();
        setNote("Saved ✓");   // the amber clearing alone was easy to miss (bench, owner pick a)
      } else {
        rebuild();
        setNote("Not saved");
      }
      break;
    }
    default:
      break;
  }
}

// The buttons on Setup: a sheet first, then a page (Back asks before
// dropping changes). On the list, the shell's own handling (and Back Home).
bool GuiShell::setAction(Action a) {
  if (_set_text) {   // Node › Name: a key cursor as in Compose; Back drops the edit
    switch (a) {
      case Action::Next: setHotKey(_hot < 0 ? 0 : int((_hot + 1) % _nkeys), false); return true;
      case Action::Prev: setHotKey(_hot <= 0 ? int(_nkeys) - 1 : _hot - 1, false); return true;
      case Action::Open: if (_hot >= 0) pressKey(_hot); return true;
      case Action::Back: closeSetText(false); return true;
      default: return false;
    }
  }
  if (_ss != SetSheet::None && _ss_count <= 0) {   // nothing to move over: only leave
    if (a == Action::Back) closeSetSheet();
    return a == Action::Back || a == Action::Next || a == Action::Prev || a == Action::Open;
  }
  if (_ss != SetSheet::None) {
    switch (_ss) {
      case SetSheet::Pick:
        if (a == Action::Next) setSheetCursor((_ss_cur + 1) % _ss_count);
        else if (a == Action::Prev) setSheetCursor((_ss_cur + _ss_count - 1) % _ss_count);
        else if (a == Action::Open) setSheetCommit();
        else if (a == Action::Back) closeSetSheet();
        else return false;
        return true;
      case SetSheet::Step:
        if (a == Action::Next) setSheetStep(1);
        else if (a == Action::Prev) setSheetStep(-1);
        else if (a == Action::Open) setSheetCommit();
        else if (a == Action::Back) closeSetSheet();
        else return false;
        return true;
      case SetSheet::Pad:
        if (a == Action::Next || a == Action::Prev) {   // over the keys there are
          int i = _ss_cur;
          const int step = a == Action::Next ? 1 : _ss_count - 1;
          do i = (i + step) % _ss_count;
          while (!_ss_item[i] && i != _ss_cur);
          setSheetCursor(i);
        } else if (a == Action::Open) setSheetPress(_ss_cur);
        else if (a == Action::Back) closeSetSheet();
        else return false;
        return true;
      case SetSheet::Confirm:
        if (a == Action::Next || a == Action::Prev) setSheetCursor(1 - _ss_cur);
        else if (a == Action::Open) setSheetPress(_ss_cur);
        else if (a == Action::Back) closeSetSheet();   // Keep editing
        else return false;
        return true;
      default:
        return false;
    }
  }
  if (_set_page != SetPage::List && a == Action::Back) {
    setBack();
    return true;
  }
  return false;
}

// --------------------------------------------------------------- sheets

void GuiShell::openSetSheet(SetSheet kind, uint8_t row) {
  closeSetSheet();
  _ss = kind;
  _ss_row = row;
  _ss_cur = 0;
  _ss_count = 0;
  _ss_gz = _model->gpsZone();   // held while the sheet is up: a fix coming or going doesn't shift the rows

  _ss_obj = sheetBackdrop();
  lv_obj_add_event_cb(
      _ss_obj,
      [](lv_event_t* e) {   // a tap on the backdrop closes it, as Cancel
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
          static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->closeSetSheet(); });
        }
      },
      LV_EVENT_CLICKED, this);
  lv_obj_t* sheet = sheetPanel(_ss_obj);
  const bool land = landscape();
  const int16_t sheet_w = int16_t(_lay.w - _lay.sheet_left);

  auto title = [&](const char* t, const char* sub) {
    lv_obj_t* tl = label(sheet, FontRole::Body, _t.ink, t);
    lv_obj_set_style_pad_left(tl, 12, 0);
    lv_obj_set_style_pad_top(tl, land ? 6 : 10, 0);
    if (sub && sub[0]) {
      lv_obj_t* sl = label(sheet, FontRole::Label, _t.dim, sub);
      lv_obj_set_width(sl, LV_PCT(100));
      lv_label_set_long_mode(sl, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_pad_hor(sl, 12, 0);
      lv_obj_set_style_pad_bottom(sl, land ? 4 : 6, 0);
    }
  };
  // A sheet button; i is its index in _ss_item.
  auto button = [&](lv_obj_t* parent, int i, const char* text, uint32_t ink, uint32_t border, bool fill) {
    lv_obj_t* b = box(parent, 0, land ? 28 : 32);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, _look.radius, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, c(border), 0);
    if (fill) {
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(b, c(border), 0);
    }
    lv_obj_center(label(b, FontRole::Body, ink, text));
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(b, reinterpret_cast<void*>(intptr_t(i) + 1));
    lv_obj_add_event_cb(
        b,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_ss_cur = int(intptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))) - 1);
          self->later([](GuiShell* g) { g->setSheetPress(g->_ss_cur); });
        },
        LV_EVENT_CLICKED, this);
    _ss_item[i] = b;
    return b;
  };
  auto buttonRow = [&]() {
    lv_obj_t* row = box(sheet, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_style_pad_top(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    return row;
  };

  char v[48];
  switch (kind) {
    case SetSheet::Pick: {
      const char* sub = nullptr;
      const char* t = pickTitle(&sub);
      _ss_count = pickCount();
      const int current = pickCurrent();
      title(t, sub);
      // Only the rows on screen are built, and refilled as the cursor or a
      // drag moves the window: the 59 presets as objects took internal RAM
      // down to 340 bytes free (bench, 2026-10-17).
      _ss_picked = current;
      _ss_top = 0;
      const int rows = _lay.pick_rows, row_h = _lay.sheet_row_h;   // class B: 5 of 26 px
      const int win = _ss_count < rows ? _ss_count : rows;
      _ss_list = box(sheet, LV_PCT(100), win * row_h);
      lv_obj_set_flex_flow(_ss_list, LV_FLEX_FLOW_COLUMN);
      lv_obj_add_flag(_ss_list, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(
          _ss_list,
          [](lv_event_t* e) {   // a drag scrolls the window, a row at a time
            GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
            const lv_event_code_t code = lv_event_get_code(e);
            const int rh = self->_lay.sheet_row_h;
            if (code == LV_EVENT_PRESSED) {
              self->_ss_drag = 0;
              self->_ss_dragged = false;
            } else if (code == LV_EVENT_PRESSING) {
              lv_point_t d;
              lv_indev_get_vect(lv_indev_get_act(), &d);
              self->_ss_drag += d.y;
              while (self->_ss_drag <= -rh || self->_ss_drag >= rh) {
                const int step = self->_ss_drag < 0 ? 1 : -1;   // up shows later rows
                self->_ss_drag += step * rh;
                self->_ss_dragged = true;
                self->setPickTop(self->_ss_top + step);
              }
            }
          },
          LV_EVENT_ALL, this);
      for (int w = 0; w < win; w++) {
        lv_obj_t* o = box(_ss_list, LV_PCT(100), row_h);
        lv_obj_set_style_pad_hor(o, 12, 0);
        lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(o, 8, 0);
        lv_obj_set_style_border_side(o, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(o, c(_t.sig), 0);
        lv_obj_set_style_bg_color(o, c(_t.wash), 0);
        lv_obj_t* rd = box(o, 12, 12);   // child 0: the radio ring, its dot inside
        lv_obj_set_style_radius(rd, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(rd, 2, 0);
        lv_obj_t* dot = box(rd, 4, 4);
        lv_obj_center(dot);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, c(_t.sig), 0);
        lv_obj_t* ol = label(o, FontRole::Body, _t.ink);   // child 1: the option
        lv_obj_set_flex_grow(ol, 1);
        oneLine(ol, FontRole::Body);
        label(o, FontRole::Meta, _t.dim);   // child 2: the right side (Time zone's offset)
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);   // the list sees the drag
        lv_obj_set_user_data(o, reinterpret_cast<void*>(intptr_t(w) + 1));
        lv_obj_add_event_cb(
            o,
            [](lv_event_t* e) {
              GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
              if (self->_ss_dragged) return;   // the end of a drag, not a pick
              const int w = int(intptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))) - 1);
              self->_ss_cur = self->_ss_top + w;
              self->later([](GuiShell* g) { g->setSheetCommit(); });
            },
            LV_EVENT_CLICKED, this);
        _ss_item[w] = o;
      }
      setSheetCursor(current >= 0 ? current : 0);
      break;
    }
    case SetSheet::Step: {   // TX power, Brightness (mockup 4), Full charge at
      const bool bright = _set_page == SetPage::Display;
      const bool full = _set_page == SetPage::Power;
      if (bright) {
        _ss_spec = StepSpec{10, 100, 10};
        _ss_val = _model->guiPrefs().brightness;
        title("Brightness", nullptr);
      } else if (full) {
        DeviceInfo d{};
        _model->deviceInfo(&d);
        _ss_spec = kFullStep;
        int f = d.full_mv;   // inside the band, to the nearest step
        f = f < kFullStep.min ? kFullStep.min : f > kFullStep.max ? kFullStep.max : f;
        _ss_val = kFullStep.min + (f - kFullStep.min + kFullStep.step / 2) / kFullStep.step * kFullStep.step;
        title("Full charge at", "Where the battery reads 100%");
      } else {
        _ss_spec = StepSpec{0, _model->txMax(), 1};
        _ss_val = _radio_now.tx_dbm;
        title("TX power", "The radio chip's setting, not the antenna's output");
      }
      lv_obj_t* st = box(sheet, LV_PCT(100), LV_SIZE_CONTENT);
      lv_obj_set_style_pad_ver(st, 8, 0);
      lv_obj_set_flex_flow(st, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(st, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(st, 14, 0);
      lv_obj_t* minus = button(st, 0, "−", _t.ink, _t.line, false);
      lv_obj_set_flex_grow(minus, 0);
      lv_obj_set_width(minus, 40);
      stepText(_ss_val, v, sizeof v);
      _ss_value = label(st, FontRole::Title, _t.ink, v);
      lv_obj_set_style_min_width(_ss_value, 90, 0);
      lv_obj_set_style_text_align(_ss_value, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_t* plus = button(st, 1, "+", _t.ink, _t.line, false);
      lv_obj_set_flex_grow(plus, 0);
      lv_obj_set_width(plus, 40);
      if (bright) textf(v, sizeof v, "10 – 100%%");
      else if (full) textf(v, sizeof v, "3.90 – 4.25 V");
      else textf(v, sizeof v, "0 – %d dBm · this board's maximum", int(_model->txMax()));
      _ss_limit = label(sheet, FontRole::Meta, _t.dim, v);
      lv_obj_set_width(_ss_limit, LV_PCT(100));
      lv_obj_set_style_text_align(_ss_limit, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_t* br = buttonRow();
      button(br, 2, "Cancel", _t.ink, _t.line, false);
      button(br, 3, "Set", _t.sigInk, _t.sig, true);
      _ss_count = 4;   // −, +, Cancel, Set; the buttons step rather than move a cursor
      break;
    }
    case SetSheet::Pad: {   // Frequency (mockup 5)
      // Frequency (Radio), or Position (Node): latitude, then longitude,
      // signed, with ± as a thirteenth key.
      const bool pos = _set_page == SetPage::Node;
      const bool pin = _set_page == SetPage::Conn;   // six digits, no point
      const char* unit = "MHz";
      const char* range = "150.000 – 2500.000 MHz";
      if (pin) {
        _ss_pad.start("", 0, 6);
        title("PIN", "Used from the next boot");
        unit = "";
        range = "6 digits · empty: new each boot";   // turns red on a refusal
      } else if (pos) {
        const NodeSettings ns = _model->nodeSettings();
        const int32_t e6 = _pad_stage == 0 ? ns.lat_e6 : ns.lon_e6;
        if (e6 != 0) {
          const int32_t a = e6 < 0 ? -e6 : e6;
          textf(v, sizeof v, "%s%ld.%06ld", e6 < 0 ? "-" : "", (long)(a / 1000000), (long)(a % 1000000));
        } else {
          v[0] = '\0';
        }
        _ss_pad.start(v, 6, 11, true);
        title(_pad_stage == 0 ? "Latitude" : "Longitude", _pad_stage == 0 ? "Then longitude" : nullptr);
        unit = "°";
        range = _pad_stage == 0 ? "-90 – 90°" : "-180 – 180°";
      } else {
        freqText(_radio_now.freq_khz, v, sizeof v);
        _ss_pad.start(v, 3, 8);
        title("Frequency", nullptr);
      }
      // Class B (mockup 4): 220 px can't stack field, pad and buttons, so the
      // field, its range and the buttons take a column left of the pad.
      const bool beside = _lay.pad_beside;
      constexpr int16_t kLeftW = 110;
      lv_obj_t* left = sheet;
      lv_obj_t* right = sheet;
      if (beside) {
        lv_obj_t* split = box(sheet, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(split, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_hor(split, 10, 0);
        lv_obj_set_style_pad_top(split, 2, 0);
        lv_obj_set_style_pad_column(split, 8, 0);
        left = box(split, kLeftW, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(left, 5, 0);
        right = box(split, 0, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(right, 1);
      }
      lv_obj_t* field = box(left, beside ? LV_PCT(100) : sheet_w - 24, pos ? 28 : 32);
      lv_obj_set_style_pad_hor(field, 8, 0);
      lv_obj_set_style_border_width(field, 1, 0);
      lv_obj_set_style_border_color(field, c(_t.sig), 0);
      lv_obj_set_style_bg_opa(field, lv_opa_t(_t.wash >> 24), 0);
      lv_obj_set_style_bg_color(field, c(_t.wash), 0);
      lv_obj_set_style_radius(field, _look.radius, 0);
      if (!beside) lv_obj_set_style_translate_x(field, 12, 0);
      lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      _ss_value = label(field, FontRole::Data, _t.ink, _ss_pad.text());
      lv_obj_t* caret = box(field, 1, 14);
      lv_obj_set_style_bg_opa(caret, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(caret, c(_t.sig), 0);
      lv_obj_t* grow = box(field, 1, 1);
      lv_obj_set_flex_grow(grow, 1);
      label(field, FontRole::BodySmall, _t.dim, unit);
      _ss_limit = label(left, FontRole::Meta, _t.dim, range);
      lv_obj_set_width(_ss_limit, LV_PCT(100));
      lv_label_set_long_mode(_ss_limit, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_align(_ss_limit, beside ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_pad_ver(_ss_limit, beside ? 0 : 4, 0);
      lv_obj_t* pad = box(right, LV_PCT(100), LV_SIZE_CONTENT);
      lv_obj_set_style_pad_hor(pad, beside ? 0 : 6, 0);
      lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_set_style_pad_row(pad, 3, 0);
      lv_obj_set_style_pad_column(pad, 3, 0);
      // The pad's inner width: the sheet less its margins (228 in class A), or
      // what the left column leaves (class B).
      const lv_coord_t pw = beside ? lv_coord_t(sheet_w - 2 * 10 - kLeftW - 8) : lv_coord_t(sheet_w - 12);
      for (int i = 0; i < (pos ? 13 : 12); i++) {
        const int key = pos ? kPosOrder[i] : i;   // slot i shows this key
        const lv_coord_t w = pos ? (i == 12 ? pw : (pw - 9) / 4) : (pw - 6) / 3;
        lv_obj_t* k = box(pad, w, pos ? 24 : 28);
        if (pin && key == 9) continue;   // no point in a PIN: the slot stays, empty
        lv_obj_set_style_radius(k, 4, 0);
        lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(k, c(_t.bg2), 0);
        const bool word = key == 9 || key >= 11;
        lv_obj_center(label(k, word ? FontRole::Body : FontRole::Data, word ? _t.ink2 : _t.ink, kPadKeys[key]));
        lv_obj_add_flag(k, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(k, reinterpret_cast<void*>(intptr_t(i) + 1));
        lv_obj_add_event_cb(
            k,
            [](lv_event_t* e) {
              GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
              const int i = int(intptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))) - 1);
              self->setSheetCursor(i);
              self->setSheetPress(i);   // a key changes text only: safe inside its event
            },
            LV_EVENT_CLICKED, this);
        _ss_item[i] = k;
      }
      lv_obj_t* br = beside ? box(left, LV_PCT(100), LV_SIZE_CONTENT) : buttonRow();
      if (beside) {
        lv_obj_set_flex_flow(br, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(br, 4, 0);
        lv_obj_set_style_pad_top(br, 4, 0);
      }
      button(br, kPadCancel, "Cancel", _t.ink, _t.line, false);
      button(br, kPadSet, pos && _pad_stage == 0 ? "Next" : "Set", _t.sigInk, _t.sig, true);
      _ss_count = kPadSet + 1;   // the frequency and PIN pads have no ± (item 12 stays empty)
      setSheetCursor(kPadSet);
      break;
    }
    case SetSheet::Confirm: {   // Discard changes? (mockup 6); an action (mockup 15)
      if (_set_page == SetPage::Sd) {   // Epic I mockup 5: Cancel first, and the default
        SdView sd;
        _model->sdView(&sd);
        char what[176];
        if (sd.state == SdState::Working) {
          textf(what, sizeof what,
                "This erases everything on the card: %lu messages and any other files. It can't be undone.\n"
                "The newest 1,000 messages stay in flash.",
                (unsigned long)sd.on_card);
        } else {
          textf(what, sizeof what,
                "This erases everything on the card. It can't be undone.\n"
                "The newest 1,000 messages stay in flash.");
        }
        title("Format the SD card?", what);
        lv_obj_t* br = buttonRow();
        button(br, 0, "Cancel", _t.ink, _t.line, false);
        button(br, 1, "Format", _t.bad, _t.bad, false);
        _ss_count = 2;
        setSheetCursor(0);
        break;
      }
      if (_set_page == SetPage::Actions) {   // Cancel first, and the default
        const bool reboot = row == aReboot;
        if (reboot) {
          title("Reboot?", "Saves first, then restarts");
        } else {
          const NodeSettings ns = _model->nodeSettings();
          textf(v, sizeof v, "%u contacts go back to flood routing", unsigned(ns.contacts));
          title("Delete all paths?", v);
        }
        lv_obj_t* br = buttonRow();
        button(br, 0, "Cancel", _t.ink, _t.line, false);
        if (reboot) button(br, 1, "Reboot", _t.warn, _t.warn, false);
        else button(br, 1, "Delete", _t.bad, _t.bad, false);
        _ss_count = 2;
        setSheetCursor(0);
        break;
      }
      char lines[3][48];
      const size_t k = radioChanges(_radio_was, _radio_now, lines, 3);
      char sub[176] = "";
      size_t o = 0;
      for (size_t i = 0; i < k && i < 3; i++) {
        textf(sub + o, sizeof sub - o, "%s%s", i ? "\n" : "", lines[i]);
        o = strlen(sub);
      }
      if (k > 3) {
        textf(sub + o, sizeof sub - o, "\nand %u more", unsigned(k - 3));
        o = strlen(sub);
      }
      textf(sub + o, sizeof sub - o, "%s", k == 1 ? " isn't saved" : "");
      title("Discard changes?", sub);
      lv_obj_t* br = buttonRow();
      button(br, 0, "Keep editing", _t.ink, _t.line, false);
      button(br, 1, "Discard", _t.bad, _t.bad, false);
      _ss_count = 2;
      setSheetCursor(0);
      break;
    }
    default:
      break;
  }
}

void GuiShell::closeSetSheet() {
  if (_ss_obj) lv_obj_del(_ss_obj);
  _ss_obj = _ss_list = _ss_value = _ss_limit = nullptr;
  for (auto& o : _ss_item) o = nullptr;
  _ss = SetSheet::None;
}

// The cursor: the washed option (Pick, the window following it), or an
// outlined key or button.
void GuiShell::setSheetCursor(int i) {
  if (_ss == SetSheet::None || _ss_count <= 0) return;
  if (i < 0) i = 0;
  if (i >= _ss_count) i = _ss_count - 1;
  _ss_cur = i;
  if (_ss == SetSheet::Pick) {
    const int win = _ss_count < _lay.pick_rows ? _ss_count : _lay.pick_rows;
    int top = _ss_top;
    if (i < top) top = i;
    if (i >= top + win) top = i - win + 1;
    setPickTop(top);
    return;
  }
  for (int j = 0; j < _ss_count && j < int(kSheetItems); j++) {
    lv_obj_t* o = _ss_item[j];
    if (!o) continue;
    const bool hot = j == i;
    lv_obj_set_style_outline_width(o, hot ? 2 : 0, 0);
    lv_obj_set_style_outline_color(o, c(_t.ink), 0);
    lv_obj_set_style_outline_pad(o, -4, 0);
  }
}

// The picker's window from option `top`: each row's text, the chosen one's
// filled ring, and the cursor's wash.
void GuiShell::setPickTop(int top) {
  const int win = _ss_count < _lay.pick_rows ? _ss_count : _lay.pick_rows;
  if (top > _ss_count - win) top = _ss_count - win;
  if (top < 0) top = 0;
  _ss_top = top;
  char v[48];
  for (int w = 0; w < win; w++) {
    lv_obj_t* o = _ss_item[w];
    if (!o) continue;
    const int opt = top + w;
    pickText(opt, v, sizeof v);
    const bool chosen = opt == _ss_picked, hot = opt == _ss_cur;
    lv_obj_t* rd = lv_obj_get_child(o, 0);
    lv_obj_set_style_border_color(rd, c(chosen ? _t.sig : _t.line), 0);
    lv_obj_set_style_bg_opa(lv_obj_get_child(rd, 0), chosen ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_t* ol = lv_obj_get_child(o, 1);
    lv_obj_set_style_text_color(ol, c(chosen ? _t.sig : _t.ink), 0);
    lv_label_set_text(ol, v);
    pickSide(opt, v, sizeof v);
    lv_label_set_text(lv_obj_get_child(o, 2), v);
    lv_obj_set_style_bg_opa(o, hot ? lv_opa_t(_t.wash >> 24) : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, hot ? 3 : 0, 0);
  }
}

void GuiShell::setSheetStep(int dir) {
  if (_ss != SetSheet::Step) return;
  _ss_val = stepBy(_ss_val, dir, _ss_spec);
  char v[16];
  stepText(_ss_val, v, sizeof v);
  if (_ss_value) lv_label_set_text(_ss_value, v);
}

void GuiShell::stepText(int v, char* out, size_t n) {
  if (_set_page == SetPage::Display) pctText(v, out, n);
  else if (_set_page == SetPage::Power) voltsText(uint32_t(v), out, n);
  else dbmText(v, out, n);
}

void GuiShell::stepApply(int v) {
  if (_set_page == SetPage::Power) {
    _model->setFullMv(uint16_t(v));   // the stepper keeps it inside the band
  } else if (_set_page == SetPage::Display) {
    _model->guiPrefs().brightness = uint8_t(v);
    _model->prefsChanged();   // lights the screen at the new level now
  } else {
    _radio_now.tx_dbm = int8_t(v);
  }
}

// ---------------------------------------------------------------- pickers

const char* GuiShell::pickTitle(const char** sub) {
  *sub = nullptr;
  if (_set_page == SetPage::Power) return "Screen off after";
  if (_set_page == SetPage::Sensors) {
    *sub = "Temperature; pressure stays hPa";
    return "Units";
  }
  if (_set_page == SetPage::Debug) {
    *sub = "Kept for the app's log download, and on USB serial";
    return "Serial log";
  }
  if (_set_page == SetPage::Node) return "GPS interval";
  if (_set_page == SetPage::Contacts) {
    *sub = "Nodes farther away aren't added";
    return "Up to";
  }
  if (_set_page == SetPage::Privacy) {
    switch (_ss_row) {
      case pLoc: *sub = "In your adverts"; return "Share location";
      case pBase: return "Base telemetry";
      case pLocTel: return "Location telemetry";
      default: return "Environment telemetry";
    }
  }
  if (_set_page == SetPage::Msg) {
    if (_ss_row == mDefault) {
      *sub = "Each channel's own mode can still be changed";
      return "New channels default";
    }
    *sub = "This board has no notification LED";
    return "Alert with";
  }
  if (_set_page == SetPage::Display) {
    switch (_ss_row) {
      case dLook: return "Look";
      case dClock: return "Clock";
      case dZone: *sub = "Daylight saving is automatic for US and EU zones."; return "Time zone";
      case dRotation: return "Rotation";
      case dDim: return "Dim after";
      default: return "Screen off";
    }
  }
  switch (_ss_row) {   // Radio
    case rPreset: *sub = kPresetsUpdated; return "Preset";
    case rBw: *sub = kMustMatch; return "Bandwidth";
    case rSf: *sub = kMustMatch; return "Spreading factor";
    case rCr: *sub = kMustMatch; return "Coding rate";
    default: *sub = "Needs v1.14+ across the mesh"; return "Path hash mode";
  }
}

int GuiShell::pickCount() {
  if (_set_page == SetPage::Power) return int(kOffCount);
  if (_set_page == SetPage::Sensors) return 2;   // °F, °C
  if (_set_page == SetPage::Debug) return int(kLogCount);
  if (_set_page == SetPage::Node) return int(kGpsCount);
  if (_set_page == SetPage::Contacts) return int(kHopCount);
  if (_set_page == SetPage::Privacy) return _ss_row == pLoc ? 2 : 3;
  if (_set_page == SetPage::Msg) return _ss_row == mDefault ? 3 : 2;
  if (_set_page == SetPage::Display) {
    switch (_ss_row) {
      case dLook:
      case dClock: return 2;
      case dZone: return zonePickCount(_model->zone(), _ss_gz);
      case dRotation: return int(kRotationCount);
      case dDim: return int(kDimCount);
      default: return int(kOffCount);
    }
  }
  switch (_ss_row) {
    case rPreset: return int(radioPresetCount());
    case rBw: return int(kBandwidthCount);
    case rSf: return kSfMax - kSfMin + 1;
    case rCr: return kCrMax - kCrMin + 1;
    default: return 3;
  }
}

int GuiShell::pickCurrent() {
  GuiPrefs& g = _model->guiPrefs();
  if (_set_page == SetPage::Power) return choiceIndex(kOffChoices, kOffCount, _model->screenOffSecs());
  if (_set_page == SetPage::Sensors) return g.temp_f ? 0 : 1;
  if (_set_page == SetPage::Debug) {
    DeviceInfo d{};
    _model->deviceInfo(&d);
    return d.log_index;
  }
  if (_set_page == SetPage::Node || _set_page == SetPage::Contacts || _set_page == SetPage::Privacy) {
    const NodeSettings s = _model->nodeSettings();
    if (_set_page == SetPage::Node) return choiceIndex(kGpsChoices, kGpsCount, s.gps_secs);
    if (_set_page == SetPage::Contacts) return choiceIndex(kHopChoices, kHopCount, s.auto_hops);
    switch (_ss_row) {
      case pLoc: return s.share_location ? 1 : 0;
      case pBase: return s.tel_base;
      case pLocTel: return s.tel_loc;
      default: return s.tel_env;
    }
  }
  if (_set_page == SetPage::Msg) return _ss_row == mDefault ? g.notify_default : (_model->buzzerOn() ? 0 : 1);
  if (_set_page == SetPage::Display) {
    switch (_ss_row) {
      case dLook: return g.look;
      case dClock: return g.clock_12h;
      case dZone: return zonePickIndex(_model->zone(), _ss_gz);
      case dRotation: return rotationIndex(g.rotation);
      case dDim: return choiceIndex(kDimChoices, kDimCount, g.dim_secs);
      default: return choiceIndex(kOffChoices, kOffCount, _model->screenOffSecs());
    }
  }
  switch (_ss_row) {
    case rPreset: return presetFor(_radio_now, _preset_hint);
    case rBw: return choiceIndex(kBandwidthsHz, kBandwidthCount, _radio_now.bw_hz);
    case rSf: return _radio_now.sf - kSfMin;
    case rCr: return _radio_now.cr - kCrMin;
    default: return _radio_now.path_hash_mode;
  }
}

void GuiShell::pickText(int i, char* out, size_t n) {
  if (_set_page == SetPage::Power) return durationText(kOffChoices[i], out, n);
  if (_set_page == SetPage::Sensors) {
    textf(out, n, "%s", i == 0 ? "°F" : "°C");
    return;
  }
  if (_set_page == SetPage::Debug) {
    textf(out, n, "%s", logName(i));
    return;
  }
  if (_set_page == SetPage::Node) return durationText(kGpsChoices[i], out, n);
  if (_set_page == SetPage::Contacts) return hopLimitText(kHopChoices[i], out, n);
  if (_set_page == SetPage::Privacy) {
    textf(out, n, "%s", _ss_row == pLoc ? (i ? "On" : "Off") : telemetryText(uint8_t(i)));
    return;
  }
  if (_set_page == SetPage::Msg) {
    textf(out, n, "%s", _ss_row == mDefault ? kModeName[i < 3 ? i : 0] : (i == 0 ? "Buzzer" : "Silent"));
    return;
  }
  if (_set_page == SetPage::Display) {
    switch (_ss_row) {
      case dLook: textf(out, n, "%s", kLookName[i & 1]); return;
      case dClock: textf(out, n, "%s", i ? "12-hour" : "24-hour"); return;
      case dZone: zonePickText(i, _model->zone(), _ss_gz, out, n); return;
      case dRotation: textf(out, n, "%s", rotationText(kRotations[i < int(kRotationCount) ? i : 0])); return;
      case dDim: durationText(kDimChoices[i], out, n); return;
      default: durationText(kOffChoices[i], out, n); return;
    }
  }
  switch (_ss_row) {
    case rPreset: textf(out, n, "%s", radioPreset(size_t(i)).title); return;
    case rBw: bwText(kBandwidthsHz[i], out, n); return;
    case rSf: sfText(uint8_t(kSfMin + i), out, n); return;
    case rCr: crText(uint8_t(kCrMin + i), out, n); return;
    default: pathHashText(uint8_t(i), out, n); return;
  }
}

void GuiShell::pickSide(int i, char* out, size_t n) {
  if (_set_page == SetPage::Display && _ss_row == dZone) {
    zonePickSide(i, _model->zone(), _ss_gz, _model->utcNow(), out, n);
    return;
  }
  if (n) out[0] = '\0';
}

// A pick: Radio's go to the draft (Save applies them); the others apply now.
void GuiShell::pickApply(int i) {
  GuiPrefs& g = _model->guiPrefs();
  if (_set_page == SetPage::Sensors) {   // Units
    g.temp_f = i == 0 ? 1 : 0;
    _model->prefsChanged();
    return;
  }
  if (_set_page == SetPage::Power) {   // the same setting as Display › Screen off
    _model->setScreenOffSecs(uint16_t(kOffChoices[i]));
    _model->prefsChanged();
    return;
  }
  if (_set_page == SetPage::Debug) {
    _model->setLogLevel(i);
    return;
  }
  if (_set_page == SetPage::Node || _set_page == SetPage::Contacts || _set_page == SetPage::Privacy) {
    NodeSettings s = _model->nodeSettings();
    if (_set_page == SetPage::Node) {
      s.gps_secs = kGpsChoices[i];
    } else if (_set_page == SetPage::Contacts) {
      s.auto_hops = uint8_t(kHopChoices[i]);
    } else {
      switch (_ss_row) {
        case pLoc: s.share_location = i == 1; break;
        case pBase: s.tel_base = uint8_t(i); break;
        case pLocTel: s.tel_loc = uint8_t(i); break;
        default: s.tel_env = uint8_t(i); break;
      }
    }
    _model->setNodeSettings(s);
    return;
  }
  if (_set_page == SetPage::Msg) {
    if (_ss_row == mDefault) g.notify_default = uint8_t(i);
    else _model->setBuzzerOn(i == 0);
    _model->prefsChanged();
    return;
  }
  if (_set_page == SetPage::Display) {
    switch (_ss_row) {
      case dLook: g.look = uint8_t(i); break;
      case dClock: g.clock_12h = uint8_t(i); break;
      case dZone: _model->setZone(zoneAtPick(i, _model->zone(), _ss_gz)); break;
      case dRotation: g.rotation = kRotations[i < int(kRotationCount) ? i : 0]; break;   // prefsChanged turns the panel
      case dDim: g.dim_secs = uint16_t(kDimChoices[i]); break;
      default: _model->setScreenOffSecs(uint16_t(kOffChoices[i])); break;
    }
    _model->prefsChanged();
    return;
  }
  RadioValues& n = _radio_now;
  switch (_ss_row) {
    case rPreset:
      n = applyPreset(n, size_t(i), _model->txMax());
      _preset_hint = i;
      break;
    case rBw: n.bw_hz = kBandwidthsHz[i]; break;
    case rSf: n.sf = uint8_t(kSfMin + i); break;
    case rCr: n.cr = uint8_t(kCrMin + i); break;
    default: n.path_hash_mode = uint8_t(i); break;
  }
}

void GuiShell::setSheetPress(int i) {
  switch (_ss) {
    case SetSheet::Pick:
      _ss_cur = i;
      setSheetCommit();
      break;
    case SetSheet::Step:
      if (i == 0) setSheetStep(-1);
      else if (i == 1) setSheetStep(1);
      else if (i == 2) closeSetSheet();
      else setSheetCommit();
      break;
    case SetSheet::Pad:
      if (i == kPadCancel) {
        closeSetSheet();
      } else if (i == kPadSet) {
        setSheetCommit();
      } else {
        const int key = _set_page == SetPage::Node ? kPosOrder[i] : i;   // the slot's key
        if (key == 11) _ss_pad.backspace();
        else if (key == 12) _ss_pad.key('-');   // ± toggles the sign
        else _ss_pad.key(kPadKeys[key][0]);
        if (_ss_value) lv_label_set_text(_ss_value, _ss_pad.text());
        if (_ss_limit) lv_obj_set_style_text_color(_ss_limit, c(_t.dim), 0);
      }
      break;
    case SetSheet::Confirm:
      if (i == 0) {
        closeSetSheet();   // Keep editing; an action's Cancel
      } else if (_set_page == SetPage::Sd) {
        closeSetSheet();
        actionDone(kSdFormat, _model->sdFormat() ? "Formatted ✓" : "Format failed");
      } else if (_set_page == SetPage::Actions) {
        const uint8_t row = _ss_row;
        closeSetSheet();
        if (row == aReboot) {
          _model->reboot();   // doesn't return
        } else {
          const uint16_t n = _model->deleteAllPaths();
          char note[24];
          textf(note, sizeof note, "%u deleted ✓", unsigned(n));
          actionDone(row, note);
        }
      } else {
        closeSetSheet();
        _radio_now = _radio_was;   // Discard
        _set_page = SetPage::List;
        rebuild();
      }
      break;
    default:
      break;
  }
}

// The sheet's value into the draft; the page redraws with it (in amber).
void GuiShell::setSheetCommit() {
  RadioValues& n = _radio_now;
  switch (_ss) {
    case SetSheet::Pick:
      pickApply(_ss_cur);
      break;
    case SetSheet::Step:
      stepApply(_ss_val);
      break;
    case SetSheet::Pad: {
      if (_set_page == SetPage::Conn) {   // PIN: six digits, or empty for a new one each boot
        int32_t pin = 0;
        const bool empty = _ss_pad.text()[0] == '\0';
        const bool ok = empty || (_ss_pad.scaled(0, &pin) && pinOk(uint32_t(pin)));
        if (!ok || !_model->setBlePin(empty ? 0 : uint32_t(pin))) {
          if (_ss_limit) lv_obj_set_style_text_color(_ss_limit, c(_t.bad), 0);
          return;
        }
        break;
      }
      if (_set_page == SetPage::Node) {   // Position: latitude, then longitude
        int32_t e6 = 0;
        const bool ok = _ss_pad.scaled(6, &e6) && (_pad_stage == 0 ? latOk(e6) : lonOk(e6));
        if (!ok) {
          if (_ss_limit) lv_obj_set_style_text_color(_ss_limit, c(_t.bad), 0);
          return;
        }
        if (_pad_stage == 0) {
          _pad_lat = e6;
          _pad_stage = 1;
          openSetSheet(SetSheet::Pad, _ss_row);   // the same pad, for longitude
          return;
        }
        NodeSettings s = _model->nodeSettings();
        s.lat_e6 = _pad_lat;
        s.lon_e6 = e6;
        _model->setNodeSettings(s);
        break;
      }
      uint32_t khz = 0;
      if (!_ss_pad.thousandths(&khz) || !freqOk(khz)) {   // refused: the range turns red
        if (_ss_limit) lv_obj_set_style_text_color(_ss_limit, c(_t.bad), 0);
        return;
      }
      n.freq_khz = khz;
      break;
    }
    default:
      return;
  }
  closeSetSheet();
  rebuild();
}
