#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>

RC52Board board;

// LoRa lives on SPI0. std_init() below calls spi->setPins(MISO, SCLK, MOSI) on
// nRF52 before begin(), so the P_LORA_* pins in platformio.ini are what actually
// configure the bus.
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

// No GPS on this board -- the vendor BSP defines no GPS pins, so there is no
// MicroNMEALocationProvider here and nothing to gate on ENV_INCLUDE_GPS.
EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  // NullDisplayDriver takes no constructor argument (unlike the panel drivers on
  // sibling boards, which take a peripheral-power pin) -- there is no display
  // rail to gate on a headless build.
  DISPLAY_CLASS display;

  // USER button: P1.10, external pull-up, ACTIVE LOW -> reverse = true. The
  // external pull-up is why pulldownup is left at its default false.
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  rtc_clock.begin(Wire);

  // The FEM's LNA control line (P1.07) is SX126X_RXEN, and CustomSX1262::std_init()
  // picks that up and calls setRfSwitchPins() itself -- there is deliberately no
  // manual rf-switch wiring here. TX side is DIO2 (SX126X_DIO2_AS_RF_SWITCH), so
  // there is no TXEN pin on this board.
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
