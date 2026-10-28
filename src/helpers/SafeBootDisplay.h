#pragma once

// #1889: SafeBoot puts an always-powered SSD1306 to sleep before anything else happens.
//
// An SSD1306 on I2C with no reset line and no power switch is powered whenever the board
// is, and it keeps its own copy of the picture: an MCU reset leaves the last frame on the
// glass until the firmware starts the display again. SafeBoot runs before that, so a board
// SafeBoot refuses would sleep with the panel lit and its charge pump running -- owner-seen
// on a QCC badge, 2026-10-28. A variant with such a display declares
// SAFEBOOT_SSD1306_I2C_ADDR; SafeBoot then sends the stream below first thing. The normal
// display start-up sends the full init sequence, display-on included, so a board that is
// allowed to boot loses nothing but the stale frame.
//
// SSD1306-specific on purpose: 0x8D is the SSD1306 charge-pump command, and on an SH1106
// the same byte means something else. A different controller gets its own stream.

#include <stdint.h>

namespace offband {

constexpr uint8_t kSsd1306ControlCommands = 0x00;  // control byte: commands follow
constexpr uint8_t kSsd1306DisplayOff = 0xAE;
constexpr uint8_t kSsd1306ChargePump = 0x8D;
constexpr uint8_t kSsd1306ChargePumpOff = 0x10;

constexpr uint8_t kSsd1306SleepStream[] = {
    kSsd1306ControlCommands, kSsd1306DisplayOff, kSsd1306ChargePump, kSsd1306ChargePumpOff,
};

// A 7-bit I2C address outside the reserved ranges.
constexpr bool validI2cAddress(int a) { return a >= 0x08 && a <= 0x77; }

// The Adafruit nRF52 TwoWire has no timeout: a line held low never raises LASTTX, ERROR or
// STOPPED, and endTransmission() spins forever. SafeBoot runs before any watchdog, so a
// hang there would stop the battery check itself. So the bus is only used when both lines
// idle high under their pull-ups. A device left holding SDA low by a reset mid-transfer is
// released by clocking SCL up to nine times (the standard bus clear); SCL held low cannot
// be cleared from here, and the display is left alone rather than risk the hang.
constexpr int kI2cRecoveryClocks = 9;
constexpr bool i2cBusIdle(int sda_level, int scl_level) { return sda_level == 1 && scl_level == 1; }
constexpr bool i2cBusRecoverable(int sda_level, int scl_level) { return sda_level == 0 && scl_level == 1; }

}  // namespace offband
