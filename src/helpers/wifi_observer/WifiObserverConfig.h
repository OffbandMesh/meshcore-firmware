// src/helpers/wifi_observer/WifiObserverConfig.h
//
// Compile-time configuration for the Crosswire WiFi+MQTT observer
// subsystem. Included by every translation unit under
// src/helpers/wifi_observer/ to centralize tunables.
//
// Gated by CROSSWIRE_OBSERVER -- if undefined, the whole subsystem is
// compiled out. The flag is defined by env variants in
// variants/<board>/platformio.ini under [env:<board>_*_observer_wifi].

#pragma once

#ifndef CROSSWIRE_OBSERVER
  #error "WifiObserverConfig.h included without CROSSWIRE_OBSERVER defined. \
          Check env build_flags or remove the include."
#endif

// ---------------------------------------------------------------------------
// Variant gate (companion vs. repeater)
// ---------------------------------------------------------------------------
// CROSSWIRE_OBSERVER_BLE_COMPANION is defined ONLY in companion variants.
// Plan 1 is companion-only so every Plan-1 env defines it; future repeater
// observer envs (post-v1) will leave it undefined. The wifi_observer
// subsystem itself is identical for both -- this flag is consumed by
// examples/companion_radio/ and the (future) examples/simple_repeater/
// to decide which mesh role to layer on top.

// ---------------------------------------------------------------------------
// Broker pool sizing
// ---------------------------------------------------------------------------
// Hard ceiling on configurable brokers. Plan 1 uses the EastMesh-vendored
// static 3-entry registry with letsmesh-us as the single default-enabled
// broker; Plan 2 introduces the configurable NVS-backed list up to this
// ceiling.
#ifndef CROSSWIRE_MAX_BROKERS
  #define CROSSWIRE_MAX_BROKERS 6
#endif

// ---------------------------------------------------------------------------
// Observer ring buffer
// ---------------------------------------------------------------------------
// Most-recent LoRa packets retained for web UI display. Memory budget
// sized for V3 (no PSRAM, ~280KB free heap). Plan 1 does not exercise
// this buffer; Plan 2 wires the LoRa observer pipeline.
#ifndef CROSSWIRE_MAX_RECENT_PACKETS
  #define CROSSWIRE_MAX_RECENT_PACKETS 50
#endif

// ---------------------------------------------------------------------------
// AP-mode SSID prefix
// ---------------------------------------------------------------------------
// SSID broadcast in first-boot AP mode: <prefix>-<MAC last 6 hex chars>.
// Per spec: "Crosswire-Observer-XXXXXX".
#define CROSSWIRE_AP_SSID_PREFIX  "Crosswire-Observer-"

// ---------------------------------------------------------------------------
// Serial CLI rescue
// ---------------------------------------------------------------------------
// Per spec: hold a designated button for 8 seconds during boot to enter
// CLI Rescue mode. Boot window in milliseconds during which the button
// press is sampled.
#define CROSSWIRE_CLI_RESCUE_BOOT_WINDOW_MS  8000

// ---------------------------------------------------------------------------
// Subsystem version
// ---------------------------------------------------------------------------
// Bumped manually per plan completion. Plan 1 = 0.1.0.
#define CROSSWIRE_OBSERVER_VERSION  "0.1.0-plan1"
