# Design of Record (DoR): CI Release Pipeline (Epic #14)

> Status: DRAFT for human review. T1 deliverable (#15). Approach approved (Gate 1) and
> structure approved (proceed-with-T1) 2026-06-06. No code until this DoR is approved.
> Diagnosis + plan recorded at Strycher/Crosswire#14 (issue comment). Feature: `feature:release-pipeline`.

## 1. Goal

Following the documented process must produce downloadable firmware:
- **dev channel:** every `firmware-base` push / PR yields a downloadable artifact from the Actions run.
- **release channel:** tagging `crosswire-vX.Y.Z` (or `-rcN`) builds the curated community board set
  and publishes a GitHub Release with the correct pre-release / Latest flag.

## 2. Scope decisions (from human, 2026-06-06)

- **Boards (15 user-chosen):** RAK4631; WisMesh (RAK 3401 **and** WisMesh Tag); Seeed Wio Tracker;
  Seeed XIAO ESP32 (S3 WIO **+** C3 **+** C6); Seeed XIAO nRF; Seeed Solar SenseCap; SenseCap T1000E;
  Heltec T096; Heltec T1114; Heltec V2; Heltec V3; Heltec V4 (**OLED and TFT**); LILYGO T-Echo;
  LILYGO T-Deck. (T-Deck Pro: no env exists -> follow-up issue, not in this release.)
- **Sub-variants:** base models only, EXCEPT Heltec V4 ships both OLED (`heltec_v4`) and TFT
  (`heltec_v4_tft`). Excluded: T1114 no-display, T-Echo Lite, Wio Eink.
- **Roles:** mirror MeshCore's curated release set (`companion_radio_ble`, `companion_radio_usb`,
  `repeater`, `room_server`); skip the niche envs MeshCore does not ship (kiss, sensor,
  terminal_chat, bridge/espnow, `companion_radio_wifi`, serial). **PLUS Crosswire differentiators**
  `companion_observer_wifi` and `repeater_telemetry` where they exist. (This +observer/+telemetry
  addition is the one item I interpreted from "follow MeshCore approach" -- flagged for confirmation.)

## 3. Curated release env set (73 envs, verified against `variants/*/platformio.ini`)

This becomes `.github/release-envs.txt` in T3 (not created until DoR approval).

```
# RAK4631 (nRF52)
RAK_4631_companion_radio_ble
RAK_4631_companion_radio_usb
RAK_4631_repeater
RAK_4631_room_server
# WisMesh -- RAK 3401 (nRF52)
RAK_3401_companion_radio_ble
RAK_3401_companion_radio_usb
RAK_3401_repeater
RAK_3401_room_server
# WisMesh Tag (nRF52)
RAK_WisMesh_Tag_companion_radio_ble
RAK_WisMesh_Tag_companion_radio_usb
RAK_WisMesh_Tag_repeater
RAK_WisMesh_Tag_room_server
# Seeed Wio Tracker L1 (nRF52)
WioTrackerL1_companion_radio_ble
WioTrackerL1_companion_radio_usb
WioTrackerL1_repeater
WioTrackerL1_room_server
# Seeed XIAO S3 WIO (ESP32-S3) -- includes observer
Xiao_S3_WIO_companion_observer_wifi
Xiao_S3_WIO_companion_radio_ble
Xiao_S3_WIO_companion_radio_usb
Xiao_S3_WIO_repeater
Xiao_S3_WIO_room_server
# Seeed XIAO C3 (ESP32-C3)
Xiao_C3_companion_radio_ble
Xiao_C3_companion_radio_usb
Xiao_C3_repeater
Xiao_C3_room_server
# Seeed XIAO C6 (ESP32-C6) -- only ble + repeater envs exist (note trailing underscore in env names)
Xiao_C6_companion_radio_ble_
Xiao_C6_repeater_
# Seeed XIAO nRF52
Xiao_nrf52_companion_radio_ble
Xiao_nrf52_companion_radio_usb
Xiao_nrf52_repeater
Xiao_nrf52_room_server
# Seeed Solar SenseCap (ESP32)
SenseCap_Solar_companion_radio_ble
SenseCap_Solar_companion_radio_usb
SenseCap_Solar_repeater
SenseCap_Solar_room_server
# SenseCap T1000E (nRF52)
t1000e_companion_radio_ble
t1000e_companion_radio_usb
t1000e_repeater
t1000e_room_server
# Heltec T096 (nRF52)
Heltec_t096_companion_radio_ble
Heltec_t096_companion_radio_usb
Heltec_t096_repeater
Heltec_t096_room_server
# Heltec T1114 (nRF52)
Heltec_t114_companion_radio_ble
Heltec_t114_companion_radio_usb
Heltec_t114_repeater
Heltec_t114_room_server
# Heltec V2 (ESP32)
Heltec_v2_companion_radio_ble
Heltec_v2_companion_radio_usb
Heltec_v2_repeater
Heltec_v2_room_server
# Heltec V3 (ESP32-S3) -- includes observer
Heltec_v3_companion_observer_wifi
Heltec_v3_companion_radio_ble
Heltec_v3_companion_radio_usb
Heltec_v3_repeater
Heltec_v3_room_server
# Heltec V4 OLED (ESP32-S3) -- includes observer + telemetry
heltec_v4_companion_observer_wifi
heltec_v4_companion_radio_ble
heltec_v4_companion_radio_usb
heltec_v4_repeater
heltec_v4_repeater_telemetry   # GATED: ships only after #20 (runtime-config secrets)
heltec_v4_room_server
# Heltec V4 TFT (ESP32-S3)
heltec_v4_tft_companion_radio_ble
heltec_v4_tft_companion_radio_usb
heltec_v4_tft_repeater
heltec_v4_tft_room_server
# LILYGO T-Echo (nRF52)
LilyGo_T-Echo_companion_radio_ble
LilyGo_T-Echo_companion_radio_usb
LilyGo_T-Echo_repeater
LilyGo_T-Echo_room_server
# LILYGO T-Deck (ESP32-S3) -- no room_server env
LilyGo_TDeck_companion_radio_ble
LilyGo_TDeck_companion_radio_usb
LilyGo_TDeck_repeater
```

Notes:
- 73 envs total. `heltec_v4_repeater_telemetry` is **#20-gated**: excluded from public Releases until
  #20 (runtime-config secrets) lands, so the initial shippable set is 72.
- Observer envs exist only for V3 / V4-OLED / XIAO S3 WIO (3). Telemetry only for V4-OLED (1).

## 4. Secrets handling for public builds

Release binaries are public; they carry **no real secrets**. Same dummy-stub approach as `ci.yml`.
Runtime configuration model: companion / observer configure WiFi/MQTT at runtime (BLE system channel).
`repeater_telemetry` currently bakes creds at build time, so a usable PUBLIC binary requires #20;
hence the gate above. PSK / MQTT creds never appear in the repo, a build flag, a log, or a Release.

## 5. Channels and tag mapping (per VERSIONING.md)

| Channel | Trigger | Result |
|---|---|---|
| dev | `firmware-base` push / PR (`ci.yml`) | downloadable Actions artifact; no Release |
| pre-release | tag `crosswire-vX.Y.Z-rcN` | GitHub Release, `prerelease=true` (not "Latest") |
| stable | tag `crosswire-vX.Y.Z` | GitHub Release, `make_latest=true` |

`setup-build-environment` already parses the version from either tag form (verified).
Release gate is hardware validation (maintainer), not CI-green.

## 6. Per-task implementation design

- **T2 (#16)** -- `ci.yml`: add `actions/upload-artifact@v4` after the build step, per matrix env;
  name `crosswire-dev-<env>`; paths `.pio/build/<env>/firmware.{bin,elf,hex,zip,uf2}`;
  `if-no-files-found: warn`, `retention-days: 90`.
- **T3 (#17)** -- new `release.yml`, `permissions: contents: write`, triggers
  `push: tags: ['crosswire-v*']` + `workflow_dispatch`. Matrix reads `.github/release-envs.txt`
  (the list above, minus the #20-gated line). Each job: `setup-build-environment`, dummy secrets stub,
  `build.sh build-firmware <env>`, upload per-env artifact. Final job (`needs: build`) downloads all
  and runs `softprops/action-gh-release@v2` with `prerelease: contains(ref_name,'-rc')`,
  `make_latest: !contains(ref_name,'-rc')`, `generate_release_notes: true`.
- **T4 (#18)** -- per-file disposition: DELETE `pr-build-check.yml` (triggers `main`/`dev`, dead;
  `ci.yml` covers PR builds), `auto-promote.yml` (`if: repo==Strycher/LoRa`, inert),
  `github-pages.yml` (triggers `main`, `cname: docs.meshcore.io` upstream domain),
  `build-companion-firmwares.yml` / `build-repeater-firmwares.yml` / `build-room-server-firmwares.yml`
  (upstream-tag triggered, superseded by `release.yml`). KEEP `build-safeboot-firmwares.yml`
  (feature-scoped tags per VERSIONING.md), `sync-labels-to-board.yml`. REVIEW `branch-cleanup.yml`.
- **T5 (#19)** -- integration test (epic gate): `workflow_dispatch` a test run, confirm matrix builds,
  per-env artifacts upload, a Release is created with `prerelease=true` + downloadable files; confirm
  a `firmware-base` push yields a dev artifact; confirm a non-rc tag marks "Latest"; download one ESP32
  and one nRF52 (`.uf2`) artifact and confirm they are real images; clean up the test tag/Release.
  Human sign-off closes the epic.

## 7. Per-platform packaging

`build.sh` auto-detects platform from each env's build flags and packages into `out/`:
ESP32 -> `firmware.bin` + `firmware-merged.bin`; nRF52 -> `firmware.uf2` + `firmware.zip`;
STM32/RP2040 handled too (not in this board set). No change needed.

## 8. Risks

- **Build volume:** 72 release envs. GitHub matrix parallelizes and runners are free, but a full
  release run is long. Mitigation: env list is a tunable file; can subset later. (User chose this scope.)
- **Tag typo publishes a Release:** `release.yml` fires on any `crosswire-v*`. Pre-release flag is
  automatic; `make_latest` only on non-rc. Tagging stays a deliberate manual step.
- **#20 gate:** telemetry binary is intentionally absent from public Releases until runtime-config lands.

## 9. Open item for human confirmation

The only judgment call in this DoR: including `companion_observer_wifi` + `repeater_telemetry`
(Crosswire differentiators) on top of MeshCore's curated role set. Everything else follows directly
from your stated choices. Confirm, then T2 begins.
