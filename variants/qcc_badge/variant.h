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
// #1597: -1, so the Arduino core stops driving this pin.
//
// The core toggles LED_BLUE from a FreeRTOS timer whenever BLE advertises. Aliased onto
// PIN_LED (P0.15), and #1596's probe measured what that pin actually is: the PYRAMID
// badge's three bright front LEDs, where the sponsor badge has only the small LED on the
// ProMicro module. Same pin, different parts -- which is why this stayed invisible, since
// on the sponsor badge the core's blink lands somewhere nobody looks. Because it is a
// timer rather than the main loop it won every contest with our own LED code: it kept
// flashing straight through the probe's all-dark phase, which is how it was found.
//
// -1 is the house pattern, not an invention: heltec_rc52, heltec_t096, heltec_t1 and
// heltec_mesh_solar all set it for exactly this reason and say so in their comments. The
// badge was the one nRF52 variant that aliased it onto a real pin instead.
//
// It is also board-local by construction, so nothing else in the tree changes and no
// build flag is needed to scope it.
#define LED_BLUE             (-1)   // stops Bluefruit blinking P0.15 during advertising
#define LED_BUILTIN          PIN_LED

// #1366: P0.15, the heartbeat pin. Named, because one image runs on two badges and "the
// LED" means different parts on each (#1596):
//
//   P0.15              sponsor badge: the small LED on the ProMicro module
//                      pyramid badge: the three bright front LEDs
//   PIN_QCC_MSG_LED    sponsor badge: the LED beside the display
//   (P0.08)            pyramid badge: the single LED over the Q
//
// The owner's split: P0.15 is the heartbeat, P0.08 is traffic and notifications.
//
// LED_BLUE above WAS inherited from the ProMicro variant, aliased onto this pin, and wrong
// about the colour -- the part on this module is red. It said "left alone because shared
// code keys off the name", which was true and was the wrong conclusion: the shared code
// that keys off it is the core's advertising blink, and four other nRF52 variants key off
// it to switch that OFF. Set to -1 above (#1597).
#define PIN_QCC_MODULE_LED   PIN_LED   // P0.15: module LED, or the pyramid's front three

// Active HIGH on the sponsor badge, verified by eye on the heartbeat (#1382). The schematic
// cannot show it, since the ProMicro is drawn as a module symbol and its LED is internal,
// and clone modules differ. If a badge's P0.15 LED is lit about nine tenths of the time
// instead of a ~1 Hz blip, it is inverted there. Every reader of LED_STATE_ON pairs it with
// PIN_STATUS_LED, which on this board is P0.15 alone (the MSG_LED carries its own polarity,
// which the schematic does prove: P0.08 drives a 2N7000 gate with the LED anode at VCC, so
// it is active HIGH).
#define LED_STATE_ON         1

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1          (6)
#define BUTTON_PIN           PIN_BUTTON1
