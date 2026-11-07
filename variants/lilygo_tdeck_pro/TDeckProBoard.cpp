#include <Arduino.h>
#include "TDeckProBoard.h"

void TDeckProBoard::begin() {
  ESP32Board::begin();

  // The radio, the e-paper panel and the micro-SD share one SPI bus. Every
  // chip-select is driven high before anything talks on it, so a device whose
  // CS floats can't answer while another one is being read.
  for (int cs : {P_LORA_NSS, PIN_DISPLAY_CS, PIN_SD_CS}) {
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
  }

  // The SX1262 runs from its own regulator (RT9080, schematic sheet 1), which
  // stays off until LORA_EN is high.
  pinMode(P_LORA_EN, OUTPUT);
  digitalWrite(P_LORA_EN, HIGH);

  // UART0 TX (GPIO43) also feeds the GPS's RX, and the boot ROM and the log
  // mirror both transmit on it. The GPS is kept powered, as LilyGo's own
  // firmware does, so that pin never drives an unpowered module.
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, HIGH);

  // Not used by this build: the 4G modem, the panel's front-light and the
  // keyboard backlight.
  for (int off : {PIN_MODEM_EN, PIN_DISPLAY_FRONTLIGHT, PIN_KEYBOARD_LED}) {
    pinMode(off, OUTPUT);
    digitalWrite(off, LOW);
  }
}
