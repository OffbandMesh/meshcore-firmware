// src/helpers/wifi_observer/WifiObserverConfig.h
//
// Compile-time configuration for the Offband WiFi+MQTT observer
// subsystem. Included by every translation unit under
// src/helpers/wifi_observer/ to centralize tunables.
//
// Gated by OFFBAND_OBSERVER -- if undefined, the whole subsystem is
// compiled out. The flag is defined by env variants in
// variants/<board>/platformio.ini under [env:<board>_*_observer_wifi].

#pragma once

// #536: the broker-pool engine (MqttBrokerPool/MqttBroker) legitimately needs
// the engine tunables below (OFFBAND_MAX_BROKERS / OFFBAND_MAX_LIVE_TLS /
// OFFBAND_TLS_HEAP_FLOOR_BYTES) on the REPEATER too, without the observer
// pipeline. Accept OFFBAND_MQTT_POOL as an alternative gate. The AP-SSID / CLI-
// rescue tunables further down are observer-only but harmless to the repeater.
#if !defined(OFFBAND_OBSERVER) && !defined(OFFBAND_MQTT_POOL)
  #error "WifiObserverConfig.h needs OFFBAND_OBSERVER or OFFBAND_MQTT_POOL \
          (the broker-pool engine, #536) defined. Check env build_flags."
#endif

// ---------------------------------------------------------------------------
// Variant gate (companion vs. repeater)
// ---------------------------------------------------------------------------
// OFFBAND_OBSERVER_BLE_COMPANION is defined ONLY in companion variants.
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
// pre-seeded public brokers (under the #317/#677 layout: OKIMesh x2 +
// MeshMapper + CoreComms.net + Eastmesh.au = 5 slots, slots 0-4) leave five
// free slots (5-9) for operator-custom brokers.
//
// Static cost: MqttBrokerPool holds brokers_[OFFBAND_MAX_BROKERS], and
// BrokerConfig is ~1.2 KB/slot (jwt_token[512] dominates), so 10 slots is
// ~12 KB static. MEASURED fine even on no-PSRAM HV3: the Heltec_v3 observer
// build uses 27.4% RAM (89.7 KB / 327 KB internal SRAM) with this at 10 --
// ~238 KB free, the +4.7 KB of the 6->10 bump is ~1.4% of SRAM. This is the
// CONFIGURABLE ceiling, NOT a concurrency target: simultaneously *enabling*
// many wss/TLS brokers is heap-bound by mbedTLS (~3-5 on HV3), independent of
// this number.
//
// Caveat (#95/#98): `mqtt status` assembles every configured slot into one
// kSystemChannelReplyBufLen (768 B) buffer before splitting to _sys frames.
// The 6-broker default set already sits near that ceiling, so configuring the
// custom slots 6-9 can truncate the tail of `mqtt status` on the _sys surface
// until that buffer is paged/enlarged (tracked in #98).
#ifndef OFFBAND_MAX_BROKERS
  #define OFFBAND_MAX_BROKERS 10
#endif

// #171 -- TLS broker concurrency cap (TEMPORARY; superseded by the round-robin
// scheduler #175). Each live wss/TLS broker holds a ~60KB mbedTLS context.
// MEASURED on HV3 (~124KB free with WiFi up): ONE wss broker settles at ~63KB
// free (heap_min ~52KB) -- safe; TWO wss brokers collapse free heap to ~1.5KB
// (heap_min 456 bytes) -- the #171 OOM knife-edge. So HV3 safely holds exactly
// ONE concurrent TLS context. The pool refuses to START a TLS handshake past
// this many live contexts; the broker self-defers to BrokerState::HeldNoHeap
// (no retry burned, released once the live slot frees). Plaintext (tcp) brokers
// hold no mbedTLS context -- exempt. (=1 also removes the start-race where two
// brokers begin handshakes in one pass before either's async ~60KB lands.) The
// scheduler #175 cycles feeds through this 1-wide budget on PSRAM-less boards;
// PSRAM boards override higher.
#ifndef OFFBAND_MAX_LIVE_TLS
  #define OFFBAND_MAX_LIVE_TLS 1
#endif

// Free-heap floor (bytes): the actual safety guarantee. Bringing up a wss broker
// transiently peaks at ~72KB consumed (MEASURED on HV3: free 124KB -> heap_min
// ~52KB during the handshake, settling to ~63KB). We refuse to START a TLS
// handshake unless free heap exceeds this floor, so the transient always fits --
// the floor MUST exceed the ~72KB transient or the check is worse than useless
// (at 50KB free it would pass, then OOM mid-handshake; Gemini #171 BLOCKER).
// 80KB = ~72KB measured transient + margin. With OFFBAND_MAX_LIVE_TLS=1 this is
// a backstop (the count cap already prevents a 2nd context); it also guards the
// 1st bring-up under any pre-existing low-heap condition. ESP.getFreeHeap() units.
#ifndef OFFBAND_TLS_HEAP_FLOOR_BYTES
  #define OFFBAND_TLS_HEAP_FLOOR_BYTES (80u * 1024u)
#endif

// ---------------------------------------------------------------------------
// AP-mode SSID prefix
// ---------------------------------------------------------------------------
// SSID broadcast in first-boot AP mode: <prefix>-<MAC last 6 hex chars>.
// Per spec: "Offband-Observer-XXXXXX".
#define OFFBAND_AP_SSID_PREFIX  "Offband-Observer-"

// ---------------------------------------------------------------------------
// Serial CLI rescue
// ---------------------------------------------------------------------------
// Per spec: hold a designated button for 8 seconds during boot to enter
// CLI Rescue mode. Boot window in milliseconds during which the button
// press is sampled.
#define OFFBAND_CLI_RESCUE_BOOT_WINDOW_MS  8000

// ---------------------------------------------------------------------------
// Offband fork version (per VERSIONING.md Pattern B)
// ---------------------------------------------------------------------------
// OFFBAND_VERSION is injected at build time by scripts/inject_offband_version.py
// from `git describe --tags --match 'offband-v*' --abbrev=7 --dirty`. The
// upstream baseline lives in MeshCore's FIRMWARE_VERSION (set per-example in
// MyMesh.h). Every Offband-aware log line / banner / UI surface should
// expose BOTH identifiers per VERSIONING.md Pattern B:
//   "MC <FIRMWARE_VERSION> / Offband <OFFBAND_VERSION>"
//
// Host test builds don't invoke the PIO extra_script, so provide a sentinel
// fallback here. Real device builds always have the macro injected and this
// branch is never taken.
#ifndef OFFBAND_VERSION
  #define OFFBAND_VERSION  "host-untagged"
#endif
