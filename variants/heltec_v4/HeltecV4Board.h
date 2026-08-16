#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>
#include "LoRaFEMControl.h"

#ifndef ADC_MULTIPLIER
  #define ADC_MULTIPLIER 5.42
#endif

class HeltecV4Board : public ESP32Board {

protected:
  float adc_mult = ADC_MULTIPLIER;
  bool _led_enabled = true;   // #542: default on = today's behavior

public:
  RefCountedDigitalPin periph_power;
  LoRaFEMControl loRaFEMControl;
  HeltecV4Board() : periph_power(PIN_VEXT_EN,PIN_VEXT_EN_ACTIVE) { }

  void begin();
  void onBeforeTransmit(void) override;
  void onAfterTransmit(void) override;
  void powerOff() override;
  bool setLoRaFemLnaEnabled(bool enable) override;
  bool canControlLoRaFemLna() const override;
  bool isLoRaFemLnaEnabled() const override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override;

  // External FEM LNA runtime control (used by `fem on/off` CLI + telemetry).

  // Status/traffic (TX) LED runtime control (`led on/off` CLI). #542.
  bool setLedEnabled(bool on) override;
  bool canControlLed() const override;
  bool isLedEnabled() const override;

  bool setAdcMultiplier(float multiplier) override {
    if (multiplier == 0.0f) {
      adc_mult = ADC_MULTIPLIER;
    } else {
      adc_mult = multiplier;
    }
    return true;
  }
  float getAdcMultiplier() const override { return adc_mult; }
};
