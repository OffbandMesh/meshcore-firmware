#pragma once

// The 20 px status strip at the top of every screen but Lock (#1496, PLAN.md
// §3 D2; spec v0.2/v0.3 "Status bar"): clock, unread, then LoRa bars,
// Bluetooth, GPS and battery on the right. Icons are drawn from LVGL shapes,
// not font glyphs, so each theme recolors them.
//
// A long press on the LoRa bars opens a popover with SNR, RSSI and noise
// floor (owner-approved mockup 4, 2026-10-16); any tap, Back or 5 s closes it.

#include <lvgl.h>
#include <stdint.h>

#include <helpers/gui/GuiTheme.h>
#include <helpers/gui/GuiViews.h>
#include <helpers/gui/SdMonitor.h>

struct StatusReadings {
  char clock[12];   // "21:07", or "9:07 PM" (12-hour)
  bool utc;         // no zone is set: the clock is UTC and is tagged so (#1610)
  uint32_t unread;
  bool lora_heard;   // at least one packet in the SNR window
  uint8_t bars;      // 0..4
  offband::gui::BleIcon ble;
  offband::gui::GpsIcon gps;
  uint8_t batt_pct;
  offband::gui::SdIcon sd;   // Epic I: green working, red gone or unreadable, none without a card
};

struct LoraReadings {
  size_t packets;    // in the SNR window, up to kSnrWindow
  float snr_db;      // median
  int16_t rssi;      // last packet, dBm
  int16_t noise;     // noise floor, dBm
  uint8_t sf;
};

class StatusBar {
public:
  using LongPressFn = void (*)(void* ctx);

  // Builds the strip at the top of `parent`. Call again after a theme change.
  void build(lv_obj_t* parent, const offband::gui::ThemeTokens& t, LongPressFn on_lora, void* ctx);
  void update(const StatusReadings& r, uint32_t now_ms);

  void openPopover(const LoraReadings& r, uint32_t now_ms);
  void closePopover();
  bool popoverOpen() const { return _pop != nullptr; }
  // Closes the popover when its time is up.
  void tick(uint32_t now_ms);

private:
  offband::gui::ThemeTokens _t{};
  lv_obj_t* _clock = nullptr;
  lv_obj_t* _utc = nullptr;       // the amber UTC tag, hidden once a zone is set
  lv_obj_t* _mail = nullptr;      // icon + count, hidden at 0
  lv_obj_t* _mail_count = nullptr;
  lv_obj_t* _sd = nullptr;        // the SD disk (Epic I), hidden without a card
  lv_obj_t* _sd_cut = nullptr;    // its clipped corner, drawn in the strip's color
  lv_obj_t* _bars = nullptr;
  lv_obj_t* _bar[4] = {};
  lv_obj_t* _ble = nullptr;
  lv_obj_t* _gps = nullptr;
  lv_obj_t* _gps_ring = nullptr;
  lv_obj_t* _gps_dot = nullptr;
  lv_obj_t* _batt_fill = nullptr;
  lv_obj_t* _batt_pct = nullptr;
  lv_obj_t* _pop = nullptr;       // the popover's scrim; null when closed
  uint32_t _pop_at = 0;
  LongPressFn _on_lora = nullptr;
  void* _ctx = nullptr;
  StatusReadings _shown{};
  bool _have_shown = false;   // this strip has had a reading (reset by build)
  bool _ever_shown = false;   // any strip has: _shown holds the last readings
};
