#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// Heltec RadioCore RC52 -- nRF52840 + HT-RA62A (SX1262 + FEM).
//
// NRF52BoardDCDC (not plain NRF52Board): the schematic fits L2 and L4, both
// 10uH/80mA, against the nRF52840's DCC and DCCH pins -- i.e. both DC/DC
// converters are populated, so enabling them is correct and saves current.
// [verified: RC52-L62_V1.02 schematic, read 2026-08-19]
//
// Deliberately NOT here (each belongs to a later task, and inventing them now is
// exactly the failure mode #602 and #835 came from):
//
//   * No LoRaFEMControl. That class drives the KCT8103L's CSD/CTX pins on the
//     T096/V4. The RC52's FEM is an HT-RA62A whose LNA line IS the RadioLib RXEN
//     pin -- CustomSX1262::std_init() wires it from SX126X_RXEN on its own. FEM
//     characterisation is #858.
//   * No setLoRaFemLnaEnabled() / canControlLoRaFemLna() override. Claiming that
//     capability would mean taking RXEN away from RadioLib's rf-switch; that is a
//     design decision for #858, not a scaffold detail.
//   * No power-management / SafeBoot voltage gating. The battery divider ratio is
//     schematic-derived but UNMEASURED -- see variant.h and #857.
//   * No display. Headless by design (#854); the nRF52 display port is #872.

class RC52Board : public NRF52BoardDCDC {
public:
  RC52Board() : NRF52Board("RC52_OTA") {}

  void begin();

  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override;
  void powerOff() override;
};
