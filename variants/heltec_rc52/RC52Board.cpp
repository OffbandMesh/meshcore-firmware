#include "RC52Board.h"

#include <Arduino.h>
#include <Wire.h>

#include <helpers/diagnostics/CrashLog.h>   // #857 boot-voltage self-report

#ifdef NRF52_POWER_MANAGEMENT
// #953 enabled NRF52_POWER_MANAGEMENT here for the RESETREAS capture; #857 then
// established the battery path well enough to arm boot protection too.
//
// checkBootVoltage() does two things: initPowerMgr(), which reads the reset
// reason captured pre-SystemInit, and the low-voltage boot gate. Both are wanted
// now. The gate was held off until the reading was corroborated -- see variant.h
// for the three-way comparison (RC52 4130 mV vs MAX17048 4147 vs INA219 4144,
// -0.36%) that settled it.
const PowerMgtConfig power_config = {
  // Unused on this board: only configureVoltageWake() reads them, and that is
  // reached only from a board-specific initiateShutdown() override, which RC52
  // does not have. See variant.h -- the board will not self-wake on recovery.
  .lpcomp_ain_channel = 0,
  .lpcomp_refsel      = 0,
  .voltage_bootlock   = PWRMGT_VOLTAGE_BOOTLOCK,
};
#endif

#ifdef NRF52_POWER_MANAGEMENT
// #857: the ONLY practical wake source on this board.
//
// The base NRF52Board::initiateShutdown() goes straight to SYSTEMOFF with no
// wake armed at all, which leaves a boot-locked board dead until someone finds
// the RST pin. This adds USB-attach wake and nothing else.
//
// WHY NOT LPCOMP / cell-recovery wake, which is what the other nRF52 boards use:
// it needs the battery voltage present at an LPCOMP pin while the board is off.
// P0.31 is AIN7 so the pin is capable, but RC52's divider is FET-GATED --
// [verified: rc52.pdf, 2026-08-23] VBAT -> Q3 (AO3401A, P-ch) -> R17 390K ->
// ADC_IN -> R18 100K -> GND, with Q3's gate held at VBAT by R13 (10K) and pulled
// down by Q2 (S9013) whose base is driven through R15 (1K).
//
// Holding that divider connected through SYSTEMOFF means holding Q2 on, which
// costs roughly (3.3 - 0.7) / 1K = 2.6 mA CONTINUOUSLY. A board that shut down
// to protect a depleted cell would then drain it at 2.6 mA while "off" -- faster
// than it could plausibly recover. Self-defeating, so it is not armed.
//
// T096 can afford LPCOMP because its divider has no gate at all and simply costs
// ~8 uA all the time. That is a hardware difference, not a firmware one.
//
// USB wake costs nothing standing. Exiting SYSTEMOFF is a reset, so plugging in
// restarts the node -- and since #953 the boot line names the reason it woke.
void RC52Board::initiateShutdown(uint8_t reason) {
  configureUsbWake();
  enterSystemOff(reason);
}
#endif

void RC52Board::begin() {
  NRF52Board::begin();

#ifdef NRF52_POWER_MANAGEMENT
  // Two jobs: initPowerMgr() surfaces RESETREAS to getResetReasonString() -- what
  // puts a real cause in the "[boot] <role> up; reset=..." line the mirror UART
  // carries (#953) -- AND the low-voltage boot gate.
  //
  // ⚠ THE GATE IS ARMED. PWRMGT_VOLTAGE_BOOTLOCK is 3300 mV (variant.h), so
  // this CAN shut the board down here. An earlier version of this comment said
  // "bootlock is 0, so this returns immediately"; that was true before #857 set
  // the threshold and was not updated when it did. Corrected 2026-08-25.
  //
  // ⚠ Consequence specific to this board: a false trip has no automatic escape.
  // initiateShutdown() below arms USB-attach wake ONLY -- no LPCOMP cell-recovery
  // wake, for the FET-gated-divider reason documented there -- so a board that
  // boot-locks stays locked until someone plugs in USB. The single-sample nature
  // of the check (a sag can read as a depleted cell) is tracked fleet-wide on
  // #982; do not "fix" it by zeroing the threshold, which trades a false trip for
  // a brownout boot loop on a genuinely flat cell.
  checkBootVoltage(&power_config);

  // #857: self-report the boot reading on the mirror UART, next to the reset
  // reason. This is the CROSS-CHECK, made permanent instead of one-shot.
  //
  // Why here and not in shared code: getBattMilliVolts() is an ADC read, and
  // adding one to crashLogStandardInit would change boot behaviour for every
  // board in the fleet to answer a question about this one.
  //
  // What it is for: #602's failure was GROSS -- a fully charged board reading as
  // dead, divider disabled or inverted -- not a percent-level error. Printing the
  // value every boot, on the wire the bench already reads, lets it be compared
  // against the MAX17048 + INA219 on the same battery (which agree to within
  // 0-2 mV) without a CLI query, a monitor, or a host. If it lands within tens of
  // mV, the path is proven and a bootlock threshold is safe to set.
  //
  // ADC_MULTIPLIER is 4.90, nominal from R17/R18 in the vendor schematic. At 1%
  // parts that spans ~4.82-4.98, and the nRF52 internal 3.0V reference adds ~2%,
  // so expect agreement to roughly +/-3.5% (~+/-145 mV at 4.15 V) -- NOT exact
  // agreement. Anything inside that band confirms the path.
  offband::crashLogf("[boot] vbat=%u mV (multiplier %.2f, nominal)",
                     (unsigned)getBattMilliVolts(), (double)ADC_MULTIPLIER);
#endif

  Wire.begin();

  // Bring the FEM up to a defined ON state. The powered-up sequence and all of
  // its evidence live in setLoRaFemLnaEnabled() now that the same path is a
  // runtime capability (#983) -- this is the boot default before any app applies
  // the persisted radio_fem_rxgain pref (which itself defaults ON).
  setLoRaFemLnaEnabled(true);
}

// FEM module power as the OFFBAND_CAP_FEM_LNA capability (#983). See RC52Board.h
// for why "enable" here means WHOLE-MODULE power (FEM_EN + VFEM_CTRL), not the
// RX-LNA-path toggle it is on T096/HV4 -- the RXEN/LNA line stays with RadioLib.
bool RC52Board::setLoRaFemLnaEnabled(bool enable) {
  if (enable) {
    // -------------------------------------------------------------------------
    // ORDER: raise the FEM rail FIRST, then assert FEM_EN. (#879 review gate.)
    //
    // This was the other way round until 2026-08-24, on the argument that
    // powering the module while its enable input floated was the greater risk.
    // That argument was tagged [hypothesis: untested] and it was the wrong call:
    // driving 3.3 V into a control input of an UNPOWERED chip forward-biases that
    // pin's ESD diode and back-powers the module through it. Unreliable start-up
    // is the mild outcome; pin degradation is the durable one. powerOff() (and
    // the disable path below) already do the correct inverse -- FEM_EN low, THEN
    // the rail down -- so the principle was applied on the way down and simply
    // missed on the way up.
    //
    // ⚠ THIS DIVERGES FROM n30nex, the only shipping RC52 implementation, which
    // asserts FEM_EN before VFEM_Ctrl. [hypothesis: DISPROVEN 2026-08-24] The
    // reorder was expected to recover the -6.9 dB TX deficit; a same-chain
    // re-measure came back +14.1 dBm, flat vs the +15.1 baseline, so the
    // power-order hypothesis is dead. The reorder stands on general
    // power-sequencing grounds only, not as a fix. See #931 / #858 / #975.
    //
    // VFEM_Ctrl drives EN (pin 3) of U14, a TLV75733PDBVR 3.3 V LDO, ACTIVE HIGH,
    // turning on VDD_FEM. Polarity DERIVED from the schematic, not a sibling
    // board: [verified: RC52-L62_V1.02, U14 = TLV75733PDBVR, VFEM_Ctrl -> EN].
    // FEM_EN is ACTIVE HIGH; no HT-RA62A datasheet exists (FEM is inside the
    // module), so its polarity is corroborated by n30nex, not derived
    // [verified: n30nex HeltecRC52Board.cpp, read 2026-08-20] -- a legitimate
    // suspect if RF behaves oddly.
    // -------------------------------------------------------------------------
    pinMode(RADIOCORE_VFEM_CTRL, OUTPUT);
    digitalWrite(RADIOCORE_VFEM_CTRL, HIGH);

    // The LDO must be up before its load's enable is driven. TLV75733PDBVR
    // start-up is hundreds of microseconds; 1 ms is generous and runs rarely.
    delay(1);

    pinMode(RADIOCORE_FEM_EN, OUTPUT);
    digitalWrite(RADIOCORE_FEM_EN, HIGH);
    delay(1);
  } else {
    // Inverse order: de-assert FEM_EN BEFORE dropping the rail, so a push-pull
    // output never holds 3.3 V into an unpowered input (the back-powering the
    // enable path above avoids). Same reasoning as powerOff().
    pinMode(RADIOCORE_FEM_EN, OUTPUT);
    digitalWrite(RADIOCORE_FEM_EN, LOW);

    pinMode(RADIOCORE_VFEM_CTRL, OUTPUT);
    digitalWrite(RADIOCORE_VFEM_CTRL, LOW);
  }

  _fem_lna_enabled = enable;
  return true;
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
  // Drop the FEM through the capability setter: FEM_EN low BEFORE the rail, so a
  // push-pull output never holds 3.3 V into an unpowered input for the sleep.
  // Single source of truth for the sequence -- see setLoRaFemLnaEnabled().
  setLoRaFemLnaEnabled(false);

  // Return the battery divider gate to its inactive sense.
  pinMode(ADC_CTRL, OUTPUT);
  digitalWrite(ADC_CTRL, ADC_CTRL_DISABLED);

  NRF52Board::powerOff();
}

const char* RC52Board::getManufacturerName() const {
  return "Heltec RC52";
}
