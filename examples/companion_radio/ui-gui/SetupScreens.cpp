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
const char* const kPadKeys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "⌫"};
constexpr int kPadCancel = 12, kPadSet = 13;

}  // namespace

// ---------------------------------------------------------------- state

void GuiShell::resetSetup() {
  _set_page = SetPage::List;
  _set_focus = SetPage::Node;
  _set_focus_row = 0xFF;
  _set_note[0] = '\0';
}

void GuiShell::setOpen(SetPage p) {
  _set_focus = p;
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
  _set_page = SetPage::List;
  rebuild();
}

void GuiShell::setNote(const char* text) {
  textf(_set_note, sizeof _set_note, "%s", text);
  _set_note_at = lv_tick_get();
  if (_set_save) lv_label_set_text(lv_obj_get_child(lv_obj_get_child(_set_save, 0), 0), _set_note);
}

void GuiShell::updateSetup(uint32_t now_ms) {
  (void)now_ms;
  if (!_set_note[0] || lv_tick_elaps(_set_note_at) < 2000) return;
  _set_note[0] = '\0';
  if (_set_save) lv_label_set_text(lv_obj_get_child(lv_obj_get_child(_set_save, 0), 0), "Save · radio restarts");
}

// ------------------------------------------------------------ building

void GuiShell::buildSetup(lv_obj_t* content) {
  SlowStep slow("setup", int(_set_page));
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  if (_set_page == SetPage::List) buildSetupList(content);
  else if (_set_page == SetPage::Radio) buildRadioPage(content);
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

// Pages that come later in this Epic (G3-G5).
void GuiShell::buildSetupLater(lv_obj_t* content) {
  setHeader(content, kPageName[size_t(_set_page) < kPages ? size_t(_set_page) : 0]);
  lv_obj_t* l = label(content, FontRole::BodySmall, _t.dim, "This page comes later in Epic G.");
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(l, 24, 0);
}

// ------------------------------------------------------------- actions

void GuiShell::setRowPressed(uint8_t id) {
  if (_set_page != SetPage::Radio) return;
  _set_focus_row = id;
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
        if (a == Action::Next) setSheetCursor((_ss_cur + 1) % _ss_count);
        else if (a == Action::Prev) setSheetCursor((_ss_cur + _ss_count - 1) % _ss_count);
        else if (a == Action::Open) setSheetPress(_ss_cur);
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
      int current = 0;
      const char* t = "";
      const char* sub = kMustMatch;
      switch (row) {
        case rPreset:
          t = "Preset";
          sub = kPresetsUpdated;
          _ss_count = int(radioPresetCount());
          current = presetFor(_radio_now, _preset_hint);
          break;
        case rBw:
          t = "Bandwidth";
          _ss_count = int(kBandwidthCount);
          for (size_t i = 0; i < kBandwidthCount; i++) {
            if (kBandwidthsHz[i] == _radio_now.bw_hz) current = int(i);
          }
          break;
        case rSf:
          t = "Spreading factor";
          _ss_count = kSfMax - kSfMin + 1;
          current = _radio_now.sf - kSfMin;
          break;
        case rCr:
          t = "Coding rate";
          _ss_count = kCrMax - kCrMin + 1;
          current = _radio_now.cr - kCrMin;
          break;
        default:   // rPath
          t = "Path hash mode";
          sub = "Needs v1.14+ across the mesh";
          _ss_count = 3;
          current = _radio_now.path_hash_mode;
          break;
      }
      if (_ss_count > int(kSheetItems)) _ss_count = int(kSheetItems);
      title(t, sub);
      _ss_list = box(sheet, LV_PCT(100), _ss_count > 6 ? 6 * 30 : _ss_count * 30);
      lv_obj_set_flex_flow(_ss_list, LV_FLEX_FLOW_COLUMN);
      if (_ss_count > 6) {
        lv_obj_add_flag(_ss_list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(_ss_list, LV_SCROLLBAR_MODE_OFF);
      }
      for (int i = 0; i < _ss_count; i++) {
        switch (row) {
          case rPreset: textf(v, sizeof v, "%s", radioPreset(size_t(i)).title); break;
          case rBw: bwText(kBandwidthsHz[i], v, sizeof v); break;
          case rSf: sfText(uint8_t(kSfMin + i), v, sizeof v); break;
          case rCr: crText(uint8_t(kCrMin + i), v, sizeof v); break;
          default: pathHashText(uint8_t(i), v, sizeof v); break;
        }
        lv_obj_t* o = box(_ss_list, LV_PCT(100), 30);
        lv_obj_set_style_pad_hor(o, 12, 0);
        lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(o, 8, 0);
        lv_obj_t* rd = box(o, 12, 12);
        lv_obj_set_style_radius(rd, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(rd, 2, 0);
        lv_obj_set_style_border_color(rd, c(i == current ? _t.sig : _t.line), 0);
        if (i == current) {
          lv_obj_t* dot = box(rd, 4, 4);
          lv_obj_center(dot);
          lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
          lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
          lv_obj_set_style_bg_color(dot, c(_t.sig), 0);
        }
        lv_obj_t* ol = label(o, FontRole::Body, i == current ? _t.sig : _t.ink, v);
        lv_obj_set_flex_grow(ol, 1);
        oneLine(ol, FontRole::Body);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(o, reinterpret_cast<void*>(intptr_t(i) + 1));
        lv_obj_add_event_cb(
            o,
            [](lv_event_t* e) {
              GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
              self->_ss_cur = int(intptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))) - 1);
              self->later([](GuiShell* g) { g->setSheetCommit(); });
            },
            LV_EVENT_CLICKED, this);
        _ss_item[i] = o;
      }
      setSheetCursor(current >= 0 ? current : 0);
      break;
    }
    case SetSheet::Step: {   // TX power (mockup 4)
      _ss_spec = StepSpec{0, _model->txMax(), 1};
      _ss_val = _radio_now.tx_dbm;
      title("TX power", "The radio chip's setting, not the antenna's output");
      lv_obj_t* st = box(sheet, LV_PCT(100), LV_SIZE_CONTENT);
      lv_obj_set_style_pad_ver(st, 8, 0);
      lv_obj_set_flex_flow(st, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(st, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(st, 14, 0);
      lv_obj_t* minus = button(st, 0, "−", _t.ink, _t.line, false);
      lv_obj_set_flex_grow(minus, 0);
      lv_obj_set_width(minus, 40);
      dbmText(_ss_val, v, sizeof v);
      _ss_value = label(st, FontRole::Title, _t.ink, v);
      lv_obj_set_style_min_width(_ss_value, 90, 0);
      lv_obj_set_style_text_align(_ss_value, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_t* plus = button(st, 1, "+", _t.ink, _t.line, false);
      lv_obj_set_flex_grow(plus, 0);
      lv_obj_set_width(plus, 40);
      textf(v, sizeof v, "0 – %d dBm · this board's maximum", int(_model->txMax()));
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
      freqText(_radio_now.freq_khz, v, sizeof v);
      _ss_pad.start(v, 3, 8);
      title("Frequency", nullptr);
      lv_obj_t* field = box(sheet, kW - 24, 32);
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
      label(field, FontRole::BodySmall, _t.dim, "MHz");
      _ss_limit = label(sheet, FontRole::Meta, _t.dim, "150.000 – 2500.000 MHz");
      lv_obj_set_width(_ss_limit, LV_PCT(100));
      lv_obj_set_style_text_align(_ss_limit, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_pad_ver(_ss_limit, 4, 0);
      lv_obj_t* pad = box(sheet, LV_PCT(100), LV_SIZE_CONTENT);
      lv_obj_set_style_pad_hor(pad, 6, 0);
      lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_set_style_pad_row(pad, 3, 0);
      lv_obj_set_style_pad_column(pad, 3, 0);
      for (int i = 0; i < 12; i++) {
        lv_obj_t* k = box(pad, (kW - 12 - 6) / 3, 28);
        lv_obj_set_style_radius(k, 4, 0);
        lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(k, c(_t.bg2), 0);
        const bool word = i == 9 || i == 11;
        lv_obj_center(label(k, word ? FontRole::Body : FontRole::Data, word ? _t.ink2 : _t.ink, kPadKeys[i]));
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
      button(br, kPadSet, "Set", _t.sigInk, _t.sig, true);
      _ss_count = 14;
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

// The cursor: the washed option (Pick), or an outlined key or button.
void GuiShell::setSheetCursor(int i) {
  if (_ss == SetSheet::None || _ss_count <= 0) return;
  if (i < 0) i = 0;
  if (i >= _ss_count) i = _ss_count - 1;
  for (int j = 0; j < _ss_count && j < int(kSheetItems); j++) {
    lv_obj_t* o = _ss_item[j];
    if (!o) continue;
    const bool hot = j == i;
    if (_ss == SetSheet::Pick) {
      lv_obj_set_style_bg_opa(o, hot ? lv_opa_t(_t.wash >> 24) : LV_OPA_TRANSP, 0);
      lv_obj_set_style_bg_color(o, c(_t.wash), 0);
      lv_obj_set_style_border_side(o, LV_BORDER_SIDE_LEFT, 0);
      lv_obj_set_style_border_width(o, hot ? 3 : 0, 0);
      lv_obj_set_style_border_color(o, c(_t.sig), 0);
    } else {
      lv_obj_set_style_outline_width(o, hot ? 2 : 0, 0);
      lv_obj_set_style_outline_color(o, c(_t.ink), 0);
      lv_obj_set_style_outline_pad(o, -4, 0);
    }
  }
  _ss_cur = i;
  if (_ss == SetSheet::Pick && _ss_item[i]) lv_obj_scroll_to_view(_ss_item[i], LV_ANIM_OFF);
}

void GuiShell::setSheetStep(int dir) {
  if (_ss != SetSheet::Step) return;
  _ss_val = stepBy(_ss_val, dir, _ss_spec);
  char v[16];
  dbmText(_ss_val, v, sizeof v);
  if (_ss_value) lv_label_set_text(_ss_value, v);
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
        if (i == 11) _ss_pad.backspace();
        else _ss_pad.key(kPadKeys[i][0]);
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
      switch (_ss_row) {
        case rPreset:
          n = applyPreset(n, size_t(_ss_cur), _model->txMax());
          _preset_hint = _ss_cur;
          break;
        case rBw: n.bw_hz = kBandwidthsHz[_ss_cur]; break;
        case rSf: n.sf = uint8_t(kSfMin + _ss_cur); break;
        case rCr: n.cr = uint8_t(kCrMin + _ss_cur); break;
        default: n.path_hash_mode = uint8_t(_ss_cur); break;
      }
      break;
    case SetSheet::Step:
      n.tx_dbm = int8_t(_ss_val);
      break;
    case SetSheet::Pad: {
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
