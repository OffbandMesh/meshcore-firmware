# Offband Documentation

Offband is a [MeshCore](https://github.com/meshcore-dev/MeshCore) fork for cross-role firmware (companion/observer + repeater). See the [project README](https://github.com/OffbandMesh/meshcore-firmware#readme) for what Offband is, the roles it covers, and how to get started.

## Offband guides

- [Observer CLI (`_sys` channel)](./observer-cli-commands.md) — WiFi / MQTT broker config for the observer
- [Observer GPS & location](./observer-gps-location-config.md) — GPS / time-source / privacy configuration
- [Serial CLI & MQTT commands](./cli-and-mqtt-commands.md) — serial CLI plus the MQTT command / OTA reference
- [Capturing a serial log](./serial-log-capture.md) — download & run the host-side capture tool (redacts WiFi creds) to send us a device log
- [SafeBoot](./safeboot.md) — pre-init power guard for solar / battery nodes
- [Observer architecture](./architecture/2026-06-01-observer-architecture-review.md) — design of record

## Inherited MeshCore docs

These are upstream MeshCore references, kept unmodified:

- [Frequently Asked Questions](./faq.md)
- [CLI Commands](./cli_commands.md)
- [Companion Protocol](./companion_protocol.md)
- [Packet Format](./packet_format.md)
- [QR Codes](./qr_codes.md)

If you find a mistake in the inherited docs, the upstream source is the place to fix it: [MeshCore docs](https://github.com/meshcore-dev/MeshCore/tree/main/docs).
