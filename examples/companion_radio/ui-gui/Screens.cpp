#include "Screens.h"

#include <string.h>

#include <helpers/gui/hw/GuiFonts.h>

using namespace offband::gui;

namespace {

constexpr int16_t kW = 240;
constexpr int16_t kStatusH = 20;
constexpr int16_t kTabH = 40;
constexpr int16_t kContentH = 320 - kStatusH - kTabH;

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
  char buf[kMsgTextMax * 2 + 1];
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
  _locked = false;
  _tab = tab;
  rebuild();
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
  _msg_list = nullptr;
  _focus_row = nullptr;
  if (_tab == Tab::Chat) _shown_rev = _model->store().revision();
  _status.build(scr, _t, [](void* m) { static_cast<ShellModel*>(m)->loraPopover(); }, _model);
  lv_obj_t* content = box(scr, kW, kContentH);
  lv_obj_set_pos(content, 0, kStatusH);
  if (_tab == Tab::Home) buildHome(content);
  else if (_tab == Tab::Chat && _thread >= 0) buildConversation(content);
  else if (_tab == Tab::Chat) buildChats(content);
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
    lv_obj_add_event_cb(
        t,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_pending_tab = Tab(lv_obj_get_index(lv_event_get_target(e)));
          self->later([](GuiShell* s) {
            if (s->_pending_tab == Tab::Chat) s->_thread = -1;   // the tab opens on the list
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

  // The screen object outlives rebuilds, so the handler is added once.
  if (!_gesture_hooked) {
    _gesture_hooked = true;
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        scr,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          if (self->_locked && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP) {
            self->later([](GuiShell* s) { s->showTab(s->_tab); });
          }
        },
        LV_EVENT_GESTURE, this);
  }
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
      lv_group_focus_next(_group);
      if (lv_obj_t* f = lv_group_get_focused(_group)) lv_obj_scroll_to_view(f, LV_ANIM_ON);
      return true;
    case Action::Prev:
      lv_group_focus_prev(_group);
      if (lv_obj_t* f = lv_group_get_focused(_group)) lv_obj_scroll_to_view(f, LV_ANIM_ON);
      return true;
    case Action::Open: {
      lv_obj_t* f = lv_group_get_focused(_group);
      if (f) lv_event_send(f, LV_EVENT_CLICKED, nullptr);
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
void emptyState(lv_obj_t* parent, const ThemeTokens& t, bool list, const char* title, const char* sub,
                const char* hint) {
  lv_obj_t* col = box(parent, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_center(col);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 5, 0);
  lv_obj_set_style_pad_hor(col, 22, 0);
  lv_obj_t* ic = box(col, 34, 34);
  if (list) {
    line(ic, kBubbleA, 8, t.line);
    line(ic, kBubbleB, 8, t.line);
  } else {
    line(ic, kBubble, 8, t.line);
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
    emptyState(list, _t, true, "No chats yet", "Channels you join and people who message you show up here.",
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
    emptyState(_msg_list, _t, false, "No messages yet", sub, nullptr);
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
  lv_obj_scroll_to_y(_msg_list, LV_COORD_MAX, LV_ANIM_OFF);

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

  // The keyboard: one area takes every touch and snaps it to the nearest
  // key, so a tap in a gap still lands (mockup 7a).
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
  refreshDraft();
}

// The keys of the current page, labeled for the shift state.
void GuiShell::buildKeys() {
  if (!_kb) return;
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
    snprintf(text, sizeof text, "%s", k.label);
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
  const char* l = _keys[i].label;
  const KeyPage page_before = _page.page();
  const Shift shift_before = _shift.state();

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
    _draft.backspace();
    _shift.autoCap(_draft.text(), _draft.length());
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
    if (_draft.insert(ch, strlen(ch))) {
      _shift.typedLetter();
      _shift.autoCap(_draft.text(), _draft.length());
      _page.typed(ch[0]);
    }
  }
  if (_page.page() != page_before || _shift.state() != shift_before) buildKeys();
  refreshDraft();
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
