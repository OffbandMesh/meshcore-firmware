# RC52 (nRF52840) bring-up — handoff

Written 2026-08-19, after RC32 (shipped) and RCC6 (#806, shipped). Companion to **#625**.
Board 3 of 4 of the RadioCore set.

> **This document is deliberately short, and most of it is pointers.**
>
> The RCC6 handoff was long and restated facts. Three of them were wrong by the time
> anyone read it, and it cost a session hours: it declared "no reference variant exists"
> when community *and vendor* prior art both did, and it marked a **correct** LoRa pin map
> `[hypothesis:] do NOT trust` and sent the next session to re-derive it. That correction
> is #804. **The failure mode was restating what lives elsewhere.** So:
>
> - Pin assignments: **the shipped `variants/heltec_rc52/platformio.ini`** once it exists.
> - Family reference: **`docs/radiocore/README.md`** — it already carries an RC52 pin map,
>   FEM pins, and the battery-gate hazard, and it stayed accurate while the handoff rotted.
> - This file: only what those two do not cover, plus the traps that actually cost time.

---

## 1. The authoritative source is the vendor pinout image

**`docs/radiocore/vendor/RC52_nRF52/image.png`** (gitignored, per-host) is Heltec's own RC52
pinout diagram, and it is **authoritative**. Treat it the way the RCC6 work treated the
schematic: as the thing you derive from, not a sketch to be double-checked against guesses.

It gives, directly and unambiguously:

- the full 20-pin carrier header with physical pin numbers
- every module-internal net under "Other Pins", named (`LORA_*`, `FEM_*`, `ADC_*`)
- pin-class colouring (GPIO / GND / Power / ADC-DAC / Serial-SPI-I2C / Connected / Other)

`[verified: read directly from image.png, 2026-08-19]`

**What it does NOT give**, and what you therefore cannot derive from it: component values.
No resistor network, so **no battery divider ratio**. See §5 — that is a real gate, and the
RCC6 session got burned precisely there.

## 2. `[verified:]` P1 carrier header — read from the image

```
 1 GND              11 P0.08  AIN6, nRF_TX
 2 VBAT             12 P0.07  AIN5, nRF_RX
 3 GND              13 P0.20
 4 5V               14 P1.01
 5 P1.10  USER      15 P1.06
 6 P0.30  SWDIO     16 P1.04
 7 P0.28            17 P1.02
 8 P1.13            18 P0.18  RST
 9 P0.09  AIN7      19 VDD
10 P0.10  NFC1      20 GND
```

Module-internal (**not** broken out to the header):

```
LORA_NSS   P0.13    LORA_MISO  P0.14    LORA_MOSI  P0.22    LORA_SCK  P0.25
LORA_BUSY  P0.24    LORA_NRSET P1.00    DIO1       P0.11
FEM_EN     P0.26    VFEM_CTRL  P0.16    FEM_LNA_CTRL P1.07
ADC_Ctrl   P0.04    ADC_IN     P0.31    nRF_RST    P0.18
```

## 3. ⚠ THE CARRIER IS NOT PIN-CONSISTENT ACROSS THE FAMILY

**Do not carry wiring or assumptions across boards.** This is the single highest-cost trap
here, and it has already produced one wrong instruction during the RCC6 session.

| Pin | RCC6 | **RC52** |
|---|---|---|
| 19 | GND | **VDD** |
| 20 | **VDD_3V3** | **GND** |
| 11 | U0RXD | **nRF_TX** (P0.08) |
| 12 | U0TXD | **nRF_RX** (P0.07) |

Two independent inversions:

- **Power/ground on 19/20 are SWAPPED between RCC6 and RC52.** The RCC6 handoff warns
  "pin 20 is VDD_3V3, NOT ground — use pin 19." **On RC52 that advice is backwards** and
  puts a ground clip on VDD. Use **pin 20** (or 1, or 3) for ground on RC52.
- **UART direction is SWAPPED.** On RCC6 the board transmits on pin 12; on RC52 it
  transmits on **pin 11**. A sniffer harness moved over from RCC6 listens to the wrong pin
  and sees silence — which reads as "board dead" rather than "wire wrong."

`[verified: RC52 from image.png; RCC6 from docs/radiocore/rcc6-bringup-handoff.md §2]`

**Re-read the pinout for each board. Every time.** "Same carrier" is true of the connector,
not of the pin assignments.

## 4. What is genuinely different about RC52

- **No WiFi.** nRF52840 has none. The entire observer / MQTT / TLS-heap thread from the
  RCC6 session — broker rotation, `OFFBAND_MAX_LIVE_TLS`, `OFFBAND_TLS_HEAP_FLOOR_BYTES` —
  **does not apply to this board at all.** Do not port it, do not re-argue it. Roles here
  are BLE companion, USB companion, repeater, room server.
- **There is a FEM.** `FEM_EN` (P0.26), `VFEM_CTRL` (P0.16), `FEM_LNA_CTRL` (P1.07).
  Neither RC32 nor RCC6 has one. This is a new RF surface and a new way to get TX power
  and LNA state wrong — and RF faults present as poor range, not clean failures.
- **Flash path is nRF52, not esptool.** `pio-flash` handles it via
  `--artifact <env>/firmware.zip` (touch → bootloader → `adafruit-nrfutil`, app DFU
  preserves config). UF2 drag-drop is the fallback. `preview` exits non-zero and is not
  chainable.
- **SWDIO is on the header (pin 6 / P0.30) but there is no SWD probe** and none is wanted.
  Debug is USB-CDC serial only. Do not propose a J-Link.
- **No display block appears in the pinout.** Whether RC52 carries the T108/NV3001B panel
  at all is **unconfirmed** — do not assume the RC32/RCC6 display work transfers.

## 5. The schematic EXISTS — and the battery values are already derived

**A published RC52 schematic exists.** `[verified: fetched 2026-08-19, HTTP 200,
application/pdf, 228,546 bytes, Last-Modified 2026-07-27; content confirms nRF52840,
ADC_Ctrl, ADC_IN, FEM, RadioCore]`

```
https://resource.heltec.cn/download/RadioCore/RC52/schematic/RC52-L62_V1.0.pdf
```

Saved to `docs/radiocore/vendor/RC52_nRF52/RC52-L62_V1.0-schematic.pdf` (gitignored).

- **Exact part string is `RC52-L62`**, matching `RC32-L62` / `RCC6-L62`.
- **Version mismatch worth knowing:** the published filename says `V1.0`; the internal
  Altium sheet name is `RC52-L62_V1.02.SchDoc`. Cite the internal revision.
- **CDN pattern:** `resource.heltec.cn/download/RadioCore/<PRODUCT>/<schematic|datasheet|pinmap>/`.

**No datasheet is published.** `RC52/datasheet/` and `RC52/pinmap/` return 404 while the
identical `RC32/` and `RCC6/` paths return 200, and `Footprint/` has `RC32.SchLib` and
`RCC6.SchLib` but no `RC52.SchLib`. Schematic-only, consistent with closed beta. The
pinout image (§1) is the stand-in for the missing datasheet — which is why it is
authoritative here rather than merely useful.

### Battery divider — derived, so nobody has to invent it

This is the exact spot where the RCC6 session went wrong: `ADC_MULTIPLIER` was **invented**
as `4.95` when the divider derives `4.90`, biasing every sample ~1% high, caught only while
validating telemetry before a discharge run (#835). Pre-empted here.

From the schematic's battery block:

```
VBAT -> Q3 (AO3401A, P-ch) -> R17 390K -> [ADC_IN tap] -> R18 100K -> GND

ADC_MULTIPLIER = (R17 + R18) / R18 = (390K + 100K) / 100K = 4.90
```

Enable path, same shape as RCC6 with different parts:
`ADC_Ctrl` → R15 1K → Q2 (S9013, NPN) base; R16 100K pulldown holds it low. Q2 collector →
R14 1K → gate of Q3, which is **P-channel**, so gate-low turns it **on**.

⇒ **`ADC_CTRL_ENABLED = HIGH`**, and the divider is **off at reset** (R16) — no idle drain.

`[schematic-derived: RC52-L62_V1.02 — NOT measured]`

**This is the nominal ratio, not a calibration.** At 1% parts the true ratio spans
~4.83–4.97 — wider than the correction that mattered on RCC6. And per #602, SafeBoot
battery polarity must not be inherited from a sibling: this is derived from **this board's
own schematic**, which is legitimate, but confirm it on hardware before trusting SafeBoot
with it. On the Wio Tracker L1 Pro an inherited polarity made SafeBoot disable the divider,
sample it dead, read below `SLEEP_MV` and deep-sleep before USB init — a fully charged
board presenting as completely dead.

## 6. Prior art — check it FIRST

The RCC6 session lost hours to a blank display whose answer was sitting in the vendor's own
Arduino library. Look here before deriving anything:

- **[n30nex/NeonPocketMC](https://github.com/n30nex/NeonPocketMC)** — ships RC52 builds
  (BLE companion, headless repeater, room server ± TFT). MIT, not a fork.
- **[HelTecAutomation/Heltec_nRF52](https://github.com/HelTecAutomation/Heltec_nRF52)** —
  **the vendor's own nRF52 BSP ships an RC52 board.** `variants/heltec_rc52/` exists, and
  `boards.txt` carries `heltec_rc52.name=Heltec RC52`, `vid.0=0x239A`, `pid.0=0x8071`.
  `[verified: GitHub API, repo pushed 2026-08-03]` **Read `variants/heltec_rc52/variant.h`
  before deriving any pin.** This is the analogue of the file that finally explained the
  RCC6 display, and it is vendor-authored.
- **[HelTecAutomation/RadioCore_Library](https://github.com/HelTecAutomation/RadioCore_Library)**
  — vendor board headers. `src/boards/heltec_rcc6.h` is what explained the RCC6 display
  (`USE_SOFTWARE_SPI 1`, because no SPI peripheral is free).
- `docs/radiocore/README.md` → **Prior art** table.

## 7. Traps that cost the RCC6 session — none are board-specific

1. **Derive constants; never choose one.** An invented `ADC_MULTIPLIER` shipped and was
   caught hours later (#835). If a value has a canonical source, read the source.
2. **Read the config header before asserting from a log.** A "heap crisis" was declared
   from a serial capture; the answer — that `OFFBAND_MAX_LIVE_TLS=1` is the *global*
   default and the heap floor gates *starting* a handshake, not running level — was in
   `WifiObserverConfig.h`, never opened (#830).
3. **A designed mechanism working is not a fault.** Broker rotation was read as a board
   barely surviving. It is the architecture.
4. **Compute from geometry; do not eyeball renders.** The Vext answer only became solid
   when net connectivity was extracted from the PDF's vector segments and cross-checked
   against its own pin designators (#805).
5. **Check prior art before deriving.** See §6.
6. **Never withhold or gate what the owner directed** on your own analysis (#830).
7. **Confidence markers are load-bearing** — `[verified:]` means an artifact produced the
   evidence and it is cited; `[hypothesis:]` means unconfirmed. But note the #804 lesson:
   *unconfirmed is not wrong.* Marking a correct map "do NOT trust" sent a session to
   re-derive an answer it already had. Say "unconfirmed — confirm by X," and say how long
   X takes.

## 8. Bench notes

- ⚠ **RC52 collides with the T096 on VID:PID.** The vendor BSP gives RC52
  `239A:8071` — and `hardware-devices.yaml` already registers a **Heltec Mesh Node T096**
  (nRF52840, bootstrapped 2026-08-11) on that same `239A:8071`. Both are nRF52840 boards
  on the Adafruit bootloader VID, so **passive identification cannot tell them apart.**
  This is the #503 class-vs-identity case exactly: identity comes from a live MAC read, or
  the operator asserts the port with `--known-port`. Do **not** let a class match decide
  which board gets written. `[verified: boards.txt + registry + live port enumeration]`
- **Same sniffer rig works**, with the §3 corrections applied: ground on **pin 20**, listen
  on **pin 11** (board TX).
- `tools/diag/rc32-boot-740/scripts/capture.py` — durable timestamped capture; `--queue RST`
  injects into a **running** capture (the port is held exclusively; `--send` cannot).
- The nRF52 has no USB-CDC-on-boot behaviour to fight, so the RCC6 USB fault (#818 —
  fails enumeration after chip reset on some hubs) is an ESP32-C6 story and should **not**
  be expected here. If something similar appears, it is a new finding, not that one.

---

**Status:** no `variants/heltec_rc52` in tree. #625 is the epic. Nothing claimed.
