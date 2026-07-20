
---

### Which file do I download?

| File | What it is | When to use it |
|---|---|---|
| `*-merged.bin` (ESP32 — Heltec V3/V4, XIAO) | **Full image** — bootloader + partition table + app in one, flashed at `0x0` after a chip erase. Self-contained, works on a blank chip. | **First install / clean setup.** |
| `*.bin` (ESP32) | **App only** — flashed at the app offset (`0x10000`); the bootloader must already be on the chip. | **Updating** an existing node — OTA. Keeps the device identity + WiFi/MQTT config. |
| `*.uf2` (nRF52 — RAK, T-Echo, XIAO nRF52) | Complete self-contained image. | **First install *and* updates** — double-tap reset, then drag-drop onto the USB drive. (nRF52 has no merged/app split.) |
| `*.zip` (nRF52) | **Adafruit DFU package** — the same image as the `*.uf2`, DFU-wrapped. | Serial DFU tooling (`adafruit-nrfutil`). Most people want the `*.uf2` instead. |

> ⚠️ **ESP32:** the app-only `*.bin` will **not boot** if flashed at `0x0` — use `*-merged.bin` for a fresh install. A full erase wipes the device's identity + saved config, so use it only for a first install or recovery, **never** a routine update.

### How do I flash it?

- **nRF52** — double-tap reset; the board mounts as a USB drive; drag the `*.uf2` onto it. No tools needed.
- **ESP32** — [MeshCore web flasher](https://flasher.meshcore.io/) → **Custom Firmware**, which takes a file straight off your disk. Chromium-based browser required (Web Serial). It detects the `-merged.bin` suffix and warns before erasing — expected on a first install.
- **From source** — `pio run -e <env> -t upload`.
