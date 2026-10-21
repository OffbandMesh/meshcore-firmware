#pragma once

// Host stand-in for src/helpers/ESP32Board.h, for board-control tests (#1338).
// It keeps only what a variant board class inherits and calls: startup_reason,
// begin(), getStartupReason(), reboot() and powerOff(). The real header pulls
// in Wire, the ROM RTC and the task watchdog, none of which exist on the host.
// This directory is searched before src/, so variant code that includes
// <helpers/ESP32Board.h> compiles against this file in the test env only.

#include <MeshCore.h>
#include <Arduino.h>
#include "esp_system.h"
#include "esp_sleep.h"
#include <driver/rtc_io.h>

inline int g_mock_esp32_poweroff_calls = 0;

class ESP32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason = BD_STARTUP_NORMAL;

public:
  void begin() {
    startup_reason = BD_STARTUP_NORMAL;
#ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
#endif
  }

  uint8_t getStartupReason() const override { return startup_reason; }
  void reboot() override {}

  // The real ESP32Board::powerOff() clears every wake source and enters deep
  // sleep; the host only counts that it was reached.
  void powerOff() override { g_mock_esp32_poweroff_calls++; }
};
