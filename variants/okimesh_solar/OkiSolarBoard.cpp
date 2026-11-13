#include <Arduino.h>
#include <hal/nrf_lpcomp.h>
#include "OkiSolarBoard.h"

// The low-voltage policy starts with SafeBoot, which can't see this board's headers:
// without these flags it compiles out and the boot gate does nothing.
#if !defined(SAFEBOOT_PIN_VBAT_READ) || !defined(SAFEBOOT_ADC_MULTIPLIER) || !defined(SAFEBOOT_ADC_SAMPLE_US)
#error "OKIMesh Solar: set SAFEBOOT_PIN_VBAT_READ, SAFEBOOT_ADC_MULTIPLIER and SAFEBOOT_ADC_SAMPLE_US"
#endif
static_assert(SAFEBOOT_PIN_VBAT_READ == PIN_VBAT_READ, "SafeBoot must read the board's battery pin");
static_assert(SAFEBOOT_ADC_MULTIPLIER - qcc::kDividerRatio < 0.0005f &&
              qcc::kDividerRatio - SAFEBOOT_ADC_MULTIPLIER < 0.0005f,
              "SAFEBOOT_ADC_MULTIPLIER must equal the divider ratio");
static_assert(SAFEBOOT_ADC_SAMPLE_US == qcc::kAdcSampleUs, "SafeBoot must sample like the board");

// #1688's wake source, the same line as the badge on the same divider.
#if !defined(BATTERY_LPCOMP_INPUT) || !defined(BATTERY_LPCOMP_THRESHOLD)
#error "OKIMesh Solar: set BATTERY_LPCOMP_INPUT and BATTERY_LPCOMP_THRESHOLD"
#endif
static_assert(BATTERY_LPCOMP_INPUT == NRF_LPCOMP_INPUT_7, "the battery pin, P0.31, is AIN7");
static_assert(BATTERY_LPCOMP_THRESHOLD == NRF_LPCOMP_REF_SUPPLY_11_16,
              "the wake line must match qcc::kLpcompWakeMv");
static_assert(qcc::kLpcompWakeMv > DEFAULT_SAFE_BOOT_WAKE_MV && qcc::kLpcompWakeMv < BATT_MAX_MILLIVOLTS,
              "the wake line must be above WAKE_MV and below where a charged cell settles");

// Owner, 2026-11-02: one line for this board. A board that trips the cutoff restarts into
// SafeBoot's sleep instead of booting again, so the two lines can be equal here.
static_assert(AUTO_SHUTDOWN_MILLIVOLTS == DEFAULT_SAFE_BOOT_SLEEP_MV,
              "the running cutoff and the refuse-to-boot line are one number on this board");
static_assert(DEFAULT_SAFE_BOOT_SLEEP_MV <= DEFAULT_SAFE_BOOT_WAKE_MV, "sleep must not exceed wake");

uint16_t OkiSolarBoard::getBattMilliVolts() {
  analogReference(AR_INTERNAL);
  analogSampleTime(qcc::kAdcSampleUs);
  analogReadResolution(qcc::kAdcBits);

  uint32_t raw = 0;
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    raw += analogRead(PIN_VBAT_READ);
  }
  raw /= BATTERY_SAMPLES;
  return qcc::scaleToMilliVolts(adc_mult, raw);  // adc_mult is user-settable
}

// PromicroBoard resets to its own 1.815; this board resets to its divider.
bool OkiSolarBoard::setAdcMultiplier(float multiplier) {
  adc_mult = (multiplier == 0.0f) ? qcc::kBattMvPerCount : multiplier;
  return true;
}

float OkiSolarBoard::getAdcMultiplier() const {
  return (adc_mult == 0.0f) ? qcc::kBattMvPerCount : adc_mult;
}
