/*
 * variant.h -- Heltec RadioCore RC52 (nRF52840 + HT-RA62A / SX1262 + FEM)
 *
 * Every pin below is taken from Heltec's own board support package:
 *   HelTecAutomation/Heltec_nRF52 :: variants/heltec_rc52/variant.h
 * read directly 2026-08-19 (see OffbandMesh/meshcore-firmware#854). Nothing here
 * is guessed or carried over from a sibling RadioCore board -- the carrier is NOT
 * pin-consistent across the family (see the warning block below).
 *
 * MIT (this file); vendor values reproduced for interoperability.
 */

#pragma once

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// !! THE CARRIER IS NOT PIN-CONSISTENT ACROSS THE RADIOCORE FAMILY
//
// Do not carry wiring or assumptions over from the RC32 or RCC6:
//
//   header pin | RCC6      | RC52
//   -----------|-----------|---------------------
//   19         | GND       | VDD        <-- swapped
//   20         | VDD_3V3   | GND        <-- swapped
//   11         | U0RXD     | nRF_TX (P0.08)
//   12         | U0TXD     | nRF_RX (P0.07)
//
// Ground a probe on pin 20 (or 1, or 3) -- NOT pin 19. The RCC6 handoff says the
// opposite and it is wrong for this board; following it puts ground on VDD.
// A sniffer listens on pin 11, because that is the pin the RC52 transmits on.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Clock

#define USE_LFXO                        // 32.768 kHz crystal is fitted
#define VARIANT_MCK             (64000000ul)

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (1)
#define NUM_ANALOG_OUTPUTS      (0)

////////////////////////////////////////////////////////////////////////////////
// !! THERE IS NO LED ON THIS BOARD
//
// Vendor BSP: PIN_LED1 -1, PIN_NEOPIXEL -1, NEOPIXEL_NUM 0. No status-LED
// heartbeat is possible on the RC52. If a board appears "dead" because nothing
// blinks, that is this hardware fact and NOT a fault -- do not diagnose it as one.
// P_LORA_TX_LED is deliberately left undefined (it is #ifdef-guarded upstream).

#define LED_BUILTIN             (-1)
#define PIN_LED                 LED_BUILTIN
#define LED_RED                 LED_BUILTIN
#define LED_BLUE                (-1)    // also stops Bluefruit blinking during advertising
#define LED_STATE_ON            1

////////////////////////////////////////////////////////////////////////////////
// Buttons

#define PIN_BUTTON1             (32 + 10)   // P1.10, external pull-up, ACTIVE LOW
#define BUTTON_PIN              PIN_BUTTON1
#define PIN_USER_BTN            BUTTON_PIN

////////////////////////////////////////////////////////////////////////////////
// Battery
//
// ADC_CTRL (P0.04) gates the divider feeding BATTERY_PIN (P0.31).
//
// ADC_MULTIPLIER 4.9 is DERIVED, not measured: schematic RC52-L62_V1.02 gives
// VBAT -> Q3 (AO3401A, P-ch) -> R17 390K -> [tap] -> R18 100K -> GND, so
// (390K + 100K) / 100K = 4.90. Independently corroborated by the vendor BSP's
// own BAT_AMPLIFY 4.9F. Two sources; take it, do not re-derive or "improve" it.
//
// !! This is the NOMINAL ratio, not a calibration. At 1% parts the true value
// spans ~4.83-4.97. Confirm on hardware before anything trusts it -- see #857.

#define BATTERY_PIN             (0 + 31)    // P0.31 / AIN7
#define ADC_CTRL                (0 + 4)     // P0.04
#define ADC_CTRL_ENABLED        HIGH        // Q2 NPN -> gate of P-ch Q3: HIGH enables
#define ADC_CTRL_DISABLED       LOW         // explicit; do not write !ADC_CTRL_ENABLED
#define ADC_MULTIPLIER          (4.90F)
#define AREF_VOLTAGE            (3.0)
#define MV_LSB                  (3000.0F / 4096.0F)  // read at 12-bit / AR_INTERNAL_3_0

// ADC_RESOLUTION mirrors the vendor BSP's declared 14-bit capability, while
// getBattMilliVolts() deliberately reads at 12-bit (matching MV_LSB above).
//
// That is NOT an order-dependent bug, and a review flagged it as one -- so the
// refutation is recorded here to stop it being re-raised:
//   * The Adafruit nRF52 core does NOT seed its read resolution from this macro.
//     wiring_analog_nRF52.c defaults to `static int readResolution = 10` and only
//     changes it via analogReadResolution(). The apparent "ADC_RESOLUTION" hits in
//     that file are its own SAADC_RESOLUTION_VAL_* enum names, not this symbol.
//   * Nothing under src/ or examples/ reads ADC_RESOLUTION at all.
// [verified: framework-arduinoadafruitnrf52 wiring_analog_nRF52.c + repo grep,
//  2026-08-19]
#define ADC_RESOLUTION          (14)        // vendor-declared capability (informational)

////////////////////////////////////////////////////////////////////////////////
// !! SafeBoot battery gating is DELIBERATELY NOT CONFIGURED HERE
//
// #602: on the Seeed Wio Tracker L1 Pro an INHERITED battery polarity made
// SafeBoot disable the divider, sample it dead, read below SLEEP_MV and deep-sleep
// before USB init -- a fully charged board presenting as completely dead.
//
// CORRECTED (2026-08-23): this block used to say the POLARITY was unverified.
// That was wrong, and it muddled two different things. The polarity is settled by
// circuit topology and needs no bench measurement:
//   Q3 is an AO3401A P-channel MOSFET with its gate held at VBAT through R16
//   (100K) -- off by default. Q2 (S9013, NPN) pulls that gate low when ADC_Ctrl
//   is driven HIGH, which turns Q3 on and connects the divider. HIGH enables.
//   R17 (390K) over R18 (100K) gives (390+100)/100 = 4.90.
// [verified: rc52.pdf schematic, 2026-08-23] -- matching ADC_CTRL_ENABLED and
// ADC_MULTIPLIER above.
//
// What is NOT established is the SCALE FACTOR'S ACCURACY. 4.90 is nominal; at 1%
// parts the true ratio spans ~4.83-4.97, before ADC reference error. A
// PWRMGT_VOLTAGE_BOOTLOCK threshold compares an ABSOLUTE voltage, so it is the
// calibration -- not the polarity -- that it depends on. #602 is the cautionary
// case: a fully charged board deep-sleeping before USB init because the value it
// compared was wrong.
//
// RESOLVED 2026-08-23 -- the threshold IS now set, and the calibration worry was
// overstated. The cross-check that settled it, all on the same battery within
// seconds, captured on the mirror UART:
//
//     RC52 via this divider   4130 mV
//     MAX17048 fuel gauge     4147 mV
//     INA219 bus register     4144 mV
//
// -15 mV, or -0.36%, against a predicted worst case of +/-3.5%. Three independent
// paths inside 17 mV. The nominal 4.90 is good, and #602's failure mode -- a
// charged board reading as dead -- is ruled out, since that is an error of
// hundreds of percent, not tenths.
//
// PWRMGT_VOLTAGE_BOOTLOCK is 3300, matching the fleet (8 other nRF52 variants;
// two use 3100). At 0.36% observed error that is ~12 mV of uncertainty on the
// comparison, against ~800 mV of headroom to a charged cell.
#define PWRMGT_VOLTAGE_BOOTLOCK 3300   // Won't boot below this voltage (mV)

// PWRMGT_LPCOMP_AIN / REFSEL are deliberately NOT defined, and RC52Board's
// power_config leaves them 0. They are consumed ONLY by configureVoltageWake(),
// which is reached only from a board-specific initiateShutdown() override. RC52
// does not override it, so the base NRF52Board::initiateShutdown() runs and calls
// enterSystemOff() directly -- LPCOMP is never configured.
//
// CONSEQUENCE, stated rather than discovered later: if this board ever boot-locks
// it stays in SYSTEMOFF until a reset or USB attach. It will NOT self-wake when
// the cell recovers. Correct for a bench board; revisit for a deployed one.
//
// #953 UPDATE: NRF52_POWER_MANAGEMENT IS now defined for this board, and this
// paragraph still holds unchanged. That flag gates two unrelated things: the
// RESETREAS capture (wanted -- it is why the crash log can name a cause) and
// boot-voltage gating (NOT wanted yet). RC52Board.cpp sets voltage_bootlock = 0,
// which disables the gating outright, so checkBootVoltage() returns before it can
// reach initiateShutdown() or configureVoltageWake(). Still no threshold defined
// here, still nothing gating boot on an unmeasured reading.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// I2C

#define WIRE_INTERFACES_COUNT   (1)
#define PIN_WIRE_SDA            (0 + 6)     // P0.06
#define PIN_WIRE_SCL            (0 + 29)    // P0.29

////////////////////////////////////////////////////////////////////////////////
// Serial -- header UART. Board TRANSMITS on P0.08 = carrier header pin 11.

#define PIN_SERIAL1_RX          (0 + 7)     // P0.07 -> header pin 12
#define PIN_SERIAL1_TX          (0 + 8)     // P0.08 -> header pin 11

////////////////////////////////////////////////////////////////////////////////
// LoRa -- SX1262 (HT-RA62A), on SPI0

#define USE_SX1262
#define LORA_CS                 (0 + 13)    // P0.13
#define SX126X_CS               LORA_CS
#define SX126X_DIO1             (0 + 11)    // P0.11
#define SX126X_BUSY             (0 + 24)    // P0.24
#define SX126X_RESET            (32 + 0)    // P1.00
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

////////////////////////////////////////////////////////////////////////////////
// FEM (HT-RA62A front-end module) -- NEITHER the RC32 NOR the RCC6 has one.
//
// FEM_LNA_CTRL (P1.07) IS the RadioLib RXEN pin: CustomSX1262::std_init() picks
// up SX126X_RXEN and calls setRfSwitchPins(RXEN, TXEN) itself, so the RX path
// needs no board-class code. TX switching is handled by DIO2 (above), hence
// there is no TXEN.
//
// FEM_EN and VFEM_CTRL are plain enables driven in RC52Board::begin().
// Characterising this FEM -- gain, LNA behaviour, TX power -- is #858, NOT here.

#define SX126X_RXEN             (32 + 7)    // P1.07, HT-RA62A LNA_Ctrl
#define RADIOCORE_FEM_EN        (0 + 26)    // P0.26
#define RADIOCORE_VFEM_CTRL     (0 + 16)    // P0.16, FEM regulator enable

////////////////////////////////////////////////////////////////////////////////
// SPI
//
// SPI0 = LoRa. SPI1 = TFT, backed by SPIM3 (the nRF52840's single 32 MHz
// instance) per the vendor BSP's SPI_32MHZ_INTERFACE 1.

#define SPI_INTERFACES_COUNT    (2)
#define SPI_32MHZ_INTERFACE     (1)

#define PIN_SPI_MISO            (0 + 14)    // P0.14
#define PIN_SPI_MOSI            (0 + 22)    // P0.22
#define PIN_SPI_SCK             (0 + 25)    // P0.25
#define PIN_SPI_NSS             LORA_CS

#define PIN_SPI1_MISO           (0 + 12)    // P0.12 -- NOT wired to the panel;
                                            // SPIClass still needs a valid pin
#define PIN_SPI1_MOSI           (32 + 2)    // P1.02 -- carrier header pin 17
#define PIN_SPI1_SCK            (0 + 30)    // P0.30 -- carrier header pin 6

////////////////////////////////////////////////////////////////////////////////
// TFT (T108 / NV3001B) -- LIVE. Driven by variants/heltec_rc52/RC52Display.cpp
// under HELTEC_RC52_WITH_DISPLAY (#948/#949/#950).
//
// ⚠ EVERY VALUE BELOW IS THE VENDOR'S, TAKEN FROM THE SOURCE OF RECORD --
// HelTecAutomation/RadioCore_Library :: src/boards/heltec_rc52.h, read as raw
// bytes via the GitHub API on 2026-08-23, not from a summary or a diagram:
//
//   RADIOCORE_NV3001B_SCK   (0 + 30)    RADIOCORE_NV3001B_MOSI  (32 + 2)
//   RADIOCORE_NV3001B_CS    (32 + 4)    RADIOCORE_NV3001B_DC    (0 + 28)
//   RADIOCORE_NV3001B_RST   (0 + 10)    RADIOCORE_NV3001B_ENABLE (32 + 13) LOW
//   RADIOCORE_NV3001B_BACKLIGHT (0 + 9) HIGH
//   USE_HARDWARE_SPI 1, SPI_INSTANCE SPI1, SPI_FREQUENCY 8000000UL
//
// ⚠⚠ MOSI AND CS WERE BOTH WRONG BY ONE UNTIL 2026-08-23 -- P1.03 and P1.05
// instead of P1.02 and P1.04. Neither P1.03 nor P1.05 appears ANYWHERE in
// Heltec's RC52 pin map; P1.02 and P1.04 are broken out on carrier header pins
// 17 and 16. The board flashed, booted, ran the radio and lit its backlight
// with the panel receiving nothing at all, because a write-only panel has no
// readback to fail on and begin() returns true regardless. If this file ever
// disagrees with the vendor header again, THIS FILE is the defect.
//
// !! THE TFT OCCUPIES CARRIER HEADER PINS 6-10. Anything wired to those pins
// conflicts with a display build, and SWDIO (P0.30) is also TFT_SCK, so SWD and
// the display are mutually exclusive on this board.
//
// !! TFT_EN is ACTIVE LOW while TFT_BL is ACTIVE HIGH. Opposite polarities on the
// two power/backlight controls; getting one backwards presents as a dead panel.
//
// !! RST is P0.10 = NFC1 and BL is P0.09 = NFC2. On nRF52840 those are reserved
// for NFC unless UICR NFCPINS is cleared, which needs CONFIG_NFCT_PINS_AS_GPIOS
// -- defined NOWHERE in this tree or the framework (checked 2026-08-23). If the
// panel is still dark after the pin fix above, that is the next hypothesis, and
// the cost of testing it is a one-way UICR write.

#define PIN_TFT_SCK             (0 + 30)    // P0.30 -- also SWDIO, header pin 6
#define PIN_TFT_MOSI            (32 + 2)    // P1.02 -- header pin 17
#define PIN_TFT_MISO            (-1)        // write-only panel, no readback
#define PIN_TFT_CS              (32 + 4)    // P1.04 -- header pin 16
#define PIN_TFT_DC              (0 + 28)    // P0.28 -- header pin 7
#define PIN_TFT_RST             (0 + 10)    // P0.10 -- header pin 10
#define PIN_TFT_VDD_CTL         (32 + 13)   // P1.13 -- header pin 8
#define TFT_VDD_ENABLE          LOW         // ACTIVE LOW
#define PIN_TFT_LEDA_CTL        (0 + 9)     // P0.09 -- header pin 9
#define TFT_LEDA_ENABLE         HIGH        // ACTIVE HIGH
