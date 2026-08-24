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
//     T096/V4. The RC52's FEM is an HT-RA62A with a different control surface --
//     see the FEM/LNA capability note below.
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

  // ---------------------------------------------------------------------------
  // FEM/LNA runtime control -- the stock OFFBAND_CAP_FEM_LNA capability (#983).
  //
  // Same framework as T096/HV4 -- the three MainBoard virtuals, the
  // OFFBAND_CAP_FEM_LNA bit, the `fem on/off` CLI, the persisted radio_fem_rxgain
  // pref -- but a DIFFERENT physical effect under the hood, because the hardware
  // differs and this was an owner-approved divergence (2026-08-24):
  //
  //   T096/HV4 (KCT8103L): "off" pulls the LNA out of the RX path only. TX and
  //   module power are untouched.
  //
  //   RC52 (HT-RA62A): "off" cuts WHOLE-MODULE power (FEM_EN + VFEM_CTRL low) --
  //   TX and RX both. It does NOT touch the LNA/RXEN line (P1.07); that stays with
  //   RadioLib's rf-switch, which is what the earlier "can't do it" note got wrong
  //   -- it mapped the capability to RXEN instead of to the two board-owned power
  //   pins. Same label, different meaning; documented so `fem off` on RC52 is not
  //   read as "RX gain only".
  //
  // begin() brings the module up through setLoRaFemLnaEnabled(true), so boot is a
  // defined FEM-ON state before any app applies the (default-ON) pref.
  // ---------------------------------------------------------------------------
  bool canControlLoRaFemLna() const override { return true; }
  bool setLoRaFemLnaEnabled(bool enable) override;
  bool isLoRaFemLnaEnabled() const override { return _fem_lna_enabled; }

#ifdef NRF52_POWER_MANAGEMENT
protected:
  // #857: arm USB-attach wake before SYSTEMOFF -- "plug it in to bring it back".
  // Deliberately NOT LPCOMP; see the implementation for why that is impractical
  // on this board's FET-gated divider.
  void initiateShutdown(uint8_t reason) override;
#endif

private:
  // Mirrors the FEM module-power state so isLoRaFemLnaEnabled() can report it
  // without reading a write-only GPIO back. begin() sets it via the setter.
  bool _fem_lna_enabled = false;
};
