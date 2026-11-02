/*
 * variant.h -- OKIMesh Solar / Queen City Con node (#1626).
 * Derived from variants/promicro/variant.h, Copyright (C) 2023 Seeed K.K., MIT License.
 *
 * A ProMicro nRF52840 carrier with an Ra-01SH, a GPS header and CN3163 solar charging. Its
 * GPS UART, GPS power switch and battery divider are wired like the QCC badge's, confirmed
 * from the copper in the board layout. ProMicro's default buses land on the wrong pins here
 * too: Serial1 on the raw GPIO6 and GPIO8 pads, and SPI on the GPS UART. So every default is
 * this board's real bus.
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
// Board peripherals (indices into g_ADigitalPinMap)

#define PIN_OKI_GPS_POWER    (5)    // P0.24 -> MOSFET Q1 -> GPS header ground (HIGH = on)
#define PIN_OKI_SPARE_GPIO38 (9)    // P1.06 -> "GPIO38" pad: the diag log mirror

////////////////////////////////////////////////////////////////////////////////
// UART pin definition -- the GPS header, NOT the ProMicro default (see header)

#define PIN_SERIAL1_TX       (3)    // P0.20 -> GPS RX
#define PIN_SERIAL1_RX       (4)    // P0.22 <- GPS TX

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition -- the SDA and SCL pads

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

#define PIN_LED              (22)   // P0.15: the module's LED, the heartbeat
#define LED_PIN              PIN_LED
// -1 so the Arduino core stops blinking P0.15 from a timer while BLE advertises (#1597).
#define LED_BLUE             (-1)
#define LED_BUILTIN          PIN_LED
// Active HIGH on the badge's ProMicro modules (#1382). Clone modules differ, and no
// OKIMesh board has been checked. An inverted one shows the heartbeat lit most of the time.
#define LED_STATE_ON         1

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1          (6)
#define BUTTON_PIN           PIN_BUTTON1
