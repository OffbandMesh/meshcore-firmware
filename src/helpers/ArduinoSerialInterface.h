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

  // #1093: deferred, non-blocking USB-Serial-JTAG terminator. A frame whose on-wire length
  // is a multiple of the 64-byte USB packet size is held host-side until the next write
  // (see CdcConsoleFlush.h). Rather than block writeFrame() draining it inline (which would
  // regress the #149 non-blocking contract), writeFrame() arms _zlp_pending and loop() emits
  // the terminating zero-length packet once the TX ring has fully drained (availableForWrite
  // back to _zlp_ring_capacity) and the FIFO is writable -- so the ZLP terminates the frame's
  // final packet, correctly even for several 64-multiple frames written back to back, and
  // never fires early (while the host is still draining, or is not yet connected -- the data
  // then sits in the ring and availableForWrite stays below capacity, so loop() defers).
  // _zlp_ring_capacity is the empty-ring free level (== ring capacity): seeded at enable()
  // from the known HWCDC default so it is never under-learned under sustained load, and
  // raised by the running max in writeFrame() if a larger ring is ever configured. loop()
  // treats availableForWrite() >= this as "ring fully drained". See CdcConsoleFlush.h.
  bool _zlp_pending;
  int  _zlp_ring_capacity;

public:
  ArduinoSerialInterface() { _isEnabled = false; _state = 0; _zlp_pending = false; _zlp_ring_capacity = 0; }

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

  // #1093: per-main-loop service (driven by MultiSerialInterface::loop). Emits the deferred
  // USB-Serial-JTAG terminator armed by writeFrame(), non-blocking. No-op on every other
  // transport and on non-USB-Serial-JTAG boards.
  void loop() override;

  // #411: shared with the console only when the framed protocol runs on `Serial`
  // itself (USB-serial companion). A dedicated-UART bridge (begin(companion_serial))
  // leaves the console Serial free.
  bool isConsoleSharedWithProtocol() const override {
    return _serial == static_cast<Stream*>(&Serial);
  }
};