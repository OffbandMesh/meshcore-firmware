#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#include <math.h>

MeshsmithPhotonNRFBoard board;
#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  // MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  #if defined(MESHSMITH_PHOTON_LOW_ACTIVITY_GPS)
    #include "PhotonGPSLocationProvider.h"
    PhotonGPSLocationProvider nmea = PhotonGPSLocationProvider(Serial1, &rtc_clock);
  #else
    MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  #endif
  PhotonSensorManager sensors = PhotonSensorManager(nmea);
#else
  PhotonSensorManager sensors;
#endif

bool radio_init() {
    rtc_clock.begin(Wire);
  
    return radio.std_init(&SPI);
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

bool PhotonSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  EnvironmentSensorManager::querySensors(requester_permissions, telemetry);

  if ((requester_permissions & TELEM_PERM_BASE) == 0) {
    return true;
  }

  float charge_rate_pct_per_hour = board.getBattChargeRatePctPerHour();
  if (!isnan(charge_rate_pct_per_hour)) {
    // Reuse a standard signed Cayenne field so existing clients can decode it; the payload value remains %/hr.
    telemetry.addCurrent(TELEM_CHANNEL_BATTERY_CHARGE_RATE, charge_rate_pct_per_hour);
  }

  return true;
}
