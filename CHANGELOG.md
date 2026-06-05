# Changelog

All notable changes to Crosswire are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). This
project uses **Pattern B fork versioning** — an independent
`crosswire-vMAJOR.MINOR.PATCH` line with the upstream MeshCore baseline tracked
separately. See [VERSIONING.md](VERSIONING.md) for the cadence (commit per task /
compile, PR per epic, tag per landing), the `+N` dev-build suffix, and the
dev / `-rc` / stable release channels.

This changelog begins at the unified `firmware-base` line (**0.13.0**). Versions
prior to 0.13.0 predate it; see `git tag -l 'crosswire-v*'` and the tag
annotations for that history (highlights: v0.12.0 NimBLE migration, v0.11.x
Plan-3 web UI, v0.10.x observer multi-broker pipeline, v0.5.0 initial backfill).

## [Unreleased]

### Added
- Versioning + release discipline: this `CHANGELOG.md`, the cadence and
  release-channel sections in `VERSIONING.md`, and a Releases & versioning
  pointer in `README.md`. (#11)

_On merge this rolls into the next version tag (PATCH — docs landing)._

## [0.13.1] - 2026-06-05

### Added
- Green status-LED heartbeat on the RAK4631 BLE companion
  (`-D PIN_STATUS_LED=LED_BUILTIN`, green LED1 / P1.03). (#7)
- Repeater heartbeat status LED in `simple_repeater`, enabled on the RAK3401
  (WisMesh 1W) repeater env. (#9)

### Fixed
- nRF52 companion builds: guard `ESP.getFreeHeap()` in the HomeScreen so
  non-ESP32 targets compile. (#8)
- Added `scripts/firmware_identity.py` to the repo — closes the P5.2 pio-flash
  wrapper gap (the wrapper imported a module that wasn't present). (#6)

## [0.13.0] - 2026-06-05

Baseline of the unified `firmware-base` line — the first version tag after the
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
