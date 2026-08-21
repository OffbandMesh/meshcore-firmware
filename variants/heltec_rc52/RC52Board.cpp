#include "RC52Board.h"

#include <Arduino.h>
#include <Wire.h>

void RC52Board::begin() {
  NRF52Board::begin();

  Wire.begin();

  // ---------------------------------------------------------------------------
  // ORDER: settle FEM_EN before raising the FEM rail.
  //
  // [hypothesis: untested] The reasoning is that raising VFEM_Ctrl first leaves a
  // window in which the module is powered while its enable input is still
  // floating. That is an argument, not a measurement -- see the FEM_EN block
  // below for why the whole pulldown decision is unverified. The ordering costs
  // nothing either way, so it is kept, but it is NOT evidence-backed.
  // ---------------------------------------------------------------------------

  // ---------------------------------------------------------------------------
  // FEM_EN -- ACTIVE HIGH. Asserted here.
  //
  // Heltec publishes no HT-RA62A datasheet (schematic only; RC52/datasheet/ and
  // RC52/pinmap/ both 404), and the schematic cannot settle this either: the FEM
  // is INSIDE the module. U6 exposes FEM_EN on pin 11, VDD_FEM on 25 and
  // LNA_Ctrl on 36, and stops there.
  //
  // What settles it is a shipping implementation. n30nex/NeonPocketMC-RC52
  // (v1.1.0-rc.4) drives it HIGH in its board begin(), alongside VFEM_Ctrl HIGH:
  //
  //     digitalWrite(RADIOCORE_FEM_EN, HIGH);
  //     digitalWrite(RADIOCORE_VFEM_CTRL, HIGH);
  //
  // Its VFEM_Ctrl polarity matches this board's schematic derivation
  // independently, which is what makes the FEM_EN half credible rather than
  // merely present. [verified: n30nex HeltecRC52Board.cpp, read 2026-08-20]
  //
  // Corroboration by implementation, NOT by datasheet -- so if RF behaves oddly,
  // this line is a legitimate suspect. An earlier revision left the pin on
  // INPUT_PULLDOWN, asserting nothing; that was worse, because it left the FEM
  // in a state nobody had chosen.
  pinMode(RADIOCORE_FEM_EN, OUTPUT);
  digitalWrite(RADIOCORE_FEM_EN, HIGH);

  // ---------------------------------------------------------------------------
  // FEM supply rail -- raised only now that FEM_EN is at a defined level.
  //
  // VFEM_Ctrl drives the EN pin (pin 3) of U14, a TLV75733PDBVR -- a TI 3.3 V LDO
  // whose enable is ACTIVE HIGH. Driving it high turns on VDD_FEM, the front-end
  // module's supply. This polarity is DERIVED from the schematic, not copied from
  // a sibling board: [verified: RC52-L62_V1.02, U14 = TLV75733PDBVR, VFEM_Ctrl ->
  // EN]. #602 is what happens when a battery/enable polarity is inherited instead.
  // ---------------------------------------------------------------------------
  pinMode(RADIOCORE_VFEM_CTRL, OUTPUT);
  digitalWrite(RADIOCORE_VFEM_CTRL, HIGH);

  delay(1);
}

uint16_t RC52Board::getBattMilliVolts() {
  // The divider feeding BATTERY_PIN is gated by ADC_CTRL and is OFF at reset (R16
  // pulls the enable transistor low), so it costs no idle current -- enable it,
  // settle, sample, disable.
  //
  // Read at 12-bit against the internal 3.0 V reference, matching MV_LSB and the
  // established pattern on this repo's other nRF52 boards. variant.h declares
  // ADC_RESOLUTION 14 because the vendor BSP does; the read here is deliberately
  // 12-bit and the two are consistent, not contradictory.
  analogReadResolution(12);
  analogReference(AR_INTERNAL_3_0);

  pinMode(BATTERY_PIN, INPUT);
  pinMode(ADC_CTRL, OUTPUT);
  digitalWrite(ADC_CTRL, ADC_CTRL_ENABLED);

  delay(10);
  int adcvalue = analogRead(BATTERY_PIN);
  digitalWrite(ADC_CTRL, ADC_CTRL_DISABLED);

  // ADC_MULTIPLIER is the NOMINAL divider ratio (390K + 100K) / 100K = 4.90.
  // Unmeasured on hardware -- #857.
  return (uint16_t)((float)adcvalue * MV_LSB * ADC_MULTIPLIER);
}

void RC52Board::powerOff() {
  // Drop the FEM rail before sleeping so the LDO is not left enabled into a
  // low-power state.
  pinMode(RADIOCORE_VFEM_CTRL, OUTPUT);
  digitalWrite(RADIOCORE_VFEM_CTRL, LOW);

  // Return the battery divider gate to its inactive sense.
  pinMode(ADC_CTRL, OUTPUT);
  digitalWrite(ADC_CTRL, ADC_CTRL_DISABLED);

  NRF52Board::powerOff();
}

const char* RC52Board::getManufacturerName() const {
  return "Heltec RC52";
}
