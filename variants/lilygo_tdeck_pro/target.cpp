#include <Arduino.h>
#include "target.h"

TDeckProBoard board;

// The radio and the e-paper panel are on the same SPI pins, and a pin routes
// to one SPI peripheral at a time. So the radio uses the panel driver's own
// SPIClass (FSPI; GxEPDDisplay.cpp, or EinkPanel.cpp in the GUI build) rather
// than a second one: whichever of the two begins it first, the other's
// begin() returns early and both share one bus.
extern SPIClass SPI1;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI1);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI1);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
