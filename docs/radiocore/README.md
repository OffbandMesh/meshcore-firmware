# Heltec RadioCore — reference notes

Working notes for the **Heltec RadioCore** beta hardware. Tracking: [#622](https://github.com/OffbandMesh/meshcore-firmware/issues/622) (feature), [#623](https://github.com/OffbandMesh/meshcore-firmware/issues/623) RC32 · [#624](https://github.com/OffbandMesh/meshcore-firmware/issues/624) RCC6 · [#625](https://github.com/OffbandMesh/meshcore-firmware/issues/625) RC52.

## What RadioCore is

A socket system, not three separate boards: interchangeable **core modules** (MCU + radio) on a common **20-pin board-to-board connector**, sharing identical power/reset pins and a common display. Heltec's silkscreen calls it a "Wireless Barebone System."

| Module | MCU | Radio | Role target |
|---|---|---|---|
| **RC32** | ESP32-S3, 16 MB flash / 8 MB OPI PSRAM | SX1262 (RA62A-HF) | full role set |
| **RCC6** | ESP32-C6 | SX1262 (RA62A-HF) | WiFi + WebUI |
| **RC52** | nRF52840 | SX1262 + FEM (RA62A-HF) | BLE / serial companion, repeater — **no WiFi** |
| HT-HC01 V2 | (host: ESP32-S3) | Morse Micro MM6108, 802.11ah | deferred — see below |

Common header pins on all three: `1 GND · 2 VBAT · 3 GND · 4 5V … 18 RST · 19 3V3/VDD · 20 GND`.

## Why the pinout images are not in this repo

The Heltec pinout artwork, Heltec's verbatim beta-programme message, and third-party
screenshots are **not redistributable under this repo's MIT licence**, and amount to
~19 MB of binaries that git history cannot later shed. They live locally, gitignored, in:

```
docs/radiocore/vendor/          <- gitignored, local only
  RC32_ESP32/RC32.jpg           Heltec RC32 pinout
  RCC6/RCC6.jpg                 Heltec RCC6 pinout
  RC52_nRF52/image.png          Heltec RC52 pinout
  Heltec_RadioCore_Beta_Feedback.md   Heltec beta brief, verbatim
  DUBS_LCD_image1.png           third-party device photo
  n30nex_stats_image.png        third-party dashboard screenshot
```

**Everything load-bearing from those images is transcribed below**, so the data survives
without them.

## Pin maps (transcribed from the vendor diagrams)

### RC32 — ESP32-S3

Header-exposed GPIO: `4, 5, 6, 16, 17, 0` (pins 10→5) and `44, 43, 42, 41, 40, 39, 38` (pins 11→17).

| Function | Pins |
|---|---|
| TFT | `SCL=17  SDA=38  CS=39  DC=16  RST=4  EN=6 (active LOW)  BL=5 (active HIGH)` |
| LoRa *(module-internal, not on header)* | `NSS=10  RESET=9  BUSY=1  SCLK=11  MISO=13  MOSI=12  DIO1=14` |
| Battery *(module-internal)* | `ADC_CTRL=15  VBAT_READ=7  ADC_CTRL_ENABLED=HIGH  multiplier 4.9` |

⚠ Upstream MeshCore's `variants/heltec_rc32` additionally assigns **buzzer 48, TX LED 47,
sensor power 46, sensor reset 2, I²C SDA 21 / SCL 18, GPS enable 45** — none of which the
bare module brings out on its 20-pin header. That variant appears written against a
complete Heltec product rather than a socketed core module. Confirm the physical unit
before assuming those pins exist.

### RCC6 — ESP32-C6

Header-exposed GPIO: `0, 1, 2, 3, 4, 9` (pins 10→5) and `17, 16, 23, 22, 21, 18, 15` (pins 11→17).
Pins 13/14/15 (GPIO 23/22/21) are marked **LoRa Module** on the diagram.

| Function | Pins |
|---|---|
| TFT (software SPI) | `SCL=4  SDA=15  CS=18  DC=3  RST=0  EN=2 (active LOW)  BL=1 (active HIGH)` |
| LoRa | `SCLK=21  MISO=20  MOSI=22  NSS=23  DIO1=19  BUSY=10  RESET=8` |
| Battery | `ADC_CTRL=5  VBAT_READ=6  multiplier 4.95` |
| Button | `9` |

### RC52 — nRF52840

| Function | Pins |
|---|---|
| LoRa | `NSS=P0.13  MISO=P0.14  MOSI=P0.22  BUSY=P0.24  SCK=P0.25  NRST=P1.00  DIO1=P0.11` |
| FEM | `FEM_EN=P0.26  VFEM_CTRL=P0.16  FEM_LNA_CTRL=P1.07` |
| Battery | `ADC_Ctrl=P0.04` gating `ADC_IN=P0.31` |
| Other | `USER=P1.10  nRF_RST=P0.18  SWDIO=P0.30  NFC1=P0.10  nRF_RX=P0.07  nRF_TX=P0.08` |

⚠ **`ADC_Ctrl` gates the battery divider — the same shape that bricked the Wio Tracker L1
Pro (#602/#620).** Any SafeBoot battery config for RC52 must take its polarity from
measuring *this* board, never from a sibling variant.

✅ **`FEM_LNA_CTRL` (P1.07) is an independent LNA line** — RC52 would be only the second
board class able to exercise Offband's FEM/LNA control (cap bit `0x04`, `0xC3`), after the
Heltec V4.3 / KCT8103L.

## Display — T108 / NV3001B

128 × 220, 16-bit RGB565, 4-wire write-only SPI, no MISO. `RDDID` returns `0x300101`.
Full driver notes, init sequence and troubleshooting in [`RCC6 TFT Guide.md`](./RCC6%20TFT%20Guide.md);
standalone Arduino test in [`radiocore-c6-tft-test.ino`](./radiocore-c6-tft-test.ino).

**Unresolved:** the guide (hardware-tested here) states the panel is portrait and *must*
run rotation 0, with landscape producing a garbled 128×128 square. The n30nex community
firmware describes the same panel as 220×128 landscape. Both cannot be literally true —
most likely MeshCore's `NV3001BDisplay` handles rotation the bare test sketch does not,
but that is untested. Settle it on hardware.

## Prior art

| Board | Upstream MeshCore | Community (n30nex, MIT) |
|---|---|---|
| RC32 | **full `variants/heltec_rc32`**, 16 envs + `NV3001BDisplay` | — |
| RCC6 | none | companion BLE/USB/WiFi/Web-AP, repeater, room server, MQTT observer + WebUI |
| RC52 | none | BLE companion, headless repeater, room server ± TFT |

`variants/heltec_rc32` and `src/helpers/ui/NV3001BDisplay.*` landed upstream in commit
`17d68e32` (2026-07-08) — **after 1.16.0, inside 1.17.0**. Offband is on the 1.16.0 base,
so RadioCore support is coupled to the 1.17.0 base update (#614) or to cherry-picking that
commit. All three n30nex firmwares are 1.17.0-based too.

## Wi-Fi HaLow — deferred

HT-HC01 V2 is a **Morse Micro MM6108**: 802.11ah, 863–870 / 902–928 MHz, 1/2/4/8 MHz
channels, up to 32 Mbps, 27 ±1 dBm at 915 MHz, VDD 3.0–3.6 V with **VFEM at 5 V**. It is a
**companion module requiring a host** over SDIO 2.0 or UART, driven by the Morse Micro SDK.

It carries IP, not MeshCore packets — it is not a longer-range LoRa and not a drop-in for
one. Owner-deferred; likely belongs outside a MeshCore fork entirely. `dut.md` notes HaLow
is populated optionally.

## References

- Upstream variant: `git show upstream/main:variants/heltec_rc32/platformio.ini`
- [n30nex/NeonPocketMC](https://github.com/n30nex/NeonPocketMC) — release catalog (RC52 + RCC6 builds)
- [n30nex/NeonPocketMC-RCC6](https://github.com/n30nex/NeonPocketMC-RCC6) · [RCC6-Repeater](https://github.com/n30nex/NeonPocketMC-RCC6-Repeater)
- [HelTecAutomation/RadioCore_Library](https://github.com/HelTecAutomation/RadioCore_Library) — Arduino board-config + sensor library (MIT); no LoRa/HaLow
- [HT-HC01 V2 product page](https://heltec.org/project/ht-hc01-v2-wifi-halow-module/)
- Meshtastic PR #11041 (heltec_rcc6 pin mapping), PR #10876 (RC32)
