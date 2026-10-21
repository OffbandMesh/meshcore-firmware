#include <Arduino.h>
#include "target.h"

TDeckBoard board;

#if defined(OFFBAND_GUI) && defined(P_LORA_SCLK)
  // Epic J #1434 (owner, 2026-10-21, option A): the display, micro-SD and
  // radio are three devices on SPI2 under ESP-IDF's bus lock. The display and
  // card use ESP-IDF's drivers, so the radio can't use Arduino's SPIClass on
  // the same peripheral; RadioLib reaches it through an ESP-IDF device
  // instead. The SPIClass is only there because ArduinoHal wants one: it is
  // never begun, so it never takes the pins.
  #include <helpers/radiolib/EspSharedSpiHal.h>
  static SPIClass s_unbegun_spi(FSPI);
  static EspSharedSpiHal s_radio_hal(s_unbegun_spi, SPI2_HOST, P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI,
                                     GUI_PANEL_WIDTH * 32 * 2);   // GuiPanel::begin()'s DMA size
  RADIO_CLASS radio = new Module(&s_radio_hal, P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#elif defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  Wire.begin(18, 8);

#if defined(OFFBAND_GUI) && defined(P_LORA_SCLK)
  return radio.std_init(nullptr);   // no Arduino SPI to begin: the HAL joins SPI2
#elif defined(P_LORA_SCLK)
  return radio.std_init(&spi);
#else
  return radio.std_init();
#endif
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}
