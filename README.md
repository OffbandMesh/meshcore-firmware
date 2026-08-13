# Offband

A [MeshCore](https://github.com/meshcore-dev/MeshCore) fork for **cross-role firmware enhancements and optimization** -- not a single-role project. Offband adds capabilities and tuning across MeshCore firmware roles, maintained as a standalone fork (several of these enhancements were originally intended as upstream contributions).

## Roles

| Role | Status | What Offband adds |
|------|--------|---------------------|
| **Companion / Observer** | Active | WiFi + MQTT publishing of LoRa-mesh observations to public brokers (CoreScope, CoreComms.net, LetsMesh), NimBLE BLE stack, multi-broker pool with TLS + JWT auth, a GPS&nbsp;>&nbsp;NTP phone-free wall clock for unattended JWT auth, position in the MQTT `/status`, and full **in-app configuration over BLE** — WiFi, the MQTT broker pool, and display, via the companion-API config command — alongside the `_sys`-channel CLI |
| **Repeater** | Active | MQTT telemetry bridging (to Mosquitto and other brokers), burst-WiFi telemetry, heap and power tuning |
| **Room server** | Not yet | -- |
| **Bridge** | Not yet | -- |

Cross-cutting work used by multiple roles: a NimBLE migration off Bluedroid, a CrashLog / boot-survival diagnostics layer, and ongoing heap and power optimization for memory-constrained ESP32-S3 boards (Heltec V3 / V4, XIAO).

## Status

The firmware lives here: **`firmware-base`** is the canonical Offband tree (the old `Strycher/MeshCore` fork is archived). Design-of-record for the observer architecture is in [`docs/architecture/`](docs/architecture/). Operator setup for the observer is in [`docs/observer-cli-commands.md`](docs/observer-cli-commands.md) (the `_sys` CLI) and [`docs/observer-gps-location-config.md`](docs/observer-gps-location-config.md) (GPS / location).

## Getting started

**Pre-built firmware** — grab the file for your board from the [Releases page](../../releases) and flash it. **Which file depends on whether it's a first install or an update:**

| File | What it is | When |
|---|---|---|
| `<env>-...-merged.bin` (ESP32) | full image (bootloader + partitions + app), flashed at `0x0` after erase | **first install** — self-contained, boots a blank chip |
| `<env>-....bin` (ESP32) | app only, flashed at the app offset (`0x10000`) | **update** — keeps identity + config |
| `<env>-....uf2` (nRF52) | complete self-contained image | first install **and** update — double-tap reset, drag-drop |
| `<env>-....zip` (nRF52) | Adafruit DFU package — the same image as the `.uf2`, DFU-wrapped | serial DFU tooling (`adafruit-nrfutil`). Most people want the `.uf2` instead |

> On ESP32 the app-only `.bin` will **not boot** if written at `0x0` — use `-merged.bin` for a fresh install. A full erase wipes the device identity + config (first-install / recovery only). nRF52 has no merged/app split; the `.uf2` and `.zip` carry the same image in different wrappers.

**Flashing — no toolchain required:**

- **nRF52 (RAK, T-Echo, XIAO nRF52)** — double-tap reset; the board mounts as a USB drive; drag the `.uf2` onto it. Nothing to install. This is the simplest path.
- **ESP32** — the [MeshCore web flasher](https://flasher.meshcore.io/) → **Custom Firmware**, which takes a file straight off your disk. Needs a Chromium-based browser (Web Serial). It detects the `-merged.bin` suffix and warns before erasing — expected on a first install.
- **From source** — `pio run -e <env> -t upload`.

Each release's notes repeat this guidance.

**Build from source** — install [PlatformIO](https://platformio.org/), then build the env that matches your board + role:

```bash
pio run -e heltec_v4_companion_observer_wifi     # observer — Heltec V4
pio run -e Heltec_v3_companion_observer_wifi     # observer — Heltec V3
pio run -e Xiao_S3_WIO_companion_observer_wifi   # observer — XIAO S3 WIO
pio run -e heltec_v4_repeater_telemetry          # repeater telemetry — Heltec V4
```

The full env list lives in [`platformio.ini`](platformio.ini). Build artifacts land in `.pio/build/<env>/`; flash with `pio run -e <env> -t upload`.

## Filing requests

Requests and bug reports are welcome now -- use the issue templates (Bug report / Feature request). When reporting a bug, include the role, board, and the Offband version from the OLED splash or serial banner, and **never paste WiFi/MQTT credentials**.

## Releases & versioning

Offband uses an independent `offband-vMAJOR.MINOR.PATCH` version (see [VERSIONING.md](VERSIONING.md)); every build self-identifies on the OLED splash + serial banner. Changes are tracked in [CHANGELOG.md](CHANGELOG.md). Releases come in three channels: **dev** (CI artifacts, untested on hardware), **`-rc` pre-releases** (community testing), and **stable** (the "Latest" GitHub Release). The release gate is hardware validation, not just a green build.

## License

MIT, inherited from upstream MeshCore (Copyright (c) 2025 Scott Powell / rippleradios.com). See [`license.txt`](license.txt). Offband's additions are released under the same terms.

## Hardware

Targets ESP32-S3 MeshCore boards (Heltec LoRa32 V3 / V4, Seeed XIAO ESP32-S3, RAK). Companion/observer and repeater roles are exercised on real hardware; room and bridge roles are not yet addressed.
