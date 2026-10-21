# Offband — Hardware

Public hardware reference for Offband (`OffbandMesh/meshcore-firmware`): which boards the
firmware targets, what is known about each, and where the per-host bench details live.

> **This file is committed and public.**
> It must never contain MAC addresses, LAN IPs, SSIDs, PSKs, BLE PINs, admin passwords,
> GPS coordinates, or per-unit serial numbers. Those live in **`HARDWARE.local.md`**, which
> is per-host and gitignored.

## The two hardware files

| File | Committed? | Contents |
|---|---|---|
| `HARDWARE.md` (this file) | **yes, public** | Supported boards, variant names, public specs, known-issue pointers |
| `HARDWARE.local.md` | **no — gitignored** | Per-host bench inventory: device identities, MACs, LAN IPs, BLE PINs, GPS, per-unit config |

**Both files belong to this repository.** Neither may live outside it, and neither may be a
symlink — see the blocked warning in `CLAUDE.md`. A symlink to an external path previously
caused two months of Offband hardware documentation to be written into an unrelated
repository (#735).

## Per-host state lives outside every checkout

Since **#1012**, the device registry, flash history and flash backups are **one set per
host, outside all clones and worktrees** — normally `C:\Dev\.offband\` on Windows,
`~/.offband` elsewhere, resolved by `scripts/offband_state.py`:

| Asset | Purpose |
|---|---|
| `hardware-devices.yaml` | device registry — seeded from the tracked `hardware-devices.example.yaml` |
| `flash-backups/` | pre-flash images, including irreplaceable beta-hardware backups |
| `flash-history.jsonl` | flash audit trail |

A `hardware-devices.yaml` inside a checkout is **not** read. The registry holds LAN IPs and
MACs: never commit it or paste its contents.

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
`docs/radiocore/vendor/` — **gitignored, not redistributable**.
Source tree: `https://resource.heltec.cn/download/RadioCore/`

**RC32 header (Heltec datasheet Table 2.1.2, 20 pins):**
1/3/20 GND · 2 VBAT · 4 5V · **5 GPIO0 (USER btn)** · 6 GPIO17 · 7 GPIO16 · 8 GPIO6 ·
9 GPIO5 · 10 GPIO4 · **11 U0RXD/GPIO44** · **12 U0TXD/GPIO43** · 13 GPIO42 · 14 GPIO41 ·
15 GPIO40 · 16 GPIO39 · 17 GPIO38 · **18 RST btn** · 19 3V3

Headers are female on **both** sides and the LCD occupies the top, so there is no exposed
male pin to clip onto — tap from the bottom with a male-to-female jumper.

**ESP32-S3 strapping pins are GPIO0, GPIO3, GPIO45, GPIO46.** Check every new peripheral
against that list before assigning it. Offband has hit this defect class repeatedly:
- #211 — RAK3401: GPS probe floated WB_IO2, the radio power-enable
- #704 — heltec_rc32: `PIN_GPS_EN` is GPIO45, the VDD_SPI voltage strap
- #719 — heltec_rc32: `SENSOR_POWER_CTRL_PIN` is GPIO46, a boot strap (closed as refuted
  for the RST symptom, but the hygiene point stands)

## Reading an ESP32-S3 ROM boot banner

A reset emits a banner on UART0 before any firmware exists. Reading it correctly matters —
two of these fields were misread during #702 and cost real time.

```
ESP-ROM:esp32s3-20210327
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
load:0x3fce3808  load:0x403c9700  load:0x403cc700
entry 0x403c98d0
```

⚠ **`entry 0x…` is the SECOND-STAGE BOOTLOADER's entry point, not the application's.**
Confirmed by `readelf` on `bootloader_qio_80m.elf`. That bootloader is built
`LOG_LEVEL_NONE`, so **silence after `entry` is what a healthy boot looks like** — it is not
evidence that the application was reached. Treating `entry` as the app's start puts the
analysis a whole stage too early.

⚠ **`boot:0x8` reports the GPIO0/GPIO46 boot mode ONLY.** It does **not** speak to
GPIO45/VDD_SPI, which is a separate strap that never appears in that field. Do not cite
`boot:0x8` as refuting a VDD_SPI theory.

## Known open issue — RC32 companion dark after RST (#702)

**Root cause: the companion hangs inside `offband::crashLogStandardInit()`.** Confirmed by
A/B on hardware — with `-D OFFBAND_CRASHLOG_DISABLED` and nothing else changed, the same
board boots clean through to `setup:DONE`. Credit: BlueElk.

⚠ **This is not RC32-specific.** `crashLogStandardInit` is enabled **fleet-wide for all
non-observer companions** (#472). `crashLogBegin()` reads and dumps the **RTC_NOINIT** ring
retained from the previous reset. RTC_NOINIT survives an RST (CHIP_PU) and a *fast* replug,
and drains on a *slow* one — which is exactly why this failure defeated every
firmware-level theory. **If a board goes dead after RST but recovers on a slow power cycle,
suspect this before anything else.**

⚠ **`nvs_count` is CrashLog's own counter.** It does not increment on a failing RST because
the hang is *inside the function that increments it*. A flat counter here means "hung before
the tick", **not** "the application never ran".

Workaround: power-cycle instead of pressing RST.

## Two debugging traps on these boards — read before diagnosing anything

**1. The console is SPLIT.** ESP-IDF log output (`[E][esp32-hal-...]`) goes to **UART0**,
while MeshLog/`MESH_DEBUG_PRINTLN` and the retained crash-log ring go to **USB-CDC**.
**Neither channel alone ever shows a whole boot.** Watch both, or you will conclude a stage
never ran when it simply logged somewhere you were not looking.

**2. The ESP32-S3 ROM auto-selects its console.** With USB enumerated it routes boot
messages to USB-Serial-JTAG and **UART0 stays silent**. During an RST the USB link drops, so
the ROM falls back to UART0 — which is why a reset is the only condition where an external
sniffer sees the ROM banner. To force UART0 output, power the board **without USB** (VBAT
pin 2 or 5V pin 4).

**Corollary that invalidates naive debugging:** these boards' consoles are generated by the
chip itself, so **they disappear when the chip fails to boot** — and attaching to one
power-cycles the board. Every log captured that way is necessarily from a boot that
*succeeded*. To observe a failure you need a separate USB device as an external UART reader.

## Flash discipline

Flashing goes through `scripts/pio-flash.py` — never raw `esptool` or `pio run -t upload`.
The wrapper enforces identity matching against the per-host `hardware-devices.yaml` and a
two-stage preview/confirm requiring explicit owner approval naming the device.
