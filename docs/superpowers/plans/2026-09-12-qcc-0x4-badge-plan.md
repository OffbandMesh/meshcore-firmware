# QCC 0x4 Badge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> **In this repo:** execute inline (superpowers:executing-plans). Subagents need the owner's explicit approval (SAFELANE §5, cost).

**Goal:** Bring the Queen City Con 0x4 badge up on Offband as a BLE companion that shows the QCC splash, blinks its LED, and reads its battery correctly (epic 1), and lay out the chain for keyboard messaging, the badge-to-client bridge, sound/lights/GPS, the CTF beacon and integration testing (epics 2–6).

**Architecture:** A new `variants/qcc_badge/` reuses the ProMicro radio wiring and `PromicroBoard` (by subclass, no ProMicro changes), remaps `Serial1` off the buzzer/LED pins, and adds a board-level event splash through one additive hook in the shared `offband::drawSplash`. Text on the badge uses the ThingPulse ArialMT fonts already in the tree, through a small driver-agnostic renderer. Every badge behavior is switched on by the badge envs' flags only.

**Tech Stack:** PlatformIO (`nordicnrf52`, Adafruit nRF52 core, S140 6.1.1), C++ (gnu++11 on device, C++17 in native tests), googletest (`[env:native]`), pytest (scripts), Adafruit SSD1306.

**Spec:** `docs/architecture/2026-09-12-qcc-0x4-badge-design.md` (approved 2026-09-12).
**Feature** #1172 · **Epic 1** #1173 · this plan: #1175.
**Agent:** MaroonDesert (session 81df237c)

---

## Scope of this plan

- **Epic 1 (bring-up)** is written out in full below: every file, test, command and expected output.
- **Epics 2–6** are outlined at task level so the whole chain can be filed when the owner agrees this plan. Each gets its own detailed plan when it starts, written against what epic 1 measures.

## Measured baseline (2026-09-12)

`pio run -e ProMicro_companion_radio_ble` on `firmware-base` `ce9ef8ef`: **Flash 418,832 / 712,704 B (58.8%)**, **RAM 163,400 / 235,520 B (69.4%)**. Flash headroom is ~290 KB; RAM is the tighter budget.

## Owner decisions needed at AGREE

| # | Decision | Proposed |
|---|---|---|
| D1 | Radio defaults seeded by the departure env | The US community preset your fleet runs: 910.525 MHz / 62.5 kHz / SF7 / CR 5. The ProMicro envs inherit the upstream default (869.618 MHz); this build is for a US event. |
| D2 | Low-voltage policy (one policy; SafeBoot runs first, before `board.begin()`) | SafeBoot sleep **3500 mV** / wake **3700 mV**, runtime `AUTO_SHUTDOWN_MILLIVOLTS` **3400 mV**. Sleeping above the runtime cutoff stops a pack that just tripped shutdown from reboot-looping. `PWRMGT_VOLTAGE_BOOTLOCK` is not compiled for this board. ProMicro today: 3400 / 3700 configured, but SafeBoot is compiled out there (D5), and no runtime cutoff. |
| D3 | CI matrix | Add `QCC_Badge_companion_radio_ble` to `.github/workflows/ci.yml` so the badge build is gated like the others (matrix changes are owner-approved). |
| D4 | Spec change | Keep the ProMicro sensor drivers the spec said to drop. Flash is at 58.8%, and SAO add-ons may carry sensors. |
| D5 | FYI (corrected 2026-09-12, Task 1.2) | On the stock ProMicro env, **SafeBoot is compiled out**: `PIN_VBAT_READ` and `ADC_MULTIPLIER` live in `PromicroBoard.h`, which `SafeBoot.cpp` never includes, so `SAFE_BOOT_ENABLED` is 0 and the env's thresholds do nothing (`nm`: `SafeBoot::checkAndMaybeSleep()` is the 12-byte stub). The ~12% multiplier mismatch first reported here is latent behind that. Tracked on #1176 under #206. The badge sets `SAFEBOOT_PIN_VBAT_READ` explicitly (Task 1.4). |

## Conventions for every task

- **Worktree:** `C:\Dev\.worktrees\meshcore-firmware-1173`, branch `epic/1173-qcc-badge-bringup`. Every command below runs there unless stated.
- **Commits:** `DW_PROJECT=Crosswire DW_TASK=<task's Citadel id> git commit ...`, subject `feat(#N): ...` where N is **this task's own issue number** from the filing table at the end (filled in when the chain is filed). End the message with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- **Device build:** `pio run -e <env>`. Read pio's per-env `SUCCESS`/`FAILED` table, never the exit code alone.
- **Native tests:** `export PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH" && pio test -e native -f <test_dir> -v`. Trust the gtest `[  PASSED  ] N tests` line; pio's "0 test cases" summary is cosmetic for custom `main()` suites.
- **Python tests:** `python -m pytest scripts/<test_file> -q`.
- **Gemini review, per task, before it closes** (2.5 only):
  `python "C:/Dev/meshcore-firmware/scripts/llm-consult.py" --backend gemini --model gemini-2.5-pro --files <the task's files> --prompt-file <scratch prompt.md> --topic qcc-<task> --dry-run`, then the same without `--dry-run`. Fix every finding or record why not on the task issue. On a non-zero exit, read the audit log in `C:\Dev\meshcore-firmware\docs\llm-consultations\` before anything else; never re-run blindly.
- **Close** each task on GitHub and in Citadel together, with the evidence (test output, build table, SHA) posted on its issue.

## File map (epic 1)

| File | Status | Responsibility |
|---|---|---|
| `variants/qcc_badge/variant.h` | new | Pin map header: ProMicro map, `Serial1` moved to the GPS pins, named badge pins |
| `variants/qcc_badge/variant.cpp` | new | `g_ADigitalPinMap` (identical to ProMicro) |
| `variants/qcc_badge/QccBattery.h` | new | Pure battery constants and math (host-testable) |
| `variants/qcc_badge/QccBadgeBoard.h/.cpp` | new | Board class: `PromicroBoard` subclass, battery read, identity |
| `variants/qcc_badge/target.h/.cpp` | new | Radio, display, button, sensors wiring |
| `variants/qcc_badge/platformio.ini` | new | `[QCC_Badge]` base, prod env, diag block and env |
| `variants/qcc_badge/art/qcc_eye_50x50.xbm` | new | Source art (standard XBM, LSB-first) |
| `variants/qcc_badge/QccSplashArt.h` | generated | MSB-first eye for `drawXbm` |
| `variants/qcc_badge/QccSplashLayout.h` | new | Splash A geometry and the corner-version helper declaration |
| `variants/qcc_badge/QccSplash.cpp` | new | `offband::drawEventSplash` for the badge |
| `src/helpers/ui/TpFont.h/.cpp` | new | ThingPulse-font metrics and renderer over `DisplayDriver::fillRect` |
| `src/helpers/ui/OLEDDisplayFonts.h` | modify | `PROGMEM` fallback so the font tables compile in native tests |
| `src/helpers/ui/OffbandSplash.h/.cpp` | modify | `drawEventSplash` declaration and the `OFFBAND_EVENT_SPLASH` hook |
| `src/SafeBoot.cpp` | modify | `SAFEBOOT_ADC_SAMPLE_US` hook (nRF52) |
| `examples/companion_radio/ui-new/UITask.h/.cpp` | modify | Diag-only self-test legend screen |
| `platformio.ini` (`[env:native]`) | modify | Native build list gains the font renderer, font tables, badge splash |
| `scripts/gen-qcc-splash.py` + `scripts/test_gen_qcc_splash.py` | new | Art generator and its tests |
| `scripts/test_qcc_variant_pins.py` | new | Pin-map and env-flag guard |
| `test/test_qcc_battery/`, `test/test_tp_font/`, `test/test_qcc_splash/` | new | Native unit tests |

---

## Epic 1 — bring-up

### Task 1.1: Pin map

**Files:**
- Create: `variants/qcc_badge/variant.h`
- Create: `variants/qcc_badge/variant.cpp`
- Test: `scripts/test_qcc_variant_pins.py`

- [ ] **Step 1: Write the failing test**

```python
"""Pin-map guard for variants/qcc_badge (#1172).

The badge drives its buzzer from P0.06 and its LED from P0.08 -- the ProMicro
variant's default Serial1 TX/RX. Opening Serial1 on those defaults would hold the
buzzer on. This test fails if a badge role, or Serial1, lands on the wrong GPIO.
Run: python -m pytest scripts/test_qcc_variant_pins.py -q
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QCC = ROOT / "variants" / "qcc_badge"


def pin_map(path):
    src = path.read_text()
    body = src[src.index("g_ADigitalPinMap"):]
    body = body[body.index("{") + 1: body.index("}")]
    return [int(t) for t in re.findall(r"\d+", body)]


def define(name):
    m = re.search(r"#define\s+%s\s+\((\d+)\)" % name, (QCC / "variant.h").read_text())
    assert m, f"{name} must be defined as (N) in variant.h"
    return int(m.group(1))


def gpio(name):
    return pin_map(QCC / "variant.cpp")[define(name)]


def test_pin_map_is_the_promicro_map():
    assert pin_map(QCC / "variant.cpp") == pin_map(ROOT / "variants" / "promicro" / "variant.cpp")


def test_badge_roles_land_on_their_schematic_gpios():
    assert gpio("PIN_QCC_MSG_LED") == 8         # P0.08 -> Q2 -> MSG_LED, connector Q2D
    assert gpio("PIN_QCC_BUZZER") == 6          # P0.06 -> Q3 -> MUTE -> BZ1
    assert gpio("PIN_QCC_GPS_POWER") == 24      # P0.24 -> Q1 -> GPS header ground
    assert gpio("PIN_QCC_SPARE_GPIO33") == 33   # P1.01 -> "GPIO33" pad


def test_serial1_stays_off_the_buzzer_and_led():
    assert gpio("PIN_SERIAL1_TX") == 20   # P0.20 -> GPS RX
    assert gpio("PIN_SERIAL1_RX") == 22   # P0.22 <- GPS TX
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `python -m pytest scripts/test_qcc_variant_pins.py -q`
Expected: FAIL (`FileNotFoundError` for `variants/qcc_badge/variant.cpp`).

- [ ] **Step 3: Create `variants/qcc_badge/variant.cpp`**

```cpp
#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

// Identical to variants/promicro: the badge is a ProMicro nRF52840 carrier.
const uint32_t g_ADigitalPinMap[] = {
  8, 6, 17, 20, 22, 24, 32, 11, 36, 38,
  9, 10, 43, 45, 47, 2, 29, 31,
  33, 34, 37,
  13, 15
};

void initVariant()
{
}
```

- [ ] **Step 4: Create `variants/qcc_badge/variant.h`**

```cpp
/*
 * variant.h -- Queen City Con 0x4 badge (#1172).
 * Derived from variants/promicro/variant.h, Copyright (C) 2023 Seeed K.K., MIT License.
 *
 * The badge is a ProMicro nRF52840 carrier: same pin map, with Serial1 moved to the
 * GPS pins because the ProMicro default (P0.06/P0.08) drives the badge's buzzer and
 * LED MOSFETs.
 */

#pragma once

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// Low frequency clock source

#define VARIANT_MCK          (64000000ul)

#define USE_LFRC    // 32.768 kHz RC oscillator

////////////////////////////////////////////////////////////////////////////////
// Power

#define PIN_EXT_VCC          (21)
#define EXT_VCC              (PIN_EXT_VCC)

#define BATTERY_PIN          (17)
#define ADC_RESOLUTION       12

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT           (23)
#define NUM_DIGITAL_PINS     (23)
#define NUM_ANALOG_INPUTS    (3)
#define NUM_ANALOG_OUTPUTS   (0)

////////////////////////////////////////////////////////////////////////////////
// Badge peripherals (indices into g_ADigitalPinMap)

#define PIN_QCC_MSG_LED      (0)    // P0.08 -> MOSFET Q2 -> MSG_LED and connector Q2D
#define PIN_QCC_BUZZER       (1)    // P0.06 -> MOSFET Q3 -> MUTE switch -> BZ1
#define PIN_QCC_GPS_POWER    (5)    // P0.24 -> MOSFET Q1 -> GPS header ground (HIGH = on)
#define PIN_QCC_SPARE_GPIO33 (18)   // P1.01 -> "GPIO33" pad

////////////////////////////////////////////////////////////////////////////////
// UART pin definition -- the GPS header, NOT the ProMicro default (see header)

#define PIN_SERIAL1_TX       (3)    // P0.20 -> GPS RX
#define PIN_SERIAL1_RX       (4)    // P0.22 <- GPS TX

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define WIRE_INTERFACES_COUNT 2

#define PIN_WIRE_SDA         (6)
#define PIN_WIRE_SCL         (7)
#define PIN_WIRE1_SDA        (13)
#define PIN_WIRE1_SCL        (14)

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_SCK          (2)
#define PIN_SPI_MISO         (3)
#define PIN_SPI_MOSI         (4)

#define PIN_SPI_NSS          (5)

#define PIN_SPI1_SCK         (18)
#define PIN_SPI1_MISO        (19)
#define PIN_SPI1_MOSI        (20)

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define PIN_LED              (22)
#define LED_PIN              PIN_LED
#define LED_BLUE             PIN_LED
#define LED_BUILTIN          PIN_LED
#define LED_STATE_ON         1

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1          (6)
#define BUTTON_PIN           PIN_BUTTON1
```

**As built** (`728b1d72`, review on #1177): the I2C and SPI defaults are *not* ProMicro's. `PromicroBoard::begin()` and the radio driver set their own pins, but a library that uses the defaults or `SS` would land on RXEN, the GPS pins, the GPS power switch or the user button. So `Wire` defaults to P1.04/P0.11, `SPI` to the radio bus, and SPI1/Wire1 are not defined. The guard test gained three checks for this (6 tests).

- [ ] **Step 5: Run the test and confirm it passes**

Run: `python -m pytest scripts/test_qcc_variant_pins.py -q`
Expected: `3 passed`.

- [ ] **Step 6: Gemini review** on the three files (see Conventions). Act on findings.

- [ ] **Step 7: Commit**

```bash
git add variants/qcc_badge/variant.h variants/qcc_badge/variant.cpp scripts/test_qcc_variant_pins.py
git commit -m "feat(#N): QCC badge pin map, Serial1 off the buzzer and LED pins"
```

---

### Task 1.2: Battery math and the SafeBoot sample-time hook

**Files:**
- Create: `variants/qcc_badge/QccBattery.h`
- Modify: `src/SafeBoot.cpp` (nRF52 read, after the `analogReference` block, ~line 383)
- Test: `test/test_qcc_battery/test_qcc_battery.cpp`

- [ ] **Step 1: Write the failing test** `test/test_qcc_battery/test_qcc_battery.cpp`

```cpp
#include <gtest/gtest.h>
#include "../../variants/qcc_badge/QccBattery.h"

TEST(QccBattery, DividerRatioMatchesTheSchematic) {
  // R4 680k over R5 1M (QCC 0x4 schematic), the same literal the env hands SafeBoot.
  EXPECT_FLOAT_EQ(1.68f, qcc::kDividerRatio);
}

TEST(QccBattery, MilliVoltsPerCountFoldsTheRatioAndTheReference) {
  // 1.68 * 3600 mV / 4096 counts (internal 0.6 V reference, gain 1/6, 12-bit).
  // 1.68f is not exact in binary, so compare with a tolerance, not FLOAT_EQ.
  EXPECT_NEAR(1.4765625f, qcc::kBattMvPerCount, 1e-6);
}

TEST(QccBattery, AFullCellReadsFourPointTwoVolts) {
  // 4200 mV / 1.68 = 2500 mV at the pin = 2844 counts.
  EXPECT_NEAR(4200, qcc::battMilliVolts(2844), 2);
}

TEST(QccBattery, TheSleepThresholdLandsWhereExpected) {
  // 3500 mV / 1.68 = 2083 mV at the pin = 2370 counts.
  EXPECT_NEAR(3500, qcc::battMilliVolts(2370), 2);
}

TEST(QccBattery, ZeroCountsIsZero) {
  EXPECT_EQ(0, qcc::battMilliVolts(0));
}

TEST(QccBattery, SampleTimeCoversTheDividerImpedance) {
  // 680k || 1M = ~405k source impedance; 40 us is the longest SAADC acquisition.
  EXPECT_EQ(40u, qcc::kAdcSampleUs);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `export PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH" && pio test -e native -f test_qcc_battery -v`
Expected: compile error, `QccBattery.h: No such file or directory`.

- [ ] **Step 3: Create `variants/qcc_badge/QccBattery.h`**

```cpp
#pragma once

#include <stdint.h>

// Battery sense on the QCC 0x4 badge (#1172): R4 680k over R5 1M from the switched
// battery into P0.31, read against the nRF52 internal 0.6 V reference at gain 1/6
// (3.6 V full scale), 12-bit. Both the board and SafeBoot read through this ratio;
// QccBadgeBoard.cpp static_asserts that the env's SAFEBOOT_ADC_MULTIPLIER matches it.
namespace qcc {

constexpr float kDividerTopOhms = 680000.0f;
constexpr float kDividerBottomOhms = 1000000.0f;
constexpr float kDividerRatio = (kDividerTopOhms + kDividerBottomOhms) / kDividerBottomOhms;
constexpr float kAdcFullScaleMv = 3600.0f;
constexpr int kAdcBits = 12;
constexpr float kBattMvPerCount = kDividerRatio * kAdcFullScaleMv / (float)(1 << kAdcBits);

// 680k || 1M is ~405k of source impedance; the core's 3 us default under-reads it.
constexpr unsigned kAdcSampleUs = 40;

constexpr uint16_t battMilliVolts(uint32_t raw_avg) {
  return (uint16_t)(kBattMvPerCount * (float)raw_avg + 0.5f);
}

}  // namespace qcc
```

- [ ] **Step 4: Run the test and confirm it passes**

Run: `pio test -e native -f test_qcc_battery -v`
Expected: `[  PASSED  ] 6 tests`.

- [ ] **Step 5: Add the SafeBoot sample-time hook** in `src/SafeBoot.cpp`

Replace:

```cpp
    analogReference(AR_INTERNAL); // BSP default; SafeBoot's math uses SAFEBOOT_AREF_VOLTAGE
#endif
    analogReadResolution(kBatteryResolutionBits);
```

with:

```cpp
    analogReference(AR_INTERNAL); // BSP default; SafeBoot's math uses SAFEBOOT_AREF_VOLTAGE
#endif
#ifdef SAFEBOOT_ADC_SAMPLE_US
    // A high-impedance divider needs a longer SAADC acquisition than the core's 3 us
    // default, or it under-reads. Set per env; the value must be one the core accepts
    // (3, 5, 10, 15, 20 or 40).
    analogSampleTime(SAFEBOOT_ADC_SAMPLE_US);
#endif
    analogReadResolution(kBatteryResolutionBits);
```

- [ ] **Step 6: Confirm other boards are untouched**

Run: `pio run -e ProMicro_companion_radio_ble`
Expected: `SUCCESS`, flash 418,832 B as in the baseline (the hook compiles out without the flag).

- [ ] **Step 7: Gemini review**, then **commit**

```bash
git add variants/qcc_badge/QccBattery.h src/SafeBoot.cpp test/test_qcc_battery/test_qcc_battery.cpp
git commit -m "feat(#N): QCC battery math and a SafeBoot ADC sample-time hook"
```

**As built** (`e9c3dc80`, review on #1178):
- The tests assert exact values (`battMilliVolts(2844)` is 4199, `(2370)` is 3499) and add rounding and full-scale cases, 8 in all. `QccBattery.h` static_asserts that the whole 12-bit range fits in `uint16_t`.
- The hook static_asserts the values the core accepts, and restores the core-default sample time after SafeBoot's read.
- Step 6 found that SafeBoot is compiled out on ProMicro (D5). The hook was therefore proven with SafeBoot forced on (`-D SAFEBOOT_PIN_VBAT_READ=17`), and Task 1.4 sets that flag for the badge.

---

### Task 1.3: ThingPulse font renderer

**Files:**
- Create: `src/helpers/ui/TpFont.h`, `src/helpers/ui/TpFont.cpp`
- Modify: `src/helpers/ui/OLEDDisplayFonts.h` (`PROGMEM` fallback)
- Modify: `platformio.ini` (`[env:native]` `build_src_filter`)
- Test: `test/test_tp_font/test_tp_font.cpp`

Golden values below were computed from the `ArialMT_Plain_*` tables in `OLEDDisplayFonts.cpp` on 2026-09-12.

- [ ] **Step 1: Write the failing test** `test/test_tp_font/test_tp_font.cpp`

```cpp
#include <gtest/gtest.h>
#include <set>
#include <utility>
#include <string.h>

#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/OLEDDisplayFonts.h>
#include <helpers/ui/TpFont.h>

using offband::tpfont::drawText;
using offband::tpfont::height;
using offband::tpfont::textWidth;
using offband::tpfont::charAdvance;

class PixelDisplay : public DisplayDriver {
public:
  std::set<std::pair<int, int>> px;
  PixelDisplay() : DisplayDriver(128, 64) {}
  bool isOn() override { return true; }
  void turnOn() override {}
  void turnOff() override {}
  void clear() override {}
  void startFrame(ColorVal) override {}
  void setTextSize(int) override {}
  void setColor(ColorVal) override {}
  void setCursor(int, int) override {}
  void print(const char*) override {}
  void fillRect(int x, int y, int w, int h) override {
    for (int i = 0; i < w; i++)
      for (int j = 0; j < h; j++) px.insert({x + i, y + j});
  }
  void drawRect(int, int, int, int) override {}
  void drawXbm(int, int, const uint8_t*, int, int) override {}
  uint16_t getTextWidth(const char* s) override { return (uint16_t)(strlen(s) * 6); }
  void endFrame() override {}
  int minX() const { int m = 999; for (auto& p : px) m = p.first < m ? p.first : m; return m; }
  int maxX() const { int m = -1; for (auto& p : px) m = p.first > m ? p.first : m; return m; }
  int minY() const { int m = 999; for (auto& p : px) m = p.second < m ? p.second : m; return m; }
  int maxY() const { int m = -1; for (auto& p : px) m = p.second > m ? p.second : m; return m; }
};

TEST(TpFont, HeightsComeFromTheFontHeader) {
  EXPECT_EQ(13, height(ArialMT_Plain_10));
  EXPECT_EQ(19, height(ArialMT_Plain_16));
}

TEST(TpFont, WidthsMatchTheJumpTable) {
  EXPECT_EQ(106, textWidth(ArialMT_Plain_16, "QueenCityCon"));
  EXPECT_EQ(114, textWidth(ArialMT_Plain_16, "Queen City Con"));
  EXPECT_EQ(38, textWidth(ArialMT_Plain_10, "Offband"));
  EXPECT_EQ(29, textWidth(ArialMT_Plain_10, "v1.5.0"));
  EXPECT_EQ(27, textWidth(ArialMT_Plain_10, "beta6"));
}

TEST(TpFont, SpaceAdvancesWithoutInk) {
  EXPECT_EQ(3, charAdvance(ArialMT_Plain_10, ' '));
  PixelDisplay d;
  EXPECT_EQ(3, drawText(d, 0, 0, ArialMT_Plain_10, " "));
  EXPECT_TRUE(d.px.empty());
}

TEST(TpFont, CharsOutsideTheFontDrawAsQuestionMark) {
  EXPECT_EQ(charAdvance(ArialMT_Plain_10, '?'), charAdvance(ArialMT_Plain_10, '\x01'));
}

TEST(TpFont, NullAndEmptyStringsAreHarmless) {
  PixelDisplay d;
  EXPECT_EQ(0, textWidth(ArialMT_Plain_10, nullptr));
  EXPECT_EQ(7, drawText(d, 7, 0, ArialMT_Plain_10, ""));
  EXPECT_TRUE(d.px.empty());
}

TEST(TpFont, QueenCityConRendersExactly) {
  PixelDisplay d;
  EXPECT_EQ(106, drawText(d, 0, 0, ArialMT_Plain_16, "QueenCityCon"));
  EXPECT_EQ(260u, d.px.size());
  EXPECT_EQ(1, d.minX());
  EXPECT_EQ(103, d.maxX());
  EXPECT_EQ(3, d.minY());
  EXPECT_EQ(17, d.maxY());
}

TEST(TpFont, OffbandRendersExactlyAtAnOffset) {
  PixelDisplay d;
  drawText(d, 10, 20, ArialMT_Plain_10, "Offband");
  EXPECT_EQ(88u, d.px.size());
  EXPECT_EQ(11, d.minX());
  EXPECT_EQ(46, d.maxX());
  EXPECT_EQ(23, d.minY());
  EXPECT_EQ(29, d.maxY());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Add the sources to the native build** in `platformio.ini`, `[env:native]` `build_src_filter`, after the `NativeUIColorDefs.cpp` line:

```ini
  +<../src/helpers/ui/TpFont.cpp>             ; #1172: ThingPulse-font renderer
  +<../src/helpers/ui/OLEDDisplayFonts.cpp>   ; #1172: the ArialMT tables it renders
```

- [ ] **Step 3: Run it and confirm it fails**

Run: `pio test -e native -f test_tp_font -v`
Expected: build error on `TpFont.h: No such file or directory` (and on `PROGMEM` in `OLEDDisplayFonts.h`).

- [ ] **Step 4: Give `OLEDDisplayFonts.h` a `PROGMEM` fallback**

Replace:

```cpp
#ifdef ARDUINO
#include <Arduino.h>
#elif __MBED__
#define PROGMEM
#endif
```

with:

```cpp
#ifdef ARDUINO
#include <Arduino.h>
#elif __MBED__
#define PROGMEM
#endif

#ifndef PROGMEM
#define PROGMEM   // host builds (native tests): plain const data
#endif
```

- [ ] **Step 5: Create `src/helpers/ui/TpFont.h`**

```cpp
#pragma once

#include <stdint.h>
#include "DisplayDriver.h"

namespace offband {
namespace tpfont {

// Fonts in the ThingPulse OLEDDisplay format -- the ArialMT_Plain_* tables in
// OLEDDisplayFonts.cpp. Byte 0 is the max width, byte 1 the height, byte 2 the first
// char, byte 3 the char count. Then one 4-byte jump entry per char (glyph offset MSB,
// offset LSB, glyph byte count, advance), then the glyph bytes: column by column,
// (height + 7) / 8 bytes per column, bit 0 at the top. Offset 0xFFFF marks a char
// with no ink (space). Reads the tables directly: on nRF52 and ESP32 flash is
// memory-mapped, so no pgm_read_byte is needed.

uint8_t height(const uint8_t* font);

// Advance of c in pixels. A char outside the font is drawn, and measured, as '?'.
int charAdvance(const uint8_t* font, char c);

int textWidth(const uint8_t* font, const char* s);

// Draws s with its top-left at (x, y) in the display's current color, one fillRect
// per vertical run of ink, so it works on any DisplayDriver. Returns the x just past
// the last glyph.
int drawText(DisplayDriver& display, int x, int y, const uint8_t* font, const char* s);

}  // namespace tpfont
}  // namespace offband
```

- [ ] **Step 6: Create `src/helpers/ui/TpFont.cpp`**

```cpp
#include "TpFont.h"

namespace offband {
namespace tpfont {

static const int kHeaderBytes = 4;
static const int kJumpBytes = 4;
static const int kNoInk = 0xFFFF;

static int glyphIndex(const uint8_t* font, char c) {
  const int first = font[2];
  const int count = font[3];
  int i = (int)(uint8_t)c - first;
  if (i < 0 || i >= count) i = '?' - first;
  return (i >= 0 && i < count) ? i : -1;
}

uint8_t height(const uint8_t* font) {
  return font[1];
}

int charAdvance(const uint8_t* font, char c) {
  const int i = glyphIndex(font, c);
  return i < 0 ? 0 : font[kHeaderBytes + i * kJumpBytes + 3];
}

int textWidth(const uint8_t* font, const char* s) {
  int w = 0;
  for (; s && *s; s++) w += charAdvance(font, *s);
  return w;
}

int drawText(DisplayDriver& display, int x, int y, const uint8_t* font, const char* s) {
  const int count = font[3];
  const int raster = (font[1] + 7) / 8;
  const uint8_t* data = font + kHeaderBytes + count * kJumpBytes;

  for (; s && *s; s++) {
    const int i = glyphIndex(font, *s);
    if (i < 0) continue;
    const uint8_t* jump = font + kHeaderBytes + i * kJumpBytes;
    const int offset = (jump[0] << 8) | jump[1];
    const int bytes = jump[2];

    if (offset != kNoInk) {
      const uint8_t* glyph = data + offset;
      const int cols = (bytes + raster - 1) / raster;
      for (int col = 0; col < cols; col++) {
        int run_start = -1;
        for (int row = 0; row <= raster * 8; row++) {
          const int k = col * raster + row / 8;
          const bool on = row < raster * 8 && k < bytes && ((glyph[k] >> (row % 8)) & 1);
          if (on && run_start < 0) run_start = row;
          if (!on && run_start >= 0) {
            display.fillRect(x + col, y + run_start, 1, row - run_start);
            run_start = -1;
          }
        }
      }
    }
    x += jump[3];
  }
  return x;
}

}  // namespace tpfont
}  // namespace offband
```

- [ ] **Step 7: Run the test and confirm it passes**

Run: `pio test -e native -f test_tp_font -v`
Expected: `[  PASSED  ] 7 tests`. Then run the whole native suite, `pio test -e native -v`, and confirm every existing suite still passes.

- [ ] **Step 8: Gemini review**, then **commit**

```bash
git add src/helpers/ui/TpFont.h src/helpers/ui/TpFont.cpp src/helpers/ui/OLEDDisplayFonts.h platformio.ini test/test_tp_font/test_tp_font.cpp
git commit -m "feat(#N): ThingPulse-font renderer for any DisplayDriver"
```

**As built** (review on #1179):
- `OLEDDisplayFonts.h` also needed `#include <stdint.h>`. The tables use `uint8_t`, which the header reached only through `<Arduino.h>`, and the red run failed on it first.
- The renderer clips each column to the font's height, not the byte padding below it (Gemini). `InkBelowTheCellIsNotDrawn` pins that.
- Result: 8 tests; the full native suite passes 301/301.

---

### Task 1.4: Board class, target and the badge env

**Files:**
- Create: `variants/qcc_badge/QccBadgeBoard.h`, `variants/qcc_badge/QccBadgeBoard.cpp`
- Create: `variants/qcc_badge/target.h`, `variants/qcc_badge/target.cpp`
- Create: `variants/qcc_badge/platformio.ini`
- Modify: `scripts/test_qcc_variant_pins.py` (env-flag checks)

- [ ] **Step 1: Extend the guard test** — append to `scripts/test_qcc_variant_pins.py`:

```python
def ini_flag(name):
    m = re.search(r"-D\s+%s=(\d+)" % name, (QCC / "platformio.ini").read_text())
    assert m, f"-D {name}=N missing from variants/qcc_badge/platformio.ini"
    return int(m.group(1))


def test_env_pin_flags_name_the_badge_pins():
    # Shared code reads these as bare numbers; they must equal the variant's names.
    assert ini_flag("PIN_STATUS_LED") == define("PIN_QCC_MSG_LED")
    assert ini_flag("PIN_BUZZER") == define("PIN_QCC_BUZZER")
    assert ini_flag("PIN_GPS_EN") == define("PIN_QCC_GPS_POWER")


def test_safeboot_and_board_share_one_divider_ratio():
    m = re.search(r"-D\s+SAFEBOOT_ADC_MULTIPLIER=([\d.]+)f", (QCC / "platformio.ini").read_text())
    assert m and float(m.group(1)) == 1.68


def test_safeboot_reads_the_battery_pin():
    # SafeBoot.cpp can't see PromicroBoard.h's PIN_VBAT_READ. Without this flag SafeBoot
    # compiles out and the D2 boot gate does nothing (#1176).
    assert ini_flag("SAFEBOOT_PIN_VBAT_READ") == define("BATTERY_PIN")
```

- [ ] **Step 2: Run it and confirm the new tests fail**

Run: `python -m pytest scripts/test_qcc_variant_pins.py -q`
Expected: 3 failed (`platformio.ini` missing), 6 passed.

- [ ] **Step 3: Create `variants/qcc_badge/QccBadgeBoard.h`**

```cpp
#pragma once

#include "../promicro/PromicroBoard.h"
#include "QccBattery.h"

// QCC 0x4 badge (#1172). Radio wiring, button and I2C setup are PromicroBoard's; the
// badge differs in its battery divider and its identity.
class QccBadgeBoard : public PromicroBoard {
public:
  // NRF52Board is a virtual base of NRF52BoardDCDC, so the most-derived class names
  // the OTA identity; PromicroBoard's initializer for it does not run.
  QccBadgeBoard() : NRF52Board("QCC_Badge_OTA") { adc_mult = qcc::kBattMvPerCount; }

  uint16_t getBattMilliVolts() override;
  bool setAdcMultiplier(float multiplier) override;
  float getAdcMultiplier() const override;
  const char* getManufacturerName() const override { return "QCC 0x4 Badge"; }
};
```

- [ ] **Step 4: Create `variants/qcc_badge/QccBadgeBoard.cpp`**

```cpp
#include <Arduino.h>
#include "QccBadgeBoard.h"

// The badge's low-voltage policy (D2) starts with SafeBoot, which can't see this
// board's headers: without these flags it compiles out and the boot gate does nothing.
#if !defined(SAFEBOOT_PIN_VBAT_READ) || !defined(SAFEBOOT_ADC_MULTIPLIER) || !defined(SAFEBOOT_ADC_SAMPLE_US)
#error "QCC badge: set SAFEBOOT_PIN_VBAT_READ, SAFEBOOT_ADC_MULTIPLIER and SAFEBOOT_ADC_SAMPLE_US"
#endif
// SafeBoot reads the same pin before the board is up. One pin, one divider ratio and one
// acquisition time, or the boot gate and the runtime reading disagree.
static_assert(SAFEBOOT_PIN_VBAT_READ == PIN_VBAT_READ, "SafeBoot must read the board's battery pin");
static_assert(SAFEBOOT_ADC_MULTIPLIER - qcc::kDividerRatio < 0.0005f &&
              qcc::kDividerRatio - SAFEBOOT_ADC_MULTIPLIER < 0.0005f,
              "SAFEBOOT_ADC_MULTIPLIER must equal qcc::kDividerRatio");
static_assert(SAFEBOOT_ADC_SAMPLE_US == qcc::kAdcSampleUs, "SafeBoot must sample like the board");

uint16_t QccBadgeBoard::getBattMilliVolts() {
  analogReference(AR_INTERNAL);
  analogSampleTime(qcc::kAdcSampleUs);
  analogReadResolution(qcc::kAdcBits);

  uint32_t raw = 0;
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    raw += analogRead(PIN_VBAT_READ);
  }
  raw /= BATTERY_SAMPLES;
  return (uint16_t)(adc_mult * (float)raw + 0.5f);
}

// PromicroBoard resets to its own 1.815; the badge resets to its divider.
bool QccBadgeBoard::setAdcMultiplier(float multiplier) {
  adc_mult = (multiplier == 0.0f) ? qcc::kBattMvPerCount : multiplier;
  return true;
}

float QccBadgeBoard::getAdcMultiplier() const {
  return (adc_mult == 0.0f) ? qcc::kBattMvPerCount : adc_mult;
}
```

- [ ] **Step 5: Create `variants/qcc_badge/target.h`**

```cpp
#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include "QccBadgeBoard.h"
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/SSD1306Display.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

#include <helpers/sensors/EnvironmentSensorManager.h>

extern QccBadgeBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
```

- [ ] **Step 6: Create `variants/qcc_badge/target.cpp`**

```cpp
#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

QccBadgeBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
```

- [ ] **Step 7: Create `variants/qcc_badge/platformio.ini`** (radio and low-voltage values per D1/D2 at AGREE)

```ini
; Queen City Con 0x4 badge (#1172): ProMicro nRF52840 + Ai-Thinker Ra-01SH (SX1262),
; 128x64 OLED, CardKB-compatible keyboard, buzzer, LED line, optional GPS.
; Radio wiring is variants/promicro's; this variant reuses PromicroBoard by subclass.
; Design: docs/architecture/2026-09-12-qcc-0x4-badge-design.md
[QCC_Badge]
extends = nrf52_base
board = promicro_nrf52840
board_build.ldscript = boards/nrf52840_s140_v6_extrafs.ld
board_upload.maximum_size = 712704
build_flags = ${nrf52_base.build_flags}
  -I variants/qcc_badge
  -D QCC_BADGE
  -D USE_SX1262
  -D RADIO_CLASS=CustomSX1262
  -D WRAPPER_CLASS=CustomSX1262Wrapper
  -D LORA_TX_POWER=22
  -D SX126X_CURRENT_LIMIT=140
  -D SX126X_RX_BOOSTED_GAIN=1
  -D PIN_BOARD_SCL=7
  -D PIN_BOARD_SDA=8
  -D PIN_OLED_RESET=-1
  -D PIN_USER_BTN=6
  -D PIN_GPS_RX=3
  -D PIN_GPS_TX=4
  -D PIN_GPS_EN=5          ; PIN_QCC_GPS_POWER, P0.24 -> Q1 (HIGH = GPS powered)
  -D PIN_STATUS_LED=0      ; PIN_QCC_MSG_LED, P0.08 -> Q2 -> MSG_LED (heartbeat)
  -D PIN_BUZZER=1          ; PIN_QCC_BUZZER, P0.06 -> Q3 -> MUTE switch -> BZ1
  -D ENV_INCLUDE_GPS=1
  -D ENV_INCLUDE_AHTX0=1
  -D ENV_INCLUDE_BME280=1
  -D ENV_INCLUDE_BMP280=1
  -D ENV_INCLUDE_INA3221=1
  -D ENV_INCLUDE_INA219=1
  ; --- one low-voltage policy (D2): SafeBoot runs first, then the runtime cutoff ---
  -D SAFEBOOT_PIN_VBAT_READ=17         ; P0.31; SafeBoot can't see PromicroBoard.h (#1176)
  -D SAFEBOOT_ADC_MULTIPLIER=1.68f     ; divider RATIO; must equal qcc::kDividerRatio
  -D SAFEBOOT_ADC_SAMPLE_US=40         ; ~405k source impedance
  -D DEFAULT_SAFE_BOOT_WAKE_MV=3700
  -D DEFAULT_SAFE_BOOT_SLEEP_MV=3500
  -D DEFAULT_SAFE_BOOT_RECHECK_SECS=120
  -D DEFAULT_SAFE_BOOT_MAX_RECHECK_SECS=600
  -D AUTO_SHUTDOWN_MILLIVOLTS=3400
build_src_filter = ${nrf52_base.build_src_filter}
  +<helpers/sensors>
  +<helpers/ui/TpFont.cpp>
  +<helpers/ui/OLEDDisplayFonts.cpp>
  +<../variants/qcc_badge>
  +<../variants/promicro/PromicroBoard.cpp>
lib_deps = ${nrf52_base.lib_deps}
  adafruit/Adafruit SSD1306 @ ^2.5.13
  adafruit/Adafruit INA3221 Library @ ^1.0.1
  adafruit/Adafruit INA219 @ ^1.2.3
  adafruit/Adafruit AHTX0 @ ^2.0.5
  adafruit/Adafruit BME280 Library @ ^2.3.0
  adafruit/Adafruit BMP280 Library@^2.6.8
  stevemarple/MicroNMEA @ ^2.0.6

; Departure flavor: normal radio defaults (D1). The con flavor, with the con's own radio
; config and the first-boot move back to these defaults, lands in epic 6.
[env:QCC_Badge_companion_radio_ble]
extends = QCC_Badge
build_flags = ${QCC_Badge.build_flags}
  -I examples/companion_radio/ui-new
  -D MAX_CONTACTS=350
  -D MAX_GROUP_CHANNELS=40
  -D BLE_PIN_CODE=123456    ; with a display, MyMesh turns this into a random PIN per session
  -D BLE_DEBUG_LOGGING=1
  -D OFFLINE_QUEUE_SIZE=256
  -D DISPLAY_CLASS=SSD1306Display
  -D LORA_FREQ=910.525
  -D LORA_BW=62.5
  -D LORA_SF=7
build_src_filter = ${QCC_Badge.build_src_filter}
  +<helpers/nrf52/SerialBLEInterface.cpp>
  +<helpers/ui/SSD1306Display.cpp>
  +<helpers/ui/MomentaryButton.cpp>
  +<../examples/companion_radio/*.cpp>
  +<../examples/companion_radio/ui-new/*.cpp>
lib_deps = ${QCC_Badge.lib_deps}
  adafruit/RTClib @ ^2.1.3
  densaugeo/base64 @ ~1.4.0
```

`OFFBAND_EVENT_SPLASH` is deliberately not set yet: this env shows the stock Offband splash until Task 1.6 adds the badge's.

- [ ] **Step 8: Run the guard test and confirm it passes**

Run: `python -m pytest scripts/test_qcc_variant_pins.py -q`
Expected: `9 passed`.

- [ ] **Step 9: Build the badge env** (first link of the variant)

Run: `pio run -e QCC_Badge_companion_radio_ble`
Expected: `SUCCESS`. Record flash and RAM on the task issue next to the 418,832 / 163,400 B baseline.

Then confirm SafeBoot is in the image, not the stub:
`~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm -C -S .pio/build/QCC_Badge_companion_radio_ble/firmware.elf | grep -E "checkAndMaybeSleep|analogSampleTime"`
Expected: `SafeBoot::checkAndMaybeSleep()` in the hundreds of bytes (ProMicro with SafeBoot forced on: 684), not the 12-byte disabled stub, and `analogSampleTime` present.

- [ ] **Step 10: Gemini review**, then **commit**

```bash
git add variants/qcc_badge/QccBadgeBoard.h variants/qcc_badge/QccBadgeBoard.cpp variants/qcc_badge/target.h variants/qcc_badge/target.cpp variants/qcc_badge/platformio.ini scripts/test_qcc_variant_pins.py
git commit -m "feat(#N): QCC badge board class, target and BLE companion env"
```

**As built** (review on #1180):
- `PIN_BUZZER` pulls in the existing buzzer code. The env therefore also needs `+<helpers/ui/buzzer.cpp>` and `end2endzone/NonBlockingRTTTL@^1.3.0`, as the other buzzer boards have; the first build failed without them.
- The reading goes through `qcc::scaleToMilliVolts()` in `QccBattery.h`, which is host-tested. `adc_mult` is user-settable, so a NaN, infinite or non-positive result reads 0 and an oversized one saturates at 65535.
- Device builds use `-Ofast`, so NaN is detected from the float's bits. The disassembly confirms the `0x7f800000` test survives.
- The SafeBoot pin test compares the flag with `PromicroBoard.h`'s `PIN_VBAT_READ` and pins P0.31.
- Build: Flash 423,144 B, RAM 163,256 B. SafeBoot is compiled in (684 bytes), and only `variants/qcc_badge` supplies `variant.h`/`variant.cpp`.

---

### Task 1.5: Splash art

**Files:**
- Create: `variants/qcc_badge/art/qcc_eye_50x50.xbm`
- Create: `scripts/gen-qcc-splash.py`
- Create (generated): `variants/qcc_badge/QccSplashArt.h`
- Test: `scripts/test_gen_qcc_splash.py`

The source art is the badge team's 50x50 XBM (from their Meshtastic build), LSB-first. Our OLED drivers' `drawXbm` is Adafruit `drawBitmap`, MSB-first (`SSD1306Display.cpp:108-110`, and `OffbandLogo.h` says the same), so the generator reverses the bits of every byte. Drawn unconverted, every 8-pixel group would render mirrored.

- [ ] **Step 1: Create `variants/qcc_badge/art/qcc_eye_50x50.xbm`** (bytes copied unchanged from the badge team's `Queen_City_Con_Meshtastic_Logo_Code.h`)

```c
#define qcc_eye_width 50
#define qcc_eye_height 50
static unsigned char qcc_eye_bits[] = {
  0x00, 0x00, 0xE0, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0x01,
  0x00, 0x00, 0x00, 0xF0, 0xFF, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0xFC, 0xFF,
  0xFF, 0x7F, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x01, 0x00, 0xC0,
  0x0F, 0x00, 0x00, 0xF0, 0x07, 0x00, 0xF0, 0x01, 0x00, 0x00, 0x80, 0x0F,
  0x00, 0x78, 0x00, 0xFC, 0x3F, 0x00, 0x3C, 0x00, 0x0C, 0x80, 0xFF, 0xFF,
  0x03, 0x70, 0x00, 0x00, 0xF0, 0xF3, 0x8F, 0x0F, 0x00, 0x00, 0x00, 0xFE,
  0xF3, 0x9F, 0x7F, 0x00, 0x00, 0x80, 0xFF, 0xF3, 0x9F, 0xFF, 0x01, 0x00,
  0x80, 0xFF, 0xF3, 0x9F, 0xFF, 0x03, 0x00, 0x00, 0xFE, 0xE3, 0x8F, 0xFF,
  0x01, 0x00, 0x00, 0xF8, 0xC7, 0xC7, 0x7F, 0x00, 0x00, 0x00, 0xF0, 0x0F,
  0xE0, 0x1F, 0x00, 0x00, 0x00, 0xF0, 0x3F, 0xF8, 0x03, 0x00, 0x00, 0x00,
  0xF8, 0xFD, 0x7F, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x81, 0x03, 0x00, 0x00,
  0x00, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x80, 0xFF, 0xC1, 0x03, 0x00, 0x18, 0x00, 0x00, 0xF0,
  0xC1, 0x07, 0xC0, 0x1F, 0x00, 0x00, 0xE0, 0x81, 0x0F, 0xE0, 0x1F, 0x00,
  0x00, 0xE0, 0x01, 0x3F, 0xC0, 0x1F, 0x00, 0x00, 0xE0, 0x01, 0x7E, 0xC0,
  0x1F, 0x00, 0x00, 0xE0, 0x01, 0xFC, 0xF3, 0x0F, 0x00, 0x00, 0xE0, 0x01,
  0xF8, 0xFF, 0x0F, 0x00, 0x00, 0xC0, 0x01, 0xF0, 0xFF, 0x0F, 0x00, 0x00,
  0x80, 0x01, 0xC0, 0xFF, 0x0F, 0x00, 0x00, 0x80, 0x01, 0x80, 0xFF, 0x0F,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
  0x00, 0x80, 0x07, 0x00, 0x18, 0x00, 0x00, 0x00, 0x40, 0x0C, 0x00, 0x14,
  0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x12, 0x00, 0x00, 0x00, 0x10, 0xD0,
  0x10, 0x11, 0x00, 0x00, 0x00, 0x00, 0x10, 0xC9, 0xFB, 0x00, 0x00, 0x00,
  0x00, 0x10, 0x07, 0x10, 0x00, 0x00, 0x00, 0x10, 0x10, 0x06, 0x10, 0x00,
  0x00, 0x00, 0x30, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x60, 0x88, 0x18,
  0x10, 0x00, 0x00, 0x00, 0xC0, 0x43, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00,
};
```

- [ ] **Step 2: Write the failing test** `scripts/test_gen_qcc_splash.py`

```python
"""Tests for scripts/gen-qcc-splash.py (#1172).
Run: python -m pytest scripts/test_gen_qcc_splash.py -q
"""
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "variants" / "qcc_badge" / "art" / "qcc_eye_50x50.xbm"
HDR = ROOT / "variants" / "qcc_badge" / "QccSplashArt.h"

_spec = importlib.util.spec_from_file_location("gen_qcc_splash", ROOT / "scripts" / "gen-qcc-splash.py")
gen = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gen)


def pixels_lsb(w, h, data):
    rb = (w + 7) // 8
    return {(c, r) for r in range(h) for c in range(w) if data[r * rb + c // 8] & (1 << (c % 8))}


def pixels_msb(w, h, data):
    rb = (w + 7) // 8
    return {(c, r) for r in range(h) for c in range(w) if data[r * rb + c // 8] & (0x80 >> (c % 8))}


def test_reverse_bits():
    assert gen.reverse_bits(0x01) == 0x80
    assert gen.reverse_bits(0xE0) == 0x07
    assert gen.reverse_bits(0x0F) == 0xF0
    assert gen.reverse_bits(0x00) == 0x00


def test_source_is_the_approved_art():
    w, h, data = gen.parse_xbm(SRC.read_text())
    assert (w, h, len(data)) == (50, 50, 350)
    assert len(pixels_lsb(w, h, data)) == 649   # ink count of the art approved 2026-09-12


def test_conversion_keeps_every_pixel_in_place():
    w, h, data = gen.parse_xbm(SRC.read_text())
    assert pixels_msb(w, h, gen.to_msb_first(data)) == pixels_lsb(w, h, data)


def test_a_short_source_is_rejected():
    with pytest.raises(ValueError):
        gen.parse_xbm("#define a_width 50\n#define a_height 50\nstatic unsigned char a_bits[] = { 0x00 };")


def test_committed_header_matches_the_generator():
    w, h, data = gen.parse_xbm(SRC.read_text())
    assert HDR.read_text() == gen.render_header(w, h, gen.to_msb_first(data))
```

- [ ] **Step 3: Run it and confirm it fails**

Run: `python -m pytest scripts/test_gen_qcc_splash.py -q`
Expected: FAIL (`gen-qcc-splash.py` not found).

- [ ] **Step 4: Create `scripts/gen-qcc-splash.py`**

```python
#!/usr/bin/env python3
"""Generate variants/qcc_badge/QccSplashArt.h from the QCC 0x4 eye (#1172).

Source: variants/qcc_badge/art/qcc_eye_50x50.xbm, a standard XBM (LSB-first rows).
Output: the same pixels MSB-first, the order DisplayDriver::drawXbm expects on the
OLED drivers (Adafruit_GFX drawBitmap). Do not hand-edit the header; regenerate.

Usage:
  python scripts/gen-qcc-splash.py            # print the header to stdout
  python scripts/gen-qcc-splash.py --src PATH
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SRC = ROOT / "variants" / "qcc_badge" / "art" / "qcc_eye_50x50.xbm"


def parse_xbm(text):
    w = int(re.search(r"_width\s+(\d+)", text).group(1))
    h = int(re.search(r"_height\s+(\d+)", text).group(1))
    body = text[text.index("{") + 1: text.index("}")]
    data = [int(t, 16) for t in re.findall(r"0x[0-9A-Fa-f]{1,2}", body)]
    row_bytes = (w + 7) // 8
    if len(data) != row_bytes * h:
        raise ValueError(f"expected {row_bytes * h} bytes for {w}x{h}, got {len(data)}")
    return w, h, data


def reverse_bits(b):
    return int(f"{b:08b}"[::-1], 2)


def to_msb_first(data):
    return [reverse_bits(b) for b in data]


def render_header(w, h, data):
    row_bytes = (w + 7) // 8
    out = [
        "// QccSplashArt.h -- the QCC 0x4 eye for the badge splash (#1172).",
        "// GENERATED by scripts/gen-qcc-splash.py from variants/qcc_badge/art/qcc_eye_50x50.xbm.",
        "// 1-bit, MSB-first (Adafruit_GFX drawBitmap / DisplayDriver::drawXbm). Do not hand-edit; regenerate.",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"#define QCC_EYE_W {w}",
        f"#define QCC_EYE_H {h}",
        "",
        "static const uint8_t qcc_eye[] = {",
    ]
    for r in range(h):
        row = data[r * row_bytes:(r + 1) * row_bytes]
        out.append("  " + ", ".join(f"0x{b:02X}" for b in row) + ",")
    out.append("};")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--src", default=str(DEFAULT_SRC))
    args = ap.parse_args()
    w, h, data = parse_xbm(Path(args.src).read_text())
    sys.stdout.write(render_header(w, h, to_msb_first(data)))


if __name__ == "__main__":
    main()
```

- [ ] **Step 5: Generate the header**

Run: `python scripts/gen-qcc-splash.py > "C:/Users/stryc/AppData/Local/Temp/claude/C--Dev-meshcore-firmware/81df237c-f348-44a6-9f17-260d505f0d66/scratchpad/QccSplashArt.h"`
Then create `variants/qcc_badge/QccSplashArt.h` with the Write tool, content exactly that output. (Repo files go through the gated Write tool; the script only prints.)

- [ ] **Step 6: Run the tests and confirm they pass**

Run: `python -m pytest scripts/test_gen_qcc_splash.py -q`
Expected: `5 passed`.

- [ ] **Step 7: Gemini review**, then **commit**

```bash
git add variants/qcc_badge/art/qcc_eye_50x50.xbm scripts/gen-qcc-splash.py scripts/test_gen_qcc_splash.py variants/qcc_badge/QccSplashArt.h
git commit -m "feat(#N): QCC eye splash art and its MSB-first generator"
```

---

### Task 1.6: Event-splash hook and splash A

**Files:**
- Modify: `src/helpers/ui/OffbandSplash.h`, `src/helpers/ui/OffbandSplash.cpp`
- Create: `variants/qcc_badge/QccSplashLayout.h`, `variants/qcc_badge/QccSplash.cpp`
- Modify: `platformio.ini` (`[env:native]` `build_src_filter`)
- Modify: `variants/qcc_badge/platformio.ini` (turn the event splash on)
- Test: `test/test_qcc_splash/test_qcc_splash.cpp`

Geometry (computed 2026-09-12 from the art and the fonts): eye at (39, 0), ink x 41–85 beside corner line 1 and 46–83 beside line 2, ink rows 0–45. Corner text up to 40 px wide clears it by ≥ 2 px. "QueenCityCon" in ArialMT 16 at y = 45 has ink rows 48–62.

- [ ] **Step 1: Write the failing test** `test/test_qcc_splash/test_qcc_splash.cpp`

```cpp
#include <gtest/gtest.h>
#include <set>
#include <utility>
#include <string.h>
#include <stdlib.h>

#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/OffbandSplash.h>
#include <helpers/ui/OLEDDisplayFonts.h>
#include <helpers/ui/TpFont.h>
#include "../../variants/qcc_badge/QccSplashLayout.h"
#include "../../variants/qcc_badge/QccSplashArt.h"

class SplashDisplay : public DisplayDriver {
public:
  std::set<std::pair<int, int>> text_px;   // everything drawn with fillRect (the fonts)
  int xbm_calls = 0, xbm_x = -1, xbm_y = -1, xbm_w = 0, xbm_h = 0;
  SplashDisplay() : DisplayDriver(128, 64) {}
  bool isOn() override { return true; }
  void turnOn() override {}
  void turnOff() override {}
  void clear() override {}
  void startFrame(ColorVal) override {}
  void setTextSize(int) override {}
  void setColor(ColorVal) override {}
  void setCursor(int, int) override {}
  void print(const char*) override {}
  void fillRect(int x, int y, int w, int h) override {
    for (int i = 0; i < w; i++)
      for (int j = 0; j < h; j++) text_px.insert({x + i, y + j});
  }
  void drawRect(int, int, int, int) override {}
  void drawXbm(int x, int y, const uint8_t*, int w, int h) override {
    xbm_calls++; xbm_x = x; xbm_y = y; xbm_w = w; xbm_h = h;
  }
  uint16_t getTextWidth(const char* s) override { return (uint16_t)(strlen(s) * 6); }
  void endFrame() override {}
};

static std::set<std::pair<int, int>> eyeInk() {
  std::set<std::pair<int, int>> ink;
  const int rb = (QCC_EYE_W + 7) / 8;
  for (int r = 0; r < QCC_EYE_H; r++)
    for (int c = 0; c < QCC_EYE_W; c++)
      if (qcc_eye[r * rb + c / 8] & (0x80 >> (c % 8))) ink.insert({qcc::kEyeX + c, qcc::kEyeY + r});
  return ink;
}

static void drawWith(SplashDisplay& d, const char* ver) {
  offband::SplashInfo si(ver, "v1.17.0", "12 Sep 2026");
  offband::drawEventSplash(d, si);
}

TEST(QccSplash, DrawsTheEyeCenteredAtTheTop) {
  SplashDisplay d;
  drawWith(d, "v1.5.0-beta6");
  EXPECT_EQ(1, d.xbm_calls);
  EXPECT_EQ(39, d.xbm_x);
  EXPECT_EQ(0, d.xbm_y);
  EXPECT_EQ(50, d.xbm_w);
  EXPECT_EQ(50, d.xbm_h);
}

TEST(QccSplash, NothingIsDrawnOffTheCanvas) {
  SplashDisplay d;
  drawWith(d, "v1.5.0-beta6");
  for (auto& p : d.text_px) {
    EXPECT_GE(p.first, 0);   EXPECT_LE(p.first, 127);
    EXPECT_GE(p.second, 0);  EXPECT_LE(p.second, 63);
  }
}

TEST(QccSplash, TextNeverTouchesTheEye) {
  SplashDisplay d;
  drawWith(d, "v1.5.0-beta6+17*");
  const auto ink = eyeInk();
  for (auto& t : d.text_px)
    for (int dx = -1; dx <= 1; dx++)
      for (int dy = -1; dy <= 1; dy++)
        EXPECT_EQ(0u, ink.count({t.first + dx, t.second + dy}))
            << "text pixel at " << t.first << "," << t.second << " touches the eye";
}

TEST(QccSplash, TheConNameIsCenteredBelowTheEye) {
  SplashDisplay d;
  drawWith(d, "v1.6.0");
  int minx = 999, maxx = -1;
  for (auto& p : d.text_px) {
    if (p.second < 46) continue;
    minx = p.first < minx ? p.first : minx;
    maxx = p.first > maxx ? p.first : maxx;
  }
  EXPECT_NEAR(64, (minx + maxx + 1) / 2, 1);
}

struct Split { const char* in; const char* l1; const char* l2; };

class CornerVersion : public ::testing::TestWithParam<Split> {};

TEST_P(CornerVersion, SplitsForTheTopRightCorner) {
  char l1[24], l2[24];
  qcc::splitCornerVersion(GetParam().in, l1, sizeof(l1), l2, sizeof(l2), qcc::kCornerMaxW);
  EXPECT_STREQ(GetParam().l1, l1);
  EXPECT_STREQ(GetParam().l2, l2);
}

INSTANTIATE_TEST_SUITE_P(Qcc, CornerVersion, ::testing::Values(
  Split{"v1.5.0-beta6", "v1.5.0", "beta6"},
  Split{"v1.5.0-beta6+17", "v1.5.0", "beta6"},     // "beta6+17" is 45 px, over 40
  Split{"v1.5.0-beta6+17*", "v1.5.0", "beta6"},
  Split{"v1.5.0-rc1", "v1.5.0", "rc1"},
  Split{"v1.6.0", "v1.6.0", ""},
  Split{"v1.6.0+3*", "v1.6.0", "+3*"},
  Split{"", "", ""}
));

TEST(CornerVersionEdges, NullIsEmpty) {
  char l1[8] = "x", l2[8] = "y";
  qcc::splitCornerVersion(nullptr, l1, sizeof(l1), l2, sizeof(l2), qcc::kCornerMaxW);
  EXPECT_STREQ("", l1);
  EXPECT_STREQ("", l2);
}

TEST(CornerVersionEdges, AnOverlongReleaseIsShortenedToFit) {
  char l1[24], l2[24];
  qcc::splitCornerVersion("v123.456.789", l1, sizeof(l1), l2, sizeof(l2), qcc::kCornerMaxW);
  EXPECT_LE(offband::tpfont::textWidth(ArialMT_Plain_10, l1), qcc::kCornerMaxW);
  EXPECT_EQ(0, strncmp("v123", l1, 4));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Add the badge splash to the native build** in `platformio.ini`, `[env:native]` `build_src_filter`, after the lines added in Task 1.3:

```ini
  +<../variants/qcc_badge/QccSplash.cpp>      ; #1172: the badge's event splash
```

- [ ] **Step 3: Run it and confirm it fails**

Run: `pio test -e native -f test_qcc_splash -v`
Expected: build error (`QccSplashLayout.h` missing; `drawEventSplash` undeclared).

- [ ] **Step 4: Declare the hook** — in `src/helpers/ui/OffbandSplash.h`, after the `drawBrandLockup` declaration:

```cpp
// #1172: a board's own event splash (the QCC 0x4 badge). drawSplash() draws it INSTEAD
// of the Offband lockup on full-size mono panels when the env defines
// OFFBAND_EVENT_SPLASH; the board supplies the definition. Board-level rather than
// driver-level like colourSplashArt(), because event art belongs to one board, not
// to a display driver many boards share.
void drawEventSplash(DisplayDriver& display, const SplashInfo& info);
```

- [ ] **Step 5: Call it** — in `src/helpers/ui/OffbandSplash.cpp`, `drawSplash()`, replace:

```cpp
  const DisplayDriver::ColourArt* art = display.colourSplashArt();
  const bool colour = (art != nullptr);
```

with:

```cpp
  const DisplayDriver::ColourArt* art = display.colourSplashArt();
  const bool colour = (art != nullptr);

#ifdef OFFBAND_EVENT_SPLASH
  if (!colour && !isCompact(display)) {
    drawEventSplash(display, info);
    return;
  }
#endif
```

- [ ] **Step 6: Create `variants/qcc_badge/QccSplashLayout.h`**

```cpp
#pragma once

#include <stddef.h>

// Splash A for the QCC 0x4 badge (#1172), in 128x64 logical pixels. Values were
// derived from the art and the ArialMT tables; test_qcc_splash checks every one
// against the rendered output rather than trusting these comments.
namespace qcc {

constexpr int kEyeX = 39;           // (128 - 50) / 2
constexpr int kEyeY = 0;
constexpr int kCornerLine1Y = 0;
constexpr int kCornerLine2Y = 11;
constexpr int kRightEdgeX = 127;
constexpr int kCornerMaxW = 40;     // widest ArialMT 10 corner text that clears the eye
constexpr int kNameY = 45;          // ArialMT 16 box; ink rows 48..62

// Splits a short version ("v1.5.0-beta6+17*") for the top-right corner.
//   l1 = up to the first '-' or '+'.
//   l2 = the text after '-'; if that is wider than max_w, only the pre-release label
//        before any '+' ("beta6"). With no '-' but a '+', l2 is the "+N" part.
// Each line is then shortened from the right until it fits max_w in ArialMT 10.
// The full version lives on the About screen; the splash is branding.
void splitCornerVersion(const char* ver, char* l1, size_t n1, char* l2, size_t n2, int max_w);

}  // namespace qcc
```

- [ ] **Step 7: Create `variants/qcc_badge/QccSplash.cpp`**

```cpp
#include "QccSplashLayout.h"
#include "QccSplashArt.h"

#include <helpers/ui/OffbandSplash.h>
#include <helpers/ui/OLEDDisplayFonts.h>
#include <helpers/ui/TpFont.h>

#include <stdio.h>
#include <string.h>

namespace qcc {

static void fitWidth(char* s, int max_w) {
  size_t n = strlen(s);
  while (n > 0 && offband::tpfont::textWidth(ArialMT_Plain_10, s) > max_w) s[--n] = '\0';
}

void splitCornerVersion(const char* ver, char* l1, size_t n1, char* l2, size_t n2, int max_w) {
  if (n1) l1[0] = '\0';
  if (n2) l2[0] = '\0';
  if (!ver || !n1 || !n2) return;

  const size_t cut = strcspn(ver, "-+");
  snprintf(l1, n1, "%.*s", (int)cut, ver);

  const char* rest = ver + cut;
  if (*rest == '-') {
    rest++;
    snprintf(l2, n2, "%s", rest);
    if (offband::tpfont::textWidth(ArialMT_Plain_10, l2) > max_w) {
      snprintf(l2, n2, "%.*s", (int)strcspn(rest, "+"), rest);
    }
  } else if (*rest == '+') {
    snprintf(l2, n2, "%s", rest);
  }
  fitWidth(l1, max_w);
  fitWidth(l2, max_w);
}

}  // namespace qcc

namespace offband {

void drawEventSplash(DisplayDriver& display, const SplashInfo& info) {
  display.setColor(UIColor::primary_txt);
  display.drawXbm(qcc::kEyeX, qcc::kEyeY, qcc_eye, QCC_EYE_W, QCC_EYE_H);

  tpfont::drawText(display, 0, qcc::kCornerLine1Y, ArialMT_Plain_10, "Offband");

  char l1[24], l2[24];
  const char* ver = info.offband_ver ? info.offband_ver : offbandShortVersion();
  qcc::splitCornerVersion(ver, l1, sizeof(l1), l2, sizeof(l2), qcc::kCornerMaxW);
  const int right = qcc::kRightEdgeX + 1;
  tpfont::drawText(display, right - tpfont::textWidth(ArialMT_Plain_10, l1),
                   qcc::kCornerLine1Y, ArialMT_Plain_10, l1);
  tpfont::drawText(display, right - tpfont::textWidth(ArialMT_Plain_10, l2),
                   qcc::kCornerLine2Y, ArialMT_Plain_10, l2);

  static const char kConName[] = "QueenCityCon";
  tpfont::drawText(display, (display.width() - tpfont::textWidth(ArialMT_Plain_16, kConName)) / 2,
                   qcc::kNameY, ArialMT_Plain_16, kConName);
}

}  // namespace offband
```

- [ ] **Step 8: Run the tests and confirm they pass**

Run: `pio test -e native -f test_qcc_splash -v`
Expected: `[  PASSED  ] 13 tests`. Then `pio test -e native -v`: every suite passes, including `test_splash` (the stock splash is unchanged, because `OFFBAND_EVENT_SPLASH` is not defined natively).

- [ ] **Step 9: Turn the badge splash on** — in `variants/qcc_badge/platformio.ini`, `[QCC_Badge]` `build_flags`, after `-D AUTO_SHUTDOWN_MILLIVOLTS=3400`:

```ini
  -D OFFBAND_EVENT_SPLASH   ; splash A (QccSplash.cpp) instead of the Offband lockup
```

- [ ] **Step 10: Build the badge env**

Run: `pio run -e QCC_Badge_companion_radio_ble`
Expected: `SUCCESS`. Record flash and RAM on the task issue beside Task 1.4's figures.

- [ ] **Step 11: Confirm the stock splash still builds elsewhere**

Run: `pio run -e ProMicro_companion_radio_ble`
Expected: `SUCCESS`.

- [ ] **Step 12: Gemini review**, then **commit**

```bash
git add src/helpers/ui/OffbandSplash.h src/helpers/ui/OffbandSplash.cpp variants/qcc_badge/QccSplashLayout.h variants/qcc_badge/QccSplash.cpp variants/qcc_badge/platformio.ini platformio.ini test/test_qcc_splash/test_qcc_splash.cpp
git commit -m "feat(#N): event-splash hook and the QCC splash A"
```

---

### Task 1.7: Diag env and the self-test legend

**Files:**
- Modify: `variants/qcc_badge/platformio.ini` (diag block and env)
- Modify: `examples/companion_radio/ui-new/UITask.h`, `examples/companion_radio/ui-new/UITask.cpp`
- Modify (only if D3 is approved): `.github/workflows/ci.yml`

The legend names the badge's outputs so the owner can find them without opening the badge. P0.08 carries the heartbeat. P0.15 is blinked by Bluefruit while advertising (`Bluefruit.autoConnLed(false)` is commented out in `SerialBLEInterface.cpp:133`). The buzzer plays the existing startup tune. So nothing needs driving; the screen only names what is already happening.

- [ ] **Step 1: Add the diag block and env** — append to `variants/qcc_badge/platformio.ini`:

```ini
; Diagnostic twin, until beta (owner directive): the fleet's nRF52 diag surface.
; The log mirror transmits on the spare GPIO33 pad (P1.01) through UARTE1, never on
; P0.06/P0.08, which drive the buzzer and LED. Logs also go over the ProMicro's USB.
[qcc_badge_diag]
build_flags =
  -D OFFBAND_FORCE_CAPLOG
  -D OFFBAND_BOOT_BEACON
  -D OFFBAND_LOG_MIRROR_UART=1
  -D OFFBAND_LOG_MIRROR_TX_PIN=PIN_QCC_SPARE_GPIO33
  -D OFFBAND_LOG_MIRROR_BAUD=115200
  -D QCC_BADGE_SELFTEST

[env:QCC_Badge_companion_radio_ble_diag]
extends = env:QCC_Badge_companion_radio_ble
build_flags =
  ${env:QCC_Badge_companion_radio_ble.build_flags}
  ${qcc_badge_diag.build_flags}
```

- [ ] **Step 2: Declare the screen** — in `UITask.h`, after `UIScreen* msg_preview;`:

```cpp
#ifdef QCC_BADGE_SELFTEST
  UIScreen* self_test;
#endif
```

and after `void gotoHomeScreen() { setCurrScreen(home); }`:

```cpp
#ifdef QCC_BADGE_SELFTEST
  void gotoSelfTest();
#endif
```

- [ ] **Step 3: Route the splash through it** — in `UITask.cpp`, `SplashScreen::poll()`, replace:

```cpp
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
```

with:

```cpp
    if (millis() >= dismiss_after) {
#ifdef QCC_BADGE_SELFTEST
      _task->gotoSelfTest();
#else
      _task->gotoHomeScreen();
#endif
    }
```

- [ ] **Step 4: Add the screen** — in `UITask.cpp`, directly after the `SplashScreen` class:

```cpp
#ifdef QCC_BADGE_SELFTEST
// Bring-up legend (#1173): names the badge's physical outputs so they can be found
// without opening the badge. Shown once after the splash, on diag builds only.
class SelfTestScreen : public UIScreen {
  UITask* _task;
  unsigned long _dismiss_after = 0;

public:
  explicit SelfTestScreen(UITask* task) : _task(task) {}
  void arm() { _dismiss_after = millis() + 6000; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);
    display.drawTextLeftAlign(0, 0, "SELF-TEST (diag)");
    display.drawTextLeftAlign(0, 16, "P0.08 LED: heartbeat");
    display.drawTextLeftAlign(0, 28, "P0.15 LED: BT advert");
    display.drawTextLeftAlign(0, 40, "P0.06 buzz: boot tune");
    return 500;
  }

  void poll() override {
    if (millis() >= _dismiss_after) _task->gotoHomeScreen();
  }
};
#endif
```

- [ ] **Step 5: Create and enter it** — in `UITask::begin()`, after `msg_preview = new MsgPreviewScreen(this, &rtc_clock);`:

```cpp
#ifdef QCC_BADGE_SELFTEST
  self_test = new SelfTestScreen(this);
#endif
```

and after `UITask::setAlwaysOn(...)`'s definition, add:

```cpp
#ifdef QCC_BADGE_SELFTEST
void UITask::gotoSelfTest() {
  ((SelfTestScreen*)self_test)->arm();
  setCurrScreen(self_test);
}
#endif
```

- [ ] **Step 6: Build both badge envs and a non-badge companion**

Run: `pio run -e QCC_Badge_companion_radio_ble -e QCC_Badge_companion_radio_ble_diag -e ProMicro_companion_radio_ble`
Expected: three `SUCCESS` rows. Record the badge sizes.

- [ ] **Step 7 (only if D3 approved): add the badge env to the CI matrix** — add `QCC_Badge_companion_radio_ble` to the env list in `.github/workflows/ci.yml`, matching how the neighboring nRF52 companion entries are written there.

- [ ] **Step 8: Gemini review**, then **commit**

```bash
git add variants/qcc_badge/platformio.ini examples/companion_radio/ui-new/UITask.h examples/companion_radio/ui-new/UITask.cpp
git commit -m "feat(#N): QCC diag env and the bring-up self-test legend"
```

---

### Task 1.8: Spec touch-ups

**Files:**
- Modify: `docs/architecture/2026-09-12-qcc-0x4-badge-design.md`

- [ ] **Step 1: Edit the spec**
  - 5.7 Events: add **message sent**, a short confirm tone (owner, 2026-09-12: stock Meshtastic plays one on send).
  - 5.1: replace "drop the environmental-sensor drivers" with "keep them: measured flash is 58.8% (418,832 / 712,704 B), and SAO add-ons may carry sensors" (D4).
  - 5.12: replace "verified in epic 1" with the verified behavior: with a display and the default `BLE_PIN_CODE=123456`, the companion picks a random PIN each session and shows it (`MyMesh.cpp:1415-1422`).
  - 5.10: record D2's thresholds and the SafeBoot sample-time hook. Record that SafeBoot only runs on this board because the env sets `SAFEBOOT_PIN_VBAT_READ`: `SafeBoot.cpp` can't see `PromicroBoard.h`, and ProMicro has the same gap (#1176).

- [ ] **Step 2: Commit**

```bash
git add docs/architecture/2026-09-12-qcc-0x4-badge-design.md
git commit -m "docs(#N): QCC spec -- sent tone, sensor drivers kept, BLE PIN verified"
```

---

### Task 1.9: Bench bring-up on the owner's badge (Tier 2)

Every `pio-flash.py` run is its own owner approval: `list`, `bootstrap`, `preview`, `confirm`, `monitor`. After each run, stop and report. Never chain `preview` into `confirm`.

- [ ] **Step 1: Identify the badge.** Owner plugs it in. With approval, run `python scripts/pio-flash.py list` from the epic worktree. The owner names the port.

- [ ] **Step 2: Check the bootloader.** Owner double-taps RESET; a UF2 drive mounts. Read its `INFO_UF2.TXT` (a read-only file on the drive) and confirm `SoftDevice: S140 6.1.1`. Record the Board-ID. Owner taps RESET to leave the bootloader.

- [ ] **Step 3: Register it.** With approval: `python scripts/pio-flash.py bootstrap qcc-badge-1 --port=<COMx>` (nRF52 path: identity is the USB serial, no chip touch). Record the entry in `HARDWARE.local.md` beside the badge's hardware sheet.

- [ ] **Step 4: Confirm the image identity** (stale-stamp guard):
  `python -c "import sys; sys.path.insert(0,'scripts'); from pathlib import Path; from firmware_identity import get_firmware_identity as g; print(g(Path('.pio/build/QCC_Badge_companion_radio_ble_diag/firmware.bin'), Path('.')))"`
  If the SHA is not `HEAD`, or reads `-dirty` on a clean tree: `pio run -e QCC_Badge_companion_radio_ble_diag -t clean`, then rebuild.

- [ ] **Step 5: Stage the flash.** Owner double-taps RESET again: stock Meshtastic may ignore the 1200 bps touch, as it did on the T1000-E. With approval:
  `python scripts/pio-flash.py preview qcc-badge-1 --artifact C:/Dev/.worktrees/meshcore-firmware-1173/.pio/build/QCC_Badge_companion_radio_ble_diag/firmware.zip`
  **Stop.** Post the preview, and in the same message:
  - **On boot the badge will:** show the splash and legend, start BLE advertising, and listen on the D1 preset. It transmits on the mesh only on a user action (advert or send), under a brand-new identity nobody has as a contact.
  - **Reach:** BLE pairing requests from phones in range, which need the PIN shown on the OLED.
  - **Rollback:** double-tap RESET and drag the stock `qcc firmware.uf2` onto the drive. That restores Meshtastic but **not its settings**: our first boot formats the filesystem region.
  Wait for a GO that names `qcc-badge-1`.

- [ ] **Step 6: Flash.** With the GO: `python scripts/pio-flash.py confirm qcc-badge-1 --token <token file> --in-bootloader`. Read the tool's own verdict line.

- [ ] **Step 7: Verify on the badge** (owner observes; photos welcome), and record each result on the task issue:
  - [ ] Splash A: eye, "QueenCityCon", "Offband", version corner.
  - [ ] Legend screen for ~6 s, then Home.
  - [ ] Heartbeat: which physical LED blinks (P0.08). Does the blue ProMicro LED show while advertising (P0.15)?
  - [ ] Startup tune plays (MUTE switch off).
  - [ ] BLE: the phone app pairs with the PIN on the OLED; Settings shows model "QCC 0x4 Badge".
  - [ ] Radio: the app shows the D1 preset; the noise floor reads (radio alive).
  - [ ] Battery mV is plausible (calibrated in 1.10).
  - [ ] With approval, `python scripts/pio-flash.py monitor qcc-badge-1` (one held session): boot beacon lines, the SafeBoot battery line, `[boot]` lines.

---

### Task 1.10: Battery calibration (bench)

- [ ] **Step 1:** Owner measures the cell at the battery connector with a meter (or the INA228 inline on the battery lead) at two states of charge, for example near full and about 3.7 V. At each, read the badge's battery mV in the app.
- [ ] **Step 2:** Record the pairs on the task issue. Error = badge / meter − 1.
- [ ] **Step 3:** If |error| ≤ 2% at both points: done. Otherwise apply one correction to **both** readings: set `SAFEBOOT_ADC_MULTIPLIER` to `1.68 × meter/badge` in `variants/qcc_badge/platformio.ini`, and make `qcc::kDividerRatio` the same value in `QccBattery.h`, with a comment naming the measurement. Update the Task 1.2 and Task 1.4 tests to the new literal. The `static_assert` keeps the two readings in lockstep. Rebuild, then reflash under a **new** preview/GO, and re-measure.
- [ ] **Step 4: Commit** (if changed): `fix(#N): calibrate the QCC battery divider against a meter`.

---

### Task 1.11: Epic 1 verification

- [ ] All epic 1 tasks closed, each with evidence and a Gemini review on its issue.
- [ ] `pio test -e native -v`: every suite passes. `python -m pytest scripts/test_qcc_variant_pins.py scripts/test_gen_qcc_splash.py -q`: all pass.
- [ ] `pio run -e QCC_Badge_companion_radio_ble -e QCC_Badge_companion_radio_ble_diag -e ProMicro_companion_radio_ble`: `SUCCESS` ×3, with sizes recorded.
- [ ] Bench results from 1.9 and 1.10 recorded on the issues.
- [ ] #1173's criteria met: boots Offband, splash renders, heartbeat visible, the legend identifies the LED(s), battery within 2% of a meter, BLE pairs.
- [ ] Owner sign-off. Then push the epic branch and open the epic PR (each its own owner approval).

---

## Epics 2–6 (outline; each gets a detailed plan when it starts)

### Epic 2 — Keyboard and standalone messaging
1. **CardKB driver**: I2C 0x5F poll, detection at boot, key map from M5's CardKB documentation. Unit tests for the key decode. Files: `src/helpers/ui/CardKbInput.h/.cpp`, `test/test_cardkb/`.
2. **Input integration**: keys into `UIScreen::handleInput(char)`; button fallback when no keyboard. Files: `ui-new/UITask.cpp`.
3. **History store**: last ~100 messages, a small RAM index plus a bounded, versioned ring on the internal filesystem, tagged by source (mesh / phone / badge). Unit tests for the ring and record format. Files: `examples/companion_radio/MessageHistory.h/.cpp`, `test/test_msg_history/`.
4. **Message hooks**: record incoming, phone-sent and badge-sent messages. Files: `examples/companion_radio/MyMesh.cpp`.
5. **UI kit**: title bar, list, ArialMT word wrap, compose line. Unit tests for layout metrics. Files: `ui-new/QccUi.h/.cpp`, `test/test_qcc_ui_layout/`.
6. **Inbox screen.**
7. **Thread and compose**: send to channel or DM, retries and ACK tick for DMs.
8. **New message**: type-to-filter picker; join a channel by typing `#name`.
9. **First-boot handle, Settings (handle), About** (Offband version, "on MeshCore", build date, handle, key prefix).
10. **Type-to-talk and the new-message card.**
11. **Epic 2 verification** on the badge, with the phone app alongside.

### Epic 3 — Badge-to-client bridge
1. **Wire contract**: a new `0xC_` command and caps bit in `OffbandConfigProtocol.h`, allocated at merge after the registry check (all branches, open issues, the client repo, Agent Mail).
2. **Firmware handler**: cursor pull, records framed to `maxFramePayload()`. Unit tests for the framing.
3. **Client work**: under OffbandMesh/meshcore-client#657.
4. **Epic 3 verification** with the Offband client, and a stock client proving nothing changed for it.

### Epic 4 — Sound, lights, GPS power
1. **Sound events**: startup, channel message, DM, **message sent**, shutdown; per-event prefs (append-only prefs rule).
2. **Nokia composer → RTTTL converter.** Unit tests.
3. **Tones screen**: presets, custom entry, preview.
4. **LED scheduler**: traffic flicker > message-waiting pattern > heartbeat, never solid-on, honors `led off`; RX/TX hooks. Unit tests for the scheduler.
5. **GPS modes**: Off / On / Power save (interval default 60 min, fix timeout default 180 s, UART pins released when off). Unit tests for the state machine.
6. **GPS screen.**
7. **RGB LED option** behind `OFFBAND_RGB_LED_PIN` (off by default).
8. **Epic 4 verification**, including current per mode with the INA228 inline.

### Epic 5 — CTF beacon (parked; nice to have)
Designed when unparked: provisioning over USB (never in firmware), a zero-hop raw-custom beacon on a jittered interval, and an optional tone.

### Epic 6 — Integration testing on the badge
1. **Con flavor**: env with the con's radio config (once the owner has it), and the departure image's one-time move back to the D1 defaults.
2. **Two badges** (or a badge plus another Offband companion) messaging both ways, with the phone app.
3. **Departure-booth flow** end to end.
4. **Battery life** at the chosen defaults.
5. **Feature sign-off.**

---

## Filing the chain (at AGREE)

Epic 1 tasks 1.1–1.11 are filed as `type:task` sub-issues of #1173, each with a Citadel task depending on `Crosswire-izh`, and run in order 1.1 → 1.11: several share files (1.1/1.4 the guard test, 1.3/1.6 the native build list, 1.4/1.6/1.7 the badge `.ini`), so each depends on the one before. Epics 2–6 are filed as `type:epic` sub-issues of #1172, with their Citadel entries depending on `Crosswire-xdo`; their tasks are filed when each epic's detailed plan is written. The issue numbers go into this table, and each commit subject uses its own task's number.

Filed 2026-09-12 after the owner agreed this plan (D1 US preset; D2, D3, D4 yes).

| Task | Issue | Citadel |
|---|---|---|
| 1.1 Pin map | #1177 | Crosswire-2e1 |
| 1.2 Battery math + SafeBoot hook | #1178 | Crosswire-3r7 |
| 1.3 Font renderer | #1179 | Crosswire-6wi |
| 1.4 Board, target, env | #1180 | Crosswire-ey9 |
| 1.5 Splash art | #1181 | Crosswire-viu |
| 1.6 Event splash | #1182 | Crosswire-p4z |
| 1.7 Diag env + legend + CI entry | #1183 | Crosswire-wzu |
| 1.8 Spec touch-ups | #1184 | Crosswire-qjt |
| 1.9 Bench bring-up | #1185 | Crosswire-zdw |
| 1.10 Battery calibration | #1186 | Crosswire-3he |
| 1.11 Epic 1 verification | #1187 | Crosswire-loq |

| Epic | Issue | Citadel |
|---|---|---|
| 2 Keyboard and standalone messaging | #1188 | Crosswire-kqp |
| 3 Badge-to-client bridge | #1189 | Crosswire-jqd |
| 4 Sound, lights, GPS power | #1190 | Crosswire-30i |
| 5 CTF beacon (parked) | #1191 | Crosswire-1xv |
| 6 Integration testing | #1192 | Crosswire-zhb |
