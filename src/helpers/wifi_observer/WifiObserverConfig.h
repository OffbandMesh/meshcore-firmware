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
// Hard ceiling on configurable broker slots. Plan 2 introduced the
// configurable NVS-backed list; #95 raised the ceiling from 6 to 10 so the
// pre-seeded public brokers (CoreScope + LetsMesh US/EU + Eastme.sh +
// MeshMapper + Eastmesh.au = 6 slots, slots 0-5) leave four free slots (6-9)
// for operator-custom brokers.
//
// Static cost: MqttBrokerPool holds brokers_[CROSSWIRE_MAX_BROKERS], and
// BrokerConfig is ~1.2 KB/slot (jwt_token[512] dominates), so 10 slots is
// ~12 KB static -- fine even on HV3. This is the CONFIGURABLE ceiling, NOT a
// concurrency target: simultaneously *enabling* many wss/TLS brokers is
// heap-bound by mbedTLS (~3-5 on HV3), independent of this number.
//
// Caveat (#95/#98): `mqtt status` assembles every configured slot into one
// kSystemChannelReplyBufLen (768 B) buffer before splitting to _sys frames.
// The 6-broker default set already sits near that ceiling, so configuring the
// custom slots 6-9 can truncate the tail of `mqtt status` on the _sys surface
// until that buffer is paged/enlarged (tracked in #98).
#ifndef CROSSWIRE_MAX_BROKERS
  #define CROSSWIRE_MAX_BROKERS 10
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
