#pragma once

// Host stand-in for the ESP-IDF reset-reason API, for board-control tests
// (#1338). Tests set g_mock_reset_reason to the boot they simulate.

typedef enum {
  ESP_RST_UNKNOWN = 0,
  ESP_RST_POWERON = 1,
  ESP_RST_DEEPSLEEP = 8,
} esp_reset_reason_t;

inline esp_reset_reason_t g_mock_reset_reason = ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return g_mock_reset_reason; }
