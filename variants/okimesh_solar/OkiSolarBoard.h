#pragma once

#include "../promicro/PromicroBoard.h"
#include "../qcc_badge/QccBattery.h"

// OKIMesh Solar node (#1626). Radio wiring, button and I2C setup are PromicroBoard's. The
// battery divider is the QCC badge's 680k over 1M, so the battery math is shared with it.
class OkiSolarBoard : public PromicroBoard {
public:
  // NRF52Board is a virtual base of NRF52BoardDCDC, so the most-derived class names
  // the OTA identity; PromicroBoard's initializer for it does not run.
  OkiSolarBoard() : NRF52Board("OKIMesh_OTA") { adc_mult = qcc::kBattMvPerCount; }

  uint16_t getBattMilliVolts() override;
  bool setAdcMultiplier(float multiplier) override;
  float getAdcMultiplier() const override;
  const char* getManufacturerName() const override { return "OKIMesh Solar"; }
};
