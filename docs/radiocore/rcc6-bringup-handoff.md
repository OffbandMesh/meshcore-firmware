# RCC6 (ESP32-C6) bring-up — handoff

Findings carried out of the 2026-08-17 RC32 session so RCC6 work does not re-derive them.
Companion to **#624** (epic: RadioCore RCC6 firmware support).

> **Read the confidence markers.** `[verified:]` means an artifact produced the evidence and it
> is cited. `[hypothesis:]` means it is a lead and has NOT been confirmed. The RC32 session lost
> hours twice to claims that were asserted from partial reads, so nothing here is stated more
> strongly than it was established.

> ## ⚠ Bring-up is COMPLETE. This document is now historical.
>
> `variants/heltec_rcc6` **shipped** (#806, PR #820). The board boots, the SX1262 initialises and
> reports a live noise floor, the T108 panel paints, and `companion_radio_usb` enumerates —
> verified on `rcc6-bench-1` (`58:8c:81:2f:91:f8`), not merely compiled.
>
> **The shipped `variants/heltec_rcc6/platformio.ini` is the source of truth for pin assignments,
> not this file.** What follows is kept as the record of how the board was approached and which
> traps cost time. Sections corrected against post-bring-up evidence are marked
> **`[resolved:]`** (#804).
>
> One finding did not resolve: after a chip-level reset the RCC6 can fail USB enumeration on
> certain hubs — see **#818** and `docs/radiocore/rcc6-usb-enumeration-report.md`. That is
> vendor-side, not firmware.

---

## 1. What exists today

- ~~**No `variants/heltec_rcc6`**~~ — **`[resolved:]` it exists now** (#806). No
  `boards/heltec-rcc6.json` was needed; see §9.4.
- ~~**Upstream MeshCore has no RCC6 either — we would be first. There is no reference variant to
  copy from.**~~
  **`[resolved:]` (#804) — that was wrong, and only true of *upstream* MeshCore.** Community
  prior art existed before this doc was written:
  - **[n30nex/NeonPocketMC-RCC6](https://github.com/n30nex/NeonPocketMC-RCC6)** (MIT, not a fork,
    pushed 2026-08-13) ships a complete `variants/heltec_rcc6/` — `heltec_rcc6.{cpp,h}`,
    `pins_arduino.h`, `platformio.ini`, `target.{cpp,h}` **and `boards/heltec_rcc6.json`** —
    extending `esp32c6_base`, the same base name this repo defines.
  - **[HelTecAutomation/RadioCore_Library](https://github.com/HelTecAutomation/RadioCore_Library)**
    `src/boards/heltec_rcc6.h` — the vendor's own board header. **This is the one that mattered:**
    it sets `USE_SOFTWARE_SPI 1` / `USE_HARDWARE_SPI 0`, which is why the display is bit-banged.
  - Meshtastic **PR #11041** carries an `heltec_rcc6` pin mapping.

  `docs/radiocore/README.md` and #624 had already recorded the n30nex prior art; this handoff
  simply failed to incorporate them. **Check `docs/radiocore/README.md` §Prior art first.**
- `docs/radiocore/RCC6 TFT Guide.md` and `docs/radiocore/radiocore-c6-tft-test.ino` exist and
  predate this session; they were **not** reviewed during it. (The `.ino` was later flashed as the
  vendor-firmware control in the #818 USB investigation.)
- Vendor docs (gitignored, per-host): `docs/radiocore/vendor/RCC6/RCC6-L62_V1.0-schematic.pdf`,
  `rcc6-schematic-text.txt`, `RCC6.jpg`.

## 2. `[verified:]` P1 carrier header pinout

Read from the rendered schematic (`RCC6-L62_V1.0-schematic.pdf`, symbol **P1 "RadioCore外框"**).
This is the 20-pin carrier connector.

```
 1 GND        11 U0RXD
 2 VBAT       12 U0TXD
 3 GND        13 GPIO23
 4 VDD_5V     14 GPIO22
 5 USER_Key   15 GPIO21
 6 GPIO4      16 GPIO18
 7 GPIO3      17 GPIO15
 8 GPIO2      18 CHIP_PU
 9 GPIO1      19 GND
10 GPIO0      20 VDD_3V3
```

**Signal positions match the RC32** for U0TXD (12), CHIP_PU (18), USER_Key (5) and VBAT (2), so
the RadioCore carrier footprint is consistent across the family.

> ⚠ **Pin 20 is `VDD_3V3` on the RCC6, NOT ground.** The RC32 sniffer wiring uses pin 20 as GND.
> Carrying that habit across ties 3.3 V to the sniffer's ground rail. **Use pin 19** (or 1, or 3).

There is also a second header, **P3** (`HC-PBB40C-40DS-0.4V-2.5-02`, 40-pin board-to-board), which
carries the same nets plus USB_P/USB_N. Not transcribed here; render it if needed.

## 3. `[resolved:]` LoRa + ADC pin map — CONFIRMED, and shipped

> This section originally read *"`[hypothesis:]` — needs re-derivation, do NOT trust"*. **That
> warning was wrong** (#804). The map it distrusted was correct on every signal, and the doc sent
> the next session off to re-derive an answer it already had. Corrected in place; the reasoning
> that produced the false alarm is preserved below, because the underlying caution is still sound.

An early extraction paired LoRa nets to GPIOs by spatial row-matching and produced
`SPI_CS→23, MOSI→22, CLK→21, MISO→20, DIO1→19, RESET_N→8, BUSY→10, SELECT→7`.

**All eight were correct.** Two independent confirmations:

1. **The schematic carries its own printed net-to-GPIO table** beside the RA62A symbol (U5) —
   stated by the vendor on the sheet, not inferred by row-pairing:

   ```
   SPI_CS  GPIO23   SPI_MOSI GPIO22   SPI_CLK GPIO21   SPI_MISO GPIO20
   DIO1    GPIO19   SELECT   GPIO7    RESET_N GPIO8    BUSY     GPIO10
   VEXT_LDO_Ctrl    GPIO11
   ```

2. **The radio works on hardware.** The shipped variant uses exactly these values and the SX1262
   initialises and reports a live noise floor (−95 to −108 dBm) on `rcc6-bench-1`.

`SELECT`/`GPIO7` is the one signal the variant does **not** reference — it needs no build flag.

**`ADC_IN`/`ADC_Ctrl`/`USER_Key` are likewise confirmed**, read from the rendered ESP32-C6 symbol
(U8) bottom edge and matching n30nex independently:

| Net | Package pin | SoC function | GPIO | Shipped as |
|---|---|---|---|---|
| `ADC_Ctrl` | 11 | MTDI | **5** | `PIN_ADC_CTRL=5` |
| `ADC_IN` | 12 | MTCK | **6** | `PIN_VBAT_READ=6` |
| `USER_Key` | 15 | GPIO9 | **9** | `PIN_USER_BTN=9` |
| `VEXT_LDO_Ctrl` | — | — | **11** | `PIN_VEXT_EN=11` |

`GPIO12`/`GPIO13` carry `D_N`/`D_P` — native USB. That is the mechanism behind the §7 enumeration
observation.

### Why the false alarm happened — the caution is still right

The row-paired map was observed to match the Heltec datasheet §3.2.1 table, and §3.2.1 turned out
to be **module-side** pin names, not carrier GPIOs (§5). The agreement was therefore meaningless as
corroboration, and row-pairing is genuinely heuristic — it had produced `U0TXD` and `U0RXD` on the
same pin in an earlier pass.

Concluding *"unverified"* from that was correct. Concluding *"do NOT trust, re-derive"* over-steered:
the honest state was **unconfirmed, probably right, confirm by rendering** — which is what §4 says
to do, and it takes minutes. **Two sources that share an origin are not corroboration; that does
not make either of them wrong.**

## 4. Method that works on these PDFs

Text extraction alone repeatedly misled. What worked:

```python
import fitz, re
doc = fitz.open(PDF); page = doc[0]
words = page.get_text("words")          # x0,y0,x1,y1,text,...

# 1. find nets / part numbers
parts = sorted({w[4] for w in words
                if re.fullmatch(r"[A-Z]{2,4}[0-9]{3,5}[A-Z0-9\-]{0,6}", w[4])})

# 2. locate a symbol, then RENDER it and read it visually -- this is the step
#    that actually resolves pin assignments
r = page.search_for("Battery_ADC")[0]
clip = fitz.Rect(r.x0-260, r.y0-150, r.x1+300, r.y1+170)
page.get_pixmap(clip=clip, matrix=fitz.Matrix(6,6)).save("out.png")
```

**Row-pairing net labels to pin labels is a lead-generator, not an answer.** Confirm by rendering.
PyMuPDF, pdfplumber and pypdf are all installed.

## 5. The §3.2 trap — cost hours on RC32

The Heltec datasheet's §3.2.x tables list **module-side** pin names in `GPIOxx` notation,
identical in form to carrier SoC GPIOs. §3.2.1 is the HT-RA62A LoRa module; §3.2.2 is the
HT-HC01_V2 Wi-Fi HaLow module. Neither describes the carrier's ESP32 GPIOs.

Comparing them against a working variant looks like a contradiction and is a category error.
Submitted to Heltec as beta feedback (Q04) asking them to label the tables and add a
carrier-GPIO column.

## 6. C6-specific traps

- **Blind I2C bus scan wedges the C6 peripheral.** It hangs at address `0x0d` and never returns,
  so `setup()` never reaches `loop()`. This is why `ENV_SKIP_I2C_SENSOR_SCAN` exists (#294).
  **Probe known addresses only.**
- **The UART0 boot beacon is STILL unverified on C6** — bring-up did not settle this.
  `src/helpers/BootBeacon.h` writes raw UART0 FIFO registers and is guarded ESP32-only. The C6 is
  RISC-V with a different register layout; it reads `UART_TXFIFO_CNT_S` from the target's own
  `soc/uart_reg.h` so it *should* adapt, but that has never been tested.

  **Why it was not tested:** `OFFBAND_BOOT_BEACON` is deliberately **not** set in
  `heltec_rcc6_repeater_display_diag`. It compiles, but `simple_repeater` never defines
  `OFFBAND_BEACON_DEFINE_CTOR` nor includes `BootBeacon.h`, so there are no call sites — it would
  emit nothing while looking like live instrumentation. Only `examples/companion_radio` wires the
  beacon up. To actually verify it on RISC-V, enable it on a **companion** env.
- **ADC2 is unusable with WiFi active** on ESP32 generally — check what that leaves on C6 before
  assigning analog pins. On the RC32 this left almost nothing (#780 / #755).

## 7. Bench state as of handoff

- **`wroom-sniffer`** — a **classic ESP32** WROOM (not S3), CP2102, registry MAC
  `d4:e9:f4:6f:34:5c`, hash `6&82CE668`. Currently COM23, but **identify by hash, not port** —
  it has moved. Runs `tools/diag/rc32-boot-740/rc32_uart_sniffer_v3/rc32_uart_sniffer_v3.ino`
  with `SNIFFER_GENERIC_S3` defined → sniff RX `GPIO18`, RST `GPIO17`, BOOT/USR `GPIO16`.
  **Owner wired to 16, 18 and the board's `RX` pin — the wire on `RX` (GPIO3 = U0RXD) conflicts
  with the USB bridge and must move.** Mapping of which wire is which was not established.
- **RCC6 enumerates on native USB** — `303A:1001`, distinct from `rc32-bench-1`.
  **`[resolved:]` confirmed** (#804); direct USB flashing is available and was used throughout
  bring-up. **But** after a chip-level reset it can fail to re-enumerate on certain hubs and needs
  a physical replug — **#818**. Budget for that when benching: it looks like a dead board and is
  not one.
- **`feather-sniffer` (COM16) is busy** — running the RC32 discharge capture with a MAX17048.
  Do not repoint it; it would end a multi-hour run.

## 8. Instrumentation available on day one

RCC6 inherits a working bench rather than building one:

- `tools/diag/rc32-boot-740/scripts/capture.py` — durable timestamped capture, and `--queue RST`
  injects commands into a **running** capture (the port is held exclusively; `--send` cannot).
- The sniffer sketch: heartbeat + `rx_bytes` counter, so "nothing received" is distinguishable
  from "instrument dead"; open-drain RST/BOOT with a dead-man release; optional MAX17048.
- `tools/diag/rc32-boot-740/scripts/battery_runtime.py` — runtime projection that reports a
  range when its two methods disagree.

## 9. Suggested first steps — `[resolved:]` all four are DONE

Kept for the record of how bring-up was sequenced. **Step 1 is the one worth reusing on RC52.**

1. ~~**Prove the sniff path** before any firmware work.~~ **Done, and it earned its keep.** The C6
   ROM prints a boot banner on UART0 at 115200 before any application runs, so `rx_bytes` climbs
   with no variant flashed. This validated wiring independently — and later became the *only*
   observability channel when USB enumeration died (#818), which is exactly the situation it was
   meant for. **Do this first on RC52 too.**
2. ~~**Re-derive the LoRa pin map.**~~ **Unnecessary — the original map was correct.** See §3.
3. ~~**Confirm whether COM45 is the RCC6.**~~ **Confirmed.** See §7.
4. ~~**Scaffold `variants/heltec_rcc6` — with `boards/heltec-rcc6.json` for an ESP32-C6 target,
   which no existing variant in this repo provides a template for.**~~
   **`[resolved:]` (#804) — no custom board JSON was needed, and the "no template" claim was
   wrong.** This repo already had three C6 variants — `xiao_c6`, `lilygo_tlora_c6`,
   `m5stack_unit_c6l` — and none uses a custom board JSON; all three take stock
   `esp32-c6-devkitm-1`.

   The real gap was never C6 support, it was **flash size**: RCC6 is 16 MB, the existing three are
   4/8 MB parts (they use `min_spiffs.csv` to fit). `xiao_c6` already showed the way — override
   flash geometry rather than mint a board file. The shipped variant does exactly that:

   ```ini
   board                     = esp32-c6-devkitm-1
   board_build.partitions    = default_16MB.csv
   board_upload.flash_size   = 16MB
   board_upload.maximum_size = 16777216
   ```

   (n30nex does ship its own `boards/heltec_rcc6.json`. It works; it is simply not required.)
