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

### Changed
- **Default broker slots reseated** — the OKI Mesh's own two brokers now hold slots 0
  and 1: slot 0 `mqtt1.okimesh.org` (relabelled from "CoreScope Dayton", same host) and
  slot 1 `mqtt2.okimesh.org` (new, same plaintext/anonymous/1883 config). MeshMapper and
  eastme.sh swap to slots 2 and 3, Eastmesh.au moves 5 → 4, and slot 5 frees up for
  operator use. **LetsMesh-US and LetsMesh-EU are no longer seeded** — both remain fully
  supported and can be added by hand in any free slot (`gts-r4` still resolves). Every
  seeded slot still ships disabled per #262. Because seeding is skip-if-present, this
  only affects fresh-NVS devices; existing devices keep their current layout. (#317)

### Fixed
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
