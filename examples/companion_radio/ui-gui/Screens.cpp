#include "Screens.h"

#include <string.h>

#include <MeshLog.h>
#include <helpers/gui/SignalLevel.h>
#include <helpers/gui/hw/GuiFonts.h>

using namespace offband::gui;

namespace {

constexpr int16_t kW = 240;
constexpr int16_t kStatusH = 20;
constexpr int16_t kTabH = 40;
constexpr int16_t kContentH = 320 - kStatusH - kTabH;

// The param of a CLICKED that a button (USR hold) sent rather than a touch.
const char kByButton[] = "button";

// Names a step that held the loop past 100 ms (bench, #1534: taps lost to
// a 981 ms stall during search). One line, when it happens.
struct SlowStep {
  const char* what;
  int detail;
  uint32_t t0;
  SlowStep(const char* w, int d = 0) : what(w), detail(d), t0(lv_tick_get()) {}
  ~SlowStep() {
    const uint32_t ms = lv_tick_elaps(t0);
    if (ms > 100) mesh_log_line(MLOG_BOOT, "[gui] slow %s (%d): %lums\n", what, detail, (unsigned long)ms);
  }
};

lv_color_t c(uint32_t rgb) { return lv_color_hex(rgb & 0xFFFFFF); }

lv_obj_t* box(lv_obj_t* parent, lv_coord_t w, lv_coord_t h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t* label(lv_obj_t* parent, FontRole role, uint32_t rgb, const char* text = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font(role), 0);
  lv_obj_set_style_text_color(l, c(rgb), 0);
  lv_label_set_text(l, text);
  return l;
}

// Text from the mesh, with emoji-steering characters dropped (displayText).
void userText(lv_obj_t* l, const char* s, size_t len) {
  static char buf[kMsgTextMax * 2 + 1];   // off the stack; the GUI is single-threaded
  displayText(s, len, buf, sizeof buf);
  lv_label_set_text(l, buf);
}
void userText(lv_obj_t* l, const char* s) { userText(l, s, strlen(s)); }

// A label held to one line, cut with "…". LVGL only cuts a label whose
// height is fixed; with a content height it wraps instead (bench, #1501).
void oneLine(lv_obj_t* l, FontRole role) {
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_height(l, lv_font_get_line_height(font(role)));
}

lv_obj_t* line(lv_obj_t* parent, const lv_point_t* pts, uint16_t n, uint32_t rgb) {
  lv_obj_t* l = lv_line_create(parent);
  lv_line_set_points(l, pts, n);
  lv_obj_set_style_line_width(l, 1, 0);
  lv_obj_set_style_line_color(l, c(rgb), 0);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}

// Tab icons, 16 px, after the design's stroke icons. lv_line keeps the
// pointer, so the point lists live for the program.
const lv_point_t kHome[] = {{2, 7}, {8, 2}, {14, 7}, {14, 14}, {2, 14}, {2, 7}};
const lv_point_t kHomeDoor[] = {{6, 14}, {6, 10}, {10, 10}, {10, 14}};
const lv_point_t kChat[] = {{2, 3}, {14, 3}, {14, 11}, {6, 11}, {3, 14}, {3, 11}, {2, 11}, {2, 3}};
const lv_point_t kNodes[] = {{4, 4}, {12, 5}, {7, 12}, {4, 4}};
const lv_point_t kMap[] = {{1, 3}, {5, 2}, {10, 3}, {15, 2}, {15, 13}, {10, 14}, {5, 13}, {1, 14}, {1, 3}};
const lv_point_t kMapFold1[] = {{5, 2}, {5, 13}};
const lv_point_t kMapFold2[] = {{10, 3}, {10, 14}};
// The gear: a hub ring and eight short teeth outside it.
const lv_point_t kTeeth[8][2] = {{{8, 1}, {8, 4}},   {{8, 12}, {8, 15}}, {{1, 8}, {4, 8}},   {{12, 8}, {15, 8}},
                                 {{3, 3}, {5, 5}},   {{11, 11}, {13, 13}}, {{13, 3}, {11, 5}}, {{3, 13}, {5, 11}}};
const lv_point_t kBell[] = {{3, 10}, {3, 6}, {5, 3}, {9, 3}, {11, 6}, {11, 10}, {12, 11}, {2, 11}, {3, 10}};
const lv_point_t kBellClapper[] = {{6, 13}, {8, 13}};
const lv_point_t kBellSlash[] = {{1, 1}, {13, 13}};
const lv_point_t kChevron[] = {{7, 4}, {2, 10}, {7, 16}};   // the "‹" back mark
// Empty-state icons, 34 px: one bubble (a thread), two bubbles (the list).
const lv_point_t kBubble[] = {{5, 7}, {29, 7}, {29, 22}, {14, 22}, {8, 27}, {8, 22}, {5, 22}, {5, 7}};
const lv_point_t kBubbleA[] = {{4, 9}, {20, 9}, {20, 19}, {11, 19}, {7, 22}, {7, 19}, {4, 19}, {4, 9}};
const lv_point_t kBubbleB[] = {{24, 13}, {30, 13}, {30, 23}, {27, 23}, {27, 26}, {23, 23}, {17, 23}, {17, 22}};

// The bell in the conversation header and on muted rows: bell for All, "@"
// for Mentions, bell with a slash for Off (spec v0.3 "Channel notifications").
void bellIcon(lv_obj_t* parent, NotifyMode mode, uint32_t rgb) {
  lv_obj_t* ic = box(parent, 14, 14);
  if (mode == NotifyMode::Mentions) {
    lv_obj_t* at = label(ic, FontRole::Body, rgb, "@");
    lv_obj_center(at);
    return;
  }
  line(ic, kBell, 9, rgb);
  line(ic, kBellClapper, 2, rgb);
  if (mode == NotifyMode::Off) line(ic, kBellSlash, 2, rgb);
}

const char* const kTabName[] = {"HOME", "CHAT", "NODES", "MAP", "SETUP"};
const char* const kCellName[] = {"RADIO", "HEARD", "UNREAD", "GPS", "BATTERY", "AIRTIME"};

void dot(lv_obj_t* parent, int16_t x, int16_t y, uint32_t rgb, int16_t d = 4) {
  lv_obj_t* o = box(parent, d, d);
  lv_obj_set_pos(o, x - d / 2, y - d / 2);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, c(rgb), 0);
}

void tabIcon(lv_obj_t* parent, Tab tab, uint32_t rgb) {
  lv_obj_t* ic = box(parent, 16, 16);
  switch (tab) {
    case Tab::Home:
      line(ic, kHome, 6, rgb);
      line(ic, kHomeDoor, 4, rgb);
      break;
    case Tab::Chat:
      line(ic, kChat, 8, rgb);
      break;
    case Tab::Nodes:
      line(ic, kNodes, 4, rgb);
      dot(ic, 4, 4, rgb);
      dot(ic, 12, 5, rgb);
      dot(ic, 7, 12, rgb);
      break;
    case Tab::Map:
      line(ic, kMap, 9, rgb);
      line(ic, kMapFold1, 2, rgb);
      line(ic, kMapFold2, 2, rgb);
      break;
    default: {
      for (const auto& tooth : kTeeth) line(ic, tooth, 2, rgb);
      lv_obj_t* hub = box(ic, 8, 8);
      lv_obj_center(hub);
      lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(hub, 1, 0);
      lv_obj_set_style_border_color(hub, c(rgb), 0);
      break;
    }
  }
}

}  // namespace

void GuiShell::begin(const ThemeTokens& t, Look look, ShellModel* model) {
  _model = model;
  _group = lv_group_create();
  // Swipes, on the screen object, which outlives every rebuild: up unlocks
  // Lock; right leaves Compose (spec: Back is "‹ or swipe right"). The
  // keyboard doesn't pass its own drags up, so a slide between keys isn't a
  // swipe.
  lv_obj_t* scr = lv_scr_act();
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      scr,
      [](lv_event_t* e) {
        GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
        const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (self->_locked && dir == LV_DIR_TOP) {
          self->later([](GuiShell* s) { s->showTab(s->_tab); });
        } else if (self->_composing && dir == LV_DIR_RIGHT) {
          self->later([](GuiShell* s) { s->leaveCompose(); });
        }
      },
      LV_EVENT_GESTURE, this);
  setTheme(t, look);
}

// Runs fn after the current LVGL event: a rebuild deletes the object LVGL
// is still dispatching to.
// One switch can be pending: a second tap before it runs replaces it (the
// latest choice wins) rather than scheduling a second rebuild.
void GuiShell::later(void (*fn)(GuiShell*)) {
  const bool scheduled = _pending_fn != nullptr;
  _pending_fn = fn;
  if (scheduled) return;
  lv_async_call(
      [](void* p) {
        GuiShell* self = static_cast<GuiShell*>(p);
        void (*run)(GuiShell*) = self->_pending_fn;
        self->_pending_fn = nullptr;
        if (run) run(self);
      },
      this);
}

void GuiShell::openThread(int thread) {
  _thread = thread;
  if (thread >= 0) {
    _focus_thread = thread;
    _model->threadOpened(thread);
  }
  _shown_rev = _model->store().revision();
  rebuild();
}

void GuiShell::setTheme(const ThemeTokens& t, Look look) {
  _t = t;
  _look = lookTokens(look);
  rebuild();
}

void GuiShell::showLock() {
  if (_locked) return;
  _locked = true;
  rebuild();
}

void GuiShell::showTab(Tab tab) {
  if (!_locked && tab == _tab) return;
  if (tab != _tab && tab == Tab::Chat) _thread = -1;   // the tab opens on the list
  if (tab != _tab && tab == Tab::Nodes) resetNodes();
  if (tab != _tab && tab == Tab::Map) resetMap();
  _locked = false;
  _tab = tab;
  rebuild();
}

// Coming to Map from another tab: the radar at 3 km, nothing selected
// (mockup 3), and no detail left open from an earlier visit.
void GuiShell::resetMap() {
  _detail = _login = _login_sent = false;
  _radar_step = kRadarStartStep;
  _radar_sel_set = false;
}

// Coming to Nodes from another tab: the list, the first batch (a DM from a
// detail left Nodes with the detail, or search, still set).
void GuiShell::resetNodes() {
  _node_cap = kNodeBatch;
  _detail = _login = _login_sent = false;
  _searching = _search_kb = _search_wheel = false;
}

// Focus is drawn as a 2 px signal outline, the same on every item.
void GuiShell::focusable(lv_obj_t* o) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_outline_color(o, c(_t.sig), LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(o, 2, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_pad(o, -2, LV_STATE_FOCUSED);
  lv_group_add_obj(_group, o);
}

void GuiShell::rebuild() {
  SlowStep slow("rebuild tab", _searching ? 10 + int(_tab) : _composing ? 20 + int(_tab) : int(_tab));
  lv_obj_t* scr = lv_scr_act();
  closeSheet();   // it lives on the top layer, outside the rebuilt screen
  lv_group_remove_all_objs(_group);
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, c(_t.bg), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  _name = _key = _advert = _advert_label = nullptr;
  _lock_clock = _lock_sub = _lock_list = nullptr;
  for (auto& v : _value) v = nullptr;
  for (auto& s : _sub) s = nullptr;
  _lock_count = 0xFF;
  _lock_more = 0xFFFFFFFF;

  _kb = _draft_box = _draft_label = _caret = _counter = nullptr;
  _query_label = _match_label = _search_list = _wheel_strip = nullptr;
  _login_field = _login_status = nullptr;
  for (auto& k : _key_obj) k = nullptr;
  if (_key_preview) {
    lv_obj_del(_key_preview);
    _key_preview = nullptr;
  }

  if (_locked) {
    _status.closePopover();
    buildLock(scr);
    return;
  }
  if (_composing) {   // Compose takes the whole screen below the status bar
    _status.build(scr, _t, [](void* m) { static_cast<ShellModel*>(m)->loraPopover(); }, _model);
    buildCompose(scr);
    return;
  }
  if (_login && detailTab()) {   // the login sheet, over a node's detail
    _status.build(scr, _t, [](void* m) { static_cast<ShellModel*>(m)->loraPopover(); }, _model);
    buildLogin(scr);
    return;
  }
  if (_searching && !_detail && _tab == Tab::Nodes) {   // so does Nodes search (ObScreen: no tab bar)
    _status.build(scr, _t, [](void* m) { static_cast<ShellModel*>(m)->loraPopover(); }, _model);
    buildSearch(scr);
    return;
  }
  _msg_list = nullptr;
  _focus_row = nullptr;
  _node_count = nullptr;
  _node_list = _node_clear = nullptr;
  for (auto& t : _tab_obj) t = nullptr;
  for (auto& r : _node_row_obj) r = nullptr;
  _node_shown = 0;
  if (_tab == Tab::Chat) _shown_rev = _model->store().revision();
  _status.build(scr, _t, [](void* m) { static_cast<ShellModel*>(m)->loraPopover(); }, _model);
  lv_obj_t* content = box(scr, kW, kContentH);
  lv_obj_set_pos(content, 0, kStatusH);
  if (_tab == Tab::Home) buildHome(content);
  else if (_tab == Tab::Chat && _thread >= 0) buildConversation(content);
  else if (_tab == Tab::Chat) buildChats(content);
  else if (detailTab() && _detail) buildDetail(content);   // from Nodes, or from the radar
  else if (_tab == Tab::Nodes) buildNodes(content);
  else if (_tab == Tab::Map) buildRadar(content);
  else buildPlaceholder(content);
  buildTabs(scr);

  // Back on the list, the row that was open (or focused) keeps the focus.
  if (_focus_row) {
    lv_group_focus_obj(_focus_row);
    lv_obj_scroll_to_view(_focus_row, LV_ANIM_OFF);
  }
}

void GuiShell::buildHome(lv_obj_t* content) {
  lv_obj_set_style_pad_all(content, 8, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(content, 7, 0);

  // Header: name and key on the left, the role tag on the right.
  lv_obj_t* head = box(content, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_t* who = box(head, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(who, LV_FLEX_FLOW_COLUMN);
  _name = label(who, FontRole::Title, _t.ink);
  _key = label(who, FontRole::Meta, _t.dim);
  lv_obj_t* tag = box(head, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(tag, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_border_width(tag, 1, 0);
  lv_obj_set_style_border_color(tag, c(_t.sig), 0);
  lv_obj_set_style_pad_hor(tag, 5, 0);
  lv_obj_set_style_pad_ver(tag, 2, 0);
  label(tag, FontRole::Label, _t.sig, "COMPANION");

  // The 2x3 grid: hairlines between cells, as drawn.
  lv_obj_t* grid = box(content, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_border_side(grid, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_width(grid, 1, 0);
  lv_obj_set_style_border_color(grid, c(_t.line), 0);
  for (int i = 0; i < 6; i++) {
    lv_obj_t* cell = box(grid, (kW - 16 - 1) / 2, LV_SIZE_CONTENT);
    lv_obj_set_style_border_side(cell, LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, c(_t.line), 0);
    lv_obj_set_style_pad_hor(cell, 6, 0);
    lv_obj_set_style_pad_ver(cell, 4, 0);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    label(cell, FontRole::Label, _t.dim, kCellName[i]);
    _value[i] = label(cell, FontRole::Data, _t.ink);
    _sub[i] = label(cell, FontRole::Label, _t.ink2);
    lv_obj_set_width(_sub[i], LV_PCT(100));
    oneLine(_sub[i], FontRole::Label);
  }

  // Send advert, pinned to the bottom of the content.
  lv_obj_t* spacer = box(content, 1, 1);
  lv_obj_set_flex_grow(spacer, 1);
  _advert = box(content, LV_PCT(100), 36);
  lv_obj_set_style_border_width(_advert, 1, 0);
  lv_obj_set_style_border_color(_advert, c(_t.line), 0);
  lv_obj_set_style_radius(_advert, _look.radius, 0);
  lv_obj_set_style_bg_color(_advert, c(_t.sig), 0);
  _advert_label = label(_advert, FontRole::Body, _t.sig, "Send advert");
  lv_obj_center(_advert_label);
  focusable(_advert);
  lv_obj_add_event_cb(
      _advert, [](lv_event_t* e) { static_cast<GuiShell*>(lv_event_get_user_data(e))->_model->sendAdvert(); },
      LV_EVENT_CLICKED, this);
  _advert_shown = AdvertButton::State::Idle;
}

// Tabs whose screens come in later Epics.
void GuiShell::buildPlaceholder(lv_obj_t* content) {
  static const char* const kWhen[] = {"", "", "Nodes come in Epic E.", "The map comes in Epic F.",
                                      "Setup comes in Epic G."};
  lv_obj_t* l = label(content, FontRole::Body, _t.dim, kWhen[uint8_t(_tab)]);
  lv_obj_center(l);
}

void GuiShell::buildTabs(lv_obj_t* parent) {
  lv_obj_t* bar = box(parent, kW, kTabH);
  lv_obj_set_pos(bar, 0, 320 - kTabH);
  lv_obj_set_style_bg_color(bar, c(_t.bg2), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, c(_t.line2), 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);

  lv_obj_t* active = nullptr;
  for (uint8_t i = 0; i < uint8_t(Tab::Count); i++) {
    const bool on = i == uint8_t(_tab);
    const uint32_t rgb = on ? _t.sig : _t.dim;
    lv_obj_t* t = box(bar, kW / 5, kTabH);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(t, 2, 0);
    if (on) {
      lv_obj_set_style_bg_color(t, c(_t.wash), 0);
      lv_obj_set_style_bg_opa(t, lv_opa_t(_t.wash >> 24), 0);
      lv_obj_set_style_border_side(t, LV_BORDER_SIDE_TOP, 0);
      lv_obj_set_style_border_width(t, 2, 0);
      lv_obj_set_style_border_color(t, c(_t.sig), 0);
      active = t;
    }
    tabIcon(t, Tab(i), rgb);
    label(t, FontRole::Label, rgb, kTabName[i]);
    focusable(t);
    _tab_obj[i] = t;
    lv_obj_add_event_cb(
        t,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_pending_tab = Tab(lv_obj_get_index(lv_event_get_target(e)));
          self->later([](GuiShell* s) {
            if (s->_pending_tab == Tab::Chat) s->_thread = -1;   // the tab opens on the list
            if (s->_pending_tab == Tab::Nodes && s->_tab != Tab::Nodes) s->resetNodes();
            if (s->_pending_tab == Tab::Map && s->_tab != Tab::Map) s->resetMap();
            s->_locked = false;
            s->_tab = s->_pending_tab;
            s->rebuild();
          });
        },
        LV_EVENT_CLICKED, this);
  }
  if (active) lv_group_focus_obj(active);
}

void GuiShell::buildLock(lv_obj_t* scr) {
  lv_obj_t* col = box(scr, 210, LV_SIZE_CONTENT);
  lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 4, 0);
  _lock_clock = label(col, FontRole::Clock, _t.ink, "--:--");
  _lock_sub = label(col, FontRole::BodySmall, _t.ink2);
  _lock_list = box(col, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(_lock_list, 10, 0);
  lv_obj_set_flex_flow(_lock_list, LV_FLEX_FLOW_COLUMN);

  lv_obj_t* hint = label(scr, FontRole::BodySmall, _t.dim, "Press USR or swipe up to unlock");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);

}

bool GuiShell::action(Action a) {
  if (_locked) {
    // USR unlocks (click or hold); AUX (Prev, Back) does nothing here.
    if (a == Action::Next || a == Action::Open) {
      showTab(_tab);
      return true;
    }
    return false;
  }
  // A sheet takes the buttons while it's up: USR and AUX move, USR hold
  // picks, AUX hold closes (spec: "USER moves, PRG picks", remapped).
  if (_sheet != Sheet::None) {
    switch (a) {
      case Action::Next:
        sheetHot((_sheet_hot + 1) % _sheet_count);
        return true;
      case Action::Prev:
        sheetHot((_sheet_hot + _sheet_count - 1) % _sheet_count);
        return true;
      case Action::Open:
        sheetPick(_sheet_hot);
        return true;
      case Action::Back:
        closeSheet();
        return true;
      default:
        return false;
    }
  }
  // The login sheet: a key cursor as in Compose; Back closes it.
  if (_login && detailTab()) {
    switch (a) {
      case Action::Next:
        setHotKey(_hot < 0 ? 0 : int((_hot + 1) % _nkeys), false);
        return true;
      case Action::Prev:
        setHotKey(_hot <= 0 ? int(_nkeys) - 1 : _hot - 1, false);
        return true;
      case Action::Open:
        if (_hot >= 0) pressKey(_hot);
        return true;
      case Action::Back:
        closeLogin();
        return true;
      default:
        return false;
    }
  }
  // Node detail: Back returns to the list (or the radar); the actions take
  // the focus.
  if (_detail && detailTab() && a == Action::Back) {
    closeDetail();
    return true;
  }
  // The radar (mockup 1): USR the next node, nearest first, AUX the
  // previous; USR hold opens the selected one. Back goes Home as anywhere.
  if (_tab == Tab::Map && !_detail) {
    switch (a) {
      case Action::Next:
        radarStep(1);
        return true;
      case Action::Prev:
        radarStep(-1);
        return true;
      case Action::Open:
        radarOpen();
        return true;
      default:
        break;
    }
  }
  // Nodes search. The wheel (mockup 1): USR next, AUX back one, USR hold
  // adds the letter or acts on ⌫ / Done, AUX hold leaves search. The
  // keyboard: a key cursor as in Compose. Results alone: the list's focus.
  if (_searching && _tab == Tab::Nodes) {
    if (a == Action::Back) {
      closeSearch();
      return true;
    }
    if (_search_wheel) {
      if (a == Action::Next) _wheel.next();
      else if (a == Action::Prev) _wheel.prev();
      else if (a == Action::Open) wheelPick();
      else return false;
      if (_search_wheel && _wheel_strip) buildWheel(nullptr);   // redraw the strip
      return true;
    }
    if (_search_kb) {
      switch (a) {
        case Action::Next:
          setHotKey(_hot < 0 ? 0 : int((_hot + 1) % _nkeys), false);
          return true;
        case Action::Prev:
          setHotKey(_hot <= 0 ? int(_nkeys) - 1 : _hot - 1, false);
          return true;
        case Action::Open:
          if (_hot >= 0) pressKey(_hot);
          return true;
        default:
          return false;
      }
    }
  }
  // Compose: USR and AUX walk a cursor over the keys, USR hold presses it,
  // AUX hold leaves (the draft is dropped).
  if (_composing) {
    switch (a) {
      case Action::Next:
        setHotKey(_hot < 0 ? 0 : int((_hot + 1) % _nkeys), false);
        return true;
      case Action::Prev:
        setHotKey(_hot <= 0 ? int(_nkeys) - 1 : _hot - 1, false);
        return true;
      case Action::Open:
        if (_hot >= 0) pressKey(_hot);
        return true;
      case Action::Back:
        leaveCompose();
        return true;
      default:
        return false;
    }
  }
  // In a conversation USR and AUX scroll the messages; USR hold writes;
  // Back leaves.
  if (_tab == Tab::Chat && _thread >= 0 && _msg_list) {
    switch (a) {
      case Action::Next:
        lv_obj_scroll_by_bounded(_msg_list, 0, -60, LV_ANIM_ON);
        return true;
      case Action::Prev:
        lv_obj_scroll_by_bounded(_msg_list, 0, 60, LV_ANIM_ON);
        return true;
      case Action::Open: {
        // A channel asks first: write, or its notifications (owner pick a).
        const Thread* th = _model->store().threadAt(_thread);
        if (th && ThreadKind(th->kind) == ThreadKind::Channel) openSheet(Sheet::Menu);
        else openCompose(_thread);
        return true;
      }
      case Action::Back:
        if (_status.popoverOpen()) _status.closePopover();
        else openThread(-1);
        return true;
      default:
        return false;
    }
  }
  switch (a) {
    case Action::Next:
      // Stepping past the last built node row builds the next batch first.
      if (_tab == Tab::Nodes && _node_shown && lv_group_get_focused(_group) == _node_row_obj[_node_shown - 1]) {
        appendNodeRows();
      }
      lv_group_focus_next(_group);
      if (lv_obj_t* f = lv_group_get_focused(_group)) lv_obj_scroll_to_view(f, LV_ANIM_ON);
      return true;
    case Action::Prev:
      lv_group_focus_prev(_group);
      if (lv_obj_t* f = lv_group_get_focused(_group)) lv_obj_scroll_to_view(f, LV_ANIM_ON);
      return true;
    case Action::Open: {
      lv_obj_t* f = lv_group_get_focused(_group);
      // The param marks a button press, for handlers that differ by input
      // (Nodes search: the wheel for buttons, the keyboard for touch).
      if (f) lv_event_send(f, LV_EVENT_CLICKED, const_cast<char*>(kByButton));
      return true;
    }
    case Action::Back:
      if (_status.popoverOpen()) {
        _status.closePopover();
        return true;
      }
      if (_tab != Tab::Home) {
        showTab(Tab::Home);
        return true;
      }
      return false;
    default:
      return false;
  }
}

void GuiShell::updateHome(const HomeData& d) {
  if (_locked || _tab != Tab::Home || !_name) return;
  // LVGL redraws a label only when its text actually changes.
  auto set = [](lv_obj_t* l, const char* s) {
    if (strcmp(lv_label_get_text(l), s) != 0) lv_label_set_text(l, s);
  };
  set(_name, d.name);
  set(_key, d.key);
  for (int i = 0; i < 6; i++) {
    set(_value[i], d.cells[i].value);
    set(_sub[i], d.cells[i].sub);
  }
}

void GuiShell::setAdvertState(AdvertButton::State s) {
  if (!_advert || s == _advert_shown) return;
  _advert_shown = s;
  switch (s) {
    case AdvertButton::State::Sent:
      lv_obj_set_style_bg_opa(_advert, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(_advert, c(_t.sig), 0);
      lv_obj_set_style_text_color(_advert_label, c(_t.sigInk), 0);
      lv_label_set_text(_advert_label, "Advert sent ✓");
      break;
    case AdvertButton::State::NotSent:
      lv_obj_set_style_bg_opa(_advert, LV_OPA_TRANSP, 0);
      lv_obj_set_style_text_color(_advert_label, c(_t.warn), 0);
      lv_label_set_text(_advert_label, "Not sent, try again");
      break;
    case AdvertButton::State::Idle:
      lv_obj_set_style_bg_opa(_advert, LV_OPA_TRANSP, 0);
      lv_obj_set_style_text_color(_advert_label, c(_t.sig), 0);
      lv_label_set_text(_advert_label, "Send advert");
      break;
  }
}

void GuiShell::updateLock(const LockData& d) {
  if (!_locked || !_lock_clock) return;
  if (strcmp(lv_label_get_text(_lock_clock), d.clock) != 0) lv_label_set_text(_lock_clock, d.clock);
  if (strcmp(lv_label_get_text(_lock_sub), d.sub) != 0) lv_label_set_text(_lock_sub, d.sub);

  // The list rebuilds only when what it shows changes.
  bool same = d.count == _lock_count && d.more == _lock_more;
  for (uint8_t i = 0; same && i < d.count; i++) {
    lv_obj_t* row = lv_obj_get_child(_lock_list, i);
    lv_obj_t* top = lv_obj_get_child(row, 0);
    same = strcmp(lv_label_get_text(lv_obj_get_child(top, 0)), d.items[i].label) == 0 &&
           strcmp(lv_label_get_text(lv_obj_get_child(top, 1)), d.items[i].time) == 0 &&
           strcmp(lv_label_get_text(lv_obj_get_child(row, 1)), d.items[i].text) == 0;
  }
  if (same) return;
  _lock_count = d.count;
  _lock_more = d.more;
  lv_obj_clean(_lock_list);

  if (d.count == 0) {
    lv_obj_t* none = box(_lock_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(none, 16, 0);
    lv_obj_set_flex_flow(none, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(none, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(none, 6, 0);
    lv_obj_t* bell = box(none, 14, 14);
    line(bell, kBell, 9, _t.dim);
    line(bell, kBellClapper, 2, _t.dim);
    label(none, FontRole::Body, _t.dim, "No new messages");
    return;
  }

  for (uint8_t i = 0; i < d.count; i++) {
    lv_obj_t* row = box(_lock_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(row, 6, 0);
    lv_obj_set_style_pad_hor(row, 2, 0);
    lv_obj_set_style_border_side(row, i == 0 ? LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM : LV_BORDER_SIDE_BOTTOM,
                                 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, c(_t.line), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* top = box(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_t* who = label(top, FontRole::Label, _t.sig, d.items[i].label);
    lv_obj_align(who, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* when = label(top, FontRole::Meta, _t.dim, d.items[i].time);
    lv_obj_align(when, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t* text = label(row, FontRole::BodySmall, _t.ink, d.items[i].text);
    lv_obj_set_width(text, LV_PCT(100));
    oneLine(text, FontRole::BodySmall);
  }
  if (d.more) {
    lv_obj_t* more = label(_lock_list, FontRole::BodySmall, _t.dim);
    lv_label_set_text_fmt(more, "+%lu more", (unsigned long)d.more);
    lv_obj_set_style_pad_top(more, 4, 0);
  }
}

// ------------------------------------------------- Chats and Conversation

namespace {

// A header row: 32 px with a hairline under it (ObScreen Chats/Convo).
lv_obj_t* header(lv_obj_t* parent, const ThemeTokens& t) {
  lv_obj_t* h = box(parent, LV_PCT(100), 32);
  lv_obj_set_style_pad_hor(h, 8, 0);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(h, 1, 0);
  lv_obj_set_style_border_color(h, c(t.line), 0);
  lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(h, 6, 0);
  return h;
}

// The empty states (owner-approved mockups 8a, 8b).
// One bubble (a thread), two (a list), or a ring with you at its centre (the
// radar, Epic F mockup 4).
enum class EmptyIcon : uint8_t { Bubble, Bubbles, Radar };

void emptyState(lv_obj_t* parent, const ThemeTokens& t, EmptyIcon icon, const char* title, const char* sub,
                const char* hint) {
  lv_obj_t* col = box(parent, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_center(col);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 5, 0);
  lv_obj_set_style_pad_hor(col, 22, 0);
  lv_obj_t* ic = box(col, 34, 34);
  if (icon == EmptyIcon::Bubbles) {
    line(ic, kBubbleA, 8, t.line);
    line(ic, kBubbleB, 8, t.line);
  } else if (icon == EmptyIcon::Bubble) {
    line(ic, kBubble, 8, t.line);
  } else {
    lv_obj_set_style_radius(ic, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ic, 2, 0);
    lv_obj_set_style_border_color(ic, c(t.line), 0);
    lv_obj_t* me = box(ic, 6, 6);
    lv_obj_center(me);
    lv_obj_set_style_bg_opa(me, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(me, c(t.line), 0);
  }
  label(col, FontRole::Title, t.ink2, title);
  lv_obj_t* s = label(col, FontRole::BodySmall, t.dim, sub);
  lv_obj_set_width(s, LV_PCT(100));
  lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
  if (hint) {
    box(col, 1, 3);   // with the row gap, 8 px above the hint (LVGL 8 has no margins)
    lv_obj_t* hb = box(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(hb, 1, 0);
    lv_obj_set_style_border_color(hb, c(t.line), 0);
    lv_obj_set_style_radius(hb, 3, 0);
    lv_obj_set_style_pad_hor(hb, 6, 0);
    lv_obj_set_style_pad_ver(hb, 2, 0);
    label(hb, FontRole::Meta, t.sig, hint);
  }
}

}  // namespace

void GuiShell::buildChats(lv_obj_t* content) {
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  const MsgStore& s = _model->store();

  lv_obj_t* h = header(content, _t);
  label(h, FontRole::Title, _t.ink, "Chats");
  lv_obj_t* grow = box(h, 1, 1);
  lv_obj_set_flex_grow(grow, 1);
  // "+" starts a chat: contacts live in Nodes (Epic E), so it goes there.
  lv_obj_t* plus = box(h, 24, 24);
  label(plus, FontRole::Title, _t.sig, "+");
  lv_obj_center(lv_obj_get_child(plus, 0));
  focusable(plus);
  lv_obj_add_event_cb(
      plus,
      [](lv_event_t* e) {
        GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
        self->later([](GuiShell* g) { g->showTab(Tab::Nodes); });
      },
      LV_EVENT_CLICKED, this);

  uint16_t order[64];
  const size_t n = chatOrder(s, order, 64);
  lv_obj_t* list = box(content, LV_PCT(100), 0);
  lv_obj_set_flex_grow(list, 1);
  if (n == 0) {
    emptyState(list, _t, EmptyIcon::Bubbles, "No chats yet", "Channels you join and people who message you show up here.",
               "+ to start one");
    return;
  }
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  char preview[96];
  for (size_t i = 0; i < n; i++) {
    const int t = order[i];
    const Thread* th = s.threadAt(t);
    // 44 px as drawn: 5 + 17 (name) + 1 + 15 (preview) + 5 + the hairline.
    lv_obj_t* row = box(list, LV_PCT(100), 44);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_pad_ver(row, 5, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, c(_t.line2), 0);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(intptr_t(t + 1)));
    // The two lines stack: neither can be drawn over the other, whatever
    // it holds (bench, #1501: a wrapped preview grew up over the name).
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, 1, 0);

    // Line 1: name, the muted bell, and the unread badge on the right.
    lv_obj_t* top = box(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 4, 0);
    // The name takes what the bell and badge leave, cut with "…".
    lv_obj_t* name = label(top, FontRole::Body, _t.ink);
    userText(name, th->name);
    lv_obj_set_width(name, 0);
    lv_obj_set_flex_grow(name, 1);
    oneLine(name, FontRole::Body);
    if (_model->threadMode(t) == NotifyMode::Off) bellIcon(top, NotifyMode::Off, _t.dim);
    if (th->unread) {
      lv_obj_t* badge = box(top, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
      lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(badge, c(_t.sig), 0);
      lv_obj_set_style_radius(badge, _look.pill_chips ? 8 : 0, 0);
      lv_obj_set_style_pad_hor(badge, 5, 0);
      lv_obj_t* num = label(badge, FontRole::Meta, _t.sigInk);
      lv_label_set_text_fmt(num, "%u", unsigned(th->unread));
    }

    // Line 2: the newest message.
    chatPreview(s, t, preview, sizeof preview);
    lv_obj_t* pv = label(row, FontRole::BodySmall, _t.ink2);
    userText(pv, preview);
    lv_obj_set_width(pv, LV_PCT(100));
    oneLine(pv, FontRole::BodySmall);

    focusable(row);
    if (t == _focus_thread) _focus_row = row;
    lv_obj_add_event_cb(
        row,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_pending_thread = int(intptr_t(lv_obj_get_user_data(lv_event_get_target(e)))) - 1;
          self->later([](GuiShell* g) { g->openThread(g->_pending_thread); });
        },
        LV_EVENT_CLICKED, this);
  }
}

void GuiShell::buildConversation(lv_obj_t* content) {
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  const MsgStore& s = _model->store();
  const Thread* th = s.threadAt(_thread);
  if (!th) {   // gone (evicted or deleted): back to the list
    _thread = -1;
    buildChats(content);
    return;
  }
  const bool channel = ThreadKind(th->kind) == ThreadKind::Channel;

  // Header: back, name, route, and the bell on channels.
  lv_obj_t* h = header(content, _t);
  lv_obj_t* back = box(h, 10, 20);
  line(back, kChevron, 3, _t.sig);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(back, 8);
  lv_obj_add_event_cb(
      back,
      [](lv_event_t* e) {
        static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->openThread(-1); });
      },
      LV_EVENT_CLICKED, this);
  lv_obj_t* name = label(h, FontRole::Title, _t.ink);
  userText(name, th->name);
  lv_obj_set_width(name, 0);
  lv_obj_set_flex_grow(name, 1);   // takes what the route and bell leave
  oneLine(name, FontRole::Title);
  char route[32];
  _model->routeMeta(_thread, route, sizeof route);
  label(h, FontRole::Meta, _t.dim, route);
  if (channel) {
    lv_obj_t* bell = box(h, 24, 24);
    lv_obj_set_flex_flow(bell, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    bellIcon(bell, _model->threadMode(_thread), _t.ink2);
    lv_obj_add_flag(bell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(bell, 6);
    lv_obj_add_event_cb(
        bell, [](lv_event_t* e) { static_cast<GuiShell*>(lv_event_get_user_data(e))->openSheet(Sheet::Notify); },
        LV_EVENT_CLICKED, this);
  }

  // Messages, oldest at the top, the newest resting on the compose bar.
  _msg_list = box(content, LV_PCT(100), 0);
  lv_obj_set_flex_grow(_msg_list, 1);
  lv_obj_add_flag(_msg_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(_msg_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_hor(_msg_list, 8, 0);
  lv_obj_set_style_pad_ver(_msg_list, 6, 0);
  lv_obj_set_style_pad_row(_msg_list, 7, 0);
  lv_obj_set_flex_flow(_msg_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(_msg_list, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  uint32_t seqs[40];
  const size_t n = s.threadMessages(_thread, seqs, 40);
  if (n == 0) {
    char sub[64];
    snprintf(sub, sizeof sub, "Messages with %s are kept on this device.", th->name);
    emptyState(_msg_list, _t, EmptyIcon::Bubble, "No messages yet", sub, nullptr);
  }
  const int zone = _model->zone();
  const int16_t max_w = (kW - 16) * 88 / 100;
  for (size_t i = 0; i < n; i++) {
    const MsgRecord* m = s.msg(seqs[i]);
    if (!m) continue;
    const bool own = m->own != 0;
    lv_obj_t* wrap = box(_msg_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_START, own ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          own ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(wrap, 2, 0);

    char when[8], meta[48], line_text[96];
    formatClock(zone, m->time, when, sizeof when);
    if (own) deliveryMeta(*m, channel, _model->dmAttempt(seqs[i]), kDmAttempts, meta, sizeof meta);
    else incomingMeta(m->hops, meta, sizeof meta);
    const char* from = own ? "You" : (channel || ThreadKind(th->kind) == ThreadKind::Room) ? m->sender : th->name;
    metaLine(from, when, meta, line_text, sizeof line_text);
    lv_obj_t* ml = label(wrap, FontRole::Label, _t.dim);
    userText(ml, line_text);
    if (own && Delivery(m->delivery) == Delivery::Failed) lv_obj_set_style_text_color(ml, c(_t.bad), 0);

    // The bubble. Modern: filled, rounded, with a squared corner as the tail
    // (LVGL draws one radius for all four corners). Classic: a square box.
    lv_obj_t* bub = box(wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(bub, _look.own_msg_outline ? 6 : 8, 0);
    lv_obj_set_style_pad_ver(bub, _look.own_msg_outline ? 3 : 4, 0);
    const bool failed = own && Delivery(m->delivery) == Delivery::Failed;
    uint32_t ink = own ? _t.sigInk : _t.ink;
    if (_look.own_msg_outline) {
      lv_obj_set_style_border_width(bub, 1, 0);
      lv_obj_set_style_border_color(bub, c(failed ? _t.bad : own ? _t.sig : _t.line), 0);
      if (own) {
        lv_obj_set_style_bg_opa(bub, lv_opa_t(_t.wash >> 24), 0);
        lv_obj_set_style_bg_color(bub, c(_t.wash), 0);
      }
      ink = _t.ink;
    } else if (failed) {
      lv_obj_set_style_border_width(bub, 1, 0);
      lv_obj_set_style_border_color(bub, c(_t.bad), 0);
      lv_obj_set_style_radius(bub, _look.bubble_radius, 0);
      ink = _t.ink;
    } else {
      const uint32_t fill = own ? _t.sig : _t.bg3;
      lv_obj_set_style_bg_opa(bub, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(bub, c(fill), 0);
      lv_obj_set_style_radius(bub, _look.bubble_radius, 0);
      if (_t.flat && !own) {
        lv_obj_set_style_border_width(bub, 1, 0);
        lv_obj_set_style_border_color(bub, c(_t.line), 0);
      }
      if (_look.bubble_tail) {
        lv_obj_t* tail = box(bub, _look.bubble_tail, _look.bubble_tail);
        lv_obj_add_flag(tail, LV_OBJ_FLAG_FLOATING);   // out of the bubble's size calculation
        lv_obj_set_style_bg_opa(tail, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tail, c(fill), 0);
        lv_obj_set_style_radius(tail, 2, 0);
        lv_obj_align(tail, own ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT, own ? 8 : -8, 4);
      }
    }
    if (own && Delivery(m->delivery) == Delivery::Sending) lv_obj_set_style_opa(bub, LV_OPA_60, 0);
    // A DM that wasn't delivered: hold the bubble to send it again (mockup 6).
    if (failed) {
      lv_obj_add_flag(bub, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_user_data(bub, reinterpret_cast<void*>(uintptr_t(seqs[i])));
      lv_obj_add_event_cb(
          bub,
          [](lv_event_t* e) {
            GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
            self->_model->resend(uint32_t(uintptr_t(lv_obj_get_user_data(lv_event_get_target(e)))));
          },
          LV_EVENT_LONG_PRESSED, this);
    }
    lv_obj_t* txt = label(bub, FontRole::BodySmall, ink);
    userText(txt, m->text, m->text_len);
    lv_obj_set_style_max_width(txt, max_w - 16, 0);
    lv_label_set_long_mode(txt, LV_LABEL_LONG_WRAP);
  }
  lv_obj_update_layout(_msg_list);
  // The newest message shows, unless a redraw found the reader scrolled up
  // into history (Epic D gate): then they stay where they were.
  if (_keep_scroll >= 0) lv_obj_scroll_to_y(_msg_list, _keep_scroll, LV_ANIM_OFF);
  else lv_obj_scroll_to_y(_msg_list, LV_COORD_MAX, LV_ANIM_OFF);
  _keep_scroll = -1;

  // The compose bar (Compose itself is D5).
  lv_obj_t* bar = box(content, LV_PCT(100), 42);
  lv_obj_set_style_pad_hor(bar, 6, 0);
  lv_obj_set_style_pad_ver(bar, 4, 0);
  lv_obj_set_style_pad_column(bar, 4, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, c(_t.line), 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_t* can = box(bar, 40, 34);
  lv_obj_set_style_border_width(can, 1, 0);
  lv_obj_set_style_border_color(can, c(_t.line), 0);
  lv_obj_set_style_radius(can, _look.radius, 0);
  lv_obj_center(label(can, FontRole::BodySmall, _t.sig, "Can"));
  lv_obj_t* field = box(bar, 0, 34);
  lv_obj_set_flex_grow(field, 1);
  lv_obj_set_style_border_width(field, 1, 0);
  lv_obj_set_style_border_color(field, c(_t.line), 0);
  lv_obj_set_style_radius(field, _look.radius, 0);
  lv_obj_set_style_pad_hor(field, 8, 0);
  lv_obj_t* hint = label(field, FontRole::BodySmall, _t.dim, "Tap to type…");
  lv_obj_align(hint, LV_ALIGN_LEFT_MID, 0, 0);
  // Tapping the field, or Can (whose chips sit above the keys), opens Compose.
  for (lv_obj_t* o : {field, can}) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        o,
        [](lv_event_t* e) {
          static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->openCompose(g->_thread); });
        },
        LV_EVENT_CLICKED, this);
  }
}

// ---------------------------------------------------------------- Compose

namespace {
const char* const kDefaultCanned[4] = {"On my way", "Copy that", "Check-in OK", "Need help"};
constexpr int16_t kKbTop = 320 - 4 * kKeyH - 3 * kKeyRowGap - 4;   // keyboard area top
}  // namespace

void GuiShell::openCompose(int thread) {
  if (thread < 0 || !_model->store().threadAt(thread)) return;
  _thread = thread;
  _composing = true;
  _draft.start(_model->composeBudget(thread));
  _page.select(KeyPage::Letters);
  _shift = ShiftState();
  _shift.autoCap("", 0);
  _hot = -1;
  rebuild();
}

void GuiShell::leaveCompose() {
  _composing = false;
  _shown_rev = _model->store().revision();
  rebuild();
}

void GuiShell::buildCompose(lv_obj_t* scr) {
  const Thread* th = _model->store().threadAt(_thread);
  // Header: "To <name>" and the byte counter.
  lv_obj_t* h = box(scr, kW, 28);
  lv_obj_set_pos(h, 0, kStatusH);
  lv_obj_set_style_pad_hor(h, 8, 0);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(h, 1, 0);
  lv_obj_set_style_border_color(h, c(_t.line2), 0);
  lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(h, 4, 0);
  // Back, as the spec gives it ("‹ or swipe right"): the bench found no way
  // out of Compose by touch (#1501).
  lv_obj_t* back = box(h, 10, 20);
  line(back, kChevron, 3, _t.sig);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(back, 10);
  lv_obj_add_event_cb(
      back,
      [](lv_event_t* e) {
        static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->leaveCompose(); });
      },
      LV_EVENT_CLICKED, this);
  label(h, FontRole::Body, _t.dim, "To");
  lv_obj_t* to = label(h, FontRole::Body, _t.ink);
  userText(to, th ? th->name : "");
  lv_obj_set_width(to, 0);
  lv_obj_set_flex_grow(to, 1);
  oneLine(to, FontRole::Body);
  _counter = label(h, FontRole::Meta, _t.dim);

  // The draft: a signal-bordered box on the wash, scrolling as it fills.
  _draft_box = box(scr, kW - 16, 48);
  lv_obj_set_pos(_draft_box, 8, kStatusH + 28 + 6);
  lv_obj_set_style_border_width(_draft_box, 1, 0);
  lv_obj_set_style_border_color(_draft_box, c(_t.sig), 0);
  lv_obj_set_style_bg_opa(_draft_box, lv_opa_t(_t.wash >> 24), 0);
  lv_obj_set_style_bg_color(_draft_box, c(_t.wash), 0);
  lv_obj_set_style_radius(_draft_box, _look.radius, 0);
  lv_obj_set_style_pad_hor(_draft_box, 7, 0);
  lv_obj_set_style_pad_ver(_draft_box, 5, 0);
  lv_obj_add_flag(_draft_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(_draft_box, LV_SCROLLBAR_MODE_OFF);
  _draft_label = label(_draft_box, FontRole::Body, _t.ink);
  lv_obj_set_width(_draft_label, LV_PCT(100));
  lv_label_set_long_mode(_draft_label, LV_LABEL_LONG_WRAP);
  _caret = box(_draft_box, 1, lv_font_get_line_height(font(FontRole::Body)) - 2);
  lv_obj_add_flag(_caret, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_style_bg_opa(_caret, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(_caret, c(_t.sig), 0);

  // Canned chips (the "Can" list; the design's four until Setup edits them).
  lv_obj_t* chips = box(scr, kW - 16, 24);
  lv_obj_set_pos(chips, 8, kStatusH + 28 + 6 + 48 + 6);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(chips, 4, 0);
  for (int i = 0; i < 4; i++) {
    const char* text = _model->cannedText(i);
    if (!text || !text[0]) text = kDefaultCanned[i];
    lv_obj_t* chip = box(chips, LV_SIZE_CONTENT, 24);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, c(_t.line), 0);
    lv_obj_set_style_radius(chip, _look.pill_chips ? 12 : 0, 0);
    lv_obj_set_style_pad_hor(chip, 6, 0);
    lv_obj_t* cl = label(chip, FontRole::BodySmall, _t.ink2);
    userText(cl, text);
    lv_obj_center(cl);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(chip, const_cast<char*>(text));
    lv_obj_add_event_cb(
        chip,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->insertText(static_cast<const char*>(lv_obj_get_user_data(lv_event_get_target(e))));
        },
        LV_EVENT_CLICKED, this);
  }

  buildKeyboard(scr);
  refreshDraft();
}

// The keyboard, for Compose and for Nodes search: one area takes every touch
// and snaps it to the nearest key, so a tap in a gap still lands (mockup 7a).
void GuiShell::buildKeyboard(lv_obj_t* scr) {
  _kb = box(scr, kW, 320 - kKbTop);
  lv_obj_set_pos(_kb, 0, kKbTop - 2);
  lv_obj_set_style_bg_opa(_kb, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(_kb, c(_t.bg2), 0);
  lv_obj_set_style_border_side(_kb, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(_kb, 1, 0);
  lv_obj_set_style_border_color(_kb, c(_t.line2), 0);
  lv_obj_add_flag(_kb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(_kb, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(
      _kb,
      [](lv_event_t* e) {
        GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
        const lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
          lv_point_t p;
          lv_indev_get_point(lv_indev_get_act(), &p);
          self->setHotKey(nearestKey(self->_keys, self->_nkeys, p.x, p.y), true);
        } else if (code == LV_EVENT_RELEASED) {
          const int k = self->_hot;
          self->setHotKey(-1, false);
          if (k >= 0) self->pressKey(k);
        } else if (code == LV_EVENT_PRESS_LOST) {
          self->setHotKey(-1, false);
        }
      },
      LV_EVENT_ALL, this);
  buildKeys();
}

// The keys of the current page, labeled for the shift state.
void GuiShell::buildKeys() {
  if (!_kb) return;
  SlowStep slow("keys page", int(_page.page()));
  if (_key_preview) {   // it belongs to a key that's about to go
    lv_obj_del(_key_preview);
    _key_preview = nullptr;
  }
  lv_obj_clean(_kb);
  for (auto& k : _key_obj) k = nullptr;
  _nkeys = pageLayout(_page.page(), kW, kKbTop, _keys, 40);
  const bool caps = _page.page() == KeyPage::Letters && _shift.state() != Shift::Off;
  for (size_t i = 0; i < _nkeys; i++) {
    const KeyRect& k = _keys[i];
    lv_obj_t* o = box(_kb, k.w, k.h);
    lv_obj_set_pos(o, k.x, k.y - (kKbTop - 2));
    lv_obj_set_style_radius(o, 4, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, c(_t.bg3), 0);
    const bool send = strcmp(k.label, "Send") == 0;
    const bool shift = strcmp(k.label, "⇧") == 0;
    uint32_t ink = _t.ink;
    if (send) {
      lv_obj_set_style_bg_color(o, c(_t.sig), 0);
      ink = _t.sigInk;
    } else if (shift && _shift.state() == Shift::Once) {
      lv_obj_set_style_bg_color(o, c(_t.ink2), 0);
      ink = _t.bg;
    } else if (shift && _shift.state() == Shift::Lock) {
      lv_obj_set_style_bg_color(o, c(_t.sig), 0);
      lv_obj_set_style_border_side(o, LV_BORDER_SIDE_BOTTOM, 0);
      lv_obj_set_style_border_width(o, 3, 0);
      lv_obj_set_style_border_color(o, c(_t.sigInk), 0);
      ink = _t.sigInk;
    } else if (!typesLabel(k.label)) {
      ink = _t.ink2;
    }
    char text[8];
    // Search's keyboard ends its entry with Done (ObScreen kbRowsFind).
    snprintf(text, sizeof text, "%s", send && _login ? "Log in" : send && _searching ? "Done" : k.label);
    if (caps && text[0] >= 'a' && text[0] <= 'z' && !text[1]) text[0] = char(text[0] - 'a' + 'A');
    lv_obj_t* l = label(o, typesLabel(k.label) ? FontRole::Body : FontRole::BodySmall, ink, text);
    lv_obj_center(l);
    _key_obj[i] = o;
  }
  if (_hot >= int(_nkeys)) _hot = -1;
  if (_hot >= 0) setHotKey(_hot, false);
}

// Highlights key i (-1: none). With `preview`, a finger is down: the key's
// character pops up above it (mockup 7a).
void GuiShell::setHotKey(int i, bool preview) {
  if (_hot >= 0 && _hot < int(_nkeys) && _key_obj[_hot]) {
    lv_obj_set_style_outline_width(_key_obj[_hot], 0, 0);
  }
  _hot = i;
  if (_key_preview) {
    lv_obj_del(_key_preview);
    _key_preview = nullptr;
  }
  if (i < 0 || !_key_obj[i]) return;
  lv_obj_set_style_outline_width(_key_obj[i], 1, 0);
  lv_obj_set_style_outline_color(_key_obj[i], c(_t.sig), 0);
  if (!preview || !typesLabel(_keys[i].label)) return;
  const KeyRect& k = _keys[i];
  _key_preview = box(lv_layer_top(), 36, 46);
  lv_obj_set_pos(_key_preview, k.x + k.w / 2 - 18, k.y - 50);
  lv_obj_set_style_bg_opa(_key_preview, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(_key_preview, c(_t.bg3), 0);
  lv_obj_set_style_border_width(_key_preview, 1, 0);
  lv_obj_set_style_border_color(_key_preview, c(_t.sig), 0);
  lv_obj_set_style_radius(_key_preview, 6, 0);
  char text[8];
  snprintf(text, sizeof text, "%s", k.label);
  if (_page.page() == KeyPage::Letters && _shift.state() != Shift::Off && text[0] >= 'a' && text[0] <= 'z') {
    text[0] = char(text[0] - 'a' + 'A');
  }
  lv_obj_center(label(_key_preview, FontRole::Title, _t.ink, text));
}

void GuiShell::pressKey(int i) {
  if (i < 0 || i >= int(_nkeys)) return;
  SlowStep slow("key", i);
  const char* l = _keys[i].label;
  const KeyPage page_before = _page.page();
  const Shift shift_before = _shift.state();
  // Search types into the query: no capitals of its own (matching ignores
  // case), and Done puts the keyboard away, keeping the results.
  ComposeBuffer& buf = _login ? _login_pw : _searching ? _query : _draft;
  const bool plain = _login || _searching;   // no capitals of their own

  if (_login && strcmp(l, "Send") == 0) {   // "Log in"
    _login_ok_at = 0;
    _login_sent = _model->startNodeAct(_detail_key, NodeAct::Login, _login_pw.text());
    _login_at = lv_tick_get();
    if (!_login_sent && _login_status) lv_label_set_text(_login_status, "Not sent");
    return;
  }
  if (_searching && strcmp(l, "Send") == 0) {
    _search_kb = false;
    later([](GuiShell* g) { g->rebuild(); });
    return;
  }
  if (strcmp(l, "Send") == 0) {
    if (_draft.length() == 0) return;
    if (_model->sendText(_thread, _draft.text(), _draft.length())) {
      // Leave after this event: the rebuild deletes the keyboard taking it.
      later([](GuiShell* g) { g->leaveCompose(); });
      return;
    }
    _not_sent_at = lv_tick_get();   // the counter reads "Not sent" for 2 s
    refreshDraft();
    return;
  }
  if (strcmp(l, "⇧") == 0) {
    _shift.tap(lv_tick_get());
  } else if (strcmp(l, "⌫") == 0) {
    buf.backspace();
    if (!plain) _shift.autoCap(buf.text(), buf.length());
  } else if (strcmp(l, "?123") == 0) {
    _page.select(KeyPage::Symbols);
  } else if (strcmp(l, "=\\<") == 0) {
    _page.select(KeyPage::Symbols2);
  } else if (strcmp(l, "ABC") == 0) {
    _page.select(KeyPage::Letters);
  } else {
    char ch[8];
    snprintf(ch, sizeof ch, "%s", strcmp(l, "space") == 0 ? " " : l);
    if (page_before == KeyPage::Letters && shift_before != Shift::Off && ch[0] >= 'a' && ch[0] <= 'z') {
      ch[0] = char(ch[0] - 'a' + 'A');
    }
    if (buf.insert(ch, strlen(ch))) {
      _shift.typedLetter();
      if (!plain) _shift.autoCap(buf.text(), buf.length());
      _page.typed(ch[0]);
    }
  }
  if (_page.page() != page_before || _shift.state() != shift_before) buildKeys();
  if (_login) refreshLoginField();
  else if (_searching) querySoon();
  else refreshDraft();
}

// A canned chip: its text goes in after what's typed, a space between.
void GuiShell::insertText(const char* text) {
  if (!text) return;
  if (_draft.length() && _draft.text()[_draft.length() - 1] != ' ') _draft.insert(" ", 1);
  for (const char* p = text; *p;) {
    const uint8_t b = uint8_t(*p);
    const size_t k = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
    if (!_draft.insert(p, k)) break;
    p += k;
  }
  const Shift before = _shift.state();
  _shift.autoCap(_draft.text(), _draft.length());
  if (_shift.state() != before) buildKeys();
  refreshDraft();
}

void GuiShell::refreshDraft() {
  if (!_draft_label) return;
  userText(_draft_label, _draft.text());
  char buf[24];
  const bool not_sent = _not_sent_at && lv_tick_elaps(_not_sent_at) < 2000;
  if (not_sent) snprintf(buf, sizeof buf, "Not sent");
  else _draft.counter(buf, sizeof buf);
  lv_label_set_text(_counter, buf);
  lv_obj_set_style_text_color(
      _counter, c(not_sent ? _t.warn : _draft.counterTone() == Tone::Warn ? _t.warn : _t.dim), 0);
  // The caret sits after the last character; the box keeps it in view.
  lv_obj_update_layout(_draft_box);
  lv_point_t p;
  lv_label_get_letter_pos(_draft_label, _lv_txt_get_encoded_length(lv_label_get_text(_draft_label)), &p);
  lv_obj_set_pos(_caret, p.x + 1, p.y + 1);
  lv_obj_scroll_to_view(_caret, LV_ANIM_OFF);
}

void GuiShell::updateChats() {
  if (_locked || _tab != Tab::Chat) return;
  const MsgStore& s = _model->store();
  if (s.revision() == _shown_rev) return;
  if (_composing) {   // the draft isn't disturbed; the thread redraws on leaving
    _shown_rev = s.revision();
    return;
  }
  // A message that lands in the open conversation is read as it arrives.
  if (_thread >= 0) {
    const Thread* th = s.threadAt(_thread);
    if (th && th->unread) _model->threadOpened(_thread);
    // More than a few pixels above the bottom means reading history.
    if (_msg_list && lv_obj_get_scroll_bottom(_msg_list) > 8) _keep_scroll = lv_obj_get_scroll_y(_msg_list);
  } else if (lv_obj_t* f = lv_group_get_focused(_group)) {
    // Keep the focused row focused across the redraw. Rows carry thread + 1,
    // so 0 (tabs, "+") means "not a row".
    if (const intptr_t ud = intptr_t(lv_obj_get_user_data(f))) _focus_thread = int(ud - 1);
  }
  _shown_rev = s.revision();
  rebuild();
}

// ----------------------------------------------------------- bottom sheets

namespace {
const char* const kModeTitle[3] = {"All messages", "Mentions only", "Off"};
}  // namespace

// A bottom sheet over the conversation, as drawn (ObScreen ntSheet): a dim
// backdrop below the status bar, the sheet on bg3 with the Look's top
// corners, a title, and 38 px rows; the chosen row in signal on the wash
// with a ✓.
void GuiShell::openSheet(Sheet kind) {
  closeSheet();
  const Thread* th = _model->store().threadAt(_thread);
  if (!th) return;
  _sheet = kind;
  const NotifyMode mode = _model->threadMode(_thread);

  _sheet_obj = box(lv_layer_top(), kW, 320 - kStatusH);
  lv_obj_set_pos(_sheet_obj, 0, kStatusH);
  lv_obj_set_style_bg_opa(_sheet_obj, LV_OPA_50, 0);
  lv_obj_set_style_bg_color(_sheet_obj, lv_color_black(), 0);
  lv_obj_add_flag(_sheet_obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      _sheet_obj,
      [](lv_event_t* e) {
        // A tap on the backdrop (not a row) closes it.
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
          static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->closeSheet(); });
        }
      },
      LV_EVENT_CLICKED, this);

  // Modern: the radius rounds the top corners; the sheet runs past the
  // screen's bottom by that much, so its bottom corners stay square.
  const int16_t r = _look.pill_chips ? 12 : 0;
  lv_obj_t* sheet = box(_sheet_obj, kW, LV_SIZE_CONTENT);
  lv_obj_align(sheet, LV_ALIGN_BOTTOM_MID, 0, r);
  lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(sheet, c(_t.bg3), 0);
  lv_obj_set_style_radius(sheet, r, 0);
  lv_obj_set_style_border_side(sheet, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(sheet, 1, 0);
  lv_obj_set_style_border_color(sheet, c(_t.line), 0);
  lv_obj_set_style_pad_bottom(sheet, r + 6, 0);
  lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);   // a tap on the title isn't a backdrop tap

  char title[64], raw[64];
  snprintf(raw, sizeof raw, "%s%s", kind == Sheet::Notify ? "Notifications · " : "", th->name);
  displayText(raw, strlen(raw), title, sizeof title);
  lv_obj_t* tl = label(sheet, FontRole::Body, _t.ink, title);
  lv_obj_set_style_pad_left(tl, 12, 0);
  lv_obj_set_style_pad_top(tl, 10, 0);
  lv_obj_set_style_pad_bottom(tl, 6, 0);
  lv_obj_set_width(tl, LV_PCT(100));
  oneLine(tl, FontRole::Body);
  lv_obj_set_height(tl, lv_font_get_line_height(font(FontRole::Body)) + 16);

  // The design's "Only @<name> and replies" promised replies, which MeshCore
  // can't tell apart (owner pick a, 2026-10-16).
  const char* mention = "Only when you're @mentioned";
  _sheet_count = kind == Sheet::Notify ? 3 : 2;
  for (uint8_t i = 0; i < _sheet_count; i++) {
    const bool chosen = kind == Sheet::Notify && i == uint8_t(mode);
    lv_obj_t* row = box(sheet, LV_PCT(100), 38);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 9, 0);
    if (chosen) {
      lv_obj_set_style_bg_opa(row, lv_opa_t(_t.wash >> 24), 0);
      lv_obj_set_style_bg_color(row, c(_t.wash), 0);
    }
    const uint32_t ink = chosen ? _t.sig : _t.ink;
    const char* name;
    const char* sub;
    if (kind == Sheet::Notify) {
      bellIcon(row, NotifyMode(i), ink);
      name = kModeTitle[i];
      sub = i == 0 ? "Alert on every message" : i == 1 ? mention : "Unread count only, no alert";
    } else {
      name = i == 0 ? "Write message" : "Notifications";
      sub = i == 0 ? "Open the keyboard" : kModeTitle[uint8_t(mode)];
    }
    lv_obj_t* col = box(row, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* nl = label(col, FontRole::Body, ink, name);
    lv_obj_set_width(nl, LV_PCT(100));
    oneLine(nl, FontRole::Body);
    lv_obj_t* sl = label(col, FontRole::Label, _t.dim);
    userText(sl, sub);
    lv_obj_set_width(sl, LV_PCT(100));
    oneLine(sl, FontRole::Label);
    if (chosen) label(row, FontRole::Body, _t.sig, "✓");

    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(intptr_t(i)));
    lv_obj_add_event_cb(
        row,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_sheet_hot = int(intptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))));
          self->later([](GuiShell* g) { g->sheetPick(g->_sheet_hot); });
        },
        LV_EVENT_CLICKED, this);
    _sheet_rows[i] = row;
  }
  sheetHot(kind == Sheet::Notify ? int(mode) : 0);
}

void GuiShell::closeSheet() {
  if (!_sheet_obj) return;
  lv_obj_del(_sheet_obj);
  _sheet_obj = nullptr;
  for (auto& r : _sheet_rows) r = nullptr;
  _sheet = Sheet::None;
}

// The button cursor: a signal outline on the row it rests on.
void GuiShell::sheetHot(int i) {
  for (uint8_t k = 0; k < _sheet_count; k++) {
    if (_sheet_rows[k]) lv_obj_set_style_outline_width(_sheet_rows[k], k == i ? 2 : 0, 0);
    if (_sheet_rows[k]) lv_obj_set_style_outline_pad(_sheet_rows[k], -2, 0);
    if (_sheet_rows[k]) lv_obj_set_style_outline_color(_sheet_rows[k], c(_t.sig), 0);
  }
  _sheet_hot = i;
}

void GuiShell::sheetPick(int i) {
  const Sheet kind = _sheet;
  if (kind == Sheet::Notify) {
    _model->setThreadMode(_thread, NotifyMode(i));
    rebuild();   // closes the sheet; the header bell shows the new mode
  } else if (kind == Sheet::Menu) {
    if (i == 0) openCompose(_thread);   // rebuild closes the sheet
    else openSheet(Sheet::Notify);
  }
}

// ------------------------------------------------------------------ Nodes

namespace {

const char* const kChipLabel[5] = {"All", "Chat", "Rpt", "Room", "Sens"};

// The type tile's glyph: Noto Emoji in the text fonts, one colour, so it
// takes the type's tint like the design's stroke icons.
const char* kindGlyph(NodeKind k) {
  switch (k) {
    case NodeKind::Chat: return "\xF0\x9F\x91\xA4";       // 👤
    case NodeKind::Repeater: return "\xF0\x9F\x93\xA1";   // 📡
    case NodeKind::Room: return "\xF0\x9F\x91\xA5";       // 👥
    case NodeKind::Sensor: return "\xF0\x9F\x8C\xA1";     // 🌡
    default: return "?";
  }
}

uint32_t kindTint(const ThemeTokens& t, NodeKind k) {
  switch (k) {
    case NodeKind::Chat: return t.nodeType[NodeCompanion];
    case NodeKind::Repeater: return t.nodeType[NodeRepeater];
    case NodeKind::Room: return t.nodeType[NodeRoom];
    case NodeKind::Sensor: return t.nodeType[NodeSensor];
    default: return t.dim;
  }
}

// The row's four signal bars (ObScreen: 11x9, heights 3/6/9/12 of 12).
void rowBars(lv_obj_t* parent, lv_obj_t** out) {
  lv_obj_t* bars = box(parent, 11, 9);
  lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(bars, 1, 0);
  static const int16_t kH[4] = {2, 4, 7, 9};
  for (int i = 0; i < 4; i++) {
    out[i] = box(bars, 2, kH[i]);
    lv_obj_set_style_bg_opa(out[i], LV_OPA_COVER, 0);
  }
}

// Bars by margin over the radio's demodulation floor, as the status bar.
void colorBars(lv_obj_t** bar, bool known, float snr, uint8_t sf, const ThemeTokens& t) {
  const uint8_t n = known ? signalBars(snr, sf) : 0;
  const SignalRole role = signalRole(n);
  const uint32_t lit = role == SignalRole::Ok ? t.ok : role == SignalRole::Warn ? t.warn : t.bad;
  for (int i = 0; i < 4; i++) lv_obj_set_style_bg_color(bar[i], c(i < n ? lit : t.line), 0);
}

}  // namespace

// One node row (ObScreen nodes): 38 px, the type tile, name and "2 hops ·
// 4m", the unread badge, bars and SNR, a chevron. Children, in order: tile,
// text (name, sub), [badge], bars, SNR, chevron; updateNodes relies on it.
lv_obj_t* GuiShell::nodeRow(lv_obj_t* list, const NodeRow& r) {
  char buf[32];
  lv_obj_t* row = box(list, LV_PCT(100), 38);
  lv_obj_set_style_pad_hor(row, 8, 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, c(_t.line2), 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, 0);

  // Type tile: 14% of the tint behind the tinted glyph; flat themes outline.
  const uint32_t tint = kindTint(_t, r.kind);
  lv_obj_t* tile = box(row, 22, 22);
  lv_obj_set_style_radius(tile, r.kind == NodeKind::Chat && _look.pill_chips ? LV_RADIUS_CIRCLE
                                : _look.pill_chips                           ? 6
                                                                             : 0,
                          0);
  if (_t.flat) {
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, c(tint), 0);
  } else {
    lv_obj_set_style_bg_opa(tile, 36, 0);
    lv_obj_set_style_bg_color(tile, c(tint), 0);
  }
  lv_obj_center(label(tile, FontRole::BodySmall, tint, kindGlyph(r.kind)));

  lv_obj_t* tx = box(row, 0, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(tx, 1);
  lv_obj_set_flex_flow(tx, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* nm = label(tx, FontRole::Body, _t.ink);
  userText(nm, r.name);
  lv_obj_set_width(nm, LV_PCT(100));
  oneLine(nm, FontRole::Body);
  rowSub(r, buf, sizeof buf);
  lv_obj_t* sub = label(tx, FontRole::Meta, _t.dim, buf);
  lv_obj_set_width(sub, LV_PCT(100));
  oneLine(sub, FontRole::Meta);

  if (r.unread) {
    lv_obj_t* badge = box(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, c(_t.sig), 0);
    lv_obj_set_style_radius(badge, _look.pill_chips ? 8 : 0, 0);
    lv_obj_set_style_pad_hor(badge, 5, 0);
    badgeText(r.unread, buf, sizeof buf);
    label(badge, FontRole::Meta, _t.sigInk, buf);
  }
  lv_obj_t* bars[4];
  rowBars(row, bars);
  colorBars(bars, r.snr_known, r.snr_db, _model->radioSf(), _t);
  if (r.snr_known) rowSnr(r.snr_db, buf, sizeof buf);
  else buf[0] = '\0';
  label(row, FontRole::Meta, _t.ink2, buf);
  lv_obj_t* chev = box(row, 6, 12);
  static const lv_point_t kNext[] = {{1, 1}, {5, 6}, {1, 11}};
  line(chev, kNext, 3, _t.dim);
  return row;
}

void GuiShell::buildNodes(lv_obj_t* content) {
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  size_t total = 0;
  const NodeRow* rows = _model->nodeRows(&total);
  const bool filtered = _node_filter != TypeFilter::All || _node_unread;
  if (_node_cap > kNodeRowsShown) _node_cap = kNodeRowsShown;
  _node_shown = nodeOrder(rows, total, _node_filter, _node_unread, "", _node_order, _node_cap);
  size_t matches = 0;
  for (size_t i = 0; i < total; i++) matches += passesFilter(rows[i], _node_filter, _node_unread);

  // Header: title, count, and the search pill (search itself is E3).
  lv_obj_t* h = box(content, LV_PCT(100), 32);
  lv_obj_set_style_pad_left(h, 8, 0);
  lv_obj_set_style_pad_right(h, 6, 0);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(h, 1, 0);
  lv_obj_set_style_border_color(h, c(_t.line), 0);
  lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(h, 6, 0);
  label(h, FontRole::Title, _t.ink, "Nodes");
  char count[24];
  nodeCount(matches, total, filtered, count, sizeof count);
  _node_count = label(h, FontRole::Meta, _t.dim, count);
  lv_obj_t* grow = box(h, 1, 1);
  lv_obj_set_flex_grow(grow, 1);
  lv_obj_t* search = box(h, 86, 26);
  lv_obj_set_style_border_width(search, 1, 0);
  lv_obj_set_style_border_color(search, c(_t.line), 0);
  lv_obj_set_style_radius(search, _look.pill_chips ? 13 : 0, 0);
  lv_obj_set_style_pad_hor(search, 7, 0);
  lv_obj_set_flex_flow(search, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(search, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(search, 5, 0);
  label(search, FontRole::BodySmall, _t.dim, "\xF0\x9F\x94\x8D");   // 🔍
  label(search, FontRole::BodySmall, _t.dim, "Search");
  focusable(search);
  // A touch opens the keyboard; a USR hold opens the letter wheel (spec:
  // "Button-only: … a letter wheel").
  lv_obj_add_event_cb(
      search,
      [](lv_event_t* e) {
        GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
        if (lv_event_get_param(e) == kByButton) self->later([](GuiShell* g) { g->openSearch(true); });
        else self->later([](GuiShell* g) { g->openSearch(false); });
      },
      LV_EVENT_CLICKED, this);

  // Chips: the five types, then Unread at the right.
  lv_obj_t* chips = box(content, LV_PCT(100), 30);
  lv_obj_set_style_pad_hor(chips, 6, 0);
  lv_obj_set_style_border_side(chips, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(chips, 1, 0);
  lv_obj_set_style_border_color(chips, c(_t.line2), 0);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(chips, 3, 0);
  const lv_coord_t chip_r = _look.pill_chips ? 10 : 0;
  for (uint8_t i = 0; i < 5; i++) {
    const bool on = uint8_t(_node_filter) == i;
    lv_obj_t* ch = box(chips, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(ch, 5, 0);
    lv_obj_set_style_pad_ver(ch, 3, 0);
    lv_obj_set_style_radius(ch, chip_r, 0);
    lv_obj_set_style_border_width(ch, 1, 0);
    lv_obj_set_style_border_color(ch, c(on ? _t.sig : _t.line), 0);
    if (on) {
      lv_obj_set_style_bg_opa(ch, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(ch, c(_t.sig), 0);
    }
    label(ch, FontRole::Label, on ? _t.sigInk : _t.ink2, kChipLabel[i]);
    lv_obj_set_user_data(ch, reinterpret_cast<void*>(uintptr_t(i)));
    focusable(ch);
    lv_obj_add_event_cb(
        ch,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_node_filter = TypeFilter(uintptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))));
          self->_node_cap = kNodeBatch;
          self->later([](GuiShell* g) { g->rebuild(); });
        },
        LV_EVENT_CLICKED, this);
  }
  lv_obj_t* spacer = box(chips, 1, 1);
  lv_obj_set_flex_grow(spacer, 1);
  lv_obj_t* un = box(chips, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(un, 5, 0);
  lv_obj_set_style_pad_ver(un, 3, 0);
  lv_obj_set_style_radius(un, chip_r, 0);
  lv_obj_set_style_border_width(un, 1, 0);
  lv_obj_set_style_border_color(un, c(_node_unread ? _t.sig : _t.line), 0);
  lv_obj_set_flex_flow(un, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(un, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(un, 4, 0);
  lv_obj_t* dot = box(un, 6, 6);
  lv_obj_set_style_radius(dot, _look.pill_chips ? LV_RADIUS_CIRCLE : 0, 0);
  lv_obj_set_style_border_width(dot, 1, 0);
  lv_obj_set_style_border_color(dot, c(_t.sig), 0);
  if (_node_unread) {
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, c(_t.sig), 0);
  }
  label(un, FontRole::Label, _node_unread ? _t.sig : _t.ink2, "Unread");
  focusable(un);
  lv_obj_add_event_cb(
      un,
      [](lv_event_t* e) {
        // Unread on shows companions with unread DMs (Chat); off resets both.
        GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
        self->_node_unread = !self->_node_unread;
        self->_node_filter = self->_node_unread ? TypeFilter::Chat : TypeFilter::All;
        self->_node_cap = kNodeBatch;
        self->later([](GuiShell* g) { g->rebuild(); });
      },
      LV_EVENT_CLICKED, this);

  // The rows.
  lv_obj_t* list = box(content, LV_PCT(100), 0);
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  if (_node_shown == 0) {
    if (_node_unread) {
      lv_obj_t* f = label(list, FontRole::BodySmall, _t.dim, "No unread DMs · Clear");
      lv_obj_set_width(f, LV_PCT(100));
      lv_obj_set_style_text_align(f, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_pad_top(f, 8, 0);
    } else {
      emptyState(list, _t, EmptyIcon::Bubbles, emptyTitle(_node_filter),
                 "Nodes appear as their adverts are heard. Send an advert from Home so others find you.", nullptr);
    }
  } else {
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    // Near the end of what's built, build the next batch.
    lv_obj_add_event_cb(
        list,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          if (lv_obj_get_scroll_bottom(lv_event_get_target(e)) < 60) {
            self->later([](GuiShell* g) { g->appendNodeRows(); });
          }
        },
        LV_EVENT_SCROLL_END, this);
  }
  _node_list = list;
  for (size_t i = 0; i < _node_shown; i++) {
    const NodeRow& r = rows[_node_order[i]];
    lv_obj_t* row = nodeRow(list, r);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(uintptr_t(i + 1)));
    focusable(row);
    rowOpens(row, i, r.key);
    _node_row_obj[i] = row;
    if (_focus_key_set && r.key && memcmp(r.key, _focus_key, 6) == 0) _focus_row = row;
  }

  // The unread view says what it shows, with Clear (as drawn).
  if (_node_unread && _node_shown) {
    lv_obj_t* f = label(list, FontRole::BodySmall, _t.dim, "Only companions with unread DMs · Clear");
    lv_obj_set_width(f, LV_PCT(100));
    lv_obj_set_style_text_align(f, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_ver(f, 8, 0);
  }
  if (_node_unread) {   // Clear: the footer's tap target, also in the focus order
    lv_obj_t* clear = lv_obj_get_child(list, -1);
    lv_obj_add_flag(clear, LV_OBJ_FLAG_CLICKABLE);
    focusable(clear);
    _node_clear = clear;
    lv_obj_add_event_cb(
        clear,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_node_unread = false;
          self->_node_filter = TypeFilter::All;
          self->later([](GuiShell* g) { g->rebuild(); });
        },
        LV_EVENT_CLICKED, this);
  }
}

void GuiShell::updateNodes(uint32_t now_ms) {
  // Search: its results redraw once typing pauses (querySoon), not on the
  // list's timer.
  if (_searching && !_detail) {
    if (_search_due && int32_t(now_ms - _search_due) >= 0) {
      _search_due = 0;
      refreshSearch();
    }
    return;
  }
  if (_locked || _composing || _tab != Tab::Nodes || _sheet != Sheet::None || _detail) return;
  if (int32_t(now_ms - _node_refresh_at) < 0) return;
  _node_refresh_at = now_ms + 1000;

  size_t total = 0;
  const NodeRow* rows = _model->nodeRows(&total);
  uint16_t order[kNodeRowsShown];
  const size_t shown = nodeOrder(rows, total, _node_filter, _node_unread, "", order, _node_cap);
  bool same = shown == _node_shown;
  for (size_t i = 0; same && i < shown; i++) same = order[i] == _node_order[i];
  for (size_t i = 0; same && i < shown; i++) {
    // A badge appearing or going changes the row's layout: rebuild for that.
    const bool has_badge = lv_obj_get_child_cnt(_node_row_obj[i]) == 6;
    same = has_badge == (rows[order[i]].unread != 0);
  }

  if (!same) {
    // The order moved (a node was heard): rebuild, but not more than every
    // 5 s, and keep the focused row.
    if (int32_t(now_ms - _node_rebuild_at) < 0) return;
    _node_rebuild_at = now_ms + 5000;
    lv_obj_t* f = lv_group_get_focused(_group);
    const uintptr_t ud = f ? uintptr_t(lv_obj_get_user_data(f)) : 0;
    _focus_key_set = false;
    if (ud && ud <= _node_shown && f == _node_row_obj[ud - 1] && rows[_node_order[ud - 1]].key) {
      memcpy(_focus_key, rows[_node_order[ud - 1]].key, 6);
      _focus_key_set = true;
    }
    rebuild();
    return;
  }

  // Same rows: refresh the second line, bars and SNR in place.
  char buf[32];
  for (size_t i = 0; i < shown; i++) {
    const NodeRow& r = rows[order[i]];
    lv_obj_t* row = _node_row_obj[i];
    lv_obj_t* tx = lv_obj_get_child(row, 1);
    rowSub(r, buf, sizeof buf);
    lv_obj_t* sub = lv_obj_get_child(tx, 1);
    if (strcmp(lv_label_get_text(sub), buf) != 0) lv_label_set_text(sub, buf);
    const uint32_t base = r.unread ? 3 : 2;   // tile, text, [badge], bars, snr, chevron
    lv_obj_t* barbox = lv_obj_get_child(row, base);
    lv_obj_t* bars[4];
    for (int b = 0; b < 4; b++) bars[b] = lv_obj_get_child(barbox, b);
    colorBars(bars, r.snr_known, r.snr_db, _model->radioSf(), _t);
    if (r.snr_known) rowSnr(r.snr_db, buf, sizeof buf);
    else buf[0] = '\0';
    lv_obj_t* snr = lv_obj_get_child(row, base + 1);
    if (strcmp(lv_label_get_text(snr), buf) != 0) lv_label_set_text(snr, buf);
  }
}

// ---------------------------------------------------------- Nodes search

void GuiShell::openSearch(bool wheel) {
  _searching = true;
  _search_wheel = wheel;
  _search_kb = !wheel;
  mesh_log_line(MLOG_BOOT, "[gui] search open: %s\n", wheel ? "wheel (button)" : "keyboard (touch)");
  _query.start(64);   // room for a whole public key in hex
  _search_rows = _model->nodeRows(&_search_total);   // once per search, not per key
  _search_due = 0;
  _wheel.reset();
  _page.select(KeyPage::Letters);
  _shift = ShiftState();   // no capitals of its own: matching ignores case
  _hot = -1;
  rebuild();
}

// The next batch of rows, added to the built list. The focus order stays
// rows-then-footer-then-tabs: the footer and tabs leave the group and
// rejoin after the new rows. If the order moved meanwhile, it rebuilds.
void GuiShell::appendNodeRows() {
  if (!_node_list || _tab != Tab::Nodes || _searching || _node_shown >= kNodeRowsShown) return;
  size_t total = 0;
  const NodeRow* rows = _model->nodeRows(&total);
  const size_t cap = _node_cap + kNodeBatch < kNodeRowsShown ? _node_cap + kNodeBatch : kNodeRowsShown;
  uint16_t order[kNodeRowsShown];
  const size_t n = nodeOrder(rows, total, _node_filter, _node_unread, "", order, cap);
  if (n <= _node_shown) return;   // nothing more to show
  for (size_t i = 0; i < _node_shown; i++) {
    if (order[i] != _node_order[i]) {
      _node_cap = cap;
      rebuild();
      return;
    }
  }
  if (_node_clear) lv_group_remove_obj(_node_clear);
  for (lv_obj_t* t : _tab_obj) {
    if (t) lv_group_remove_obj(t);
  }
  for (size_t i = _node_shown; i < n; i++) {
    lv_obj_t* row = nodeRow(_node_list, rows[order[i]]);
    if (_node_clear) lv_obj_move_to_index(row, lv_obj_get_index(_node_clear));
    lv_obj_set_user_data(row, reinterpret_cast<void*>(uintptr_t(i + 1)));
    focusable(row);
    rowOpens(row, i, rows[order[i]].key);
    _node_row_obj[i] = row;
    _node_order[i] = order[i];
  }
  if (_node_clear) lv_group_add_obj(_group, _node_clear);
  for (lv_obj_t* t : _tab_obj) {
    if (t) lv_group_add_obj(_group, t);
  }
  _node_shown = n;
  _node_cap = cap;
}

void GuiShell::closeSearch() {
  _node_cap = kNodeBatch;
  _searching = _search_kb = _search_wheel = false;
  rebuild();
}

// ObScreen nodesfind: the field takes the header row (signal border on the
// wash, caret, ×), the match line under it, results, and the keyboard or
// the wheel pinned at the bottom.
void GuiShell::buildSearch(lv_obj_t* scr) {
  lv_obj_t* h = box(scr, kW, 32);
  lv_obj_set_pos(h, 0, kStatusH);
  lv_obj_set_style_pad_hor(h, 8, 0);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(h, 1, 0);
  lv_obj_set_style_border_color(h, c(_t.line), 0);
  lv_obj_t* field = box(h, LV_PCT(100), 26);
  lv_obj_center(field);
  lv_obj_set_style_border_width(field, 1, 0);
  lv_obj_set_style_border_color(field, c(_t.sig), 0);
  lv_obj_set_style_bg_opa(field, lv_opa_t(_t.wash >> 24), 0);
  lv_obj_set_style_bg_color(field, c(_t.wash), 0);
  lv_obj_set_style_radius(field, _look.pill_chips ? 13 : 0, 0);
  lv_obj_set_style_pad_hor(field, 7, 0);
  lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(field, 5, 0);
  label(field, FontRole::BodySmall, _t.dim, "\xF0\x9F\x94\x8D");   // 🔍
  _query_label = label(field, FontRole::Body, _t.ink);
  lv_obj_t* caret = box(field, 1, 12);
  lv_obj_set_style_bg_opa(caret, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(caret, c(_t.sig), 0);
  lv_obj_t* grow = box(field, 1, 1);
  lv_obj_set_flex_grow(grow, 1);
  // ×: leaves search. Drawn as two short lines (no × in the mono font here).
  lv_obj_t* x = box(field, 10, 10);
  static const lv_point_t kX1[] = {{1, 1}, {9, 9}};
  static const lv_point_t kX2[] = {{9, 1}, {1, 9}};
  line(x, kX1, 2, _t.dim);
  line(x, kX2, 2, _t.dim);
  lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(x, 10);
  lv_obj_add_event_cb(
      x, [](lv_event_t* e) { static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->closeSearch(); }); },
      LV_EVENT_CLICKED, this);
  // With the keyboard away, a tap on the field brings it back.
  if (!_search_kb) {
    lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        field,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_search_kb = true;
          self->_search_wheel = false;
          self->later([](GuiShell* g) { g->rebuild(); });
        },
        LV_EVENT_CLICKED, this);
  }

  _match_label = label(scr, FontRole::Label, _t.dim);
  lv_obj_set_pos(_match_label, 8, kStatusH + 32 + 4);

  const int16_t top = kStatusH + 32 + 18;
  const int16_t bottom = _search_kb ? kKbTop - 2 : _search_wheel ? 320 - 70 : 320;
  _search_list = box(scr, kW, bottom - top);
  lv_obj_set_pos(_search_list, 0, top);
  lv_obj_set_flex_flow(_search_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(_search_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(_search_list, LV_SCROLLBAR_MODE_OFF);

  if (_search_kb) buildKeyboard(scr);
  if (_search_wheel) buildWheel(scr);
  refreshSearch();
}

void GuiShell::querySoon() {
  if (_query_label) userText(_query_label, _query.text());
  _search_due = lv_tick_get() + 250;
  if (_search_due == 0) _search_due = 1;
}

// The query, the match line and the results.
void GuiShell::refreshSearch() {
  if (!_query_label || !_search_list) return;
  SlowStep slow("search results", int(_query.length()));
  userText(_query_label, _query.text());
  const size_t total = _search_total;
  const NodeRow* rows = _search_rows;
  size_t matches = 0;
  for (size_t i = 0; i < total; i++) {
    matches += passesFilter(rows[i], _node_filter, _node_unread) && searchMatches(rows[i], _query.text());
  }
  char line_text[48];
  matchLine(matches, _node_filter, line_text, sizeof line_text);
  for (char* p = line_text; *p; p++) {   // caps, as drawn (9 px, +0.1em)
    if (*p >= 'a' && *p <= 'z') *p = char(*p - 'a' + 'A');
  }
  lv_label_set_text(_match_label, line_text);

  lv_obj_clean(_search_list);
  // While typing only what fits above the keyboard or wheel is built (about
  // 3 rows); all of them once Done puts it away.
  const size_t cap = (_search_kb || _search_wheel) ? 6 : kNodeRowsShown;
  _node_shown = nodeOrder(rows, total, _node_filter, _node_unread, _query.text(), _node_order, cap);
  mesh_log_line(MLOG_BOOT, "[gui] search \"%s\": %u matches\n", _query.text(), unsigned(matches));
  if (matches == 0 && _query.length()) {
    char none[112];
    textf(none, sizeof none, "No node name contains \"%s\", and no key starts with it.", _query.text());
    lv_obj_t* l = label(_search_list, FontRole::BodySmall, _t.dim);
    userText(l, none);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(l, 16, 0);
    return;
  }
  // With the keyboard or the wheel up, the buttons type; the rows join the
  // focus order only once Done has put them away.
  const bool rows_focusable = !_search_kb && !_search_wheel;
  for (size_t i = 0; i < _node_shown; i++) {
    lv_obj_t* row = nodeRow(_search_list, rows[_node_order[i]]);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(uintptr_t(i + 1)));
    if (rows_focusable) focusable(row);
    rowOpens(row, i, rows[_node_order[i]].key);
  }
}

// The wheel strip (mockup 1): the current entry boxed in the middle, three
// either side, and the hint. With scr null it redraws the existing strip.
void GuiShell::buildWheel(lv_obj_t* scr) {
  if (scr) {
    _wheel_strip = box(scr, kW, 70);
    lv_obj_set_pos(_wheel_strip, 0, 320 - 70);
    lv_obj_set_style_bg_opa(_wheel_strip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(_wheel_strip, c(_t.bg2), 0);
    lv_obj_set_style_border_side(_wheel_strip, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(_wheel_strip, 1, 0);
    lv_obj_set_style_border_color(_wheel_strip, c(_t.line), 0);
    // A touch on the wheel switches to the keyboard.
    lv_obj_add_flag(_wheel_strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        _wheel_strip,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_search_wheel = false;
          self->_search_kb = true;
          self->later([](GuiShell* g) { g->rebuild(); });
        },
        LV_EVENT_CLICKED, this);
  }
  if (!_wheel_strip) return;
  lv_obj_clean(_wheel_strip);
  lv_obj_t* row = box(_wheel_strip, kW, 46);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 4, 0);
  for (int off = -3; off <= 3; off++) {
    const char* l = _wheel.labelAt(off);
    const bool control = strlen(l) > 1 && strcmp(l, "⌫") != 0;   // "space", "Done"
    if (off == 0) {
      lv_obj_t* cur = box(row, LV_SIZE_CONTENT, 42);
      lv_obj_set_style_min_width(cur, 36, 0);
      lv_obj_set_style_pad_hor(cur, 4, 0);
      lv_obj_set_style_border_width(cur, 1, 0);
      lv_obj_set_style_border_color(cur, c(_t.sig), 0);
      lv_obj_set_style_bg_opa(cur, lv_opa_t(_t.wash >> 24), 0);
      lv_obj_set_style_bg_color(cur, c(_t.wash), 0);
      lv_obj_set_style_radius(cur, 6, 0);
      lv_obj_center(label(cur, control ? FontRole::Body : FontRole::Title, _t.ink, l));
    } else {
      const bool near = off == -1 || off == 1;
      label(row, control ? FontRole::Label : near ? FontRole::Body : FontRole::BodySmall, near ? _t.ink2 : _t.dim, l);
    }
  }
  lv_obj_t* hint = label(_wheel_strip, FontRole::Label, _t.dim, "USR next · AUX back one · USR hold add · AUX hold done");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// USR hold on the wheel: add the letter, delete one, or Done (keeps the
// results; the rows then take the buttons).
void GuiShell::wheelPick() {
  switch (_wheel.pick()) {
    case LetterWheel::Pick::Char: {
      const char ch = char(_wheel.ch() >= 'A' && _wheel.ch() <= 'Z' ? _wheel.ch() - 'A' + 'a' : _wheel.ch());
      _query.insert(&ch, 1);
      querySoon();
      break;
    }
    case LetterWheel::Pick::Backspace:
      _query.backspace();
      querySoon();
      break;
    case LetterWheel::Pick::Done:
      _search_wheel = false;
      rebuild();
      break;
  }
}

// ----------------------------------------------------------- Node detail

namespace {
void upcase(char* s) {
  for (; *s; s++) {
    if (*s >= 'a' && *s <= 'z') *s = char(*s - 'a' + 'A');
  }
}
}  // namespace

// A row opens its node's detail: by touch, or USR hold on the focused row.
void GuiShell::rowOpens(lv_obj_t* row, size_t i, const uint8_t* key) {
  if (!key || i >= kNodeRowsShown) return;
  memcpy(_row_keys[i], key, 32);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      row,
      [](lv_event_t* e) {
        GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
        const uintptr_t ud = uintptr_t(lv_obj_get_user_data(lv_event_get_current_target(e)));
        if (ud == 0 || ud > kNodeRowsShown) return;
        memcpy(self->_detail_key, self->_row_keys[ud - 1], 32);
        self->later([](GuiShell* g) { g->openDetail(g->_detail_key); });
      },
      LV_EVENT_CLICKED, this);
}

void GuiShell::openDetail(const uint8_t* key) {
  if (key != _detail_key) memcpy(_detail_key, key, 32);
  _detail = true;
  _login = _login_sent = false;
  _act_focus = 0;
  _model->detailOpened(_detail_key);
  rebuild();
}

// Back to where the detail was opened from: the list, on its row, or search.
void GuiShell::closeDetail() {
  _detail = _login = _login_sent = false;
  memcpy(_focus_key, _detail_key, 6);
  _focus_key_set = true;
  rebuild();
}

void GuiShell::openLogin() {
  _login = true;
  _login_sent = false;
  _login_ok_at = 0;
  _login_pw.start(kPasswordLen - 1);
  // Remembered from the last good login on this device (owner pick a).
  if (const char* pw = _model->savedPassword(_detail_key)) _login_pw.insert(pw, strlen(pw));
  _page.select(KeyPage::Letters);
  _shift = ShiftState();   // passwords are typed as they are
  _hot = -1;
  rebuild();
}

void GuiShell::closeLogin() {
  _login = _login_sent = false;
  rebuild();
}

// ObScreen node detail, extended by mockups 2-6: back to Nodes, the kind
// line, name, key, the grid, the path (and a trace's SNRs), and the actions
// pinned at the bottom.
void GuiShell::buildDetail(lv_obj_t* content) {
  NodeDetail d;
  if (!_model->nodeDetail(_detail_key, &d)) {   // the contact went: back to the list
    _detail = _login = false;
    buildNodes(content);
    return;
  }
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  _detail_kind = d.row.kind;

  lv_obj_t* h = box(content, LV_PCT(100), 28);
  lv_obj_set_style_pad_hor(h, 8, 0);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(h, 1, 0);
  lv_obj_set_style_border_color(h, c(_t.line), 0);
  lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(h, 6, 0);
  lv_obj_t* back = box(h, 10, 20);
  line(back, kChevron, 3, _t.sig);
  label(h, FontRole::BodySmall, _t.ink2, _tab == Tab::Map ? "Map" : "Nodes");   // where Back goes
  lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      h, [](lv_event_t* e) { static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->closeDetail(); }); },
      LV_EVENT_CLICKED, this);

  lv_obj_t* b = box(content, LV_PCT(100), 0);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_style_pad_hor(b, 8, 0);
  // Tight enough that a repeater's six cells, the path and a trace's SNRs
  // fit the 150 px between header and buttons (bench #1534: the SNR line
  // sat below the edge).
  lv_obj_set_style_pad_ver(b, 3, 0);
  lv_obj_set_style_pad_row(b, 2, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(b, LV_SCROLLBAR_MODE_OFF);

  char buf[96];
  detailKind(d.row.kind, d.row.out_path_len, d.row.snr_known, d.row.snr_db, buf, sizeof buf);
  upcase(buf);
  label(b, FontRole::Label, _t.sig, buf);
  lv_obj_t* name = label(b, FontRole::Title, _t.ink);
  userText(name, d.row.name);
  lv_obj_set_width(name, LV_PCT(100));
  oneLine(name, FontRole::Title);
  keySummary(d.key, buf, sizeof buf);
  label(b, FontRole::Meta, _t.dim, buf);

  // The grid, three across: what was heard, and the status once a Ping
  // answers (dashes before).
  ActView ping;
  _model->nodeActState(NodeAct::Ping, &ping);
  const bool pinged = ping.state == ActView::Ok;
  Cell cells[6];
  const size_t n = detailCells(d.row, d.rssi, pinged ? ping.body : nullptr, pinged ? ping.len : 0,
                               pinged ? ping.rtt_ms : 0, cells, 6);
  lv_obj_t* grid = box(b, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(grid, 1, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_border_side(grid, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_width(grid, 1, 0);
  lv_obj_set_style_border_color(grid, c(_t.line), 0);
  for (size_t i = 0; i < n; i++) {
    lv_obj_t* cell = box(grid, (kW - 16 - 1) / 3, LV_SIZE_CONTENT);
    lv_obj_set_style_border_side(cell, LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, c(_t.line), 0);
    lv_obj_set_style_pad_hor(cell, 5, 0);
    lv_obj_set_style_pad_ver(cell, 1, 0);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    upcase(cells[i].key);
    label(cell, FontRole::Label, _t.dim, cells[i].key);
    const bool dash = strcmp(cells[i].value, "—") == 0;
    label(cell, dash ? FontRole::Meta : FontRole::Data, dash ? _t.dim : _t.ink, cells[i].value);
  }

  if (d.path[0]) {
    lv_obj_t* p = label(b, FontRole::Meta, _t.ink2);
    userText(p, d.path);
    lv_obj_set_width(p, LV_PCT(100));
    lv_label_set_long_mode(p, LV_LABEL_LONG_WRAP);
  }
  ActView tr;
  _model->nodeActState(NodeAct::Trace, &tr);
  if (tr.state == ActView::Ok) {
    traceSnrLine(tr.body, tr.len, buf, sizeof buf);
    if (buf[0]) {
      lv_obj_t* snr = label(b, FontRole::Meta, _t.dim, buf);
      // The newest result shows even if the body runs past its height.
      lv_obj_update_layout(b);
      lv_obj_scroll_to_view(snr, LV_ANIM_OFF);
    }
    mesh_log_line(MLOG_BOOT, "[gui] trace line: \"%s\" (%u bytes)\n", buf, unsigned(tr.len));
  }

  // The actions (mockups 2-6): the primary across, the others under it.
  static const NodeAct kRpt[] = {NodeAct::Login, NodeAct::Trace, NodeAct::Ping};
  static const NodeAct kSens[] = {NodeAct::Ping, NodeAct::Trace};
  static const NodeAct kChat[] = {NodeAct::Count, NodeAct::Share, NodeAct::Trace};   // Count: DM
  const NodeAct* plan = kChat;
  _act_count = 3;
  if (d.row.kind == NodeKind::Repeater || d.row.kind == NodeKind::Room) plan = kRpt;
  else if (d.row.kind == NodeKind::Sensor) {
    plan = kSens;
    _act_count = 2;
  }
  lv_obj_t* acts = box(content, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(acts, 8, 0);
  lv_obj_set_style_pad_top(acts, 3, 0);
  lv_obj_set_style_pad_bottom(acts, 4, 0);
  lv_obj_set_style_pad_row(acts, 4, 0);
  lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* second = nullptr;
  for (uint8_t i = 0; i < _act_count; i++) {
    lv_obj_t* parent = acts;
    if (i > 0) {
      if (!second) {
        second = box(acts, LV_PCT(100), 34);
        lv_obj_set_flex_flow(second, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(second, 4, 0);
      }
      parent = second;
    }
    lv_obj_t* btn = box(parent, i == 0 ? LV_PCT(100) : 0, 34);
    if (i > 0) lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_radius(btn, _look.radius, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_center(label(btn, FontRole::Body, _t.ink));
    lv_obj_set_user_data(btn, reinterpret_cast<void*>(uintptr_t(i)));
    _act_btn[i] = btn;
    _act_kind[i] = plan[i];
    _act_off[i] = plan[i] == NodeAct::Trace && !d.can_trace;   // no route to trace
    if (_act_off[i]) continue;
    focusable(btn);
    lv_obj_add_event_cb(
        btn,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_pending_act = uint8_t(uintptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))));
          self->later([](GuiShell* g) { g->actPressed(g->_act_kind[g->_pending_act]); });
        },
        LV_EVENT_CLICKED, this);
  }
  for (uint8_t i = _act_count; i < 3; i++) _act_btn[i] = nullptr;
  refreshActs(_detail_now);
  _detail_sig = detailSignature();
  const uint8_t f = _act_focus < _act_count && !_act_off[_act_focus] ? _act_focus : 0;
  _focus_row = _act_btn[f];
}

// The buttons' text and look from where each action stands (mockup 6).
void GuiShell::refreshActs(uint32_t now_ms) {
  const NodeKind kind = _detail_kind;
  for (uint8_t i = 0; i < _act_count; i++) {
    lv_obj_t* btn = _act_btn[i];
    if (!btn) continue;
    const NodeAct a = _act_kind[i];
    ActView v{};
    if (a != NodeAct::Count) _model->nodeActState(a, &v);
    char text[32];
    bool pri = i == 0, ok = false, bad = false, busy = false;
    switch (a) {
      case NodeAct::Count:
        textf(text, sizeof text, "DM");
        break;
      case NodeAct::Login:
        ok = v.state == ActView::Ok;
        textf(text, sizeof text, "%s", ok ? "Logged in ✓" : kind == NodeKind::Room ? "Log in" : "Admin login");
        break;
      case NodeAct::Share: {
        const bool recent = int32_t(now_ms - v.done_ms) < 2000;   // "Shared ✓" for 2 s
        ok = v.state == ActView::Ok && recent;
        bad = v.state == ActView::Failed && recent;
        textf(text, sizeof text, "%s", ok ? "Shared ✓" : bad ? "Not sent" : "Share contact");
        break;
      }
      default: {   // Ping, Trace
        const bool is_ping = a == NodeAct::Ping;
        busy = v.state == ActView::Busy;
        ok = v.state == ActView::Ok;
        bad = v.state == ActView::Failed;
        if (busy) textf(text, sizeof text, "%s", is_ping ? "Pinging…" : "Tracing…");
        else if (bad) textf(text, sizeof text, "No reply");
        else if (ok) {   // the round trip on both (owner pick a, 2026-10-17)
          const unsigned t = (v.rtt_ms + 50) / 100;
          textf(text, sizeof text, "%s ✓ %u.%u s", is_ping ? "Ping" : "Trace", t / 10, t % 10);
        } else textf(text, sizeof text, "%s", is_ping ? "Ping" : "Trace");
        break;
      }
    }
    uint32_t ink = _t.ink, border = _t.line;
    bool fill = false;
    if (_act_off[i]) {
      ink = _t.dim;
      border = _t.line2;
    } else if (bad) {
      ink = border = _t.bad;
    } else if (pri) {
      fill = true;
      ink = _t.sigInk;
      border = _t.sig;
    } else if (ok) {
      ink = border = _t.sig;
    } else if (busy) {
      ink = _t.dim;
    }
    lv_obj_set_style_bg_opa(btn, fill ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(btn, c(_t.sig), 0);
    lv_obj_set_style_border_color(btn, c(border), 0);
    lv_obj_t* l = lv_obj_get_child(btn, 0);
    lv_obj_set_style_text_color(l, c(ink), 0);
    if (strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
  }
}

void GuiShell::actPressed(NodeAct a) {
  if (!_detail) return;
  if (a == NodeAct::Login) {
    openLogin();
    return;
  }
  if (a == NodeAct::Count) {   // DM: its conversation, with Compose up (mockup 2)
    const int t = _model->openDm(_detail_key);
    if (t < 0) return;
    _detail = false;
    _tab = Tab::Chat;
    _focus_thread = t;
    _model->threadOpened(t);
    openCompose(t);
    return;
  }
  _model->startNodeAct(_detail_key, a, nullptr);
  refreshActs(_detail_now);
}

// What the detail's body shows: a result landing, or the node heard anew.
uint32_t GuiShell::detailSignature() {
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
  for (NodeAct a : {NodeAct::Ping, NodeAct::Trace}) {
    ActView v;
    _model->nodeActState(a, &v);
    mix(v.state == ActView::Ok ? v.done_ms + 1 : 0);
  }
  NodeDetail d;
  if (_model->nodeDetail(_detail_key, &d)) {
    mix(d.row.heard ? d.row.heard_age_s / 60 + 1 : 0);
    mix(d.row.snr_known ? uint32_t(int32_t(d.row.snr_db * 4)) : 0x7FFF);
    mix(d.row.unread);
    mix(d.row.out_path_len);
    mix(uint32_t(d.rssi));
  } else {
    mix(0xDEAD);
  }
  return h;
}

// The login sheet (mockup 5) over the detail: the title, the masked
// password, how the login stands, and the keyboard with "Log in" as Send.
void GuiShell::buildLogin(lv_obj_t* scr) {
  NodeDetail d;
  if (!_model->nodeDetail(_detail_key, &d)) {
    _login = _detail = false;
    rebuild();
    return;
  }
  lv_obj_t* content = box(scr, kW, kContentH);
  lv_obj_set_pos(content, 0, kStatusH);
  buildDetail(content);
  lv_group_remove_all_objs(_group);   // the sheet takes the buttons

  lv_obj_t* scrim = box(scr, kW, 320 - kStatusH);
  lv_obj_set_pos(scrim, 0, kStatusH);
  lv_obj_set_style_bg_opa(scrim, LV_OPA_50, 0);
  lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
  lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);   // a tap above the sheet closes it
  lv_obj_add_event_cb(
      scrim, [](lv_event_t* e) { static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->closeLogin(); }); },
      LV_EVENT_CLICKED, this);

  const int16_t top = kKbTop - 2 - 84;
  lv_obj_t* sheet = box(scr, kW, 320 - top);
  lv_obj_set_pos(sheet, 0, top);
  lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(sheet, c(_t.bg3), 0);
  lv_obj_set_style_border_side(sheet, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(sheet, 1, 0);
  lv_obj_set_style_border_color(sheet, c(_t.line), 0);
  lv_obj_set_style_radius(sheet, 12, 0);
  lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);   // taps on the sheet stay on it

  char title[64];
  textf(title, sizeof title, "%s · %s", d.row.kind == NodeKind::Room ? "Log in" : "Admin login", d.row.name);
  lv_obj_t* t = label(sheet, FontRole::Body, _t.ink);
  userText(t, title);
  lv_obj_set_width(t, kW - 24);
  oneLine(t, FontRole::Body);
  lv_obj_set_pos(t, 12, 9);

  _login_field = box(sheet, kW - 24, 32);
  lv_obj_set_pos(_login_field, 12, 30);
  lv_obj_set_style_border_width(_login_field, 1, 0);
  lv_obj_set_style_border_color(_login_field, c(_t.sig), 0);
  lv_obj_set_style_bg_opa(_login_field, lv_opa_t(_t.wash >> 24), 0);
  lv_obj_set_style_bg_color(_login_field, c(_t.wash), 0);
  lv_obj_set_style_radius(_login_field, _look.radius, 0);
  lv_obj_set_style_pad_hor(_login_field, 9, 0);
  lv_obj_set_flex_flow(_login_field, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(_login_field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(_login_field, 4, 0);

  _login_status = label(sheet, FontRole::BodySmall, _t.dim);
  lv_obj_set_pos(_login_status, 12, 66);

  buildKeyboard(scr);
  refreshLoginField();
}

// One dot a character (the fonts carry no bullet), then the caret.
void GuiShell::refreshLoginField() {
  if (!_login_field) return;
  lv_obj_clean(_login_field);
  for (size_t i = 0; i < _login_pw.length(); i++) {
    lv_obj_t* d = box(_login_field, 6, 6);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, c(_t.ink), 0);
  }
  lv_obj_t* caret = box(_login_field, 1, 14);
  lv_obj_set_style_bg_opa(caret, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(caret, c(_t.sig), 0);
}

void GuiShell::updateDetail(uint32_t now_ms) {
  _detail_now = now_ms;
  if (_locked || _composing || !detailTab() || !_detail || _sheet != Sheet::None) return;

  if (_login) {
    if (!_login_status || !_login_sent) return;
    ActView v;
    _model->nodeActState(NodeAct::Login, &v);
    char s[40] = "";
    if (v.state == ActView::Busy) {
      const unsigned t = lv_tick_elaps(_login_at) / 100;
      textf(s, sizeof s, "Logging in… %u.%u s", t / 10, t % 10);
    } else if (v.state == ActView::Ok) {
      // "Logged in ✓", then the sheet closes and a Ping fills the grid.
      textf(s, sizeof s, "Logged in ✓");
      if (!_login_ok_at) {
        _login_ok_at = lv_tick_get() | 1;
      } else if (lv_tick_elaps(_login_ok_at) >= 1000) {
        closeLogin();
        _model->startNodeAct(_detail_key, NodeAct::Ping, nullptr);
        return;
      }
    } else if (v.state == ActView::Failed) {
      textf(s, sizeof s, "%s", v.login_refused ? "Wrong password" : "No reply");
    }
    lv_obj_set_style_text_color(_login_status, c(v.state == ActView::Failed ? _t.bad
                                                 : v.state == ActView::Ok   ? _t.sig
                                                                            : _t.dim),
                                0);
    if (strcmp(lv_label_get_text(_login_status), s) != 0) lv_label_set_text(_login_status, s);
    return;
  }

  if (int32_t(now_ms - _detail_at) < 0) return;
  _detail_at = now_ms + 250;
  uint32_t sig;
  {
    SlowStep slow("detail check");
    sig = detailSignature();
  }
  if (sig != _detail_sig) {
    lv_obj_t* f = lv_group_get_focused(_group);
    for (uint8_t i = 0; i < _act_count; i++) {
      if (f && f == _act_btn[i]) _act_focus = i;
    }
    rebuild();
    return;
  }
  refreshActs(now_ms);
}

// ------------------------------------------------------------------ Map

namespace {

// Every contact row's distance and bearing from this device, for the radar's
// pick. Off the stack; the GUI is single-threaded.
constexpr size_t kRadarRowsMax = 512;
RadarItem s_radar_items[kRadarRowsMax];
uint16_t s_radar_bearing[kRadarRowsMax];

size_t radarItems(const NodeRow* rows, size_t total, GeoPos own) {
  const size_t n = total < kRadarRowsMax ? total : kRadarRowsMax;
  for (size_t i = 0; i < n; i++) {
    const GeoPos p{rows[i].lat_e6, rows[i].lon_e6};
    s_radar_items[i].has_pos = hasPos(p);
    s_radar_items[i].dist_m = s_radar_items[i].has_pos ? distanceM(own, p) : 0;
    s_radar_bearing[i] = s_radar_items[i].has_pos ? bearingDeg(own, p) : 0;
  }
  return n;
}

// The plot: a square that fits between the header (28) and the sheet (36),
// 6 px clear all round, as drawn.
constexpr int16_t kPlot = kContentH - 28 - 36 - 12;
constexpr int16_t kPlotR = kPlot / 2 - 4;   // the outer ring's radius for pins, inside the stroke
const lv_point_t kAxisV[] = {{kPlot / 2, 0}, {kPlot / 2, kPlot}};
const lv_point_t kAxisH[] = {{0, kPlot / 2}, {kPlot, kPlot / 2}};
const lv_point_t kDiamond[] = {{3, 0}, {6, 3}, {3, 6}, {0, 3}, {3, 0}};

// A pin, 7 px, in the Nodes shapes and tints: companion a dot, repeater a
// diamond, room a square, sensor a hollow ring.
lv_obj_t* radarPin(lv_obj_t* plot, NodeKind kind, int16_t cx, int16_t cy, const ThemeTokens& t) {
  const uint32_t tint = kindTint(t, kind);
  lv_obj_t* p = box(plot, 7, 7);
  lv_obj_set_pos(p, cx - 3, cy - 3);
  switch (kind) {
    case NodeKind::Repeater: {
      lv_obj_t* d = line(p, kDiamond, 5, tint);
      lv_obj_set_style_line_width(d, 2, 0);
      break;
    }
    case NodeKind::Room:
      lv_obj_set_style_radius(p, 2, 0);
      lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(p, c(tint), 0);
      break;
    case NodeKind::Sensor:
      lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(p, 2, 0);
      lv_obj_set_style_border_color(p, c(tint), 0);
      break;
    default:
      lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(p, c(tint), 0);
      break;
  }
  return p;
}

}  // namespace

// ObScreen radar, with the gap mockups: the header with the scale where
// Tiles | Radar was, the plot, and the selection sheet (or its hint).
void GuiShell::buildRadar(lv_obj_t* content) {
  SlowStep slow("radar", int(_radar_step));
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

  lv_obj_t* h = box(content, LV_PCT(100), 28);
  lv_obj_set_style_pad_hor(h, 8, 0);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(h, 1, 0);
  lv_obj_set_style_border_color(h, c(_t.line), 0);
  lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  label(h, FontRole::Title, _t.ink, "Map");
  lv_obj_t* grow = box(h, 1, 1);
  lv_obj_set_flex_grow(grow, 1);
  // The scale: a tap goes to the next step (mockup 2).
  lv_obj_t* sc = box(h, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_border_width(sc, 1, 0);
  lv_obj_set_style_border_color(sc, c(_t.line), 0);
  lv_obj_set_style_radius(sc, _look.radius, 0);
  lv_obj_set_style_pad_hor(sc, 6, 0);
  lv_obj_set_style_pad_ver(sc, 2, 0);
  char buf[48];
  ringText(_radar_step, buf, sizeof buf);
  label(sc, FontRole::Meta, _t.ink2, buf);
  lv_obj_add_flag(sc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(sc, 6);
  lv_obj_add_event_cb(
      sc,
      [](lv_event_t* e) {
        GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
        self->_radar_step = nextStep(self->_radar_step);
        self->later([](GuiShell* g) { g->rebuild(); });
      },
      LV_EVENT_CLICKED, this);

  GeoPos own;
  if (!_model->ownPos(&own)) {   // mockup 4
    lv_obj_t* body = box(content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(body, 1);
    emptyState(body, _t, EmptyIcon::Radar, "No position yet",
               "The radar needs this device's position. It appears with a GPS fix, or set one in Setup.", nullptr);
    _radar_sig = radarSignature();
    return;
  }
  size_t total = 0;
  const NodeRow* rows = _model->nodeRows(&total);
  const size_t n = rows ? radarItems(rows, total, own) : 0;
  const uint32_t outer = radarStepM(_radar_step);
  uint16_t shown[kRadarPins];
  size_t beyond = 0, beyond_max = 0;
  const size_t k = radarPick(s_radar_items, n, outer, shown, kRadarPins, &beyond);
  uint16_t any[1];
  const bool in_range = radarPick(s_radar_items, n, radarStepM(kRadarStepCount - 1), any, 1, &beyond_max) > 0;

  // The selection, by key: a row of the full list, drawn or not.
  int sel_row = -1;
  for (size_t i = 0; _radar_sel_set && i < n; i++) {
    if (s_radar_items[i].has_pos && rows[i].key && memcmp(rows[i].key, _radar_sel, 32) == 0) sel_row = int(i);
  }
  if (sel_row < 0) _radar_sel_set = false;

  // The plot: three rings, the axes, N, the corner note, the pins, you.
  lv_obj_t* area = box(content, LV_PCT(100), 0);
  lv_obj_set_flex_grow(area, 1);
  lv_obj_t* plot = box(area, kPlot, kPlot);
  lv_obj_center(plot);
  for (int f = 3; f >= 1; f--) {
    const lv_coord_t d = kPlot * f / 3;
    lv_obj_t* ring = box(plot, d, d);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_border_color(ring, c(_t.line), 0);
  }
  line(plot, kAxisV, 2, _t.line2);
  line(plot, kAxisH, 2, _t.line2);
  lv_obj_t* north = label(area, FontRole::Label, _t.sig, "N");
  lv_obj_align_to(north, plot, LV_ALIGN_OUT_TOP_MID, 0, 4);
  beyondText(beyond, _radar_step, buf, sizeof buf);
  if (buf[0]) {
    lv_obj_t* note = label(area, FontRole::Meta, _t.dim, buf);
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 3, -1);
  }

  LabelBox boxes[kRadarPins];
  int16_t px[kRadarPins], py[kRadarPins];
  int sel_shown = -1;
  const lv_font_t* lf = font(FontRole::Label);
  for (size_t i = 0; i < k; i++) {
    const uint16_t r = shown[i];
    const PlotPt pt = plotPoint(s_radar_items[r].dist_m, s_radar_bearing[r], outer, kPlotR);
    px[i] = int16_t(kPlot / 2 + pt.x);
    py[i] = int16_t(kPlot / 2 + pt.y);
    if (int(r) == sel_row) sel_shown = int(i);
    lv_point_t sz;
    lv_txt_get_size(&sz, rows[r].name, lf, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    // Right of the pin, or left of it where the plot's edge would cut it.
    const int16_t w = int16_t(sz.x + 4), lh = int16_t(sz.y);
    const int16_t x = px[i] + 6 + w > kPlot ? int16_t(px[i] - 6 - w) : int16_t(px[i] + 6);
    boxes[i] = LabelBox{x, int16_t(py[i] - lh / 2), w, lh};
  }
  bool show[kRadarPins];
  placeLabels(boxes, k, sel_shown, show);
  for (size_t i = 0; i < k; i++) {
    const NodeRow& r = rows[shown[i]];
    lv_obj_t* pin = radarPin(plot, r.kind, px[i], py[i], _t);
    if (int(i) == sel_shown) {
      lv_obj_set_style_outline_width(pin, 2, 0);
      lv_obj_set_style_outline_pad(pin, 2, 0);
      lv_obj_set_style_outline_color(pin, c(_t.sig), 0);
    }
    // A tap selects (a pin is small: the touch area reaches 8 px round it).
    if (r.key) memcpy(_radar_keys[i], r.key, 32);
    lv_obj_set_user_data(pin, reinterpret_cast<void*>(uintptr_t(i + 1)));
    lv_obj_add_flag(pin, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(pin, 8);
    lv_obj_add_event_cb(
        pin,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          const uintptr_t ud = uintptr_t(lv_obj_get_user_data(lv_event_get_current_target(e)));
          if (ud == 0 || ud > kRadarPins) return;
          memcpy(self->_radar_sel, self->_radar_keys[ud - 1], 32);
          self->_radar_sel_set = true;
          self->later([](GuiShell* g) { g->rebuild(); });
        },
        LV_EVENT_CLICKED, this);
    if (!show[i]) continue;
    const bool sel = int(i) == sel_shown;
    lv_obj_t* l = label(plot, FontRole::Label, sel ? _t.ink : _t.ink2);
    userText(l, r.name);
    lv_obj_set_pos(l, boxes[i].x + (sel ? 0 : 2), boxes[i].y);
    if (sel) {
      lv_obj_set_style_bg_opa(l, LV_OPA_80, 0);
      lv_obj_set_style_bg_color(l, c(_t.bg), 0);
      lv_obj_set_style_pad_hor(l, 2, 0);
      lv_obj_set_style_radius(l, 3, 0);
    }
  }
  lv_obj_t* me = box(plot, 9, 9);   // you: a signal square on the background
  lv_obj_set_pos(me, kPlot / 2 - 4, kPlot / 2 - 4);
  lv_obj_set_style_bg_opa(me, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(me, c(_t.sig), 0);
  lv_obj_set_style_border_width(me, 2, 0);
  lv_obj_set_style_border_color(me, c(_t.bg), 0);

  // The sheet (mockup 1), or what selects (mockups 3 and 5).
  lv_obj_t* sh = box(content, LV_PCT(100), 36);
  lv_obj_set_style_pad_hor(sh, 8, 0);
  lv_obj_set_style_bg_opa(sh, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(sh, c(_t.bg2), 0);
  lv_obj_set_style_border_side(sh, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(sh, 1, 0);
  lv_obj_set_style_border_color(sh, c(_t.line), 0);
  lv_obj_set_flex_flow(sh, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(sh, 6, 0);
  if (sel_row < 0) {
    lv_obj_set_flex_align(sh, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(sh, FontRole::BodySmall, _t.dim,
          in_range ? "USR or tap a pin to select" : "No nodes with a position within 30 km");
    _radar_sig = radarSignature();
    return;
  }
  lv_obj_set_flex_align(sh, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  const NodeRow& r = rows[sel_row];
  const uint32_t tint = kindTint(_t, r.kind);
  lv_obj_t* tile = box(sh, 22, 22);
  lv_obj_set_style_radius(tile, r.kind == NodeKind::Chat && _look.pill_chips ? LV_RADIUS_CIRCLE
                                : _look.pill_chips                           ? 6
                                                                             : 0,
                          0);
  if (_t.flat) {
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, c(tint), 0);
  } else {
    lv_obj_set_style_bg_opa(tile, 36, 0);
    lv_obj_set_style_bg_color(tile, c(tint), 0);
  }
  lv_obj_center(label(tile, FontRole::BodySmall, tint, kindGlyph(r.kind)));
  lv_obj_t* tx = box(sh, 0, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(tx, 1);
  lv_obj_set_flex_flow(tx, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* nm = label(tx, FontRole::Body, _t.ink);
  userText(nm, r.name);
  lv_obj_set_width(nm, LV_PCT(100));
  oneLine(nm, FontRole::Body);
  sheetSub(r.out_path_len, s_radar_items[sel_row].dist_m, s_radar_bearing[sel_row], buf, sizeof buf);
  label(tx, FontRole::Meta, _t.dim, buf);
  lv_obj_t* bars[4];
  rowBars(sh, bars);
  colorBars(bars, r.snr_known, r.snr_db, _model->radioSf(), _t);
  if (r.snr_known) rowSnr(r.snr_db, buf, sizeof buf);
  else buf[0] = '\0';
  label(sh, FontRole::Meta, _t.ink2, buf);
  lv_obj_t* chev = box(sh, 6, 12);
  static const lv_point_t kNext[] = {{1, 1}, {5, 6}, {1, 11}};
  line(chev, kNext, 3, _t.dim);
  lv_obj_add_flag(sh, LV_OBJ_FLAG_CLICKABLE);   // a tap opens the node
  lv_obj_add_event_cb(
      sh, [](lv_event_t* e) { static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->radarOpen(); }); },
      LV_EVENT_CLICKED, this);
  _radar_sig = radarSignature();
}

// The next or previous node with a position within 30 km, nearest first,
// zoomed so it shows (mockup 2).
void GuiShell::radarStep(int dir) {
  GeoPos own;
  if (!_model->ownPos(&own)) return;
  size_t total = 0;
  const NodeRow* rows = _model->nodeRows(&total);
  if (!rows) return;
  const size_t n = radarItems(rows, total, own);
  uint16_t order[kRadarPins];
  size_t beyond = 0;
  const size_t k = radarPick(s_radar_items, n, radarStepM(kRadarStepCount - 1), order, kRadarPins, &beyond);
  if (k == 0) return;
  int cur = -1;
  for (size_t i = 0; _radar_sel_set && i < k; i++) {
    if (rows[order[i]].key && memcmp(rows[order[i]].key, _radar_sel, 32) == 0) cur = int(i);
  }
  const int next = cur < 0 ? (dir > 0 ? 0 : int(k) - 1) : (cur + dir + int(k)) % int(k);
  const NodeRow& r = rows[order[next]];
  if (!r.key) return;
  memcpy(_radar_sel, r.key, 32);
  _radar_sel_set = true;
  const int st = stepFor(s_radar_items[order[next]].dist_m);
  if (st >= 0) _radar_step = size_t(st);
  rebuild();
}

// USR hold or a tap on the sheet: the selected node's detail, over the
// Map tab, so Back returns here.
void GuiShell::radarOpen() {
  if (!_radar_sel_set) return;
  size_t total = 0;
  const NodeRow* rows = _model->nodeRows(&total);
  for (size_t i = 0; rows && i < total; i++) {
    if (rows[i].key && memcmp(rows[i].key, _radar_sel, 32) == 0) {
      openDetail(rows[i].key);
      return;
    }
  }
}

// What the radar draws: your position to ~10 m, the scale, the selection,
// and each shown node's place to ~1% of the ring.
uint32_t GuiShell::radarSignature() {
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(uint32_t(_radar_step));
  mix(_radar_sel_set ? (uint32_t(_radar_sel[0]) << 8 | _radar_sel[1]) + 1 : 0);
  GeoPos own;
  if (!_model->ownPos(&own)) return h ^ 0xDEAD;
  mix(uint32_t(own.lat_e6 / 100));
  mix(uint32_t(own.lon_e6 / 100));
  size_t total = 0;
  const NodeRow* rows = _model->nodeRows(&total);
  if (!rows) return h;
  const size_t n = radarItems(rows, total, own);
  const uint32_t outer = radarStepM(_radar_step);
  uint16_t shown[kRadarPins];
  size_t beyond = 0;
  const size_t k = radarPick(s_radar_items, n, outer, shown, kRadarPins, &beyond);
  mix(uint32_t(k));
  mix(uint32_t(beyond));
  for (size_t i = 0; i < k; i++) {
    mix(shown[i]);
    mix(s_radar_items[shown[i]].dist_m * 100 / outer);
    mix(s_radar_bearing[shown[i]] / 4);
  }
  return h;
}

// Every 2 s while the radar is up: a redraw only if something it shows moved.
void GuiShell::updateRadar(uint32_t now_ms) {
  if (_locked || _composing || _tab != Tab::Map || _detail || _sheet != Sheet::None) return;
  if (int32_t(now_ms - _radar_at) < 0) return;
  _radar_at = now_ms + 2000;
  uint32_t sig;
  {
    SlowStep slow("radar check");
    sig = radarSignature();
  }
  if (sig != _radar_sig) rebuild();
}
