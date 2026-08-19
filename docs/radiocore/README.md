# Heltec RadioCore — reference notes

Working notes for the **Heltec RadioCore** beta hardware. Tracking: [#622](https://github.com/OffbandMesh/meshcore-firmware/issues/622) (feature), [#623](https://github.com/OffbandMesh/meshcore-firmware/issues/623) RC32 · [#624](https://github.com/OffbandMesh/meshcore-firmware/issues/624) RCC6 · [#625](https://github.com/OffbandMesh/meshcore-firmware/issues/625) RC52.

> **Snapshot, not live state.** Captured 2026-08-10, revised 2026-08-13 against
> `firmware-base` @ `117d719d` — which is now on the **MeshCore 1.17.0 base** (merged
> `78a84df7`, PR #644, ci-green 45/45; released as `offband-v1.4.0`).
> Re-verify before relying on any of it.

## What RadioCore is

A socket system, not three separate boards: interchangeable **core modules** (MCU + radio) on a common **20-pin board-to-board connector**, sharing identical power/reset pins and a common display. Heltec's silkscreen calls it a "Wireless Barebone System."

| Module | MCU | Radio | Role target |
|---|---|---|---|
| **RC32** | ESP32-S3, 16 MB flash / 8 MB OPI PSRAM | SX1262 (RA62A-HF) | full role set |
| **RCC6** | ESP32-C6 | SX1262 (RA62A-HF) | WiFi + WebUI |
| **RC52** | nRF52840 | SX1262 + FEM (RA62A-HF) | BLE / serial companion, repeater — **no WiFi** |
| HT-HC01 V2 | (host: ESP32-S3) | Morse Micro MM6108, 802.11ah | deferred — see below |

### Connector

Two rows of ten. Pins **1–10** on one edge, **11–20** on the other, numbered from the
end opposite the module's keying triangle. Power/reset pins are identical on all three
modules `[verified: the three vendor pinout diagrams]`:

```
 1 GND    2 VBAT   3 GND    4 5V        18 RST   19 3V3 / VDD   20 GND
```

`[not captured: connector pitch, mating part number, mechanical drawing, current ratings.]`
Those are in the vendor diagrams / Heltec's mechanical docs only — see `vendor/` below.

## Why the pinout images are not in this repo

The Heltec pinout artwork, Heltec's verbatim beta-programme message, and third-party
screenshots are **not redistributable under this repo's MIT licence**, and amount to
~19 MB of binaries that git history could never later shed. They live locally, gitignored,
in `docs/radiocore/vendor/` — see [`vendor/README.md`](./vendor/README.md) for the manifest.

The pin data and electrical facts needed for bring-up are transcribed below. **The
transcription is not a complete substitute for the diagrams** — see *What is not
transcribed* at the end.

## Pin maps

All tables below: `[verified: transcribed by hand from the vendor pinout diagrams,
2026-08-10]`. Hand transcription can introduce errors — check against the board before
trusting a single pin.

### RC32 — ESP32-S3

**Header-exposed GPIO**, with the alternate functions the diagram marks:

| Pin | GPIO | Also |
|---|---|---|
| 10 | 4 | ADC1_CH3, TOUCH4 |
| 9 | 5 | ADC1_CH4, TOUCH5 |
| 8 | 6 | ADC1_CH5, TOUCH6, FSPIQ |
| 7 | 16 | ADC2_CH5, U0CTS, XTAL_32K_N |
| 6 | 17 | ADC2_CH6, U1TXD |
| 5 | 0 | USER button (pull-up) |
| 11 | 44 | U0RXD, CLK_OUT2 |
| 12 | 43 | U0TXD, CLK_OUT1 |
| 13 | 42 | MTMS |
| 14 | 41 | MTDI, CLK_OUT1 |
| 15 | 40 | MTDO, CLK_OUT2 |
| 16 | 39 | MTCK, CLK_OUT3, SUBSPICS1 |
| 17 | 38 | FSPIWP, SUBSPIWP |

| Function | Pins |
|---|---|
| TFT | `SCL=17  SDA=38  CS=39  DC=16  RST=4  EN=6 (active LOW)  BL=5 (active HIGH)` |
| LoRa — **module-internal, none on the header** | `NSS=10  RESET=9  BUSY=1  SCLK=11  MISO=13  MOSI=12  DIO1=14` |
| Battery — **module-internal** | `ADC_CTRL=15  VBAT_READ=7  ADC_CTRL_ENABLED=HIGH  multiplier 4.9` |

⚠ **Upstream's `variants/heltec_rc32` assigns pins that are NOT on this module's 20-pin
header**: buzzer 48, TX LED 47, sensor power 46, sensor reset 2, I²C SDA 21 / SCL 18,
GPS enable 45. `[verified: upstream variants/heltec_rc32/platformio.ini + variant.h,
compared against the header list above]` The TFT pins *are* all on the header; those are
not. `[hypothesis: untested]` The likeliest explanation is that the variant targets a
complete Heltec product rather than a bare socketed module — but the mechanism is not
confirmed, so establish what our physical unit exposes before trusting those pins.

### RCC6 — ESP32-C6

**Header-exposed GPIO:**

| Pin | GPIO | Also |
|---|---|---|
| 10 | 0 | ADC1_CH0, XTAL_32K_P, LP_UART_DTRN |
| 9 | 1 | ADC1_CH1, XTAL_32K_N, LP_UART_DSRN |
| 8 | 2 | ADC1_CH2, FSPIQ, LP_UART_RTSN |
| 7 | 3 | ADC1_CH3, LP_UART_CTSN |
| 6 | 4 | ADC1_CH4, FSPIHD, MTMS, LP_UART_RXD |
| 5 | 9 | USER button (pull-up) |
| 11 | 17 | U0RXD, FSPICS1 |
| 12 | 16 | U0TXD, FSPICS0 |
| 13 | 23 | SDIO_DATA3 — **shared with LoRa module** |
| 14 | 22 | SDIO_DATA2 — **shared with LoRa module** |
| 15 | 21 | SDIO_DATA1, FSPICS5 — **shared with LoRa module** |
| 16 | 18 | SDIO_CMD, FSPICS2 |
| 17 | 15 | — |

| Function | Pins |
|---|---|
| TFT (software SPI) | `SCL=4  SDA=15  CS=18  DC=3  RST=0  EN=2 (active LOW)  BL=1 (active HIGH)` |
| LoRa | `SCLK=21  MISO=20  MOSI=22  NSS=23  DIO1=19  BUSY=10  RESET=8` |
| Battery | `ADC_CTRL=5  VBAT_READ=6  multiplier 4.95` |
| Button | `9` |
| `Vext_3V3` LDO enable | `11` — **does NOT power the radio**, see below |
| `SELECT` (RF path?) | `7` — **unresolved**, see below |

#### The two RCC6 power/RF rails you will otherwise re-derive

**`Vext_3V3` (GPIO11) almost certainly does NOT gate the LoRa supply** —
`[schematic-derived: #805 — NOT measured]`. The board has two 3V3 regulators and only one
is unconditional:

| Rail | Regulator | `EN` | Default |
|---|---|---|---|
| `VDD_3V3` | U1 `CE6260B33M` | tied to `IN` | **always on** — and this is what powers the RA62A (U5 pin 10) |
| `Vext_3V3` | U3 `TLV75733PDBVR` | `VEXT_LDO_Ctrl` = **GPIO11**, 100 K pulldown | off until firmware asserts it |

`Vext_3V3`'s **only** destination is **U5 pin 25, which the RA62A symbol labels `NC`**. The
net name appears exactly twice on the whole schematic — the U3 output and that pin. Its
10 µF + 100 nF decoupling makes it look like a module supply; it is one, it just lands on a
pin this population does not connect. Most likely provisioned for the HaLow module on the
shared carrier `[hypothesis:]`.

Practical effect: **the radio should come up whether or not you touch GPIO11.** The variant
asserts it anyway, deliberately: this is a schematic inference, not a measurement, and the
asymmetry favours asserting — if the inference is right it costs quiescent draw, if it is
wrong not asserting is a silent dead SX1262.

**To settle it, measure.** Multimeter on U5 pin 10 and pin 25 with GPIO11 de-asserted and
asserted. That is cheap and definitive; everything above is inference from the PDF, and the
part is dual-variant (`RA62A_LF/RA62A_HF`), so an `NC` pin on this population could be live
on the other.

**`SELECT` = GPIO7 is NOT resolved** — and it is the one that can bite quietly. The module
is marked `RA62A_LF/RA62A_HF`, a dual LF/HF part, so `SELECT` plausibly chooses the RF path.
Neither our variant nor n30nex drives it. If it *is* a band select, the wrong default
presents as **mediocre range, not a clean failure** — the failure mode nobody files a bug
for. Settle it with an RSSI/range comparison against a known peer, GPIO7 driven each way,
before trusting RF numbers off this board. Tracked on #805.

**Only three LoRa pins reach the header** — `SCLK=21`, `MOSI=22`, `NSS=23`, which is
exactly the block the diagram rings with a dashed "LoRa Module" box (pins 13/14/15).
`MISO=20`, `DIO1=19`, `BUSY=10` and `RESET=8` are **module-internal and not broken out**.
Do not expect to probe the whole LoRa bus on the connector.

### RC52 — nRF52840

| Function | Pins |
|---|---|
| LoRa | `NSS=P0.13  MISO=P0.14  MOSI=P0.22  BUSY=P0.24  SCK=P0.25  NRST=P1.00  DIO1=P0.11` |
| FEM | `FEM_EN=P0.26  VFEM_CTRL=P0.16  FEM_LNA_CTRL=P1.07` |
| Battery | `ADC_Ctrl=P0.04` gating `ADC_IN=P0.31` |
| Other | `USER=P1.10  nRF_RST=P0.18  SWDIO=P0.30  NFC1=P0.10  nRF_RX=P0.07  nRF_TX=P0.08` |

Header-exposed: `P0.10 (NFC1, AIN7)`, `P0.09`, `P1.13`, `P0.28`, `P0.30 (SWDIO)`,
`P1.10 (USER)` on pins 10–5; `P0.08 (AIN6, nRF_TX)`, `P0.07 (AIN5, nRF_RX)`, `P0.20`,
`P1.01`, `P1.06`, `P1.04`, `P1.02`, `P0.18 (RST)` on pins 11–18.

## Hardware hazards

### RC52 — enable-gated battery divider (the #602 shape)

`ADC_Ctrl` (P0.04) gates the divider feeding `ADC_IN` (P0.31). **This is the same pattern
that bricked the Seeed Wio Tracker L1 Pro on 2026-08-10**: `SAFEBOOT_VBAT_ENABLE_ACTIVE`
was copied from `xiao_nrf52` without checking which way that board drives the pin, so
SafeBoot disabled the divider, sampled it dead, read below `SLEEP_MV` and deep-slept
before USB init — at full charge, presenting as a completely dead board. See #602 / #620.

**Rule: any SafeBoot battery config for RC52 takes its polarity from measuring _this_
board. Never copy it from a sibling variant.** `[verified: RC52 pinout diagram shows the
gate; the #602 failure mode is documented in HARDWARE.local.md]`

### RCC6 — ESP32-C6 heap, MEASURED

**The observer runs on the RCC6.** `[verified: rcc6-bench-1, owner-configured with four
brokers, 354 s continuous — WiFi + MQTT + radio up, brokers rotating normally]` This
supersedes the earlier "measure before designing any WiFi/Observer work" gating note; the
measurement is done and it did not block.

**Read the two numbers correctly — this is where a prior analysis went wrong (#830):**

- **`OFFBAND_MAX_LIVE_TLS` = 1 is the GLOBAL default**, not a C6 limit. Each live wss
  broker holds a ~60 KB mbedTLS context, so *every* PSRAM-less board holds exactly one;
  HV3 included. Seeing `live=1/1` on RCC6 is the designed cap, not a symptom. The #175
  rotation scheduler exists precisely to cycle more brokers through that 1-wide budget.
- **`OFFBAND_TLS_HEAP_FLOOR_BYTES` = 80 KB gates *starting* a handshake — it is not a
  required running heap level.** *"We refuse to START a TLS handshake unless free heap
  exceeds this floor, so the transient always fits."* With `MAX_LIVE_TLS=1` the source
  calls it "a backstop." Sitting below it between handshakes is normal operation.
  `[verified: src/helpers/wifi_observer/WifiObserverConfig.h]`

| | RCC6 (measured) | HV3 (source-documented) |
|---|---|---|
| Live TLS contexts | 1 | 1 — same global cap |
| Free heap, one broker live | ~28–38 KB | ~63 KB |
| Free heap between brokers | ~86 KB — **above** the floor | ~124 KB with WiFi up |
| `heap_min` watermark | **900 B** | ~52 KB one-broker; 456 B two-broker (#171 knife-edge) |

Heap recovering above the floor between rotations is the mechanism working: the next
handshake only starts once there is room for its ~72 KB transient.

**Genuinely open — the `heap_min` gap.** 900 B against HV3's ~52 KB for the equivalent
one-broker case is a wide difference and is not explained. `heap_min` is a **lifetime
watermark**, so *when* it occurred is unestablished — boot, a transient, or a deferred
handshake are all consistent with the log. Worth an instrumented look; it is **not** a
reason to withhold the build, and it was wrong to treat it as one.

Working headroom on this part is roughly half HV3's. Size WiFi/Observer work against
~35 KB with a broker live, not against S3 numbers — a web UI or high contact count on top
of an active TLS broker is where this would get tight.

### RC52 — opportunity, not hazard

`FEM_LNA_CTRL` (P1.07) is an independent LNA control line. `[verified: Offband's
FEM/LNA control (cap bit 0x04, 0xC3) is exercised today only on Heltec V4.3 / KCT8103L;
GC1109 and SKY66122 boards structurally lack an RX-path bypass — see HARDWARE.local.md]`
RC52 would therefore be a second bench target for that path.

⚠ **The FEM implementation underneath that feature is changing.** `LoRaFEMControl` is
Heltec's (Quency-D, `9312fe78`, 2026-02-26) and was already in our 1.16.0 base — we
inherited it. Upstream independently added FEM CLI control + `radio_fem_rxgain`
(`8435464c`, authored 2026-03-24, merged after our base point); Offband added its own
`radio_fem_rxgain` separately (`0a813249`, 2026-05-12). Parallel development on a
vendor-provided class, **not** upstream adopting ours.

Owner decision: **adopt upstream's FEM, retire our divergence, keep our client surface on
top** — tracked as **#629**. So scope any RC52 FEM work against #629's outcome rather
than today's Offband implementation. `[owner decision, recorded on #629 — read that
issue for the current position rather than this summary]`

Related: upstream's `def("fem_rxgain", _parent->rx_boosted_gain)` is a real copy-paste bug
(`radio_fem_rxgain` declared but never serialized). **Do not file it** — upstream PR #3137
already fixes it and is open and actively iterating.

## Display — T108 / NV3001B

128 × 220, 16-bit RGB565, 4-wire write-only SPI, no MISO. `RDDID` returns `0x300101`.
Full driver notes, init sequence and troubleshooting in [`RCC6 TFT Guide.md`](./RCC6%20TFT%20Guide.md);
standalone Arduino test in [`radiocore-c6-tft-test.ino`](./radiocore-c6-tft-test.ino).

**UNRESOLVED — do not treat either answer as settled.** The guide (bench-tested here)
states the panel is portrait and *must* run rotation 0, with landscape producing a
garbled 128×128 square. The n30nex community firmware describes the same panel as
220×128 landscape. Both cannot be literally true. `[hypothesis: untested]` The likeliest
reconciliation is that MeshCore's `NV3001BDisplay` handles rotation in a way the bare
test sketch does not — unverified. Settle it on hardware before building UI work on
either assumption.

## Prior art

| Board | In `firmware-base` today | Community (n30nex, MIT) |
|---|---|---|
| RC32 | ✅ **`variants/heltec_rc32`**, 16 envs + `NV3001BDisplay` — arrived with the 1.17.0 base | — |
| RCC6 | ❌ none | companion BLE/USB/WiFi/Web-AP, repeater, room server, MQTT observer + WebUI |
| RC52 | ❌ none | BLE companion, headless repeater, room server ± TFT |

`variants/heltec_rc32` and `src/helpers/ui/NV3001BDisplay.*` were authored upstream in
commit `17d68e32`, dated **2026-07-08** — after the 1.16.0 release (2026-06-06) and inside
1.17.0 (2026-08-09). `[verified: git merge-base --is-ancestor 17d68e32 companion-v1.16.0
→ NO; … companion-v1.17.0 → YES]`

**They are now in our tree.** The 1.17.0 base update (#614/#628) merged on 2026-08-13, so
the earlier framing — RadioCore being gated on that update, or needing `17d68e32`
cherry-picked onto 1.16.0 — is **historical**. The variant is present; what remains is
validating it against our actual hardware.

Author note: `heltec_rc32` and `NV3001BDisplay` are by **Quency-D**, a Heltec vendor
engineer — 98 upstream commits, essentially all on Heltec variants (`heltec_v4` ×31,
`heltec_rc32` ×21, tracker/tower/solar/t096/vision_master, plus the `boards/heltec_*.json`
definitions). `[verified: git log --author=Quency-D upstream/main -- variants/ ]` So this is
vendor-maintained board support, not a community contribution — relevant to how much we
should diverge from it.

### rc32 BLE config: nothing to do

**For `heltec_rc32`, no config changes are needed.** The NimBLE declarations Offband
requires are already present on `firmware-base`, on all six companion envs. Do not add
them; they are there.

Verify in one command:

```
grep -cE "esp32_ble\.lib_deps|esp32_no_ble\.build_src_filter" variants/heltec_rc32/platformio.ini
```

`[verified: → 6 as of 2026-08-14]` They arrived via `42cdf09a` — *"fix(#645): declare the
#199 NimBLE escape hatch on 21 upstream-new ESP32 envs"* — which
`git merge-base --is-ancestor 42cdf09a 78a84df7` places **inside the 1.17.0 merge**.

If that grep ever returns fewer than 6, the declarations have been lost and the section
below explains what to restore.

#### Why the declaration is needed — background, for *other* modules

This matters when adding a **new** upstream-derived ESP32 env, not for rc32.

Offband migrated ESP32 BLE from Bluedroid to **NimBLE** (#288). `SerialBLEInterface.h:4`
includes `<NimBLEDevice.h>` **unconditionally**, and companion envs pull
`+<helpers/esp32/*.cpp>` with a greedy wildcard — which drags
`helpers/esp32/SerialBLEInterface.cpp` into *every* ESP32 env, BLE or not. Upstream envs
arrive Bluedroid-shaped and declare neither escape hatch, so they fail. The two hatches
work differently:

| Declaration | What it actually does |
|---|---|
| `${esp32_ble.lib_deps}` | adds `h2zero/NimBLE-Arduino @ ^2.0.0` so `<NimBLEDevice.h>` resolves |
| `${esp32_no_ble.build_src_filter}` | `-<helpers/esp32/SerialBLEInterface.cpp>` — removes the file from the build entirely |

`[verified: platformio.ini [esp32_ble] / [esp32_no_ble]; src/helpers/esp32/SerialBLEInterface.h:4]`

So a non-BLE env that omits its hatch fails at **compile** time on an unresolvable header,
not at link time — the `.cpp` is compiled when it should not have been built at all.

#### CI does not cover rc32

As of 2026-08-14, no rc32 env appears in `.github/workflows/ci.yml` or
`.github/release-envs.txt` (`grep -c rc32` → 0 in both), so **CI will not catch an rc32
break — verify current status rather than assuming coverage.** Adding an rc32 env to the
matrix is a separate, owner-approved decision; the matrix is a required merge gate and is
not grown or shrunk casually.

`scripts/check_esp32_ble_deps.py` (#199) runs in `config-lint` and catches this class.
A second cross-check, `scripts/check_env_capability_claims.py` (#649), is **not in the
tree** — check whether it exists before relying on it. Its absence is deliberate, not an
oversight; consult PR #650 itself for status rather than inferring intent from the
issue/PR state.

### nRF52 companions on the 1.17.0 base

`fix(#668)`: nRF52 BLE companions crash-boot looped on 1.17.0 — the merge ported only the
ESP32 branch to upstream's `MultiSerialInterface`, leaving the nRF52/STM32 legacy
`serial_interface.begin()` live alongside the new common block, so
`SerialBLEInterface::begin()` ran twice. Fixed; confirmed on a RAK4631 and a T1000-E.
Relevant to **RC52** (#625), which is nRF52840 — branch from a base containing #668.
Side benefit: the fix recovered **4,624 bytes of static RAM on every companion build**,
ESP32 included. `[verified: commit fix(#668) on firmware-base — read its body for the
measurement and the two-begin() call trace]`

## Wi-Fi HaLow — deferred

HT-HC01 V2 is a **Morse Micro MM6108**: 802.11ah, 863–870 / 902–928 MHz, 1/2/4/8 MHz
channels, up to 32 Mbps, 27 ±1 dBm at 915 MHz, VDD 3.0–3.6 V with **VFEM at 5 V**. It is a
**companion module requiring a host** over SDIO 2.0 or UART, driven by the Morse Micro SDK.
`[verified: heltec.org HT-HC01 V2 product page, 2026-08-10]`

It carries IP, not MeshCore packets — it is not a longer-range LoRa and not a drop-in for
one. **Owner-deferred**; likely belongs outside a MeshCore fork entirely. `dut.md` notes
HaLow is populated optionally, so it may be a board option rather than a separate unit.

## What is not transcribed

Honest limits of this document — these live only in `vendor/`, or nowhere:

- Connector pitch, mating part number, and mechanical/keying drawing.
- Pin voltage tolerance, rail current limits, and other electrical characteristics.
- Physical placement of buttons, LEDs, antenna connectors and the display mount.
- Full multiplexed-function lists per pin (the tables above carry the functions the
  diagrams label, not every ESP32/nRF alternate function).
- Anything about the two HC01 HaLow units beyond the module datasheet.

## Files here

| File | What it is |
|---|---|
| `README.md` | this document |
| `RCC6 TFT Guide.md` | T108 / NV3001B display guide — identity, wiring, init, troubleshooting |
| `radiocore-c6-tft-test.ino` | standalone Arduino NV3001B test sketch (see its attribution header) |
| `dut.md` | device-under-test plan for the beta units — power measurement (PPK2 / INA228 / FNB58), integration, stress, Pi 5 reporting. Owner's working notes; terse by design |
| `vendor/` | **gitignored** — third-party assets, see `vendor/README.md` |

## References

- Upstream variant: `git show upstream/main:variants/heltec_rc32/platformio.ini`
- [n30nex/NeonPocketMC](https://github.com/n30nex/NeonPocketMC) — release catalog (RC52 + RCC6 builds)
- [n30nex/NeonPocketMC-RCC6](https://github.com/n30nex/NeonPocketMC-RCC6) · [RCC6-Repeater](https://github.com/n30nex/NeonPocketMC-RCC6-Repeater)
- [HelTecAutomation/RadioCore_Library](https://github.com/HelTecAutomation/RadioCore_Library) — Arduino board-config + sensor library (MIT); no LoRa/HaLow
- [HT-HC01 V2 product page](https://heltec.org/project/ht-hc01-v2-wifi-halow-module/)
- Meshtastic PR #11041 (heltec_rcc6 pin mapping), PR #10876 (RC32)
