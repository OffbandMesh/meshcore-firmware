/*
 * variant.h -- Queen City Con 0x4 badge (#1172).
 * Derived from variants/promicro/variant.h, Copyright (C) 2023 Seeed K.K., MIT License.
 *
 * The badge is a ProMicro nRF52840 carrier with the same pin map, but ProMicro's default
 * buses land on badge pins: Serial1 on the buzzer and LED MOSFETs (P0.06/P0.08), SPI on
 * RXEN, the GPS UART and the GPS power switch, and Wire SDA on the user button. Here every
 * default is the badge's real bus. SPI1 and Wire1 have no device on the badge and are not
 * defined: SPI1 SCK is the GPIO33 pad, and Wire1 sits on the radio's NSS and MOSI.
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
#define PIN_QCC_SPARE_GPIO33 (18)   // P1.01 -> "GPIO33" pad; not connected as built (#1210)
#define PIN_QCC_SPARE_GPIO38 (9)    // P1.06 -> "GPIO38" pad: the diag log mirror (#1210)

// The four spare pads, for the diag pad ID beacon (#1210). Raw nRF pin numbers
// (port * 32 + pin), which is also how the badge names them. P1.07 has no entry in
// the inherited pin map, so the beacon never goes through g_ADigitalPinMap. As built,
// only GPIO38 is connected: P1.01, P1.02 and P1.07 are the ProMicro's inner holes, and
// the owner's badge has no header pins in them.
#define OFFBAND_PAD_BEACON_PADS \
  { {33, "GPIO33 P1.01"}, {34, "GPIO34 P1.02"}, {38, "GPIO38 P1.06"}, {39, "GPIO39 P1.07"} }

////////////////////////////////////////////////////////////////////////////////
// UART pin definition -- the GPS header, NOT the ProMicro default (see header)

#define PIN_SERIAL1_TX       (3)    // P0.20 -> GPS RX
#define PIN_SERIAL1_RX       (4)    // P0.22 <- GPS TX

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition -- the badge bus: OLED, keyboard, SAO

#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA         (8)    // P1.04
#define PIN_WIRE_SCL         (7)    // P0.11

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition -- the radio bus

#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_SCK          (12)   // P1.11
#define PIN_SPI_MISO         (15)   // P0.02
#define PIN_SPI_MOSI         (14)   // P1.15

#define PIN_SPI_NSS          (13)   // P1.13

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
