#pragma once

#include <Arduino.h>

#include <helpers/sensors/MicroNMEALocationProvider.h>

class ATGM336HLocationProvider : public MicroNMEALocationProvider {
  static const unsigned long CONFIG_SETTLE_MS = 250;
  static const unsigned long COMMAND_DELAY_MS = 40;
  static const size_t CONFIG_COMMAND_COUNT = 2;

  unsigned long _next_command_at_ms = 0;
  size_t _next_config_command = CONFIG_COMMAND_COUNT;

  void sendQueuedCommand(const char *sentence) {
    _gps_serial->print(sentence);
    _gps_serial->print("\r\n");
    _next_command_at_ms = millis() + COMMAND_DELAY_MS;
  }

  void scheduleConfig() {
    _next_config_command = 0;
    _next_command_at_ms = millis() + CONFIG_SETTLE_MS;
  }

  void runDeferredCommands() {
    static const char *const config_commands[CONFIG_COMMAND_COUNT] = {
      "$PCAS02,1000*2E",              // 1 Hz fixes.
      "$PCAS03,1,0,0,0,1,0,0,0*02",  // GGA + RMC only.
    };

    if (_next_config_command >= CONFIG_COMMAND_COUNT) {
      return;
    }
    if ((long)(millis() - _next_command_at_ms) < 0) {
      return;
    }

    sendQueuedCommand(config_commands[_next_config_command++]);
  }

public:
  ATGM336HLocationProvider(Stream &ser, mesh::RTCClock *clock = NULL)
      : MicroNMEALocationProvider(ser, clock) {}

  void begin() override {
    MicroNMEALocationProvider::begin();
    scheduleConfig();
  }

  void loop() override {
    MicroNMEALocationProvider::loop();
    runDeferredCommands();
  }
};
