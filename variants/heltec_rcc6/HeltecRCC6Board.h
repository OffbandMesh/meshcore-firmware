#pragma once

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <helpers/ESP32Board.h>

#ifndef ADC_MULTIPLIER
  #define ADC_MULTIPLIER 4.95f
#endif

// Heltec RadioCore RCC6 -- ESP32-C6 + RA62A-HF (SX1262), 16 MB flash.
//
// PIN PROVENANCE. Every pin below is read from the schematic's OWN printed
// net-to-GPIO table (docs/radiocore/vendor/RCC6/RCC6-L62_V1.0-schematic.pdf,
// beside the RA62A symbol U5) -- the vendor states the mapping directly on the
// sheet. It is NOT spatial row-pairing, which is a lead generator and produced
// wrong answers on this family before (#804). Independently matched against
// n30nex/NeonPocketMC-RCC6 (MIT) on all seven LoRa pins.
//
// STRAPPING-PIN OVERLAP -- read before blaming firmware for boot oddities.
// This board puts board functions on ESP32-C6 strapping pins: GPIO8 carries the
// LoRa RESET_N and GPIO15 the TFT SDA, with GPIO4/GPIO5/GPIO9 also strap-capable.
// That is the same class of hazard as #702 on the RC32, where three of four S3
// straps were repurposed and produced a reset-gesture-dependent boot failure.
// A boot symptom on this board is a strap suspect before it is a code suspect.
class HeltecRCC6Board : public ESP32Board {
protected:
  float adc_mult = ADC_MULTIPLIER;

public:
  void begin();
  uint16_t getBattMilliVolts() override;
  bool setAdcMultiplier(float multiplier) override;
  float getAdcMultiplier() const override { return adc_mult; }
  const char* getManufacturerName() const override;
};
