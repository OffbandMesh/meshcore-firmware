#pragma once

#include "BaseSerialInterface.h"
#include "UsbLinkTracker.h"
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

  // #1283 (D2): the largest free space this transport has ever reported, i.e. its
  // empty-buffer capacity. isWriteBusy() gates on a max-frame threshold ONLY once
  // this shows the threshold is reachable -- a UART bridge whose TX buffer is 64 or
  // 128 bytes could never satisfy it, and gating unconditionally would stall every
  // stream on those builds forever. Same learned-capacity idea as
  // _zlp_ring_capacity above (#1093).
  //
  // Learned in enable() and writeFrame(), which are non-const and already read
  // availableForWrite(); isWriteBusy() only READS it, so the predicate stays pure and
  // there is no read-modify-write on a const path. RESET in begin(), because that
  // rebinds the Stream -- carrying a 256-byte USB capacity onto a 64-byte UART would
  // gate it shut forever.
  int _max_writable_seen;

  // #1315: frames that were COMMITTED (header on the wire) but did not go out whole.
  // writeFrame has to return `len` for these, because a retry would write a second frame
  // behind a partial one -- so the return value cannot carry the loss. Adversarial review
  // fairly objected that this makes the transport hide a data-integrity failure from the
  // layer above. This counter is the answer that does not require changing a virtual with
  // six implementations: the loss is still reported, and a caller or diagnostic that cares
  // can see it. Monotonic, never reset -- a consumer samples it around an operation and
  // compares. Read it with committedLossCount().
  uint32_t _committed_loss_count = 0;

public:
  ArduinoSerialInterface() { _isEnabled = false; _state = 0; _zlp_pending = false; _zlp_ring_capacity = 0; _max_writable_seen = 0; }

  void begin(Stream& serial) {
    _serial = &serial;
    _max_writable_seen = 0;   // #1283 (D2): a new transport, so forget the old one's capacity
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
  // #718 CONTRACT, corrected by #1315: returns `len` when the frame is off the caller's
  // hands, or 0 meaning THE FRAME WAS NOT SENT and NOTHING reached the wire. It never
  // returns a short count -- a partial frame would desync the length-prefixed protocol
  // for every frame that follows.
  //
  // 0 means retryable, and ONLY the pre-commit refusal returns it. Once the 3-byte header
  // has reached the wire the frame is COMMITTED: the receiver is already counting toward
  // the length it declared, so writeFrame finishes the frame within a bounded window and
  // pads the tail if it must, then returns `len`. A committed frame therefore reports
  // success even when its payload is partly filler -- the loss is reported through the
  // log, not through the return value, because a caller that retried it would write a
  // second frame behind a partial one. That is the corruption #1315 removed; the previous
  // code returned 0 from those branches under a comment claiming nothing had been written.
  //
  // A caller that needs delivery MUST retry on 0; a caller that fires and forgets drops
  // the frame under back-pressure instead of corrupting the stream, which is the better
  // of the two failures but IS a behaviour change.
  //
  // REQUIRES the underlying Stream to implement availableForWrite() meaningfully.
  // The base Stream returns 0, which this reads as "no room" and would refuse forever;
  // every transport actually bound here (HWCDC, USBCDC, HardwareSerial, Adafruit USB
  // CDC) implements it. Do not bind a Stream that does not.
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  // #1315: how many committed frames have gone out incomplete. Monotonic. Sample it
  // around a stream to find out whether what you sent is what the client received --
  // writeFrame's return value cannot tell you, by design (see its contract above).
  uint32_t committedLossCount() const { return _committed_loss_count; }

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

private:
  // #1315: finishing a COMMITTED frame -- one whose header has already reached the wire,
  // so it can no longer be refused or retried. All three are bounded by a deadline the
  // caller computes ONCE for the whole frame, so a committed frame costs at most one
  // COMMITTED_COMPLETE_MS window however many calls it takes.
  //
  // pushBounded: keep writing `p[0..len)` until it is gone or the deadline passes.
  // padBounded: write `count` filler bytes, to honor a length the header already declared.
  // reportCommittedLoss: say a frame left malformed; refuses to log into _serial itself.
  size_t pushBounded(const uint8_t* p, size_t len, uint32_t deadline);
  size_t padBounded(size_t count, uint32_t deadline);
  void   reportCommittedLoss(const char* where, size_t on_wire, size_t declared);

  // #1072: USB link lifecycle on the log channel; see pollUsbLifecycle().
  void pollUsbLifecycle();
  UsbLinkTracker _usb_link;
  bool _first_frame_logged = false;  // re-armed whenever the link went down
};
