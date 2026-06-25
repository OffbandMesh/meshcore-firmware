#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <MeshsmithPhotonNRFBoard.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

#define TELEM_CHANNEL_BATTERY_CHARGE_RATE TELEM_CHANNEL_SELF

class PhotonSensorManager : public EnvironmentSensorManager {
public:
  #if ENV_INCLUDE_GPS
  PhotonSensorManager(LocationProvider &location): EnvironmentSensorManager(location) {}
  #else
  PhotonSensorManager() = default;
  #endif

  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
};

#ifdef DISPLAY_CLASS
  #include <helpers/ui/NullDisplayDriver.h>
  #include <helpers/ui/MomentaryButton.h>
  extern DISPLAY_CLASS display;
  // extern MomentaryButton user_btn;
#endif

extern MeshsmithPhotonNRFBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern PhotonSensorManager sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
