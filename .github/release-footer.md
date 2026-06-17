
---

### Which file do I download?

| File | What it is | When to use it |
|---|---|---|
| `*-merged.bin` (ESP32 — Heltec V3/V4, XIAO) | **Full image** — bootloader + partition table + app in one, flashed at `0x0` after a chip erase. Self-contained, works on a blank chip. | **First install / clean setup.** In a web flasher this is the **"Full Firmware"** option. |
| `*.bin` (ESP32) | **App only** — flashed at the app offset (`0x10000`); the bootloader must already be on the chip. | **Updating** an existing node — OTA / **"Update Only."** Keeps the device identity + WiFi/MQTT config. |
| `*.uf2` (nRF52 — RAK, T-Echo, XIAO nRF52) | Complete self-contained image. | **First install *and* updates** — double-tap reset, then drag-drop onto the USB drive. (nRF52 has no merged/app split.) |

> ⚠️ **ESP32:** the app-only `*.bin` will **not boot** if flashed at `0x0` — use `*-merged.bin` for a fresh install. A full erase / "Full Firmware" wipes the device's identity + saved config, so use it only for a first install or recovery, **never** a routine update.
