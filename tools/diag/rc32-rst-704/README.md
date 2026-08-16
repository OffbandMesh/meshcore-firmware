# RC32 RST investigation — diagnostic artifacts (#704 / #719)

Everything here is **diagnostic tooling and evidence**, not shippable firmware. It exists
because the RC32 companion-role RST failure could not be observed with any instrument the
firmware normally provides, and the next session needs the instruments that were built to
chase it.

**Start with [`HANDOFF.md`](HANDOFF.md)** — hard facts, refuted theories, next step.
Same text is posted as a comment on issue #704.

## The one fact that matters

On a companion RST the application runs **zero times** — `nvs_count` went 89 → 90 across one
RST press *plus* one serial attach, and the attach alone accounts for the +1. The failure is
**below our firmware**. Four fixes were built, flashed and tested against theories this single
measurement would have eliminated; all four failed.

⚠ **Attaching to the RC32's USB console power-cycles the board.** Every "it booted fine"
observation taken from a serial attach is an artifact of the attach. The console is native
USB-Serial-JTAG and dies with the chip, so *every* log ever captured is from a boot that
succeeded. This is why an external reader is needed at all.

## Contents

| Path | What it is |
|---|---|
| `HANDOFF.md` | Full write-up: facts, refuted list, open questions, branch state |
| `rc32_uart_sniffer/rc32_uart_sniffer.ino` | **Arduino IDE** sketch — UART sniffer for a Feather ESP32-S3 |
| `sniffer/` | PlatformIO version of the same. ⚠ Its USB config is **unresolved** — see below |
| `scripts/pull_ring.py` | Pulls the caplog ring over the companion protocol, verifying force-caplog took effect at runtime first |
| `scripts/display_mode.py` | Reads live display mode via the `0xC5` device-UI command |
| `evidence/nvs-count-baseline-89.log` | Baseline capture, full boot to `setup:DONE` |
| `evidence/nvs-count-after-rst-90.log` | Post-RST capture — **the +1 delta that proves the app never ran** |
| `evidence/heltec-rc32-datasheet-pin-tables.txt` | Extracted from Heltec's datasheet: 20-pin header table + the RC32-L62 LoRa pin list that **does not match our variant** |
| `evidence/caplog-ring-corrupted-718.txt` | Corrupted caplog pull — became #718 |

## Using the sniffer

Wiring (listen-only, two wires):

```
RC32 pin 12 (U0TXD / GPIO43) ──► Feather RX
RC32 pin 20 (GND)            ──► Feather GND
Feather TX                   ──  NOT CONNECTED
```

⚠ RC32 headers are **female on both sides** and the LCD occupies the top — nothing male to
clip onto. Tap from the **bottom** with a male-to-female Dupont, or a male header pin plus a
female jumper.

**Use Arduino IDE, not the PlatformIO project.** Two attempts at the Feather's USB config
failed: with `ARDUINO_USB_CDC_ON_BOOT=1`, `Serial` lands on the TinyUSB CDC peripheral while
the port that actually enumerates is USB-Serial-JTAG (`303A:1001`), so it flashes, verifies,
and prints nothing. Adding `ARDUINO_USB_MODE=1` did not fix it either. Arduino IDE handles
the Feather's USB setup correctly out of the box. The PlatformIO project is kept only so the
next session does not repeat the same two experiments.

The sketch heartbeats `[hb] SNIFFER-v2 alive n=… up=…s rx_bytes=…` once a second for 30 s,
then every 10 s. **`rx_bytes` is the payoff** — a rising count proves the wire carries data
even if the bytes are undecodable, separating "wrong baud" from "no signal".

| Feather shows | Meaning |
|---|---|
| no `[hb]` | reader not running — fix that before concluding anything |
| `[hb]`, `rx_bytes=0` | reader fine, nothing on the wire — wiring or wrong pin |
| `[hb]`, `rx_bytes` rising, garbage | wire good, baud mismatch |
| `[hb]` + `[rc32-probe]` lines | **wire proven** — only now is pressing RST meaningful |

To get `[rc32-probe]` lines, build the companion with `-D OFFBAND_UART0_PROBE` (the change is
on this branch): it TXs a marker on GPIO43 at boot and every 2 s. **Verify the define reached
`CPPDEFINES`** via `pio run -t envdump` — passing it through `PLATFORMIO_BUILD_FLAGS` has
silently failed to apply in this repo before.

⚠ **Likely complication:** the ESP32-S3 ROM *auto-selects* its console. With USB enumerated it
may route boot messages to USB-Serial-JTAG and leave UART0 silent. If the probe is visible but
the ROM banner never is, power the RC32 **without USB** (VBAT pin 2 or 5V pin 4) so the ROM
falls back to UART0.

## What we are trying to read

```
ESP-ROM:esp32s3-20210327
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
```

`rst:` is the reset cause from silicon — which would finally explain the `ESP_RST_UNKNOWN`
readings. `boot:` is **the mode the strapping pins actually selected**, which decides whether
the application ever gets a chance to start. That is the layer never observed.
