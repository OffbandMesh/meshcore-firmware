#include <Arduino.h>
#include "target.h"

HeltecRCC6Board board;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
#endif

// SPI host 0 (FSPI on C6). The LoRa bus is the only SPI peripheral configured on
// this variant -- I2C is disabled outright (see platformio.ini) so nothing
// contends for it.
static SPIClass spi(0);
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

bool radio_init() {
  fallback_clock.begin();

  // rtc_clock.begin(Wire) is deliberately NOT called here.
  //
  // A blind I2C bus scan wedges the ESP32-C6 peripheral: it hangs at address
  // 0x0d and never returns, so setup() never reaches loop() and the board
  // presents as a silent brick (#294, the reason ENV_SKIP_I2C_SENSOR_SCAN
  // exists). This variant has no I2C devices -- PIN_BOARD_SDA/SCL are -1 -- so
  // there is nothing to discover and every reason not to go looking.
  //
  // If an I2C device is ever added to this board, probe its known address
  // directly. Do not re-enable a scan.

  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  return radio.std_init(&spi);
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(int8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
