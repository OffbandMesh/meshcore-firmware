#pragma once

#include <stdint.h>

// Battery sense on the QCC 0x4 badge (#1172): R4 680k over R5 1M from the switched
// battery into P0.31, read against the nRF52 internal 0.6 V reference at gain 1/6
// (3.6 V full scale), 12-bit. Both the board and SafeBoot read through this ratio;
// QccBadgeBoard.cpp static_asserts that the env's SAFEBOOT_ADC_MULTIPLIER matches it.
namespace qcc {

constexpr float kDividerTopOhms = 680000.0f;
constexpr float kDividerBottomOhms = 1000000.0f;
constexpr float kDividerRatio = (kDividerTopOhms + kDividerBottomOhms) / kDividerBottomOhms;
constexpr float kAdcFullScaleMv = 3600.0f;
constexpr int kAdcBits = 12;
constexpr float kBattMvPerCount = kDividerRatio * kAdcFullScaleMv / (float)(1 << kAdcBits);

// 680k || 1M is ~405k of source impedance; the core's 3 us default under-reads it.
constexpr unsigned kAdcSampleUs = 40;

// raw_avg is an average of 12-bit reads (0..4095), rounded to the nearest mV.
constexpr uint16_t battMilliVolts(uint32_t raw_avg) {
  return (uint16_t)(kBattMvPerCount * (float)raw_avg + 0.5f);
}
static_assert(battMilliVolts((1u << kAdcBits) - 1) < 65535u, "the ADC range must fit in uint16_t");

}  // namespace qcc
