#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <Wire.h>
#include <helpers/NRF52Board.h>

#ifdef XIAO_NRF52

class MeshsmithPhotonNRFBoard : public NRF52BoardDCDC {
  static constexpr uint8_t FUEL_GAUGE_I2C_ADDRESS = 0x36;
  static constexpr uint8_t FUEL_GAUGE_VCELL_REGISTER = 0x02;
  static constexpr uint8_t FUEL_GAUGE_CRATE_REGISTER = 0x16;

  bool readFuelGaugeRegister(uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(FUEL_GAUGE_I2C_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
      return false;
    }

    if (Wire.requestFrom(FUEL_GAUGE_I2C_ADDRESS, (uint8_t)2) != 2) {
      return false;
    }

    value = (Wire.read() << 8) | Wire.read();
    return true;
  }

public:
  MeshsmithPhotonNRFBoard() : NRF52Board("XIAO_NRF52_OTA") {}
  void begin();

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED on
    #if defined(LED_BLUE)
       // turn off that annoying blue LED before transmitting
       digitalWrite(LED_BLUE, HIGH);
    #endif
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED off
    #if defined(LED_BLUE)
       // do it after transmitting too, just in case
       digitalWrite(LED_BLUE, HIGH);
    #endif
  }
#endif

  uint16_t getBattMilliVolts() override {
    // MAX17048 VCELL uses 78.125uV LSB units.
    uint16_t vcell = 0;
    if (readFuelGaugeRegister(FUEL_GAUGE_VCELL_REGISTER, vcell)) {
      return (uint32_t)(vcell * 5) / 64;
    }
    return 0;
  }

  float getBattChargeRatePctPerHour() {
    // MAX17048 CRATE is a signed 16-bit value with 0.208 %/hr LSB units.
    uint16_t crate = 0;
    if (!readFuelGaugeRegister(FUEL_GAUGE_CRATE_REGISTER, crate)) {
      return NAN;
    }

    return (float)((int16_t)crate) * 0.208f;
  }

  const char *getManufacturerName() const override {
    return MANUFACTURER_STRING;
  }
};

#endif
