#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#include <math.h>

MeshsmithPhotonC6Board board;
#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
#endif

static SPIClass spi(0);
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

// #1054: declared, not discovered. This board has no RTC chip -- the only I2C
// device on it is the MAX17048 fuel gauge (0x36) -- so the four-address probe
// AutoDiscoverRTCClock walked at boot could only ever fail, or worse, ACK a
// non-RTC part and serve garbage as the time (lilygo_techo_card, 0x68). The
// ESP32 system clock is what every getCurrentTime() was already returning.
ESP32RTCClock rtc_clock;

#if ENV_INCLUDE_GPS
  #ifndef GPS_SERIAL
    #define GPS_SERIAL Serial1
  #endif
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  #if defined(MESHSMITH_PHOTON_ATGM336H_GPS) && MESHSMITH_PHOTON_ATGM336H_GPS
    #include "ATGM336HLocationProvider.h"
    ATGM336HLocationProvider nmea = ATGM336HLocationProvider(GPS_SERIAL, &rtc_clock);
  #else
    MicroNMEALocationProvider nmea = MicroNMEALocationProvider(GPS_SERIAL, &rtc_clock);
  #endif
  PhotonSensorManager sensors = PhotonSensorManager(nmea);
#else
  PhotonSensorManager sensors;
#endif

bool radio_init() {
  rtc_clock.begin();   // #1054: no bus probe -- see declaration above

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
  return mesh::LocalIdentity(&rng);
}

bool PhotonSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  EnvironmentSensorManager::querySensors(requester_permissions, telemetry);

  if ((requester_permissions & TELEM_PERM_BASE) == 0) {
    return true;
  }

  float charge_rate_pct_per_hour = board.getBattChargeRatePctPerHour();
  if (!isnan(charge_rate_pct_per_hour)) {
    telemetry.addCurrent(TELEM_CHANNEL_BATTERY_CHARGE_RATE, charge_rate_pct_per_hour);
  }

  return true;
}
