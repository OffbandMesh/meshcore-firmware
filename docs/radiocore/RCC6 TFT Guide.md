# Heltec RadioCore C6 (RCC6) — T108 TFT Display Guide

Guide and documentation for the **Heltec T108 V1.0.1 (RST108)** TFT display used with the **Heltec RadioCore C6 (RCC6)** board, plus instructions for the included Arduino test sketch.

---

## 1. Display identity

| Property | Value |
|---|---|
| **Display name** | Heltec **T108** TFT |
| PCB marking | **T108** / **V1.0.1** / **RST108** |
| Controller | **NV3001B** (Heltec custom TFT controller) |
| Resolution | **128 × 220** (portrait physical panel) |
| Color depth | 16-bit RGB565 (`COLMOD 0x05`) |
| Interface | 4-wire SPI (write-only, no MISO) |
| SPI clock | 4 MHz |
| Required rotation | **Rotation 0 (portrait, MADCTL `0x00`)** |

> ⚠️ **Orientation note:** the panel is physically **128 wide × 220 tall (portrait)**.
> It must be driven in **rotation 0**. If you send rotation 3 (landscape,
> MADCTL `0xA0`), only a 128×128 square renders correctly and the rest of the
> panel shows jumbled garbage. This is a software/orientation issue, **not** a
> hardware fault.

---

## 2. Wiring (verified)

Pin mapping verified from Meshtastic PR #11041 ("Add heltec_rcc6 board",
`variants/esp32c6/heltec_rcc6/variant.h`) and cross-checked against the RCC6
public header pinout.

| Signal | ESP32-C6 GPIO | Header label |
|---|---|---|
| TFT_SCL (SPI SCK) | GPIO4 | ADC1_CH4 / FSPIHD |
| TFT_SDA (SPI MOSI) | GPIO15 | USER GPIO |
| TFT_CS | GPIO18 | SDIO_CMD / FSPICS2 |
| TFT_DC (RS) | GPIO3 | ADC1_CH3 |
| TFT_RST | GPIO0 | ADC1_CH0 |
| TFT_EN (panel power) | GPIO2 | ADC1_CH2 / FSPIQ |
| TFT_BL (backlight) | GPIO1 | ADC1_CH1 |

Control polarity:

- `TFT_EN` is **active LOW** (drive LOW to power the panel).
- `TFT_BL` is **active HIGH** (drive HIGH for backlight on).

MISO is not connected (`GFX_NOT_DEFINED`).

> Do not assume these match the older **RC32** (ESP32-S3) RadioCore board —
> that board uses different pins (SCL=17, SDA=38, CS=39, DC=16, RST=4, EN=6,
> BL=5). The C6 mapping above is the only one that applies to the RCC6.

---

## 3. Getting started (Arduino IDE)

1. Install the ESP32 core (3.x):
   - *File → Preferences → Additional boards manager URLs* →
     `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   - *Boards Manager* → install **esp32**.
2. Open `radiocore-c6-tft-test.ino` (sketch folder must keep its name).
3. *Tools → Board → ESP32 Arduino →* **ESP32C6 Dev Module**.
4. Set **USB CDC On Boot: Enabled**, select the board's COM port.
5. Upload. If it won't enter download mode, hold **PRG/USER BOOT**, tap **RST**,
   release PRG.
6. Open Serial Monitor @ 115200. Expect:

   ```
   === RCC6 NV3001B TFT test ===
   RDDID=0x300101
   RDID=0x300101
   NV3001B confirmed
   TFT initialized
   fill: RED
   fill: GREEN
   ...
   ```

Expected display behaviour: full-panel color cycle **RED → GREEN → BLUE →
WHITE → BLACK** (1 s each), then **"TFT OK"** on black for 3 s, repeating.

---

## 4. How the sketch works

Power-up sequence in `setup()`:

1. `TFT_EN` → LOW (panel power on), `TFT_BL` → HIGH (backlight on).
2. `probeDisplayId()` — bit-banged read of the `RDDID`/`RDID1..3` registers.
   A value of `0x300101` confirms an NV3001B controller.
3. Hardware reset pulse on `TFT_RST`.
4. `SPI.begin(TFT_SCL, -1, TFT_SDA, TFT_CS)` at 4 MHz, mode 0.
5. `initPanel()` — full NV3001B init (SWRESET → register block → SLPOUT →
   COLMOD=0x05 → MADCTL=0x00 → DISPON → INVOFF).

Graphics primitives implemented from scratch (no external library):
`writeCommand`, `writeCommandData`, `setAddrWindow`, `fillRect`, and a 5×7
font (`drawText`).

The init register block matches the `Arduino_NV3001B` class from
`Quency-D/Arduino_GFX` @ commit `4d5afb0` (the library used by Meshtastic
PR #11041, tested on real RCC6 hardware).

### Useful NV3001B commands

| Command | Code | Purpose |
|---|---|---|
| SWRESET | `0x01` | Software reset |
| SLPOUT | `0x11` | Sleep out |
| INVOFF / INVON | `0x20` / `0x21` | Invert display |
| DISPON | `0x29` | Display on |
| CASET | `0x2A` | Column address set |
| RASET | `0x2B` | Row address set |
| RAMWR | `0x2C` | Write to GRAM |
| RDDID | `0x04` | Read display ID |
| MADCTL | `0x36` | Memory access control |
| COLMOD | `0x3A` | Color mode (use `0x05` = 16-bit RGB) |

---

## 5. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Screen stays black | Check `TFT_EN` LOW and `TFT_BL` HIGH; check display seated correctly (installs on the opposite side of the board). |
| Only a 128×128 square works, rest jumbled | Wrong rotation — panel is portrait; use MADCTL `0x00`, not `0xA0` (rotation 3). |
| Scanlines / garbage after large fills | Writing a window larger than 128×220 wraps the GRAM. Never set CASET/RASET beyond `x ≤ 127`, `y ≤ 219`. |
| Wrong/odd colors | Missing `INVOFF`/`INVON` step; on this panel send `INVOFF` (`0x20`) after DISPON. |
| RDDID reads `0xFFFFFF` / `0x000000` | Display not detected or not powered — check EN/BL and connector. |

---

## 6. Verification summary

- Coverage sweep confirmed **128×220** is the correct window (larger windows
  wrap and glitch).
- Rotation-0 portrait fills render cleanly across the whole panel; rotated
  (landscape) renders only a square.
- Controller identified as NV3001B via RDDID/RDID (`0x300101`).

---

## 7. References

- Meshtastic PR #11041 — "Add heltec_rcc6 board" (pin mapping, NV3001B support):
  https://github.com/meshtastic/firmware/pull/11041
- `Arduino_NV3001B` driver (init sequence, rotation/invert):
  `Quency-D/Arduino_GFX` @ `4d5afb0`, `src/display/Arduino_NV3001B.cpp` /
  `.h`
- MeshCore NV3001B display (alternative driver reference):
  `meshcore-dev/MeshCore`, `src/helpers/ui/NV3001BDisplay.cpp`
- Meshtastic RC32 (ESP32-S3 RadioCore) — note: **different pins**, not for C6:
  Meshtastic PR #10876
