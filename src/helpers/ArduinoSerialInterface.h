#pragma once

#include "BaseSerialInterface.h"
#include <Arduino.h>

class ArduinoSerialInterface : public BaseSerialInterface {
  bool _isEnabled;
  uint8_t _state;
  uint16_t _frame_len;
  uint16_t rx_len;
  Stream* _serial;
  uint8_t rx_buf[MAX_FRAME_SIZE];

public:
  ArduinoSerialInterface() { _isEnabled = false; _state = 0; }

  void begin(Stream& serial) { 
    _serial = &serial; 
  #ifdef RAK_4631
    pinMode(WB_IO2, OUTPUT);
  #endif  
  }

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  bool isWriteBusy() const override;
  // #718 CONTRACT: returns `len` on success, or 0 meaning THE FRAME WAS NOT SENT and
  // nothing was written to the wire. It never returns a short count -- a partial frame
  // would desync the length-prefixed protocol for every frame that follows.
  //
  // A caller that needs delivery MUST retry the same frame; a caller that fires and
  // forgets now silently drops the frame under back-pressure instead of corrupting the
  // stream, which is the better of the two failures but IS a behaviour change.
  //
  // REQUIRES the underlying Stream to implement availableForWrite() meaningfully.
  // The base Stream returns 0, which this reads as "no room" and would refuse forever;
  // every transport actually bound here (HWCDC, USBCDC, HardwareSerial, Adafruit USB
  // CDC) implements it. Do not bind a Stream that does not.
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  // #411: shared with the console only when the framed protocol runs on `Serial`
  // itself (USB-serial companion). A dedicated-UART bridge (begin(companion_serial))
  // leaves the console Serial free.
  bool isConsoleSharedWithProtocol() const override {
    return _serial == static_cast<Stream*>(&Serial);
  }
};