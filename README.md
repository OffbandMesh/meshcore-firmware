# Crosswire

A [MeshCore](https://github.com/meshcore-dev/MeshCore) fork for **cross-role firmware enhancements and optimization** -- not a single-role project. Crosswire adds capabilities and tuning across MeshCore firmware roles, maintained as a standalone fork (several of these enhancements were originally intended as upstream contributions).

## Roles

| Role | Status | What Crosswire adds |
|------|--------|---------------------|
| **Companion / Observer** | Active | WiFi + MQTT publishing of LoRa-mesh observations to public brokers, NimBLE BLE stack, configurable broker pool, web UI |
| **Repeater** | Active | MQTT telemetry bridging (to Mosquitto and other brokers), burst-WiFi telemetry, heap and power tuning |
| **Room server** | Not yet | -- |
| **Bridge** | Not yet | -- |

Cross-cutting work used by multiple roles: a NimBLE migration off Bluedroid, a CrashLog / boot-survival diagnostics layer, and ongoing heap and power optimization for memory-constrained ESP32-S3 boards (Heltec V3 / V4, XIAO).

## Status: code migration in progress

> The firmware currently lives on the `crosswire` branch of the [Strycher/MeshCore](https://github.com/Strycher/MeshCore) fork. It is **migrating into this repository** after an in-progress architecture refactor (moving the observer from inheriting the full MeshCore messenger to composing MeshCore as a dependency). Until that lands, this repo is the **home for issues, requests, and design-of-record** -- the code follows.

The design-of-record for that refactor is in [`docs/architecture/`](docs/architecture/).

## Filing requests

Requests and bug reports are welcome now -- use the issue templates (Bug report / Feature request). When reporting a bug, include the role, board, and the Crosswire version from the OLED splash or serial banner, and **never paste WiFi/MQTT credentials**.

## License

MIT, inherited from upstream MeshCore (Copyright (c) 2025 Scott Powell / rippleradios.com). See [`LICENSE.txt`](LICENSE.txt). Crosswire's additions are released under the same terms.

## Hardware

Targets ESP32-S3 MeshCore boards (Heltec LoRa32 V3 / V4, Seeed XIAO ESP32-S3, RAK). Companion/observer and repeater roles are exercised on real hardware; room and bridge roles are not yet addressed.
