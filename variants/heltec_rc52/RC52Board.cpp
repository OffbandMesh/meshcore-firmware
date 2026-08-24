#include "RC52Board.h"

#include <Arduino.h>
#include <Wire.h>

#include "../../src/helpers/diagnostics/CrashLog.h"   // #857 boot-voltage self-report

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
  // Cannot shut down here: bootlock is 0, so this returns immediately after
  // initPowerMgr(). Present so RESETREAS reaches getResetReasonString(), which
  // is what puts a real cause in the "[boot] <role> up; reset=..." line the
  // mirror UART now carries (#953).
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

  // ---------------------------------------------------------------------------
  // ORDER: raise the FEM rail FIRST, then assert FEM_EN. (#879 review gate.)
  //
  // This was the other way round until 2026-08-24, on the argument that powering
  // the module while its enable input floated was the greater risk. That argument
  // was tagged [hypothesis: untested] and it was the wrong call: driving 3.3 V
  // into a control input of an UNPOWERED chip forward-biases that pin's ESD diode
  // and back-powers the module through it. Unreliable start-up is the mild
  // outcome; pin degradation is the durable one.
  //
  // Two things say the new order is the right one. Our own powerOff() already
  // does the correct inverse -- FEM_EN low, THEN the rail down (see below) -- so
  // the principle was applied on the way down and simply missed on the way up.
  // And a bench measurement now exists: rc52-bench-1 reads +15.1 dBm against a
  // +22 dBm chip setting with the FEM enabled, -6.9 dB, with the TX-power clamp
  // and IPEX4 seating both eliminated [verified: tinySA, owner-operated
  // 2026-08-23, see #963]. A FEM that never came up cleanly fits that sign and
  // rough magnitude.
  //
  // ⚠ THIS DIVERGES FROM THE ONLY SHIPPING IMPLEMENTATION WE HAVE. n30nex asserts
  // FEM_EN before VFEM_Ctrl (quoted below). The working hypothesis is that it
  // carries the same latent defect and nobody measured its conducted output --
  // that is a hypothesis, not a finding. [hypothesis: untested] Whether this
  // reordering recovers the missing 6.9 dB is UNPROVEN until the same rig
  // re-measures it. Tracked on #858 / #975.
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
  //
  // NOTE: the assertion itself now happens BELOW, after the rail is up. Only the
  // ordering changed; the polarity and its evidence are untouched.

  // ---------------------------------------------------------------------------
  // FEM supply rail -- raised FIRST, so the module is powered before any control
  // input is driven into it.
  //
  // VFEM_Ctrl drives the EN pin (pin 3) of U14, a TLV75733PDBVR -- a TI 3.3 V LDO
  // whose enable is ACTIVE HIGH. Driving it high turns on VDD_FEM, the front-end
  // module's supply. This polarity is DERIVED from the schematic, not copied from
  // a sibling board: [verified: RC52-L62_V1.02, U14 = TLV75733PDBVR, VFEM_Ctrl ->
  // EN]. #602 is what happens when a battery/enable polarity is inherited instead.
  // ---------------------------------------------------------------------------
  pinMode(RADIOCORE_VFEM_CTRL, OUTPUT);
  digitalWrite(RADIOCORE_VFEM_CTRL, HIGH);

  // The LDO needs to actually be up before we drive its load's enable pin.
  // TLV75733PDBVR start-up is specified in the hundreds of microseconds; 1 ms is
  // generous and this runs once at boot, so the cost is irrelevant.
  delay(1);

  // FEM_EN -- asserted now that VDD_FEM is live. See the ORDER block above.
  pinMode(RADIOCORE_FEM_EN, OUTPUT);
  digitalWrite(RADIOCORE_FEM_EN, HIGH);

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
  // ORDER: de-assert the FEM control BEFORE dropping its rail.
  //
  // The first version dropped VFEM_Ctrl and left FEM_EN driven HIGH for the
  // entire sleep -- a push-pull output holding 3.3 V into an unpowered input.
  // That forward-biases the input's ESD structure and can back-power the
  // module through a signal pin, for hours, on battery.
  //
  // This is the one sequencing question on this board with no counter-argument:
  // whichever power-UP order is correct, nothing defends leaving a control line
  // asserted into a dead rail. Found by the Gemini adversarial review, #879.
  pinMode(RADIOCORE_FEM_EN, OUTPUT);
  digitalWrite(RADIOCORE_FEM_EN, LOW);

  // [hypothesis: untested] The power-UP order in begin() -- FEM_EN settled
  // before the rail rises -- is deliberately NOT flipped to mirror this. The
  // same review argued for VFEM_Ctrl first on a general CMOS back-powering
  // argument. Two things stop that from being adopted: n30nex ships FEM_EN
  // first (v1.1.0-rc.4, corroborated 2026-08-20), and no HT-RA62A datasheet
  // exists to say whether the module's FEM_EN input is referenced to VDD_FEM
  // or to the main supply -- the FEM is INSIDE the module. Reversing it would
  // trade one untested hypothesis for another. Tracked under epic #929.

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
