#include <Arduino.h>
#include <hal/nrf_lpcomp.h>
#include "QccBadgeBoard.h"

// The badge's low-voltage policy (D2) starts with SafeBoot, which can't see this
// board's headers: without these flags it compiles out and the boot gate does nothing.
#if !defined(SAFEBOOT_PIN_VBAT_READ) || !defined(SAFEBOOT_ADC_MULTIPLIER) || !defined(SAFEBOOT_ADC_SAMPLE_US)
#error "QCC badge: set SAFEBOOT_PIN_VBAT_READ, SAFEBOOT_ADC_MULTIPLIER and SAFEBOOT_ADC_SAMPLE_US"
#endif
// SafeBoot reads the same pin before the board is up. One pin, one divider ratio and one
// acquisition time, or the boot gate and the runtime reading disagree.
static_assert(SAFEBOOT_PIN_VBAT_READ == PIN_VBAT_READ, "SafeBoot must read the board's battery pin");
static_assert(SAFEBOOT_ADC_MULTIPLIER - qcc::kDividerRatio < 0.0005f &&
              qcc::kDividerRatio - SAFEBOOT_ADC_MULTIPLIER < 0.0005f,
              "SAFEBOOT_ADC_MULTIPLIER must equal qcc::kDividerRatio");
static_assert(SAFEBOOT_ADC_SAMPLE_US == qcc::kAdcSampleUs, "SafeBoot must sample like the board");

// #1688: SafeBoot's wake source. Without it a refused badge cannot recover by itself.
#if !defined(BATTERY_LPCOMP_INPUT) || !defined(BATTERY_LPCOMP_THRESHOLD)
#error "QCC badge: set BATTERY_LPCOMP_INPUT and BATTERY_LPCOMP_THRESHOLD"
#endif
static_assert(BATTERY_LPCOMP_INPUT == NRF_LPCOMP_INPUT_7, "the badge's battery pin, P0.31, is AIN7");
static_assert(BATTERY_LPCOMP_THRESHOLD == NRF_LPCOMP_REF_SUPPLY_11_16,
              "the wake line must match qcc::kLpcompWakeMv");
static_assert(qcc::kLpcompWakeMv > DEFAULT_SAFE_BOOT_WAKE_MV && qcc::kLpcompWakeMv < BATT_MAX_MILLIVOLTS,
              "the wake line must be above WAKE_MV and below where a charged cell settles");

// #1246: 0% on the battery bar is the voltage SafeBoot refuses to start from, not the one
// a running badge dies at. Below SLEEP the badge will not come back; between SLEEP and
// WAKE it starts only on a clean fresh boot; it keeps running on the reserve below 0%
// down to AUTO_SHUTDOWN_MILLIVOLTS. One policy, three thresholds, and the bar reads the
// one a person can act on.
#if !defined(BATT_MIN_MILLIVOLTS) || !defined(DEFAULT_SAFE_BOOT_SLEEP_MV) || \
    !defined(AUTO_SHUTDOWN_MILLIVOLTS) || !defined(DEFAULT_SAFE_BOOT_WAKE_MV)
#error "QCC badge: set BATT_MIN_MILLIVOLTS with the three low-voltage thresholds"
#endif
static_assert(BATT_MIN_MILLIVOLTS == DEFAULT_SAFE_BOOT_SLEEP_MV,
              "0% on the badge's battery bar must be the voltage it will not start from");
static_assert(AUTO_SHUTDOWN_MILLIVOLTS < DEFAULT_SAFE_BOOT_SLEEP_MV &&
              DEFAULT_SAFE_BOOT_SLEEP_MV <= DEFAULT_SAFE_BOOT_WAKE_MV,
              "the low-voltage thresholds must stay ordered: shutdown < sleep <= wake");

uint16_t QccBadgeBoard::getBattMilliVolts() {
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

// PromicroBoard resets to its own 1.815; the badge resets to its divider.
bool QccBadgeBoard::setAdcMultiplier(float multiplier) {
  adc_mult = (multiplier == 0.0f) ? qcc::kBattMvPerCount : multiplier;
  return true;
}

float QccBadgeBoard::getAdcMultiplier() const {
  return (adc_mult == 0.0f) ? qcc::kBattMvPerCount : adc_mult;
}
