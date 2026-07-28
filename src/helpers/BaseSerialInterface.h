#pragma once

#include <Arduino.h>

#define MAX_FRAME_SIZE  176   // +4 for transport codes (region scoping)

class BaseSerialInterface {
protected:
  BaseSerialInterface() { }

public:
  virtual void enable() = 0;
  virtual void disable() = 0;
  virtual bool isEnabled() const = 0;

  virtual bool isConnected() const = 0;

  virtual bool isWriteBusy() const = 0;
  virtual size_t writeFrame(const uint8_t src[], size_t len) = 0;
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;

  // #411: does this transport carry its framed protocol on the same `Serial`
  // that the debug console uses? If so, the serial-capture mirror must stay off
  // to avoid corrupting the protocol line. Default false (most transports -- BLE,
  // WiFi/TCP, a dedicated UART -- leave the console Serial free).
  virtual bool isConsoleSharedWithProtocol() const { return false; }
};
