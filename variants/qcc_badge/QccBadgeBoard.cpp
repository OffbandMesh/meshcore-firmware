#include <Arduino.h>
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
