#pragma once

#include <cstdint>
#include "esp_system.h"

// Host stand-in for the ESP-IDF sleep API, for board-control tests (#1338).
// Records which wake sources were armed and whether deep sleep was entered;
// esp_deep_sleep_start() returns here so the test can inspect the result.

typedef enum { ESP_PD_DOMAIN_RTC_PERIPH = 0 } esp_sleep_pd_domain_t;
typedef enum { ESP_PD_OPTION_OFF = 0, ESP_PD_OPTION_ON = 1 } esp_sleep_pd_option_t;
typedef enum { ESP_EXT1_WAKEUP_ANY_LOW = 0, ESP_EXT1_WAKEUP_ANY_HIGH = 1 } esp_sleep_ext1_wakeup_mode_t;

inline uint64_t g_mock_ext1_wake_mask = 0;
inline uint64_t g_mock_timer_wake_us = 0;
inline int g_mock_deep_sleep_starts = 0;
inline uint64_t g_mock_ext1_wakeup_status = 0;

inline int esp_sleep_pd_config(esp_sleep_pd_domain_t, esp_sleep_pd_option_t) { return 0; }
inline int esp_sleep_enable_ext1_wakeup(uint64_t mask, esp_sleep_ext1_wakeup_mode_t) { g_mock_ext1_wake_mask = mask; return 0; }
inline int esp_sleep_enable_timer_wakeup(uint64_t us) { g_mock_timer_wake_us = us; return 0; }
inline void esp_deep_sleep_start() { g_mock_deep_sleep_starts++; }
inline uint64_t esp_sleep_get_ext1_wakeup_status() { return g_mock_ext1_wakeup_status; }
