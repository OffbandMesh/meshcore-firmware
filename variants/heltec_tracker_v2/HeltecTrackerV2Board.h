#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>
#include "LoRaFEMControl.h"

class HeltecTrackerV2Board : public ESP32Board {

public:
  RefCountedDigitalPin periph_power;
  LoRaFEMControl loRaFEMControl;

  HeltecTrackerV2Board() : periph_power(PIN_VEXT_EN,PIN_VEXT_EN_ACTIVE) { }

  void begin();
  void onBeforeTransmit(void) override;
  void onAfterTransmit(void) override;
  void powerOff() override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override ;
  bool setLoRaFemLnaEnabled(bool enable) override;
  bool canControlLoRaFemLna() const override;
  bool isLoRaFemLnaEnabled() const override;

  // #298: external FEM LNA runtime control. Capability-gated -- the base MainBoard
  // reports "not supported", and this board overrides it because its KCT8103L FEM
  // exposes an independent LNA path (same as heltec_v4).

};
