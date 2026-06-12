# Crosswire

A [MeshCore](https://github.com/meshcore-dev/MeshCore) fork for **cross-role firmware enhancements and optimization** -- not a single-role project. Crosswire adds capabilities and tuning across MeshCore firmware roles, maintained as a standalone fork (several of these enhancements were originally intended as upstream contributions).

## Roles

| Role | Status | What Crosswire adds |
|------|--------|---------------------|
| **Companion / Observer** | Active | WiFi + MQTT publishing of LoRa-mesh observations to public brokers (CoreScope, eastme.sh, LetsMesh), NimBLE BLE stack, multi-broker pool with TLS + JWT auth, a GPS&nbsp;>&nbsp;NTP phone-free wall clock for unattended JWT auth, position in the MQTT `/status`, and a `_sys`-channel CLI for WiFi/broker config |
| **Repeater** | Active | MQTT telemetry bridging (to Mosquitto and other brokers), burst-WiFi telemetry, heap and power tuning |
| **Room server** | Not yet | -- |
| **Bridge** | Not yet | -- |

Cross-cutting work used by multiple roles: a NimBLE migration off Bluedroid, a CrashLog / boot-survival diagnostics layer, and ongoing heap and power optimization for memory-constrained ESP32-S3 boards (Heltec V3 / V4, XIAO).

## Status

The firmware lives here: **`firmware-base`** is the canonical Crosswire tree (the old `Strycher/MeshCore` fork is archived). Design-of-record for the observer architecture is in [`docs/architecture/`](docs/architecture/). Operator setup for the observer is in [`docs/observer-cli-commands.md`](docs/observer-cli-commands.md) (the `_sys` CLI) and [`docs/observer-gps-location-config.md`](docs/observer-gps-location-config.md) (GPS / location).

## Filing requests

Requests and bug reports are welcome now -- use the issue templates (Bug report / Feature request). When reporting a bug, include the role, board, and the Crosswire version from the OLED splash or serial banner, and **never paste WiFi/MQTT credentials**.

## Releases & versioning

Crosswire uses an independent `crosswire-vMAJOR.MINOR.PATCH` version (see [VERSIONING.md](VERSIONING.md)); every build self-identifies on the OLED splash + serial banner. Changes are tracked in [CHANGELOG.md](CHANGELOG.md). Releases come in three channels: **dev** (CI artifacts, untested on hardware), **`-rc` pre-releases** (community testing), and **stable** (the "Latest" GitHub Release). The release gate is hardware validation, not just a green build.

## License

MIT, inherited from upstream MeshCore (Copyright (c) 2025 Scott Powell / rippleradios.com). See [`license.txt`](license.txt). Crosswire's additions are released under the same terms.

## Hardware

Targets ESP32-S3 MeshCore boards (Heltec LoRa32 V3 / V4, Seeed XIAO ESP32-S3, RAK). Companion/observer and repeater roles are exercised on real hardware; room and bridge roles are not yet addressed.
