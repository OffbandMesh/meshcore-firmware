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

void GuiShell::begin(const ThemeTokens& t, Look look, const Hooks& hooks) {
  _hooks = hooks;
  _group = lv_group_create();
  setTheme(t, look);
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

  if (_locked) {
    _status.closePopover();
    buildLock(scr);
    return;
  }
  _status.build(scr, _t, _hooks.lora_popover, _hooks.ctx);
  lv_obj_t* content = box(scr, kW, kContentH);
  lv_obj_set_pos(content, 0, kStatusH);
  if (_tab == Tab::Home) buildHome(content);
  else buildPlaceholder(content);
  buildTabs(scr);
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
    lv_label_set_long_mode(_sub[i], LV_LABEL_LONG_DOT);
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
      _advert,
      [](lv_event_t* e) {
        GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
        if (self->_hooks.send_advert) self->_hooks.send_advert(self->_hooks.ctx);
      },
      LV_EVENT_CLICKED, this);
  _advert_shown = AdvertButton::State::Idle;
}

// Tabs whose screens come in later Epics.
void GuiShell::buildPlaceholder(lv_obj_t* content) {
  static const char* const kWhen[] = {"", "Chats come in D4.", "Nodes come in Epic E.", "The map comes in Epic F.",
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
    // The switch runs after the event: rebuilding deletes this tab, which
    // LVGL is still dispatching to.
    lv_obj_add_event_cb(
        t,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_pending_tab = Tab(lv_obj_get_index(lv_event_get_target(e)));
          lv_async_call([](void* p) { static_cast<GuiShell*>(p)->showTab(static_cast<GuiShell*>(p)->_pending_tab); },
                        self);
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
            self->_pending_tab = self->_tab;
            lv_async_call(
                [](void* p) { static_cast<GuiShell*>(p)->showTab(static_cast<GuiShell*>(p)->_pending_tab); }, self);
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
  switch (a) {
    case Action::Next:
      lv_group_focus_next(_group);
      return true;
    case Action::Prev:
      lv_group_focus_prev(_group);
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
    lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
  }
  if (d.more) {
    lv_obj_t* more = label(_lock_list, FontRole::BodySmall, _t.dim);
    lv_label_set_text_fmt(more, "+%lu more", (unsigned long)d.more);
    lv_obj_set_style_pad_top(more, 4, 0);
  }
}
