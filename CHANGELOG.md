# Changelog

All notable changes to Offband are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). This
project uses **Pattern B fork versioning** -- an independent
`offband-vMAJOR.MINOR.PATCH` line (historically `crosswire-v*`) with the upstream MeshCore baseline tracked
separately. See [VERSIONING.md](VERSIONING.md) for the cadence (commit per task /
compile, PR per epic, tag per landing), the `+N` dev-build suffix, and the
dev / `-rc` / stable release channels.

This changelog begins at the unified `firmware-base` line (**0.13.0**). Versions
prior to 0.13.0 predate it; see `git tag -l 'crosswire-v*'` and the tag
annotations for that history (highlights: v0.12.0 NimBLE migration, v0.11.x
Plan-3 web UI, v0.10.x observer multi-broker pipeline, v0.5.0 initial backfill).

## [Unreleased]

## [1.5.0] - 2026-08-15

### Fixed (beta2)
- **Rotated-out brokers no longer lose the traffic they missed** (#723): a regression in
  `beta1`. Re-attaching a broker resynced its ring cursor to the head, which was meant to
  stop a *newly enabled* broker replaying stale boot backlog — but the same path runs on
  every rotation re-entry, so a broker returning from its dwell discarded exactly the
  backlog the ring exists to hold. On an observer with rotating TLS brokers this silently
  dropped most of the feed, and the drop counter read zero throughout because a resync is
  a deliberate skip, not an overrun. Resync now happens on first attach only.
- **Observer packets over ~98 bytes were silently discarded** (#726): the `/packets`
  JSON builder renders up to 1024 bytes but the publish ring only accepted 512, and a
  refused append counted nothing anywhere. Hex-encoding doubles the packet, so anything
  over ~98 bytes of air time simply vanished — present since the ring was introduced
  (#175), found in the field on beta1 by a tester whose observer published one small
  message and silently dropped the two larger ones. The ring now accepts what the
  builder produces, and a compile-time assert makes the size mismatch a build error
  rather than a silent refusal.
- **BLE transfers no longer lose 3 bytes per full frame** (#711): a regression of #450,
  reintroduced by #454 — when the cached peer MTU was ≥ 179, every full BLE frame was
  sized 3 bytes over what the link delivers and the stack clipped the tail. Caplog
  downloads noticed (they announce a byte total); every other chunked path clipped
  silently. Frame sizing is now pinned by a regression test that fails without the fix.
  Not yet device-verified — that is what this beta is for.
- **Framed serial writes are all-or-nothing** (#718): a short non-blocking write could
  emit a frame header with no payload behind it, desyncing the stream — corruption, not
  just loss. A refused frame is now retried whole; nothing partial reaches the wire.
  Also awaiting device verification on this beta.
- **Repeater OTA recovers from a stale session** (#676): starting OTA over WiFi tears
  down a stale server instead of refusing, and the repeater holds off idle light-sleep
  while a persistent-WiFi window is open so the OTA window stays reachable.

### Changed (beta2)
- **Observer publish ring reworked; `/packets` payload slimmed** (#727): the ring now
  stores compact packet records rendered per-broker at publish time instead of
  pre-rendered JSON — roughly 10 KB less RAM and a 40-deep ring, with receive
  timestamps captured at receive so a backlogged broker's messages aren't mis-stamped.
  The `/packets` body now carries the raw packet plus link metadata only: CoreScope and
  its forks (CoreComms/EastMesh, confirmed from their public ingestor source) decode
  the raw packet themselves and never read the on-device parsed fields. **If you run a
  custom `/packets` consumer that read parsed fields, decode from `raw` now.**

### Added (beta2)
- **Heltec Vision Master E290 builds** (#733): companion BLE + USB for the first e-ink
  board in the fleet, from the upstream 1.17 variant. The radio/mesh side is stock
  upstream support; the e-ink display path under the Offband stack is exactly what beta
  testing needs to prove. Repeater role deferred.

### Added
- **`wifi clear` and a visible reason for WiFi disconnects** (#696): an observer on an
  open, passwordless network could sit at `StaConnecting` indefinitely with no reason
  available anywhere, and a stored PSK could not be cleared without an NVS wipe that
  destroys the device identity. `wifi clear [pwd|all]` now removes stored credentials
  properly, and STA disconnect reasons appear in the `wifi status` reply as well as the
  retry log — the status reply being the load-bearing half, since a remote reporter
  cannot read a serial console. Association behaviour is unchanged.

### Fixed
- **Observer multi-broker rotation actually rotates** (#708): with more than two TLS
  brokers enabled, promotion was a first-eligible-by-slot-index scan whose only
  fairness mechanism only works at exactly two candidates. Every broker above the two
  lowest slots was starved permanently — measured on a Heltec V3 as 36/35/**0**
  promotions over 80 minutes, with the third broker never publishing once. Promotion is
  now oldest-served-first, verified on hardware as an even three-way cycle. The
  selection logic moved into a dependency-free header with unit tests, because the
  defect is invisible at two brokers and two brokers is what it was validated at.
- **Broker slot 0 is an always-on primary again** (#707): a tcp broker holds no mbedTLS
  context and is therefore exempt from TLS rotation — that exemption is the only thing
  that ever made slot 0 always-on. Moving slots 0–1 to `wss` in 1.4.0 silently demoted
  slot 0 into the rotating pool, where it was torn down and rebuilt every ~2 minutes
  (178 evictions in one 6.9 h soak). The seed puts slot 0 back on
  `mqtt://mqtt1.okimesh.org:1883`, with the trade recorded at the seed row so it is not
  "upgraded" again by accident. Existing devices need the matching config profile —
  firmware seeds only apply to fresh NVS.
- **JWT brokers could not be configured from a config profile** (#709): the config wire
  key was `jwt_audience` while the schema, `SCHEMA.md`, and every published profile used
  `jwt_aud`, so setting the audience by its documented name failed with "unknown broker
  field". Every JWT broker slot was unconfigurable. Both spellings are now accepted, on
  the setter and the getter, keyed off the schema constant so they cannot drift apart
  again.
- **Publish-ring overrun is counted instead of discarded silently** (#710): when a
  rotated-out broker fell behind, the ring clamped its cursor, counted nothing, logged
  nothing, and the only indicator reset itself as soon as the broker caught up — a fleet
  could discard most of its feed with every diagnostic reading clean. There is now a
  monotonic per-broker drop counter, reported in `mqtt status` as `ring_dropped` and on a
  60-second `[ring]` line so a passive serial capture measures it. A newly-attached
  broker resyncs to the ring head instead of replaying stale backlog.

### Changed
- **"Held, low heap" no longer reports normal rotation as a memory error** (#715): one
  broker state covered three different conditions and was named after the rarest. With
  multiple TLS brokers the common case is "waiting its turn", which displayed as a
  memory fault — on a 3-broker pool, 2 of 3 permanently showed a problem that did not
  exist. Serial and CLI now distinguish `waiting-turn` from `held(low-heap)`. **The app
  still shows the old wording**: the wire value is deliberately unchanged until the
  client can accept the new one.
- **Observer builds trimmed and the publish ring deepened** (#701/#710): observer envs
  drop to 5 contacts, 4 user channels and 6 broker slots, freeing 14,660 B of static
  RAM; 8,192 B of that funds a 32-slot publish ring (was 16), sized from measured
  fleet traffic rather than estimated. **On these builds the BLE-companion half is
  capped at 5 contacts and 4 channels** — devices holding more truncate on load
  (favourites and most-recent survive). Non-observer builds are byte-for-byte unchanged.


## [1.4.0] - 2026-08-13

### Added
- **Settings survive the 1.16 → 1.17 upgrade** (#627/#659/#665): a legacy-prefs layout
  detector with Offband-aware offsets migrates existing device preferences across the
  upstream prefs-layout change; cross-family records are refused rather than mis-read;
  caplog settings now apply on the 1.17 `/prefs.json` path; and every migration outcome
  leaves a persistent event record that survives reboot. Hardware-verified across 7
  device/architecture cells.
- **On-air packet hash returned on channel-send confirmation** (#611, capability-gated):
  lets the client correlate an outgoing channel message with CoreScope observer counts
  by packet hash.

### Fixed
- **Future-poisoned clocks are now recoverable** (#607, PRs #612): owner-authenticated
  time sets (client sync, CLI `time`/`clock sync`) are accepted in **both directions** —
  "clock cannot go backwards" refusals are gone. Automated clock sources (GPS decode,
  boot-time contacts bootstrap) are bounded by a plausibility window (firmware build
  date → +20 y), so a GPS week-rollover mis-decode can no longer poison the clock, and
  contact `lastmod` values written under a poisoned clock are clamped on load — stores
  self-heal at first boot. Every accepted clock set (and boot-capped rejections) emits a
  `[clock]` audit line on serial/caplog. **Note for recovered nodes:** peers that heard a
  node's poisoned (future) timestamps may ignore its messages until their per-contact
  recency view catches up; re-adding the contact on the peer resolves it immediately.
- **nRF52 BLE companions crash-boot loop on the 1.17.0 base** (#668): RAK4631 and
  T1000-E BLE companions double-initialized BLE and crash-looped ~4 s after boot;
  never in a tagged release, listed for bench-build testers.
- **Caplog retrieval uses one shared redaction pass on both paths** (#667): closes a
  gap where one retrieval path could return lines the other would have redacted.
- **Wio Tracker L1: stale SafeBoot battery-divider gate removed** (#620).

### Changed
- **MeshCore base updated 1.16.0 → 1.17.0** (#628/#643): full upstream refresh of the
  fork base, bringing upstream 1.17.0 fixes and board support forward under the
  Offband feature set.
- **Eastme.sh broker seed renamed to CoreComms.net** (#677) — the seeded slot-3 broker
  follows the service's rebrand: `wss://mqtt.corecomms.net:443/mqtt`, JWT audience
  `mqtt.corecomms.net`, CA anchor `gts-r4` (endpoint + full TLS chain verified live
  2026-08-13). Slot 4 (Eastmesh.au) also repinned `letsencrypt` → `gts-r4`: its live
  chain moved to GTS Root R4, so the old anchor no longer validates at all. Fresh-NVS
  only (#262 semantics); existing devices keep their stored config — operators with an
  old seeded slot can `mqtt clear <N>` + reboot to reseed. Re-implements external
  PR #282; thanks to **EldoonNemar** for the original contribution and the rebrand
  heads-up.

- **OKIMesh broker slots 0-1 moved to wss/TLS** — the two seeded OKIMesh brokers now use
  `wss://mqtt{1,2}.okimesh.org:9002/mqtt` over TLS (`letsencrypt` CA) instead of plaintext
  `mqtt://…:1883`. Auth stays anonymous (TLS secures the transport; no MQTT credential).
  Both remain disabled by default (#262). Note: as wss/TLS, these slots now wait for a
  valid wall clock — `mqtt status` shows `held(no-clock)` until NTP/GPS, then connects.
  Fresh-NVS only; existing devices keep their stored config. (#592)

## [1.3.0] - 2026-08-06

The **field-diagnostics** release. Adds an end-to-end crash/observability stack —
persistent CrashLog on every role, a live **caplog** capture-and-forward pipeline
(RAM ring → client download, or live UDP syslog to a remote sink), a runtime
watchdog on both ESP32 and nRF52, and an operator **command surface** to drive it
all without a serial cable. Plus user-controllable indicators (kill the LED/OLED
light show), headless-device notification scope + button-action matrix, and the
shared multi-broker MQTT engine the repeater-telemetry genericization builds on.

Most changes are additive — existing nodes keep working. One caveat: the repeater
multi-broker engine is **foundation only** this release (not yet operator-configurable
end-to-end); the field-provisionable binary is a future release (#295).

### Added

**Field diagnostics — caplog (capture + forward)**
- **caplog capture** — a serial-capture tee into a static RAM ring, with CLI verbs
  (`caplog start/stop/erase/status/dump`) and level filtering (boot/error/debug/packet). (#401, #403, #432)
- **Client download (Companion)** — framed caplog download to the companion app over
  the companion API (`0xC4`), BLE-MTU-aware chunking. (#406, #418, #451, #454)
- **Live syslog forward (Repeater, WiFi-telemetry builds)** — stream captured lines
  off-device as UDP syslog to a remote rsyslog sink during an operator-armed window,
  so a panic's lead-up survives the reboot the RAM ring can't hold. Sink host/port
  are a runtime pref (`set syslog.host/port`). (#561, #566)
- **Common across roles** — caplog and its persisted enabled-state run on
  repeater / observer / room-server / sensor, with early boot-log capture and a
  non-blocking serial mirror. (#562, #435, #448)
- **Command surface (`offband-cmd`)** — queue commands, poll status, fetch results,
  arm/tail caplog over the cmdrelay HTTP API — drive a headless node with no SSH —
  plus a receiver setup script and operator guide (`docs/remote-diagnostics.md`). (#567, #569)
- **Test harness + redaction** — companion protocol test harness (frame core +
  caplog round-trips, hardware-verified) and a redacting serial-capture tool that
  never leaks an SSID. (#414, #421, #380, #382)

**Crash observability — CrashLog**
- **Persistent CrashLog on every role** — relocated to a portable `diagnostics/`
  core; compiles and runs on nRF52 (`.noinit` retained RAM); deferred re-dump for a
  late serial connect; uniform boot-survival across all roles. (#367, #376, #377, #361, #463, #475)

**Runtime watchdog**
- **nRF52 hardware watchdog** — auto-recovers a hung main loop with a visible reset
  reason; true loop-driven green-LED heartbeat. (#257, #266, #275)
- **ESP32 runtime watchdog** — board-layer `esp_task_wdt` plumbing, activated
  sensor-first. (#518, #519)
- Gated debug CLI (`hangtest`, `resetreason`) + internal-DRAM heap-headroom in stats. (#357, #358)

**User-controllable indicators (#542)**
- Kill the light show: `led on|off|status` and `display auto|always-on|always-off`
  CLI, persisted and applied at boot across all roles; companion `0xC5` client
  control + capability bit so the app can render toggles. `FIRMWARE_VER_CODE` → 21.

**Headless-device UI**
- **Notification scope** ALL/SELF/NONE + whitespace-tolerant mention matching, via
  `0xC5`. (#510, #524, #525)
- **Button-action matrix** — dispatch button presses through a stored action matrix;
  edge-capture (not sampling) with a debounced, race-free queue on the T1000-E. (#550, #551)

**Repeater multi-broker engine** *(foundation for the telemetry-genericization feature — not yet operator-configurable end-to-end)*
- Shared `MqttBrokerPool` + round-robin TLS scheduler made reusable from the
  repeater: dwell-timer rotation, per-broker ring-log drain, heap-gated concurrency
  (measured ceiling `OFFBAND_MAX_LIVE_TLS=4`), repeater broker-config CLI over serial
  + client. (#175, #536, #537, #538, #539, #506, #512, #554)
- Per-message RSSI populated in the v3 message / data-recv frames. (#460, #465)

### Changed
- **Default broker slots reseated** — the OKI Mesh's own two brokers now hold slots 0
  and 1: slot 0 `mqtt1.okimesh.org` (relabelled from "CoreScope Dayton", same host) and
  slot 1 `mqtt2.okimesh.org` (new, same plaintext/anonymous/1883 config). MeshMapper and
  eastme.sh swap to slots 2 and 3, Eastmesh.au moves 5 → 4, and slot 5 frees up for
  operator use. **LetsMesh-US and LetsMesh-EU are no longer seeded** — both remain fully
  supported and can be added by hand in any free slot (`gts-r4` still resolves). Every
  seeded slot still ships disabled per #262. Because seeding is skip-if-present, this
  only affects fresh-NVS devices; existing devices keep their current layout. (#317)
- **Role-agnostic config dispatch** — `wifi.*` / `display.*` / broker config
  extracted into shared providers with a registration-time key-space overlap
  detector. (#364, #366, #486, #512)

### Fixed
- caplog command code `0xC3`→`0xC4` (FEM/LNA collision); cap bit `0x08`→`0x20`. (#409, #434)
- MQTT keepalive capped at 60 s + `[mqtt-err]` connect errors surfaced; keepalive
  re-asserted on JWT-refresh rebuilds; esp_mqtt client allocated on promotion, not at
  configure time. (#506, #532, #534)
- **Flash tooling** — one-approval-one-flash token TTL; serial-first device matching
  (VID:PID demoted to a gated class fallback); bootstrap records `usb_serial`;
  bridge-board (CP2102/CH340) identity + flash path. (#500, #501, #503, #355, #273, #468)
- GPS routed through the log sink with runtime client-controllable debug. (#424)
- **NVS round-trip test asserted a stale default** — the test still required slot 0 to
  seed as *enabled*, which #262 changed to disabled on 2026-07-02 without updating the
  assertion. Corrected alongside the #317 layout change. (#317)

## [1.2.0] - 2026-07-21

Adds **MeshSmith Photon‑1W support** (both MCU flavors) on the **MeshCore 1.16.0** base, and **receive-sensitivity recovery on Heltec V4** (external FEM LNA control, which stock MeshCore leaves bypassed),
plus the ESP32‑C6 I2C‑scan boot‑hang fix found bench‑validating it, a NimBLE build fix,
and flashing‑docs corrections.

### Added
- **MeshSmith Photon‑1W support — ESP32‑C6 + nRF52 (#193, #194)** — vendors MeshSmith's MIT Photon
  variants (`meshsmith_photon_esp32c6`, `meshsmith_photon_nrf52`; Seeed XIAO ESP32‑C6 / XIAO nRF52840 +
  Ebyte E22‑900M30S 1W radio) and wires all roles into CI + the release pipeline. ESP32‑C6 needed
  minimal MeshCore‑consistent base edits (antenna‑switch virtuals, protected `_gps_serial`, NimBLE dep);
  nRF52 needed none. The **ESP32‑C6 companion + repeater are bench‑verified** on hardware; the
  **nRF52 variant is not yet bench‑verified** — review findings tracked as bench checkpoints on #193/#194.
- **External FEM LNA control on Heltec V4 companions (#298).** MeshCore leaves the Heltec V4
  front-end module's LNA bypassed at boot, so V4 companions ran with degraded receive
  sensitivity. Offband now enables it at companion boot from a persisted `radio_fem_rxgain`
  preference (default on), on the boards whose FEM exposes an independent LNA line
  (`heltec_v4`, `heltec_tracker_v2`, `heltec_t096`). A new companion-API command (`0xC3`,
  SET/GET) plus a capability bit lets the client show a user toggle, gated on the
  runtime-detected FEM chip. `FIRMWARE_VER_CODE` 15 -> 16. Verified end-to-end on a
  KCT8103L (V4.3) board.
- **Model string names the detected FEM part (#327)** — reads `Heltec V4 OLED (KCT8103L)`
  or `(GC1109)`, so a V4.3 (independent LNA control) is distinguishable from a V4.2 in the app.

### Fixed
- **Photon‑1W ESP32‑C6 hung at boot, dead on the mesh (#294).** The C6 variant declares its I2C bus,
  but `EnvironmentSensorManager::begin()` ran the blind I2C scan regardless; on the C6 the scan wedges
  the I2C peripheral (hangs at addr `0x0d`) and never returns, so `setup()` never reached `loop()`.
  The scan is now skipped on boards that set `ENV_SKIP_I2C_SENSOR_SCAN` (guard vendored verbatim from
  MeshSmith's fork); every other board is unchanged.
- **CLI accepted garbage after a valid key (#299).** `get radio foobar` returned the radio
  settings, and `set radio 910.525,62.5,7,5,junk` silently applied the first four values and
  dropped the rest. Keys now require a whole-token match; extra `set radio` parameters are rejected.
- **64 ESP32 BLE companion environments could not build (#199, #90).** The NimBLE dependency was
  declared per-variant with no shared source, and the greedy source filter pulled the BLE
  interface into non-BLE (`usb`/`wifi`) builds too. Factored into a shared config; restores
  boards silently absent from prior releases, including `Heltec_v2_companion_radio_usb` and
  `Xiao_C3_companion_radio_usb`. A CI invariant now guards every ESP32 env. (The #89 fix below
  is the first, single-env instance of this class.)
- **FEM auto-detect source comment corrected (#318, #321).** The Heltec V4 FEM type is set by a
  board strap, not chip-internal pulls as the code claimed; documented against schematics + the
  GC1109 datasheet. A boot detection probe is available behind `-D FEM_DEBUG_PROBE` (off by default).
- **`pio-flash` identified devices by USB port-path, not identity (#336, #323).** Boards were
  misidentified after any USB port or device swap, and a no-discriminator registry entry
  wildcard-matched its whole chip family. Now keys on the device-unique USB serial and refuses
  ambiguous matches; hardware-verified across colliding same-VID:PID boards. Bench tooling, not
  shipped firmware.
- **`pio-flash` bootstrap parses the ESP32-C6/H2 base MAC and anchors its MAC regexes (#290, #292).**
  Bench tooling.
- **`Xiao_S3_WIO_companion_radio_usb` build (#89)** — exclude `SerialBLEInterface.cpp` from the USB
  companion env (it has no BLE), resolving the `NimBLEDevice.h` regression from the #288 NimBLE migration.
- **Flashing docs pointed at the wrong page and omitted a release file** (#326). The
  README's web-flasher link went to `meshcore.co.uk/flasher.html` — a wrapper page that
  forwards to a configurator, not the flasher. It now points at
  [`flasher.meshcore.io`](https://flasher.meshcore.io/). The per-release download table
  (README + `.github/release-footer.md`) also never listed the nRF52 `*.zip` DFU package —
  40 of the 130 assets in a release — so it now has a row, pointing at serial DFU tooling
  while steering most users to the simpler `*.uf2`. Both tables described the merged image as a web-flasher **"Full Firmware"**
  option; that is the device-catalog flow, and an Offband user takes the **Custom Firmware**
  flow instead, where the flasher auto-detects the `-merged.bin` suffix and prompts before
  erasing. Corrected, and both surfaces gained a short "how do I flash it" section covering
  the nRF52 UF2 drag-drop path, the ESP32 web-flasher path, and building from source.
  Documentation only — no firmware change.

### Changed
- **CLAUDE.md: no‑upstream‑merge policy (#197)** — Offband does not merge from upstream MeshCore; the
  `upstream` remote stays fetch‑only for reference. Keep MeshCore nomenclature/coding‑standard consistency
  for clean rebasing.

### Documentation
- Heltec V4 FEM/LNA ground-truth from schematics, the GC1109 datasheet, and on-device
  measurement, including V4.2-vs-V4.3 RX-path differences (#320, #321).
- Repeater WiFi-telemetry genericization design-of-record (#296, design only).
- Block-user companion-API contract reconciled to as-built (#313, #315).

## [1.1.2] - 2026-07-02

Companion **GPS auto-baud + on-demand GPS status**, plus the wedge/time fixes
found while validating it on real hardware, and a **safer first-flash default** — a
fresh Observer no longer auto-publishes to a preset MQTT broker. Still on the
**MeshCore 1.16.0** base.

### Added
- **GPS auto-baud detection** — the companion now detects the GPS modem's baud rate at
  runtime (probing 115200 then 9600, validating by a checksum-good NMEA sentence), so one
  image reads either a standard 9600 module or a 115200 one with no rebuild. It re-probes
  on a GPS enable and keeps trying if a modem is slow to start at boot. (#216, #233)
- **On-demand GPS status to the app (`0xC1`)** — the client can query live GPS state
  (enabled / detected / fix / baud / lat / lon / alt / sats / time) instead of only seeing
  position at connect. (#216)
- **On-device build identity** — an optional build tag shows on the app device-info
  field and the OLED splash, so a specific build is identifiable without a serial
  console. (#222)

### Changed
- **Fresh flashes no longer auto-publish to OKIMesh CoreScope.** A newly-flashed
  Observer previously seeded the CoreScope (Dayton) broker **enabled**, so a device
  flashed anywhere in the world immediately fed MQTT to OKIMesh CoreScope tagged as a
  Dayton node. On a fresh flash every broker now ships **disabled** — the operator
  opts in per slot. (#262)
- **Default region is now `XYZ`** (a non-geographic placeholder) instead of `HAO`
  (Dayton), so an out-of-region device stops mislabeling itself until the operator sets
  its region via `mqtt iata`. Fresh / NVS-erased devices only; existing devices keep
  their stored config. (#262)

### Fixed
- **GPS at high baud could wedge BLE.** An unbounded GPS read loop let a fast modem
  (e.g. 115200, multi-constellation) monopolize the main loop and starve BLE — the app
  would slog or stall. GPS ingestion is now bounded per loop. (#231)
- **GPS time showed garbage before the date was acquired.** A position fix can arrive
  before the calendar date; the device now reports `time=0` ("acquiring") until a real
  date is parsed, instead of a wrapped-garbage timestamp. (#232)

### Internal
- Build/governance tooling + GPS design & diagnostic records — no firmware-behavior
  change. (#214, #217–#219)

## [1.1.1] - 2026-06-26

Urgent patch: the **RAK3401 1W companion** radio was dead on v1.1.0. Still on the
**MeshCore 1.16.0** base.

### Fixed
- **RAK3401 1W companion radio dead on Offband (no TX/RX).** Offband's GPS support
  (#104) probed a pin that doubles as the radio's power-enable on the RAK3401; with no
  GPS attached it left the radio unpowered, so it couldn't transmit or receive. GPS is
  now disabled on the RAK3401 companion build until the probe is fixed. (#211)

## [1.1.0] - 2026-06-24

The headline is **Epic F: the companion-API config command** — a WiFi observer can now be
configured entirely from the app (WiFi, the full MQTT broker pool, display) over BLE —
plus the observer config/NVS-layer hardening that came out of bench-validating it. Still
on the **MeshCore 1.16.0** base.

### Added
- **Companion-API config command (Epic F, #159)** — configure a WiFi observer from the
  MeshCore/Offband app over BLE: WiFi credentials, the full MQTT broker pool (per-slot
  url / port / transport / auth / JWT claims / CA-cert / topic-prefix / IATA),
  enable / disable / clear a slot, and display settings. Wholly observer-gated and surfaced
  behind a new `WIFI_OBSERVER_SUPPORT` capability bit + `FIRMWARE_VER_CODE 14`, so stock
  companions are unaffected. (#160–#169)
- **Per-broker live state in the broker read** (#172) — each slot now reports its live
  connection state (`connecting` / `up` / `backoff` / `held` …) + last-error class, so the
  app shows whether a broker actually connected, not just that it's enabled.
- **Resolved-default hints** (#173, #186) — when `jwt_owner` / `iata_override` are unset,
  the read shows the value the device will actually use at connect (its own pubkey / the
  global IATA) as a placeholder instead of a confusing blank — in both the pool dump and
  the per-field read.

### Changed
- **Honest observer config writes (SAFELANE §6, #181)** — the config / NVS write path used
  to silently swallow failures (a broker disable could ACK success while nothing changed).
  Every writer, the live-reload path, and the crash logger itself now surface + log
  failures with context. This is what root-caused #179.
- **Compact broker config storage + seamless migration** (#182) — broker config is stored
  as small per-key entries (no blank fields) rather than one ~1 KB blob, so writes fit a
  near-full NVS; a one-time **invisible boot migration** reclaims space on upgrade, so the
  operator never sees a transient write error.
- **CoreScope (okimesh) default broker** → `mqtt://mqtt1.okimesh.org:1883` (#170).
- **Offband tell in MQTT** (#174) — observer nodes append a 📡 to their display name in
  MQTT payloads, so feeds / maps can flag Offband observers.

### Fixed
- **#179** — a broker disable ACK'd success while the dump still reported `enabled=1`.
  Root-caused as a near-full-NVS write failure the old code lied about; fixed by #181
  (honest write) + #182 (storage that fits). Verified on hardware.
- **Observer capped at 1 concurrent TLS broker** (#171) — a heap guard stops the OOM reboot
  when a 2nd TLS/wss broker's ~60 KB mbedTLS context exhausts the Heltec V3 heap. Measured
  on hardware (1 wss ≈ 63 KB free, 2 wss ≈ crash).
- **BLE connect survives the client's reconnect-thrash** (#178) — a deeper ESP32 BLE frame
  queue (4 → 12) + stream terminators on reconnect, so a mid-stream reconnect can't leave
  the app's contact / settings sync hung. Affects companion **and** observer. (The root —
  the client's uncapped reconnect retry — is paced client-side in meshcore-client.)
- **Crash log reliable for the whole boot** (#183) — the 1 Hz heartbeat was flooding the
  4 KB RTC crash-ring (wrapped in ~50 s) and evicting real crash diagnostics; the heartbeat
  now writes the ring every 30 s / on a sharp heap drop, so a post-reboot dump keeps the
  evidence that explains a crash past boot+50 s.

### CI / build
- **Heltec T114** (nRF52) companion added to the CI matrix (#185) — first nRF52-companion
  build coverage (complements the existing RAK4631 repeater).

## [1.0.0] - 2026-06-17

First production-stable Offband release — companion, observer, and repeater roles
all working and hardware-verified. Built on the **MeshCore 1.16.0** base.

### Base
- **MeshCore 1.16.0 base-update** (#126) — the fork is rebased onto upstream MeshCore
  1.16.0, smoke-verified across all three active roles (Companion, Observer, Repeater)
  on Heltec V3/V4 + RAK3401.

### Added
- **RAK3401 (WisMesh 1W) GPS** (#104) — the RAK12500 (u-blox ZOE-M8Q) I²C GPS now works
  in Slot D. Companion and repeater acquire a position fix. (Position only; the I²C
  path does not sync the clock.)
- **Display always-on toggle** (#141) — `display always on` keeps a USB/mains-powered
  observer's screen lit; `display normal` restores the 15 s timeout. Persists across
  reboots, applies immediately. Heltec V3, V4 OLED, V4 TFT observers.
- **Display rotation (0/180)** (#148) — `display rotate 0/180` / `display flip` over the
  `_sys` channel; persists, applies immediately. **Verified on the OLED observers
  (Heltec V3, V4 OLED).** Displays without a verified rotation driver (the V4 TFT)
  report `rotation not supported on this display` rather than silently no-op'ing; TFT
  rotation is tracked separately.

### Known issues
- **Heltec V4 observer GPS position unverified** (#149) — an attached UART GPS doesn't
  yet surface a position on the V4 observer (reads 0,0). Observer time (NTP/SNTP) and all
  other function are unaffected; GPS only adds the device's own map-position dot.

## [0.18.1] - 2026-06-14

### Fixed
- **WiFi password confirmation wording** — `set wifi.pwd` now replies
  `wifi.pwd set (N chars entered)` instead of `wifi.pwd set (length=N)`, which was
  being misread as a 17-character maximum. The reply reports the length of what was
  *entered* (never the secret PSK); it is not a cap. WiFi passwords accept the full
  WPA2 range (8–63 chars).

## [0.18.0] - 2026-06-14

### Added
- **Heltec V4 TFT observer build** (`heltec_v4_tft_companion_observer_wifi`) — the
  observer role on the TFT (ST7789) display variant, switched to the NimBLE stack
  the observer requires. Added to the CI build matrix and the release env set so it
  **always builds and ships**.

### Changed
- **Firmware download clarity** — a "Which file?" table in the README and a static
  footer appended to every GitHub Release, explaining `-merged.bin` (first install)
  vs `.bin` (update) vs `.uf2` (nRF52).

### Fixed
- **`pio-flash` device matching** — `find_in_registry` prefers an exact
  DeviceID-instance match over a class-only label, so a registered chip is no longer
  shadowed by a same-class device with null discriminators (fixes nRF52/ESP32
  mislabels where two boards share a VID:PID).

### Internal
- Wire a gitignored `HARDWARE.local.md` (symlink to the LoRa hardware inventory) plus
  a CLAUDE.md "read before any hardware work" pointer.

## [0.17.0] - 2026-06-14

First release under **OffbandMesh/meshcore-firmware**. Bundles the 0.16.0 observer
work below (which landed on `firmware-base` but was never separately tagged) with
the Crosswire→Offband rebrand and the OffbandMesh org cutover. *(Version pending
owner confirmation; the tag is hardware-gated per VERSIONING.md.)*

### Changed
- **Rebranded the fork from Crosswire to Offband** (GitHub org `OffbandMesh`):
  the C++ namespace, build macros, embedded identity blob, version prefix
  (`offband-v*`), MQTT / flash-audit identity fields (`offband_*`), brand
  strings (serial banner, `version` command, OLED splash, Home Assistant
  manufacturer), and the WiFi setup-AP SSID (`Offband-Observer-`). Historical
  `crosswire-v*` release tags are preserved; the `_sys` PSK domain separator
  and the MeshCore interop topic namespace are intentionally unchanged. (#100)
- **Repo / board / working-dir cutover to OffbandMesh** — repo
  `OffbandMesh/meshcore-firmware`, OffbandMesh org Projects board, and the
  preflight / CLAUDE.md / label-sync workflow re-pointed; removed the stale
  upstream `CNAME`. (#107, #111)

### Docs
- Finished the rebrand reference cleanup across docs + code comments. (#113, #114)
- Release-readiness pass: README getting-started + multi-role positioning, the
  docs index surfaces the observer guides, and observer `_sys` CLI reference
  corrections. (#117)

## [0.16.0] - 2026-06-12

Observer time/clock + `_sys` CLI + reporting hardening. Hardware-validated on
ST-P (Heltec V4): three brokers connected, a valid phone-free wall clock, and
correct radio + position reporting on CoreScope.

### Added
- Observer `_sys` CLI grammar standardization: `wifi status` / `wifi enable` /
  `wifi disable` (namespace-subcommand, aligned with `mqtt status/enable/disable`)
  and symmetric `get mqtt.broker.<N>.<key>` (read mirror of `set`, secrets
  redacted). `get wifi.status` kept as a backward-compat alias. (#45)
- Observer time-source arbiter, GPS > NTP > BLE: SNTP provides a fast clock and
  GPS takes over on lock (SNTP stopped once GPS owns the clock) -- a JWT-grade
  wall clock with no phone dependency. (#69)
- Position in the observer MQTT `/status` payload, selected by `advert_loc_policy`
  exactly as the LoRa advert (0,0 null-island suppressed). (#31)
- `mqtt status` broker state `held(no-clock)`: a wss/TLS slot deferred pending a
  sane wall clock now reads as *held* (no retry burn) instead of looking like a
  failure, and releases automatically once the clock syncs. (#87)

### Changed
- Observer MQTT pool no longer blocks on GPS acquisition: SNTP starts immediately
  on STA-connect and the pool comes up regardless, so tcp brokers publish at once
  while wss/TLS brokers self-defer to `held(no-clock)`. (#87)
- `/status` radio fields (`freq/bw/sf/cr`) now report the **runtime** radio config
  (`NodePrefs`, set via the companion API) instead of the compile-time `LORA_*`
  build macros. (#88)

### Docs
- `docs/observer-cli-commands.md` -- `_sys` CLI command reference. (#45)
- `docs/observer-gps-location-config.md` -- GPS / location operator guide. (#31/#69)

## [0.15.0] - 2026-06-10

Observer MQTT connectivity -- hardware-validated against three brokers
(CoreScope / W8OOF tcp-anon, eastme.sh wss/jwt, LetsMesh-US wss/jwt).

### Added
- Owner broker registry: seed the default 6-slot broker set with an `iata=HAO`
  default; per-broker GTS Root R4 + ISRG Root X2 CA certificates added and
  mapped, registry cert-names corrected. (#48)
- Per-broker JWT identity claims `jwt_owner` / `jwt_email`
  (`set mqtt.broker.<N>.jwt_owner|jwt_email`), surfaced in `mqtt status`. (#63)
- Multi-frame `mqtt status` over the BLE `_sys` channel: the per-slot broker
  table spans multiple frames instead of truncating. (#48)

### Fixed
- BLE `_sys` command channel no longer hangs when `set mqtt.broker.*` runs. The
  blocking `esp_mqtt` lifecycle ops (connect / destroy) moved off `loopTask` to a
  dedicated `mqtt_worker` task with a per-broker lock and a per-slot reconcile
  flag. (#53)
- wss/JWT broker authentication: send the MQTT CONNECT username
  `v1_<UPPERCASE pubkey>` (was a null username) so eastme.sh / LetsMesh accept
  the connection -- the broker verifies the token's `publicKey` claim against it
  and rejects a null username (CONNACK rc=5) even with an otherwise-valid token.
  (#68)

## [0.14.0] - 2026-06-07

### Added
- CI release pipeline (epic #14): dev-channel firmware artifacts on every
  `firmware-base` push / PR (`ci.yml`), and a `release.yml` workflow that builds
  the curated community board set from `crosswire-v*` tags and publishes a
  GitHub Release (pre-release for `-rc*`, "Latest" for stable). Curated env set
  in `.github/release-envs.txt` (72 envs; `heltec_v4_repeater_telemetry` gated on
  #20). Design of record: `docs/architecture/2026-06-06-ci-release-pipeline.md`.
  (#15, #16, #17)
- `pio-flash` artifact-flash mode: flash a prebuilt `.bin` through the same
  device-identity gate (ESP32 + nRF52). (#29)

### Changed
- Reconciled inherited CI workflows: removed 7 dead/superseded
  (`pr-build-check`, `auto-promote`, `github-pages`, the three upstream-tag
  `build-*-firmwares`, `branch-cleanup`); kept `build-safeboot-firmwares` +
  `sync-labels-to-board`. (#18)
- Observer firmware stripped to its minimum footprint; removed the dead RX ring
  buffer. (#42)

### Fixed
- `pio-flash`: default `firmware_dir` to the repo root with a `--firmware-dir`
  override; bootloader-port discovery + output-parsed flash verification.
  (#27, #34)
- OLED splash version carries the pre-release identifier (e.g. `-rc1`). (#33)
- Guard `_serial` in `onContactOverwrite` (was NULL during boot-load). (#42)

### Internal
- MeshCore 1.16.0 base-update impact assessment, position-to-map pipeline
  architecture draft, project-identity pinning, and the `/work` + `session-state`
  compaction-recovery tooling port. (#54, #32, #55, #59, #61)

## [0.13.2] - 2026-06-05

### Added
- Versioning + release discipline: this `CHANGELOG.md`, the cadence and
  release-channel sections in `VERSIONING.md`, and a Releases & versioning
  pointer in `README.md`. (#11)

## [0.13.1] - 2026-06-05

### Added
- Green status-LED heartbeat on the RAK4631 BLE companion
  (`-D PIN_STATUS_LED=LED_BUILTIN`, green LED1 / P1.03). (#7)
- Repeater heartbeat status LED in `simple_repeater`, enabled on the RAK3401
  (WisMesh 1W) repeater env. (#9)

### Fixed
- nRF52 companion builds: guard `ESP.getFreeHeap()` in the HomeScreen so
  non-ESP32 targets compile. (#8)
- Added `scripts/firmware_identity.py` to the repo -- closes the P5.2 pio-flash
  wrapper gap (the wrapper imported a module that wasn't present). (#6)

## [0.13.0] - 2026-06-05

Baseline of the unified `firmware-base` line -- the first version tag after the
firmware migrated out of the `Strycher/MeshCore` fork into this repository. It
carries the full migrated feature set (observer Plans 1-2, the NimBLE stack, the
SafeBoot port, and repeater-telemetry).

### Added
- Repo governance folded into the firmware tree: flash / OTA / agent-mail
  PreToolUse discipline, the Projects-v2 board sync workflow + field IDs, and
  build-verification CI. (#334)

### Fixed
- OLED splash build identity: untagged builds self-identify by abbreviated SHA
  instead of collapsing to a bare tag, and the version line is clamped to the
  panel width. (#319)
- Observer: publish heard packets to `/packets` (CoreScope schema) via the
  `logRx` hook. (#335)
- Observer: pair `esp_mqtt` stop with start on broker retry to stop a heap
  leak. (#327)
- Observer: reach the observer config CLI over USB serial
  (transport-agnostic). (#325)
