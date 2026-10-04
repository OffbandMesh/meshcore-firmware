#pragma once

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <helpers/ESP32Board.h>
#include <helpers/RefCountedDigitalPin.h>
#include "LoRaFEMControl.h"

#ifndef ADC_MULTIPLIER
  #define ADC_MULTIPLIER (4.9f * 1.035f)
#endif

class HeltecV4R8Board : public ESP32Board {
protected:
  float adc_mult = ADC_MULTIPLIER;
  bool _tx_active = false;   // #1339: between onBeforeTransmit and onAfterTransmit
  bool _led_enabled = true;  // #1340 (#542): default on = today's behavior

public:
  RefCountedDigitalPin periph_power;
  LoRaFEMControl loRaFEMControl;

  HeltecV4R8Board() : periph_power(PIN_VEXT_EN, PIN_VEXT_EN_ACTIVE) { }

  void begin();
  void onBeforeTransmit(void) override;
  void onAfterTransmit(void) override;
  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1);
  void powerOff() override;
  uint16_t getBattMilliVolts() override;
  bool setAdcMultiplier(float multiplier) override {
    if (multiplier == 0.0f) {
      adc_mult = ADC_MULTIPLIER;
    } else {
      adc_mult = multiplier;
    }
    return true;
  }
  float getAdcMultiplier() const override { return adc_mult; }
  const char* getManufacturerName() const override;

  // External FEM LNA runtime control (`fem on/off` CLI, client toggle). #1339.
  bool setLoRaFemLnaEnabled(bool enable) override;
  bool canControlLoRaFemLna() const override;
  bool isLoRaFemLnaEnabled() const override;

  // Status/traffic (TX) LED runtime control (`led on/off` CLI). #542, #1340.
  bool setLedEnabled(bool on) override;
  bool canControlLed() const override;
  bool isLedEnabled() const override;
};
