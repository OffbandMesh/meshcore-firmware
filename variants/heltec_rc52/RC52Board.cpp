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
  // FEM_EN: pulled to a DEFINED level, but not actively asserted.
  //
  // Unlike VFEM_Ctrl, FEM_EN is not an LDO enable whose polarity a part number
  // settles -- the schematic routes it to pin 11 of the HT-RA62A module (U6), and
  // Heltec publishes no datasheet for that module (schematic only; RC52/datasheet/
  // and RC52/pinmap/ both 404). So its asserted sense is UNESTABLISHED and this
  // scaffold must not guess it.
  //
  // [hypothesis: untested] The pulldown below rests on the general argument that
  // a floating CMOS input can sit at an intermediate voltage and hold the
  // receiving buffer partly conducting, wasting current. That is textbook
  // reasoning, NOT a measurement of THIS module: there is no HT-RA62A datasheet,
  // no current figure, and nothing has been scoped on this pin. It is also not
  // known whether the module biases the pin internally, which would make the
  // pulldown either redundant or a fight against an internal pull-up.
  //
  // What would settle it (#858): scope P0.26 at boot with the pulldown removed to
  // see whether it actually floats, and measure module supply current in both
  // states. If it does not float, this line should be deleted rather than kept.
  //
  // INPUT_PULLDOWN, if the reasoning holds, gives a defined level without this
  // code asserting that LOW means "enabled" -- it may well mean enabled, and that
  // is also #858's to establish.
  //
  // Consequence to expect: RF performance on this headless scaffold is NOT
  // characterised and should not be measured or trusted until #858 lands. The
  // scaffold exists to build and to boot (#855), not to be an RF reference.
  // ---------------------------------------------------------------------------
  pinMode(RADIOCORE_FEM_EN, INPUT_PULLDOWN);

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
