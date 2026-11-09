# Feature PLAN: T-Deck Pro e-paper bring-up

| | |
|---|---|
| **Feature** | [#2005](https://github.com/OffbandMesh/meshcore-firmware/issues/2005) · Citadel `Crosswire-vyl` |
| **Serves** | Initiative [#2004](https://github.com/OffbandMesh/meshcore-firmware/issues/2004), LilyGo T-Deck Pro e-paper (H693) device support. The Initiative has no plan of record of its own; this is its first Feature. |
| **Status** | Approved (merged as `0b1a62f0`, PR #2024). Owner's §9 rulings recorded 2026-11-07. |
| **Owner** | @Strycher |
| **Last updated** | 2026-11-09 |

## At a glance

- **What:** one T-Deck Pro e-paper unit on the bench, identified, backed up, and running an Offband diag build that draws the Offband GUI on its e-paper panel.
- **Why now:** two units are on the bench and nothing in this repo or upstream MeshCore can run on them.
- **How:** identify the unit, take a verified backup of its factory flash, add a `lilygo_tdeck_pro` variant with a diag env, then prove it all together.
- **Done when:** the bench unit boots the diag build, the panel shows the home screen, the capture log shows the boot and display lines, and the factory backup is on disk with matching hashes.

## 1. Diagnosis

What's true today. Each Epic traces back to a row here.

| # | Finding | Evidence | Effect |
|---|---|---|---|
| 1 | No variant in this repo or in upstream MeshCore targets the T-Deck Pro. | `variants/` has `lilygo_tdeck` and `lilygo_techo` but no T-Deck Pro; same in the fetched upstream tree. | Epic C writes a new variant. |
| 2 | `variants/lilygo_tdeck` is the nearest relative but not a base: TFT panel, keyboard on a separate MCU, different LoRa pins. | `variants/lilygo_tdeck/platformio.ini` (`[LilyGo_TDeck_hw]`, `PIN_TFT_*`, `P_LORA_NSS=9`). Vendor: T-Deck Pro keyboard is a TCA8418 on I2C. | Pins come from the vendor (Epic A), not from the T-Deck variant. |
| 3 | The hardware: ESP32-S3, 16 MB flash, 8 MB OPI PSRAM; GDEQ031T10 3.1" b/w e-paper 240x320 on a UC8253; touch CST328 on V1.0 and CST3530 on V1.1; SX1262; MIA-M10Q GPS; BQ25896 charger (0x6B); BQ27220 gauge (0x55); BHI260AP (0x28); LTR-553ALS (0x23); keyboard TCA8418 (0x34); SD on the shared SPI bus (SCK 36, MOSI 33, MISO 47, CS 48). | Vendor repo Xinyuan-LilyGO/T-Deck-Pro [read, not tested]. | Epic A confirms the revision on the real unit, since touch differs by revision. |
| 4 | The panel's RST line differs by revision. On V1.0 it is not wired to the ESP32: the schematic has no `LCD_RST` net and the vendor's V1.0 pin header sets `BOARD_EPD_RST -1`. V1.1 wires it to GPIO16 and adds a front-light on GPIO45. LilyGo's FAQ blames a blank panel on an unconnected RST and ships a fix firmware for it. | Vendor schematics V1.0 and V1.1, sheet 2 (`LCD_RST`, `BL_PWM` on V1.1 only); `examples/factory/utilities.h` (V1.0) and `examples/factory2/utilities.h` on branch `HD-V2-250915` (V1.1); vendor FAQ [read, not tested]. | The bench unit is V1.1 [verified 2026-11-07, #2014], so the variant drives RST on GPIO16 and has no V1.0 path. |
| 5 | GxEPD2 supports GDEQ031T10 from 1.5.4. This repo pins 1.6.2. Neither source says whether partial refresh works on this panel. | GxEPD2 README [read]; `variants/lilygo_techo/platformio.ini` (`zinggjm/GxEPD2 @ 1.6.2`). | The library is in hand. Partial refresh is measured in Epic C, not assumed. |
| 6 | `GxEPDDisplay` is the repo's e-paper class. It opens its own `SPIClass(FSPI)`, calls `setPartialWindow` on every frame, and declares a 128x128 logical canvas. | `src/helpers/ui/GxEPDDisplay.cpp:14,30-39`; `GxEPDDisplay.h:42`. Vendor schematic sheet 3: the LoRa radio uses SCK 36, MOSI 33, MISO 47 with CS 3 [read, not tested]. | The panel, SD and the LoRa radio share one SPI bus. Epic C settles how the class shares that bus with the radio, and checks the canvas size, before relying on it. |
| 7 | The bench unit `tdeck-pro-1` is registered and resolves by USB serial, and is V1.1: its I2C scan found the DRV2605 at 0x5A and no XL9555 at 0x20 (LilyGo's own detection table), and its factory image carries V1.1's hardware version `v2.0-250915`. The second unit stays boxed. | Per-host registry and `HARDWARE.local.md` [verified 2026-11-04]; revision [verified 2026-11-07, #2014], no device operation. | Only V1.1 is built. |
| 8 | A port-open resets an ESP32-S3, and four resets inside 60 s trip SafeBoot's partition swap. | Project records on serial resets and SafeBoot boot counting. | Every bench step holds one capture open and spaces resetting operations 60 s apart. |
| 9 | Multi-MB esptool reads on S3 USB-Serial/JTAG die with the stub. With `--no-stub`, a full 16 MB read of the bench unit took 1,717.8 s (about 29 minutes); one read stalled at 0.7% and never exited. | Project record of the 2026-06-29 recovery; `pio-flash backup --help`; read log on #2017 [verified 2026-11-04]. | The backup uses `--no-stub`, and every read runs under a hard time limit well above 29 minutes. |
| 10 | The bring-up directive: every env built is a full diag build (forced caplog, boot beacon, log mirror), named `_diag`. | Standing owner directive 2026-08-22; `[rc32_diag]` in `variants/heltec_rc32/platformio.ini`. | Epic C ships only a `_diag` env. |
| 11 | Registering a unit writes nothing to it. `pio-flash bootstrap` reads the USB identity and, on ESP32 boards, the MAC through the ROM loader, then writes the host-side registry. It resets the chip once. | `pio-flash --help` lists `bootstrap` apart from the write commands (`confirm`, `erase-region`, `factory-reset`); the per-host flash history's `bootstrap` records for ESP32-S3 units carry only the USB identity and the MAC. On the bench unit, the flash history holds one `bootstrap` and two `backup` records and no write [verified 2026-11-04]. | A1 can run before the backup without touching the factory image. |
| 12 | The GUI's class-A portrait layout is exactly 240x320, the panel's size. The GUI flushes only through the color TFT driver (`St7789Dma`), and its themes are light ink on dark backgrounds. | `src/helpers/gui/GuiLayout.h:8`; `src/helpers/gui/hw/LvglPort.cpp`; `src/helpers/gui/GuiTheme.cpp`. | C2 adds an e-paper flush: each grey pixel to black or white, inverted so every theme reads black on white, with refreshes paced because each one blocks (row 15). |
| 15 | On `tdeck-pro-1`, init plus the first full refresh took 1,881 ms and a partial refresh 772 ms, with no BUSY timeout. | Capture `C:/Dev/.offband/captures/tdeck-pro-1/diag-boot.log`, text-UI diag build `6111eecd` [verified 2026-11-07, #2037]. | The GUI leaves at least a second after each refresh before the next, so the main loop gets that time, and makes every 20th one full. Both are bench values to tune. |
| 13 | The 4-pin 1.0 mm connector (P6, the same JST-SH size as Qwiic) is UART0, not I2C. Pins: 1 GND, 2 3.3 V, 3 U0TXD (GPIO43), 4 U0RXD (GPIO44). Both nets also run to the GPS, MIA-M10Q TX to U0RXD and RX from U0TXD, each through 22 Ω, so the connector taps the GPS link. P6 is the only 4-pin 1.0 mm connector on either revision, and no connector carries the I2C bus (GPIO13/14). | Vendor schematics V1.0 and V1.1, sheet 2 (P6) and sheet 3 (GPS); `BOARD_GPS_RXD 44` and `BOARD_GPS_TXD 43` in both pin headers [read, not tested]. The schematic does not show where P6 sits on the case. | A Qwiic I2C sensor in P6 does nothing: its SDA and SCL land on TX and RX. An I2C sensor needs a wire onto the internal bus. P6 can be a 3.3 V log port outside USB, so reading it does not reset the chip (row 8), but only with the GPS held off (`GPS_EN`, GPIO39) and a log TX routed to GPIO43. Whether a powered-off GPS loads the line is not known and gets measured before anyone relies on it. |
| 14 | The radio has its own 3.3 V regulator (RT9080, U3) whose enable is GPIO46 (`LORA_EN`): the SX1262 has no power until firmware drives it HIGH. The radio is an HPB16B3 module with its TCXO and RF switch inside; LilyGo's V1.1 firmware sets TCXO 2.4 V, DIO2 as the RF switch and a 140 mA current limit. | V1.1 schematic sheet 1 (U3) and sheet 3 (U10); `examples/factory2/peri_lora.cpp` and `factory.ino` on `HD-V2-250915` [read, not tested]. Full pin map on #2007. | C1 drives `LORA_EN` high in `TDeckProBoard::begin()` and carries LilyGo's radio settings. |

## 2. Scope

**In:**
- Identify and register **one** bench unit, with its hardware revision.
- A full, verified backup of that unit's factory flash, before any write.
- A `lilygo_tdeck_pro` variant whose `_diag` env boots and draws the Offband GUI on the e-paper panel, plus the text-UI `_diag` env that came first, kept as the fallback.
- CI for that env, with positive and negative tests.
- Integration testing on the bench unit, and `OUTCOMES.md`.

**Out:**
- The second unit. It stays untouched.
- A GUI theme of its own for e-paper (the #1313 plan's "E-ink theme"). This Feature converts the existing themes to black and white.
- Keyboard input, touch, GPS, charging and the fuel gauge, IMU, light sensor, SD, 4G and audio. Each becomes its own Feature.
- A release build. Bring-up ships no non-diag env.

## 3. Epics

Tasks are 3–4 points or smaller. Each code-bearing Epic ends with a verification task and gets one PR. Gemini 2.5 Pro reviews each task before it closes.

### Epic P: PLAN OF RECORD ([#2006](https://github.com/OffbandMesh/meshcore-firmware/issues/2006))

| Task | Pts |
|---|---|
| **P1 · Write this plan** ([#2012](https://github.com/OffbandMesh/meshcore-firmware/issues/2012)): Gemini-reviewed, committed. | 3 |
| **P2 · Push, PR, merge** ([#2013](https://github.com/OffbandMesh/meshcore-firmware/issues/2013)): merging is the owner's approval. | 1 |

### Epic A: DIAGNOSIS ([#2007](https://github.com/OffbandMesh/meshcore-firmware/issues/2007))

No repo change. The record lives on the issues and in the gitignored hardware log.

| Task | Pts |
|---|---|
| **A1 · Register the bench unit** ([#2014](https://github.com/OffbandMesh/meshcore-firmware/issues/2014)): start a held capture into `C:/Dev/.offband/captures/<unit>/`, then `pio-flash bootstrap` reads the chip identity and registers the unit. Record the revision without opening the unit: LilyGo's I2C detection table and the factory image's hardware version (row 7). Checked by: the unit resolves by serial in `pio-flash list`, and its hardware-log card is filled in. | 2 |
| **A2 · Vendor facts** ([#2015](https://github.com/OffbandMesh/meshcore-firmware/issues/2015)): full pin map, panel RST/BUSY/DC/CS, which pins share the SPI bus, the GxEPD2 class for GDEQ031T10, touch per revision, I2C addresses, the RST FAQ. Every line cited. | 3 |
| **A3 · Epic verification** ([#2016](https://github.com/OffbandMesh/meshcore-firmware/issues/2016)): the owner confirms the record. | 1 |

### Epic B: BACKUP ([#2008](https://github.com/OffbandMesh/meshcore-firmware/issues/2008))

No repo change. Nothing is written to the unit.

| Task | Pts |
|---|---|
| **B1 · Two full reads** ([#2017](https://github.com/OffbandMesh/meshcore-firmware/issues/2017)): `pio-flash backup <unit> --no-stub` twice, 16 MB each, 60 s apart, capture running, each read under a hard time limit well above 29 minutes (row 9). Checked by: both files are 16,777,216 bytes and their SHA-256 match. | 2 |
| **B2 · Store and record** ([#2018](https://github.com/OffbandMesh/meshcore-firmware/issues/2018)): both files under the per-host flash-backups folder; names, sizes and hashes in the hardware log; hash and size only on #2008. | 1 |
| **B3 · Epic verification** ([#2019](https://github.com/OffbandMesh/meshcore-firmware/issues/2019)): the owner confirms the backup before the first write. | 1 |

### Epic C: DIAG BUILD ([#2009](https://github.com/OffbandMesh/meshcore-firmware/issues/2009))

Tasks filed 2026-11-07: [#2033](https://github.com/OffbandMesh/meshcore-firmware/issues/2033) to [#2038](https://github.com/OffbandMesh/meshcore-firmware/issues/2038).

| Task | Pts |
|---|---|
| **C1 · Variant skeleton:** `variants/lilygo_tdeck_pro/` (board, target, `platformio.ini`) with the A2 pin map, a `[tdeck_pro_hw]` section and a `[tdeck_pro_diag]` block carrying the fleet diag surface (forced caplog, boot beacon, log mirror where a pin exists). One env: the BLE companion `_diag`. Checked by: it builds with no warnings new to the variant. | 3 |
| **C2 · E-paper panel:** our home screen, the Offband GUI's, on GDEQ031T10. First ui-new's through `GxEPDDisplay` (bench-flashed, #2037), then the GUI through an e-paper `GuiPanel` and LVGL flush (row 12), settling the canvas size and SPI bus sharing from row 6, driving RST on GPIO16 (row 4), and logging init, BUSY timeouts and refresh time through MeshLog. Partial refresh measured, not assumed. Checked by: the build, plus native tests of any pure logic it adds. | 4 |
| **C3 · CI:** the env in the curated matrix; a config-lint check that the T-Deck Pro diag env carries the full diag surface (positive: the env passes; negative: a fixture missing a flag fails). | 3 |
| **C4 · Docs:** the `docs/changes/` record and a `GUIDE-DRAFT.md` with a `coverage: planned` catalog entry, per the documentation workflow. | 2 |
| **C5 · Bench flash and Epic verification:** after B3 closes and with the owner's go naming the unit, flash the diag image through `scripts/pio-flash`, check the image identity first, hold the capture, and confirm the panel draws our home screen, and the log shows the boot and display-init lines and the measured full- and partial-refresh times. | 2 |
| **C6 · Push, PR, merge.** | 1 |

### Epic D: INTEGRATION TESTING ([#2010](https://github.com/OffbandMesh/meshcore-firmware/issues/2010))

Tasks are filed when this plan merges.

| Task | Pts |
|---|---|
| **D1 · Integrated bench run:** cold-boot the merged diag build on the bench unit; the unit resolves to the A1 identity; the backups on disk still hash to the B2 record; the panel draws the GUI; the capture shows a clean boot with no SafeBoot fallback and no reset line on the boot screen. | 2 |
| **D2 · `OUTCOMES.md`** beside this plan: plain summary first, then what was delivered, the evidence, and what's left. | 2 |
| **D3 · Push, PR, merge.** | 1 |

## 4. Order and dependencies

P → A → B → C → D.

- **A before B:** the backup needs a registered unit, because `pio-flash backup` takes a registry name.
- **B before any write:** C1–C4 can be built while B runs, but C5's flash waits until B3 closes. No write happens to the unit before then.
- **C before D:** D proves the merged build.
- **Files:** only Epic C and D touch the repo outside this folder. C1, C2 and C3 all touch `variants/lilygo_tdeck_pro/platformio.ini`, so they run in that order.

## 5. Grants and authorization

| Term | Proposed | Why |
|---|---|---|
| **What merging this plan authorizes** | Epics C and D as written, end to end, holding merges to each Epic's PR. That is the `epic_chain` below. Epics A and B change no repo file, so no grant covers them; each of their device steps runs on its own owner go. | The plan is the master layout. |
| **Merges** | 2 | One PR each for Epic C and Epic D. Epics A and B change no repo file. This plan's own merge is the approval, not a grant use. |
| **Epic verification** | The owner approves each Epic's PR. | Each one ends on a bench result only the owner can see on the panel. |
| **Owner steps during the run** | Plug in the bench unit; confirm the panel draws in C5 and D1. | The panel is the thing under test. |
| **Stops** | A backup hash mismatch; an identity that doesn't match the A1 record; a blank panel with RST driven on GPIO16 (row 4); any SafeBoot boot count of 3. | Each one means the next step would act on a wrong premise. |
| **Flash** | None in the grant. Each device operation gets the owner's go naming the unit. | Owner directive 2026-11-04: one flash, one explicit go. |

```grant-terms
{
  "epic_chain": [2009, 2010],
  "chain": true,
  "verification": { "mode": "owner" },
  "merge": { "budget": 2, "basis": "One PR for Epic C (#2009, the variant and diag env) and one for Epic D (#2010, OUTCOMES.md). Epics A (#2007) and B (#2008) change no repo file, so they have no PR. This plan's own PR merges as the approval and uses no budget." },
  "flash": null,
  "reset": null,
  "expires_after_hours": 168
}
```

## 6. Can't-tell branches

| Where | Can't tell what | Does |
|---|---|---|
| A1 registration | Which of the two units is the bench unit | **ask**: the owner plugs in one; the other stays boxed. |
| B1 backup | The two reads' hashes differ, or a read is short | **stop**: no write; report and re-read only on the owner's go. |
| C2 panel init | BUSY never goes low | **stop** the display path: log the timeout through MeshLog and leave the panel alone. The radio boot carries on so the capture can show the failure. |
| C2 panel init | The panel reports success but draws nothing | **stop**: report it on #2009 as a bench finding, and confirm RST is driven on GPIO16 (row 4) before any code change. |
| C5 / D1 flash | The connected unit doesn't match the A1 identity | **stop**: `pio-flash` refuses; never pass `--approve-class-match` without the owner's word. |
| C5 / D1 boot | SafeBoot boot count reaches 3 | **stop** all port-opens until the unit has run 60 s. |
| C3 config-lint | The diag env lacks a flag of the diag surface | **stop**: CI fails. |

No branch proceeds without its condition, so no fail-open grant is needed.

`"flash": null` in the grant terms means the grant covers no device operation, so the flash gate asks the owner for each one. That is the template's meaning of `null`, and it is the per-flash go this plan wants.

## 7. Verification

- **Named command:** `pio run -e LilyGo_TDeck_Pro_companion_radio_ble_diag`, plus the native suite and the full config-lint job from `.github/workflows/ci.yml` (including `scripts/test_tdeck_pro_env.py`).
- **Named command, GUI:** `pio run -e LilyGo_TDeck_Pro_companion_radio_ble_gui_diag`, plus the native `test_gui_eink`.
- **Acceptance bar:** the bench unit's panel shows the GUI's home screen; the USB capture shows the display init line and the measured refresh times; the backup hashes match. Two lines the bar first named can't come from this build: the boot beacon writes to UART0 (GPIO43, P6), not USB, and the companion role never marks its boot valid (only the repeater calls `markBootValid()`), so it prints no SafeBoot validation line [read from source, #2037].

## 8. Done when

- The bench unit is registered, with its revision, in the hardware log.
- Two matching full reads of its factory flash are on disk and recorded.
- The diag build is merged, in CI, and its PR shows the named command's output.
- The bench unit runs it and the panel draws, seen by the owner.
- `OUTCOMES.md` is approved.

## 9. Your choices

Owner's rulings, 2026-11-07 ([#2009](https://github.com/OffbandMesh/meshcore-firmware/issues/2009#issuecomment-6703414223)):

| # | Question | Ruling |
|---|---|---|
| 1 | Where does this Feature sit? | Under device Initiative #2004. |
| 2 | Which unit goes on the bench? | The unit already on the bench, `tdeck-pro-1` (V1.1, row 7). The second stays boxed. |
| 3 | Which role does the diag env build? | The BLE companion `_diag`, and only that for now. USB stays free for the log. |
| 4 | What does the diag draw? | Our home screen: the Offband GUI's, on the e-paper panel, with full and partial refresh timed on it. (Recorded as ui-new's on 2026-11-07, which was wrong; corrected 2026-11-09.) |

## 10. Carried-in work

| Item | Goes to |
|---|---|
| The #1313 plan's "T-Deck Pro and the E-ink theme" later Feature | Stays with Initiative #1312; it depends on this Feature. |

Epic C's tasks are filed (#2033 to #2038). Epic D's tasks get a GitHub issue and a Citadel task when Epic C merges.
