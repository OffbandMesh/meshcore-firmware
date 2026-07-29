#pragma once
#include <cstdint> // For uint8_t, uint32_t

#define TELEM_MODE_DENY            0
#define TELEM_MODE_ALLOW_FLAGS     1     // use contact.flags
#define TELEM_MODE_ALLOW_ALL       2

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1

struct NodePrefs {  // persisted to file
  float airtime_factor;
  char node_name[32];
  float freq;
  uint8_t sf;
  uint8_t cr;
  uint8_t multi_acks;
  uint8_t manual_add_contacts;
  float bw;
  int8_t tx_power_dbm;
  uint8_t telemetry_mode_base;
  uint8_t telemetry_mode_loc;
  uint8_t telemetry_mode_env;
  float rx_delay_base;
  uint32_t ble_pin;
  uint8_t  advert_loc_policy;
  uint8_t  buzzer_quiet;
  uint8_t  gps_enabled;      // GPS enabled flag (0=disabled, 1=enabled)
  uint32_t gps_interval;     // GPS read interval in seconds
  uint8_t autoadd_config;    // bitmask for auto-add contacts config
  uint8_t rx_boosted_gain; // SX126x RX boosted gain mode (0=power saving, 1=boosted)
  // #298: external FEM LNA enable (1 = LNA on, 0 = bypass). Default 1 (ON), matching
  // the repeater. Only meaningful where MainBoard::canControlLoRaFemLna() is true.
  // NOTE: serialized LAST in DataStore (offset 137), NOT next to rx_boosted_gain --
  // the prefs file is a flat offset-tracked stream with no version/length/CRC, so new
  // fields must be appended at the end or they shift every field after them and
  // corrupt saved config on already-deployed devices.
  uint8_t radio_fem_rxgain;
  uint8_t client_repeat;
  uint8_t path_hash_mode;    // which path mode to use when sending
  uint8_t autoadd_max_hops;  // 0 = no limit, 1 = direct (0 hops), N = up to N-1 hops (max 64)
  char default_scope_name[31];
  uint8_t default_scope_key[16];
  // #428: caplog (serial-capture) persistence across reboot. When caplog_enabled,
  // boot restores the sink at caplog_level early in MyMesh::begin() so the boot log
  // is captured (Start -> reboot -> keeps capturing until Stop). Like radio_fem_rxgain,
  // these are serialized LAST in DataStore (offsets 138/139), NOT next to the fields
  // above -- the prefs file is a flat offset-tracked stream with no version/CRC, so new
  // fields MUST be appended at the end or they shift every field after them and corrupt
  // saved config on already-deployed devices. Only the FLAG persists; the capture ring
  // stays plain-RAM non-retained (empty after reboot, fills with the fresh boot log).
  uint8_t caplog_enabled;    // 0 = off, 1 = capture-on-boot until an explicit Stop
  uint8_t caplog_level;      // MLOG_* level to restore (0=boot..3=packet); default DEBUG(2)
};