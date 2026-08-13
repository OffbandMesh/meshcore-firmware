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
  virtual void loop() {};

  virtual bool isWriteBusy() const = 0;
  virtual size_t writeFrame(const uint8_t src[], size_t len) = 0;
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;

  // #411: does this transport carry its framed protocol on the same `Serial`
  // that the debug console uses? If so, the serial-capture mirror must stay off
  // to avoid corrupting the protocol line. Default false (most transports -- BLE,
  // WiFi/TCP, a dedicated UART -- leave the console Serial free).
  virtual bool isConsoleSharedWithProtocol() const { return false; }

  // #453: the largest FRAME (on-wire bytes) this transport can deliver atomically.
  // Serial/TCP fit the full MAX_FRAME_SIZE. BLE cannot: a notification carries only
  // the negotiated ATT MTU minus 3 header bytes, and the BLE stack SILENTLY CLIPS a
  // larger frame (dropping the tail -- #450). Builders that assemble variable-length
  // frames (caplog/config drains, GPS status) MUST cap their payload to
  // maxFrameSize() minus their own header bytes so nothing is clipped over BLE.
  // Default: the full frame; BLE interfaces override with the live MTU.
  virtual size_t maxFrameSize() const { return MAX_FRAME_SIZE; }

  // #453: payload bytes a builder may emit given `header_bytes` of fixed frame
  // prefix, i.e. maxFrameSize() - header, FLOORED AT 0 so it can never underflow
  // (a pathologically small MTU yields 0, not a wrapped-huge size_t). Builders that
  // chunk/cap variable data over BLE should size against this, not MAX_FRAME_SIZE.
  size_t maxFramePayload(size_t header_bytes) const {
    size_t m = maxFrameSize();
    return m > header_bytes ? m - header_bytes : 0;
  }
};
