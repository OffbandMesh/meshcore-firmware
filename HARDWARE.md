# Offband — Hardware

Public hardware reference for Offband (`OffbandMesh/meshcore-firmware`): which boards the
firmware targets, what is known about each, and where the per-host bench details live.

> **This file is committed and public.**
> It must never contain MAC addresses, LAN IPs, SSIDs, PSKs, BLE PINs, admin passwords,
> GPS coordinates, or per-unit serial numbers. Those live in **`HARDWARE.local.md`**, which
> is per-host and gitignored (`.gitignore:71`).

## The two hardware files

| File | Committed? | Contents |
|---|---|---|
| `HARDWARE.md` (this file) | **yes, public** | Supported boards, variant names, public specs, known-issue pointers |
| `HARDWARE.local.md` | **no — gitignored** | Per-host bench inventory: device identities, MACs, LAN IPs, BLE PINs, GPS, flash paths, per-unit config |

**Both files belong to this repository.** Neither may live outside it, and neither may be a
symlink. See the note in `CLAUDE.md` — a symlink to an external path previously caused two
months of Offband hardware documentation to be written into an unrelated repository.

Agents: read `HARDWARE.local.md` before asking about or acting on hardware, and record newly
stated hardware facts there so the owner never has to restate them. Never paste its contents
into issues, PRs, or chat.

## Board families

### Heltec RadioCore (beta)

`RC32` is a carrier **family**, not one board — it accepts different radio core modules, and
each combination has its own schematic. Always cite the sub-variant.

| Variant | MCU | Radio | Firmware variant |
|---|---|---|---|
| **RC32-L62** | ESP32-S3 | HT-RA62A (LoRa, SX1262) | `variants/heltec_rc32` |
| RC32-68 | ESP32-S3 | HT-HC01_V2 (Wi-Fi HaLow, MM6108) | not supported |
| RCC6 | ESP32-C6 | HT-RA62A | `variants/heltec_rcc6` |
| RC52 | nRF52840 | HT-RA62A | `variants/heltec_rc52` |

Vendor documents (datasheets, schematics, pinouts) are mirrored under
`docs/radiocore/vendor/` — **gitignored, not redistributable** (`.gitignore:113`).
Source tree: `https://resource.heltec.cn/download/RadioCore/`

**RC32 header (Heltec datasheet Table 2.1.2, 20 pins):**
1/3/20 GND · 2 VBAT · 4 5V · **5 GPIO0 (USER btn)** · 6 GPIO17 · 7 GPIO16 · 8 GPIO6 ·
9 GPIO5 · 10 GPIO4 · **11 U0RXD/GPIO44** · **12 U0TXD/GPIO43** · 13 GPIO42 · 14 GPIO41 ·
15 GPIO40 · 16 GPIO39 · 17 GPIO38 · **18 RST btn** · 19 3V3

Headers are female on **both** sides and the LCD occupies the top, so there is no exposed
male pin to clip onto — tap from the bottom with a male-to-female jumper.

**ESP32-S3 strapping pins are GPIO0, GPIO3, GPIO45, GPIO46.** Check every new peripheral
against that list before assigning it. Offband has hit this defect class three times:
- #211 — RAK3401: GPS probe floated WB_IO2, the radio power-enable
- #704 — heltec_rc32: `PIN_GPS_EN` is GPIO45, the VDD_SPI voltage strap
- #719 — heltec_rc32: `SENSOR_POWER_CTRL_PIN` is GPIO46, a boot strap

**Known open issue:** on RC32 the companion role does not boot after pressing RST (the app
runs zero times); the repeater role is unaffected. Workaround: power-cycle instead of RST.
See #704.

## Flash discipline

Flashing goes through `scripts/pio-flash.py` — never raw `esptool` or `pio run -t upload`.
The wrapper enforces identity matching against `hardware-devices.yaml` (per-host, gitignored)
and a two-stage preview/confirm requiring explicit owner approval naming the device.

Supporting per-host assets, all in this repository:

| Asset | Purpose |
|---|---|
| `hardware-devices.yaml` | device registry — gitignored; see `hardware-devices.example.yaml` |
| `flash-backups/` | pre-flash images, including irreplaceable beta-hardware backups |
| `flash-history.jsonl` | flash audit trail |
