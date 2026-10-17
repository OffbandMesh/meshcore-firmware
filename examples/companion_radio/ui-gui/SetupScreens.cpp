// Setup (Epic G, #1431): the Settings list, its pages, and the edit sheets.
// Design of record: ObScreen.dc.html (cSetList, cSetSub) and the
// owner-approved gap mockups (epic-g-gap-mockups.html, 2026-10-17).
//
// Buttons as everywhere: USR / AUX move, USR hold opens or edits, AUX hold
// goes back. A sheet takes the buttons while it's up.

#include <string.h>

#include "Screens.h"
#include "ShellWidgets.h"

using namespace offband::gui;
using namespace shell;

namespace {

enum RowKind : uint8_t { kVal, kSel, kNav, kTog, kRo };
enum RadioRow : uint8_t { rPreset, rFreq, rBw, rSf, rCr, rTx, rRepeat, rPath, rSave };
enum MsgRow : uint8_t { mOn, mDm, mCh, mDefault, mNode, mAlert, mClear, mJump };
enum DisplayRow : uint8_t { dTheme, dLook, dClock, dRotation, dBright, dDim, dOff };
enum NodeRowId : uint8_t { nName, nKey, nGps, nPos, nGpsSecs };
enum PrivacyRow : uint8_t { pLoc, pAcks, pBase, pLocTel, pEnv };
enum ContactsRow : uint8_t { cAuto, cChat, cRpt, cRoom, cSens, cHops, cCount };
// The auto-add bits (MyMesh AUTO_ADD_*), in the rows' order.
const uint8_t kTypeBit[4] = {0x02, 0x04, 0x08, 0x10};
const char* const kTypeName[4] = {"Companions", "Repeaters", "Room servers", "Sensors"};

const char* const kModeName[] = {"All messages", "Mentions only", "Off"};   // as the notification sheet
const char* const kLookName[] = {"Modern", "Classic"};

const char* const kPageName[] = {"Node",    "Radio",   "Radio stats", "Privacy", "Contacts", "Messages & alerts",
                                 "Display", "Connectivity", "Power",  "Actions", "Debug",    "About device"};
// The list's icon tiles: the font's emoji nearest the drawn icons.
const char* const kPageIcon[] = {"👤", "📡", "📈", "🛡", "👥", "🔔", "🖥", "📶", "🔋", "⚡", "🔧", "📋"};
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
}

void GuiShell::setOpen(SetPage p) {
  if (p < SetPage::Count) _set_focus = p;   // the list row to come back to
  _set_page = p;
  _set_focus_row = 0;
  _set_note[0] = '\0';
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
  // Radio stats: live, once a second while open (no sheet on it).
  if (_tab == Tab::Setup && !_locked && _set_page == SetPage::Stats && _ss == SetSheet::None &&
      _sheet == Sheet::None && int32_t(now_ms - _stats_at) >= 0) {
    _stats_at = now_ms + 1000;
    _stats_scroll = _set_body ? lv_obj_get_scroll_y(_set_body) : 0;
    rebuild();
  }
  if (!_set_note[0] || lv_tick_elaps(_set_note_at) < 2000) return;
  _set_note[0] = '\0';
  if (_set_save) lv_label_set_text(lv_obj_get_child(lv_obj_get_child(_set_save, 0), 0), "Save · radio restarts");
}

// ------------------------------------------------------------ building

void GuiShell::buildSetup(lv_obj_t* content) {
  SlowStep slow("setup", int(_set_page));
  _set_body = nullptr;
  if (_set_page != SetPage::Stats) _stats_scroll = 0;   // a fresh visit starts at the top
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  if (_set_page == SetPage::List) buildSetupList(content);
  else if (_set_page == SetPage::Radio) buildRadioPage(content);
  else if (_set_page == SetPage::Msg) buildMsgPage(content);
  else if (_set_page == SetPage::Node) buildNodePage(content);
  else if (_set_page == SetPage::Stats) buildStatsPage(content);
  else if (_set_page == SetPage::Privacy) buildPrivacyPage(content);
  else if (_set_page == SetPage::Contacts) buildContactsPage(content);
  else if (_set_page == SetPage::Display) buildDisplayPage(content);
  else if (_set_page == SetPage::Theme) buildThemePage(content);
  else buildSetupLater(content);
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
  lv_obj_set_style_min_height(row, 34, 0);
  lv_obj_set_style_pad_hor(row, 8, 0);
  lv_obj_set_style_pad_ver(row, 3, 0);
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
  if (hint && hint[0]) {
    lv_obj_t* hl = label(kk, FontRole::Label, _t.dim, hint);
    lv_obj_set_width(hl, LV_PCT(100));
    oneLine(hl, FontRole::Label);
  }
  if (v && v[0]) {
    const uint32_t ink = changed ? _t.warn : _t.ink2;
    label(row, kind == kVal || kind == kRo ? FontRole::Meta : FontRole::BodySmall, ink, v);
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
    lv_obj_t* row = box(list, LV_PCT(100), 34);
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
    lv_obj_t* vl = label(row, FontRole::BodySmall, _t.ink2);
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

// A scrolling page body under the header.
static lv_obj_t* pageBody(lv_obj_t* content) {
  lv_obj_t* body = box(content, LV_PCT(100), 0);
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  return body;
}

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
  setRow(body, dRotation, "Rotation", "Comes with landscape (Epic H)", "Portrait 0°", kNav, false, false, true);
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
    lv_obj_t* tile = box(grid, (kW - 16 - 8) / 2, 62);
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
  lv_obj_t* h = box(scr, kW, 28);
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
  lv_obj_t* field = box(scr, kW - 16, 34);
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

// Pages that come later in this Epic (G5).
void GuiShell::buildSetupLater(lv_obj_t* content) {
  setHeader(content, kPageName[size_t(_set_page) < kPages ? size_t(_set_page) : 0]);
  lv_obj_t* l = label(content, FontRole::BodySmall, _t.dim, "This page comes later in Epic G.");
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(l, 24, 0);
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
      case dDim:
      case dOff: openSetSheet(SetSheet::Pick, id); break;
      default: break;   // Rotation: Epic H
    }
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

  _ss_obj = box(lv_layer_top(), kW, 320 - kStatusH);
  lv_obj_set_pos(_ss_obj, 0, kStatusH);
  lv_obj_set_style_bg_opa(_ss_obj, LV_OPA_50, 0);
  lv_obj_set_style_bg_color(_ss_obj, lv_color_black(), 0);
  lv_obj_add_flag(_ss_obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      _ss_obj,
      [](lv_event_t* e) {   // a tap on the backdrop closes it, as Cancel
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
          static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->closeSetSheet(); });
        }
      },
      LV_EVENT_CLICKED, this);
  const int16_t r = _look.pill_chips ? 12 : 0;
  lv_obj_t* sheet = box(_ss_obj, kW, LV_SIZE_CONTENT);
  lv_obj_align(sheet, LV_ALIGN_BOTTOM_MID, 0, r);
  lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(sheet, c(_t.bg3), 0);
  lv_obj_set_style_radius(sheet, r, 0);
  lv_obj_set_style_border_side(sheet, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(sheet, 1, 0);
  lv_obj_set_style_border_color(sheet, c(_t.line), 0);
  lv_obj_set_style_pad_bottom(sheet, r + 6, 0);
  lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);

  auto title = [&](const char* t, const char* sub) {
    lv_obj_t* tl = label(sheet, FontRole::Body, _t.ink, t);
    lv_obj_set_style_pad_left(tl, 12, 0);
    lv_obj_set_style_pad_top(tl, 10, 0);
    if (sub && sub[0]) {
      lv_obj_t* sl = label(sheet, FontRole::Label, _t.dim, sub);
      lv_obj_set_width(sl, LV_PCT(100));
      lv_label_set_long_mode(sl, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_pad_hor(sl, 12, 0);
      lv_obj_set_style_pad_bottom(sl, 6, 0);
    }
  };
  // A sheet button; i is its index in _ss_item.
  auto button = [&](lv_obj_t* parent, int i, const char* text, uint32_t ink, uint32_t border, bool fill) {
    lv_obj_t* b = box(parent, 0, 32);
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
      const int win = _ss_count < kPickRows ? _ss_count : kPickRows;
      _ss_list = box(sheet, LV_PCT(100), win * 30);
      lv_obj_set_flex_flow(_ss_list, LV_FLEX_FLOW_COLUMN);
      lv_obj_add_flag(_ss_list, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(
          _ss_list,
          [](lv_event_t* e) {   // a drag scrolls the window, a row (30 px) at a time
            GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
            const lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_PRESSED) {
              self->_ss_drag = 0;
              self->_ss_dragged = false;
            } else if (code == LV_EVENT_PRESSING) {
              lv_point_t d;
              lv_indev_get_vect(lv_indev_get_act(), &d);
              self->_ss_drag += d.y;
              while (self->_ss_drag <= -30 || self->_ss_drag >= 30) {
                const int step = self->_ss_drag < 0 ? 1 : -1;   // up shows later rows
                self->_ss_drag += step * 30;
                self->_ss_dragged = true;
                self->setPickTop(self->_ss_top + step);
              }
            }
          },
          LV_EVENT_ALL, this);
      for (int w = 0; w < win; w++) {
        lv_obj_t* o = box(_ss_list, LV_PCT(100), 30);
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
    case SetSheet::Step: {   // TX power, Brightness (mockup 4)
      const bool bright = _set_page == SetPage::Display;
      if (bright) {
        _ss_spec = StepSpec{10, 100, 10};
        _ss_val = _model->guiPrefs().brightness;
        title("Brightness", nullptr);
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
      const char* unit = "MHz";
      const char* range = "150.000 – 2500.000 MHz";
      if (pos) {
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
      lv_obj_t* field = box(sheet, kW - 24, pos ? 28 : 32);
      lv_obj_set_style_pad_hor(field, 8, 0);
      lv_obj_set_style_border_width(field, 1, 0);
      lv_obj_set_style_border_color(field, c(_t.sig), 0);
      lv_obj_set_style_bg_opa(field, lv_opa_t(_t.wash >> 24), 0);
      lv_obj_set_style_bg_color(field, c(_t.wash), 0);
      lv_obj_set_style_radius(field, _look.radius, 0);
      lv_obj_set_style_translate_x(field, 12, 0);
      lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      _ss_value = label(field, FontRole::Data, _t.ink, _ss_pad.text());
      lv_obj_t* caret = box(field, 1, 14);
      lv_obj_set_style_bg_opa(caret, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(caret, c(_t.sig), 0);
      lv_obj_t* grow = box(field, 1, 1);
      lv_obj_set_flex_grow(grow, 1);
      label(field, FontRole::BodySmall, _t.dim, unit);
      _ss_limit = label(sheet, FontRole::Meta, _t.dim, range);
      lv_obj_set_width(_ss_limit, LV_PCT(100));
      lv_obj_set_style_text_align(_ss_limit, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_pad_ver(_ss_limit, 4, 0);
      lv_obj_t* pad = box(sheet, LV_PCT(100), LV_SIZE_CONTENT);
      lv_obj_set_style_pad_hor(pad, 6, 0);
      lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_set_style_pad_row(pad, 3, 0);
      lv_obj_set_style_pad_column(pad, 3, 0);
      for (int i = 0; i < (pos ? 13 : 12); i++) {
        const int key = pos ? kPosOrder[i] : i;   // slot i shows this key
        const lv_coord_t w = pos ? (i == 12 ? kW - 12 : (kW - 12 - 9) / 4) : (kW - 12 - 6) / 3;
        lv_obj_t* k = box(pad, w, pos ? 24 : 28);
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
      lv_obj_t* br = buttonRow();
      button(br, kPadCancel, "Cancel", _t.ink, _t.line, false);
      button(br, kPadSet, pos && _pad_stage == 0 ? "Next" : "Set", _t.sigInk, _t.sig, true);
      _ss_count = kPadSet + 1;   // the frequency pad has no ± (item 12 stays empty)
      setSheetCursor(kPadSet);
      break;
    }
    case SetSheet::Confirm: {   // Discard changes? (mockup 6)
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
    const int win = _ss_count < kPickRows ? _ss_count : kPickRows;
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
  const int win = _ss_count < kPickRows ? _ss_count : kPickRows;
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
  else dbmText(v, out, n);
}

void GuiShell::stepApply(int v) {
  if (_set_page == SetPage::Display) {
    _model->guiPrefs().brightness = uint8_t(v);
    _model->prefsChanged();   // lights the screen at the new level now
  } else {
    _radio_now.tx_dbm = int8_t(v);
  }
}

// ---------------------------------------------------------------- pickers

const char* GuiShell::pickTitle(const char** sub) {
  *sub = nullptr;
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
  if (_set_page == SetPage::Node) return int(kGpsCount);
  if (_set_page == SetPage::Contacts) return int(kHopCount);
  if (_set_page == SetPage::Privacy) return _ss_row == pLoc ? 2 : 3;
  if (_set_page == SetPage::Msg) return _ss_row == mDefault ? 3 : 2;
  if (_set_page == SetPage::Display) {
    switch (_ss_row) {
      case dLook:
      case dClock: return 2;
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

// A pick: Radio's go to the draft (Save applies them); the others apply now.
void GuiShell::pickApply(int i) {
  GuiPrefs& g = _model->guiPrefs();
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
        closeSetSheet();   // Keep editing
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
