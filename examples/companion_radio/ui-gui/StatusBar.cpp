#include "StatusBar.h"

#include <stdio.h>
#include <string.h>

#include <helpers/gui/SignalLevel.h>
#include <helpers/gui/hw/GuiFonts.h>

using namespace offband::gui;

namespace {

constexpr int16_t kHeight = 20;

lv_color_t c(uint32_t rgb) { return lv_color_hex(rgb & 0xFFFFFF); }

// A bare object: no theme styles, not scrollable, not clickable.
lv_obj_t* box(lv_obj_t* parent, int16_t w, int16_t h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t* line(lv_obj_t* parent, const lv_point_t* pts, uint16_t n, uint32_t rgb) {
  lv_obj_t* l = lv_line_create(parent);
  lv_line_set_points(l, pts, n);
  lv_obj_set_style_line_width(l, 1, 0);
  lv_obj_set_style_line_color(l, c(rgb), 0);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}

lv_obj_t* label(lv_obj_t* parent, FontRole role, uint32_t rgb) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font(role), 0);
  lv_obj_set_style_text_color(l, c(rgb), 0);
  lv_label_set_text(l, "");   // not LVGL's placeholder "Text" before the first reading
  return l;
}

// The icon shapes, in an 11 px box (spec: "Compact: 11px icons in a 20px
// strip"). Point lists live for the program: lv_line keeps the pointer.
const lv_point_t kMailFlap[] = {{1, 1}, {5, 5}, {9, 1}};
const lv_point_t kBle[] = {{0, 3}, {6, 8}, {3, 10}, {3, 0}, {6, 3}, {0, 8}};
const lv_point_t kTickV[] = {{5, 0}, {5, 2}};
const lv_point_t kTickV2[] = {{5, 9}, {5, 11}};
const lv_point_t kTickH[] = {{0, 5}, {2, 5}};
const lv_point_t kTickH2[] = {{9, 5}, {11, 5}};
const lv_point_t kSdCut[] = {{0, 2}, {2, 0}};   // the SD card's clipped corner

uint32_t toneRgb(const ThemeTokens& t, Tone tone) {
  switch (tone) {
    case Tone::Ok: return t.ok;
    case Tone::Warn: return t.warn;
    case Tone::Bad: return t.bad;
    case Tone::Signal: return t.sig;
    case Tone::Dim: break;
  }
  return t.dim;
}

uint32_t roleRgb(const ThemeTokens& t, SignalRole r) {
  return r == SignalRole::Ok ? t.ok : r == SignalRole::Warn ? t.warn : t.bad;
}

}  // namespace

void StatusBar::build(lv_obj_t* parent, const ThemeTokens& t, LongPressFn on_lora, void* ctx) {
  closePopover();   // it lives on the top layer, outside the rebuilt screen
  _t = t;
  _on_lora = on_lora;
  _ctx = ctx;
  _have_shown = false;

  lv_obj_t* strip = box(parent, LV_PCT(100), kHeight);
  lv_obj_align(strip, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(strip, c(t.bg2), 0);
  lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(strip, 1, 0);
  lv_obj_set_style_border_color(strip, c(t.line2), 0);
  lv_obj_set_style_pad_hor(strip, 6, 0);
  lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(strip, 5, 0);

  _clock = label(strip, FontRole::Meta, t.ink);
  lv_label_set_text(_clock, "--:--");

  // With no time zone the clock is UTC (Epic L, mockup 1): an outlined tag.
  _utc = label(strip, FontRole::Meta, t.warn);
  lv_label_set_text(_utc, "UTC");
  lv_obj_set_style_border_width(_utc, 1, 0);
  lv_obj_set_style_border_color(_utc, c(t.warn), 0);
  lv_obj_set_style_radius(_utc, 3, 0);
  lv_obj_set_style_pad_hor(_utc, 2, 0);
  lv_obj_add_flag(_utc, LV_OBJ_FLAG_HIDDEN);

  // Unread: an envelope and the count, in the signal color; hidden at 0.
  _mail = box(strip, LV_SIZE_CONTENT, kHeight);
  lv_obj_set_flex_flow(_mail, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(_mail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(_mail, 2, 0);
  lv_obj_t* env = box(_mail, 11, 9);
  lv_obj_set_style_border_width(env, 1, 0);
  lv_obj_set_style_border_color(env, c(t.sig), 0);
  lv_obj_set_style_radius(env, 1, 0);
  line(env, kMailFlap, 3, t.sig);
  _mail_count = label(_mail, FontRole::Meta, t.sig);
  lv_obj_add_flag(_mail, LV_OBJ_FLAG_HIDDEN);

  // Everything after the spacer sits at the right edge.
  lv_obj_t* spacer = box(strip, 1, 1);
  lv_obj_set_flex_grow(spacer, 1);

  // The SD disk (Epic I, owner 2026-10-21): first of the right-hand icons,
  // green while the card works, red once it's gone or won't mount. A card
  // shape: a filled body with its top-left corner clipped by a line in the
  // strip's own color.
  _sd = box(strip, 8, 11);
  lv_obj_set_style_bg_opa(_sd, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(_sd, 1, 0);
  _sd_cut = line(_sd, kSdCut, 2, t.bg2);
  lv_obj_set_style_line_width(_sd_cut, 3, 0);
  lv_obj_add_flag(_sd, LV_OBJ_FLAG_HIDDEN);

  // LoRa bars: the long-press target, padded out to a finger-sized area.
  _bars = box(strip, 11, kHeight);
  lv_obj_add_flag(_bars, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(_bars, 10);
  lv_obj_add_event_cb(
      _bars,
      [](lv_event_t* e) {
        StatusBar* self = static_cast<StatusBar*>(lv_event_get_user_data(e));
        if (self->_on_lora) self->_on_lora(self->_ctx);
      },
      LV_EVENT_LONG_PRESSED, this);
  // Bars stand on a common baseline, 5 px below the strip's middle.
  lv_obj_set_flex_flow(_bars, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(_bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(_bars, 1, 0);
  lv_obj_set_style_pad_bottom(_bars, 5, 0);
  static const int16_t kBarH[4] = {3, 5, 7, 10};
  for (int i = 0; i < 4; i++) {
    _bar[i] = box(_bars, 2, kBarH[i]);
    lv_obj_set_style_bg_opa(_bar[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(_bar[i], c(t.line), 0);
  }

  _ble = box(strip, 7, 11);
  line(_ble, kBle, 6, t.sig);

  _gps = box(strip, 12, 12);
  _gps_ring = box(_gps, 7, 7);
  lv_obj_center(_gps_ring);
  lv_obj_set_style_radius(_gps_ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(_gps_ring, 1, 0);
  _gps_dot = box(_gps, 3, 3);
  lv_obj_center(_gps_dot);
  lv_obj_set_style_radius(_gps_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(_gps_dot, LV_OPA_COVER, 0);
  line(_gps, kTickV, 2, t.sig);
  line(_gps, kTickH, 2, t.sig);
  line(_gps, kTickV2, 2, t.sig);
  line(_gps, kTickH2, 2, t.sig);

  lv_obj_t* batt = box(strip, LV_SIZE_CONTENT, kHeight);
  lv_obj_set_flex_flow(batt, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(batt, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(batt, 2, 0);
  lv_obj_t* shell = box(batt, 18, 9);
  lv_obj_set_style_border_width(shell, 1, 0);
  lv_obj_set_style_border_color(shell, c(t.ink2), 0);
  lv_obj_set_style_radius(shell, 2, 0);
  lv_obj_set_style_pad_all(shell, 1, 0);
  _batt_fill = box(shell, 0, LV_PCT(100));
  lv_obj_set_style_bg_opa(_batt_fill, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(_batt_fill, 1, 0);
  _batt_pct = label(batt, FontRole::Meta, t.ink2);

  // A screen change rebuilds the strip. Fill it from the last readings now,
  // not at the next quarter-second update, or every tab tap blanks the
  // clock and icons for a frame and shows LVGL's placeholder "Text" (T-Deck
  // bench video, #1730).
  if (_ever_shown) update(_shown, lv_tick_get());
}

void StatusBar::update(const StatusReadings& r, uint32_t now_ms) {
  if (!_clock) return;
  const bool first = !_have_shown;
  if (first || strcmp(r.clock, _shown.clock) != 0) lv_label_set_text(_clock, r.clock);
  if (first || r.utc != _shown.utc) {
    if (r.utc) lv_obj_clear_flag(_utc, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(_utc, LV_OBJ_FLAG_HIDDEN);
  }

  if (first || r.unread != _shown.unread) {
    if (showUnread(r.unread)) {
      lv_label_set_text_fmt(_mail_count, "%lu", (unsigned long)r.unread);
      lv_obj_clear_flag(_mail, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(_mail, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (first || r.sd != _shown.sd) {
    if (r.sd == SdIcon::None) {
      lv_obj_add_flag(_sd, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_set_style_bg_color(_sd, c(r.sd == SdIcon::Green ? _t.ok : _t.bad), 0);
      lv_obj_clear_flag(_sd, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (first || r.bars != _shown.bars || r.lora_heard != _shown.lora_heard) {
    const uint32_t lit = roleRgb(_t, signalRole(r.bars));
    for (int i = 0; i < 4; i++) {
      const bool on = r.lora_heard && i < r.bars;
      lv_obj_set_style_bg_color(_bar[i], c(on ? lit : _t.line), 0);
    }
  }

  if (first || r.ble != _shown.ble) {
    if (r.ble == BleIcon::Hidden) {
      lv_obj_add_flag(_ble, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(_ble, LV_OBJ_FLAG_HIDDEN);
      const uint32_t rgb = r.ble == BleIcon::Connected ? _t.sig : _t.dim;
      lv_obj_set_style_line_color(lv_obj_get_child(_ble, 0), c(rgb), 0);
    }
  }

  // GPS: colors change with the state; the seeking blink only toggles
  // visibility, so a pass that changes nothing invalidates nothing.
  if ((first || r.gps != _shown.gps) && r.gps != GpsIcon::Hidden) {
    const uint32_t rgb = r.gps == GpsIcon::Seeking ? _t.warn : r.gps == GpsIcon::TimedOut ? _t.bad : _t.ok;
    lv_obj_set_style_border_color(_gps_ring, c(rgb), 0);
    lv_obj_set_style_bg_color(_gps_dot, c(rgb), 0);
    // The filled centre marks a 3D fix; every other state is hollow.
    if (r.gps == GpsIcon::Fix3D) lv_obj_clear_flag(_gps_dot, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(_gps_dot, LV_OBJ_FLAG_HIDDEN);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(_gps); i++) {
      lv_obj_t* ch = lv_obj_get_child(_gps, i);
      if (lv_obj_check_type(ch, &lv_line_class)) lv_obj_set_style_line_color(ch, c(rgb), 0);
    }
  }
  if (first || r.gps != _shown.gps) {
    if (r.gps == GpsIcon::Hidden) lv_obj_add_flag(_gps, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(_gps, LV_OBJ_FLAG_HIDDEN);
    // Seeking pulses: the icon stays put and fades to 35% and back once a
    // second (owner, bench 2026-10-16: hiding it outright read as flicker).
    lv_anim_del(_gps, nullptr);
    lv_obj_set_style_opa(_gps, LV_OPA_COVER, 0);
    if (r.gps == GpsIcon::Seeking) {
      lv_anim_t a;
      lv_anim_init(&a);
      lv_anim_set_var(&a, _gps);
      lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_40 - 5);
      lv_anim_set_time(&a, 500);
      lv_anim_set_playback_time(&a, 500);
      lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
      lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
      lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, lv_opa_t(v), 0); });
      lv_anim_start(&a);
    }
  }
  (void)now_ms;

  if (first || r.batt_pct != _shown.batt_pct) {
    const uint8_t pct = r.batt_pct > 100 ? 100 : r.batt_pct;
    lv_obj_set_width(_batt_fill, (14 * pct + 50) / 100);
    lv_obj_set_style_bg_color(_batt_fill, c(toneRgb(_t, batteryTone(pct))), 0);
    lv_label_set_text_fmt(_batt_pct, "%u%%", (unsigned)pct);
  }

  _shown = r;
  _have_shown = true;
  _ever_shown = true;
}

void StatusBar::openPopover(const LoraReadings& r, uint32_t now_ms) {
  closePopover();
  // A scrim under the strip dims the screen; tapping it closes the popover.
  _pop = box(lv_layer_top(), LV_PCT(100), LV_PCT(100));
  lv_obj_add_flag(_pop, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(_pop, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(_pop, LV_OPA_50, 0);
  lv_obj_add_event_cb(
      _pop, [](lv_event_t* e) { static_cast<StatusBar*>(lv_event_get_user_data(e))->closePopover(); },
      LV_EVENT_CLICKED, this);

  lv_obj_t* card = box(_pop, 150, LV_SIZE_CONTENT);
  lv_obj_align(card, LV_ALIGN_TOP_RIGHT, -6, kHeight + 4);
  lv_obj_set_style_bg_color(card, c(_t.bg3), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, c(_t.line), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_set_style_pad_hor(card, 9, 0);
  lv_obj_set_style_pad_ver(card, 7, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 3, 0);

  char buf[40];
  lv_obj_t* title = label(card, FontRole::Label, _t.dim);
  loraTitle(r.packets, buf, sizeof buf);
  lv_label_set_text(title, buf);
  if (r.packets == 0) {
    _pop_at = now_ms;
    return;
  }

  auto row = [&](const char* name, const char* value, uint32_t rgb) {
    lv_obj_t* line_box = box(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_t* k = label(line_box, FontRole::Meta, _t.ink2);
    lv_label_set_text(k, name);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* v = label(line_box, FontRole::Meta, rgb);
    lv_label_set_text(v, value);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
  };
  formatSnr(r.snr_db, buf, sizeof buf);
  row("SNR (median)", buf, _t.ink);
  formatDbm(r.rssi, buf, sizeof buf);
  row("RSSI", buf, _t.ink);
  formatDbm(r.noise, buf, sizeof buf);
  row("Noise floor", buf, _t.ink);
  formatMargin(r.snr_db, r.sf, buf, sizeof buf);
  char margin[48];
  snprintf(margin, sizeof margin, "margin %s", buf);
  row(margin, signalWord(signalBars(r.snr_db, r.sf)), _t.sig);
  char floor_db[32];
  formatSnr(demodFloorDb(r.sf), buf, sizeof buf);
  snprintf(floor_db, sizeof floor_db, "SF%u floor %s", (unsigned)r.sf, buf);
  lv_obj_t* foot = label(card, FontRole::Label, _t.dim);
  lv_label_set_text(foot, floor_db);
  _pop_at = now_ms;
}

void StatusBar::closePopover() {
  if (!_pop) return;
  lv_obj_del_async(_pop);
  _pop = nullptr;
}

void StatusBar::tick(uint32_t now_ms) {
  if (_pop && now_ms - _pop_at >= kPopoverCloseMs) closePopover();
}
