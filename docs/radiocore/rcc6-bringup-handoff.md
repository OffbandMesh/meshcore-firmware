# RCC6 (ESP32-C6) bring-up — handoff

Findings carried out of the 2026-08-17 RC32 session so RCC6 work does not re-derive them.
Companion to **#624** (epic: RadioCore RCC6 firmware support).

> **Read the confidence markers.** `[verified:]` means an artifact produced the evidence and it
> is cited. `[hypothesis:]` means it is a lead and has NOT been confirmed. The RC32 session lost
> hours twice to claims that were asserted from partial reads, so nothing here is stated more
> strongly than it was established.

---

## 1. What exists today

- **No `variants/heltec_rcc6`**, no `boards/heltec-rcc6.json`. Nothing in tree.
- **Upstream MeshCore has no RCC6 either** — we would be first. There is no reference variant to
  copy from, unlike most boards in this repo.
- `docs/radiocore/RCC6 TFT Guide.md` and `docs/radiocore/radiocore-c6-tft-test.ino` exist and
  predate this session; they were **not** reviewed during it.
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

## 3. `[hypothesis:]` — needs re-derivation, do NOT trust

An early extraction paired LoRa nets to GPIOs by spatial row-matching and produced
`SPI_CS→23, MOSI→22, CLK→21, MISO→20, DIO1→19, RESET_N→8, BUSY→10, SELECT→7`.

**That was then observed to match the Heltec datasheet §3.2.1 table — and §3.2.1 turned out to be
MODULE-SIDE pin names, not carrier GPIOs.** See §5. So the apparent agreement proved nothing
about which C6 GPIO each net lands on, and the row-pairing itself is heuristic (it produced
`U0TXD` and `U0RXD` on the same pin in an early pass).

**Re-derive the LoRa pin map from the MCU symbol**, the way it was eventually done for the RC32:
render the ESP32-C6 symbol region and read the net labels against the pin rows visually. Do not
ship a variant on the row-pairing output.

Likewise `ADC_IN`/`ADC_Ctrl`/`Battery_ADC` nets **exist** on the RCC6 schematic, but their GPIO
assignments were never confirmed.

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
- **The UART0 boot beacon is unverified on C6.** `src/helpers/BootBeacon.h` writes raw UART0 FIFO
  registers and is guarded ESP32-only. The C6 is RISC-V with a different register layout; it
  reads `UART_TXFIFO_CNT_S` from the target's own `soc/uart_reg.h` so it *should* adapt, but that
  has never been tested. Verify before relying on it for boot diagnostics.
- **ADC2 is unusable with WiFi active** on ESP32 generally — check what that leaves on C6 before
  assigning analog pins. On the RC32 this left almost nothing (#780 / #755).

## 7. Bench state as of handoff

- **`wroom-sniffer`** — a **classic ESP32** WROOM (not S3), CP2102, registry MAC
  `d4:e9:f4:6f:34:5c`, hash `6&82CE668`. Currently COM23, but **identify by hash, not port** —
  it has moved. Runs `tools/diag/rc32-boot-740/rc32_uart_sniffer_v3/rc32_uart_sniffer_v3.ino`
  with `SNIFFER_GENERIC_S3` defined → sniff RX `GPIO18`, RST `GPIO17`, BOOT/USR `GPIO16`.
  **Owner wired to 16, 18 and the board's `RX` pin — the wire on `RX` (GPIO3 = U0RXD) conflicts
  with the USB bridge and must move.** Mapping of which wire is which was not established.
- **RCC6 appears to enumerate on native USB** — a `303A:1001` device (COM45) distinct from
  `rc32-bench-1`. `[hypothesis:]` not confirmed. If true, the C6 is reachable directly without
  the sniffer.
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

## 9. Suggested first steps

1. **Prove the sniff path** before any firmware work. The C6 ROM prints a boot banner on UART0
   at 115200 before any application runs, so powering the RCC6 should make `rx_bytes` climb even
   with no variant flashed. That validates wiring independently of everything else.
2. **Re-derive the LoRa pin map** from the MCU symbol (§3). Render, do not infer.
3. **Confirm whether COM45 is the RCC6.** If so, direct USB flashing is available.
4. Only then scaffold `variants/heltec_rcc6` — with `boards/heltec-rcc6.json` for an ESP32-C6
   target, which no existing variant in this repo provides a template for.
