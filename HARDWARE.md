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

**Known open issue (#702):** on RC32 the companion role does not boot after pressing RST —
the application runs **zero times**. The repeater role is unaffected. Workaround:
power-cycle instead of RST. Details below.

### RC32 companion RST failure — what the ROM says

Captured with an external UART sniffer on header **pin 12 (U0TXD/GPIO43)** + **pin 20
(GND)**. 14 confirmed blank-screen RST presses, all byte-identical:

```
ESP-ROM:esp32s3-20210327
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
load:0x3fce3808  load:0x403c9700  load:0x403cc700
entry 0x403c98d0
```

**14 ROM banners · 14 `entry` reached · 1 app start · 0 panics.**

- **`boot:0x8 SPI_FAST_FLASH_BOOT` — the strapping pins are correct.** Not download mode.
  This refutes every strapping-pin theory for this failure, from the layer that decides
  boot mode. (The GPIO45 and GPIO46 concerns above remain valid practice; they are simply
  not the cause here.)
- The chip resets cleanly, the bootloader loads all three segments, and control reaches the
  application entry point. **The application then produces nothing.**
- **Zero panics** — no `Guru Meditation`, no backtrace. It hangs, or faults before the panic
  handler is installed. It does not crash.
- **Failure window:** `entry` → the app's first observable action, i.e. IDF startup →
  C++ static constructors → `initArduino()` → `setup()`.

### Two debugging traps on this board — read before diagnosing anything

Both cost significant time before being understood.

**1. The console is SPLIT.** ESP-IDF log output (`[E][esp32-hal-...]`) goes to **UART0**,
while MeshLog/`MESH_DEBUG_PRINTLN` and the retained crash-log ring go to **USB-CDC**.
**Neither channel alone ever shows a whole boot.** Watch both, or you will conclude a stage
never ran when it simply logged somewhere you were not looking.

**2. The ESP32-S3 ROM auto-selects its console.** With USB enumerated it routes boot
messages to USB-Serial-JTAG and **UART0 stays silent**. During an RST the USB link drops, so
the ROM falls back to UART0 — which is why a reset is the only condition where an external
sniffer sees the ROM banner. To force UART0 output, power the board **without USB** (VBAT
pin 2 or 5V pin 4).

**Corollary that invalidates naive debugging:** the RC32's console is generated by the chip
itself, so **it disappears when the chip fails to boot** — and attaching to it power-cycles
the board. Every log captured that way is necessarily from a boot that *succeeded*. To
observe a failure you need a separate USB device. Instruments and captures live on branch
`diag/704-rst-instrumentation` under `tools/diag/rc32-rst-704/`.

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
