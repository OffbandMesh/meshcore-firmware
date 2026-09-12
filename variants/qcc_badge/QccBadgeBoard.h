#pragma once

#include "../promicro/PromicroBoard.h"
#include "QccBattery.h"

// QCC 0x4 badge (#1172). Radio wiring, button and I2C setup are PromicroBoard's; the
// badge differs in its battery divider and its identity.
class QccBadgeBoard : public PromicroBoard {
public:
  // NRF52Board is a virtual base of NRF52BoardDCDC, so the most-derived class names
  // the OTA identity; PromicroBoard's initializer for it does not run.
  QccBadgeBoard() : NRF52Board("QCC_Badge_OTA") { adc_mult = qcc::kBattMvPerCount; }

  uint16_t getBattMilliVolts() override;
  bool setAdcMultiplier(float multiplier) override;
  float getAdcMultiplier() const override;
  const char* getManufacturerName() const override { return "QCC 0x4 Badge"; }
};
