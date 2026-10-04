#pragma once

#include <cstdint>

// Host stand-in for the RTC GPIO API, for board-control tests (#1338).
// g_mock_rtc_hold[pin] records whether a pin's level is held through sleep.

typedef int gpio_num_t;

typedef enum {
  RTC_GPIO_MODE_INPUT_ONLY = 0,
  RTC_GPIO_MODE_OUTPUT_ONLY = 1,
} rtc_gpio_mode_t;

inline bool g_mock_rtc_hold[64] = {};

inline int rtc_gpio_hold_en(gpio_num_t pin) { if (pin >= 0 && pin < 64) g_mock_rtc_hold[pin] = true; return 0; }
inline int rtc_gpio_hold_dis(gpio_num_t pin) { if (pin >= 0 && pin < 64) g_mock_rtc_hold[pin] = false; return 0; }
inline int rtc_gpio_set_direction(gpio_num_t, rtc_gpio_mode_t) { return 0; }
inline int rtc_gpio_pulldown_en(gpio_num_t) { return 0; }
inline int rtc_gpio_deinit(gpio_num_t) { return 0; }
inline int gpio_pulldown_en(gpio_num_t) { return 0; }
