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
// AP setup form (Plan 3 Task 10b, Strycher/LoRa#272)
// ---------------------------------------------------------------------------
// CROSSWIRE_AP_SETUP_FORM_ENABLED is the build flag for the SECONDARY
// first-contact WiFi setup path: a softAP + plain-HTTP form on :80
// served from src/helpers/wifi_observer/ApSetupForm.cpp.
//
// DEFAULT OFF. Define non-zero in an env's build_flags to opt in,
// e.g.:
//   build_flags = ${env:Heltec_v3_companion_radio_ble.build_flags}
//                 -D CROSSWIRE_AP_SETUP_FORM_ENABLED=1
//
// When enabled, the no-creds branch in WifiBootstrap::begin() ALSO
// starts the AP form (in addition to posting the welcome message
// on the BLE system channel from Task 10). Both paths coexist;
// whichever the user completes first wins. Both write the same NVS
// keys (namespace "wifi", keys "ssid" + "pwd").
//
// When unset, both ApSetupForm.{h,cpp} and the call sites in
// WifiBootstrap.cpp compile out completely (verified via firmware.elf
// symbol grep -- no `apSetupForm*` symbols present in the default
// build).

// ---------------------------------------------------------------------------
// Serial CLI rescue
// ---------------------------------------------------------------------------
// Per spec: hold a designated button for 8 seconds during boot to enter
// CLI Rescue mode. Boot window in milliseconds during which the button
// press is sampled.
#define CROSSWIRE_CLI_RESCUE_BOOT_WINDOW_MS  8000

// ---------------------------------------------------------------------------
// Crosswire fork version (per VERSIONING.md Pattern B)
// ---------------------------------------------------------------------------
// CROSSWIRE_VERSION is injected at build time by scripts/inject_crosswire_version.py
// from `git describe --tags --match 'crosswire-v*' --abbrev=7 --dirty`. The
// upstream baseline lives in MeshCore's FIRMWARE_VERSION (set per-example in
// MyMesh.h). Every Crosswire-aware log line / banner / UI surface should
// expose BOTH identifiers per VERSIONING.md Pattern B:
//   "MC <FIRMWARE_VERSION> / Crosswire <CROSSWIRE_VERSION>"
//
// Host test builds don't invoke the PIO extra_script, so provide a sentinel
// fallback here. Real device builds always have the macro injected and this
// branch is never taken.
#ifndef CROSSWIRE_VERSION
  #define CROSSWIRE_VERSION  "host-untagged"
#endif

// ---------------------------------------------------------------------------
// Crosswire observer release line (per VERSIONING.md Plan-cycle tag)
// ---------------------------------------------------------------------------
// CROSSWIRE_OBSERVER_VERSION marks the WiFi+BLE Observer feature's plan
// cycle (Plan 1 / Plan 2 / Plan 3 / Plan 4) independent of the
// git-describe-derived CROSSWIRE_VERSION. Logged at boot + surfaced on
// the web UI footer + /api/status so we can correlate field reports
// with the integration milestone, not just the fork tag.
#ifndef CROSSWIRE_OBSERVER_VERSION
  #define CROSSWIRE_OBSERVER_VERSION  "0.3.0-plan3"
#endif
