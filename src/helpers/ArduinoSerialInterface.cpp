#include "ArduinoSerialInterface.h"
#include "../MeshLog.h"
#include "CdcConsoleFlush.h"   // #1093: ZLP terminator for 64-multiple frames on the HWCDC console

#if defined(OFFBAND_WRITEFRAME_REFUSAL_TRACE) && OFFBAND_WRITEFRAME_REFUSAL_TRACE
// #1299 step 3 (DIAGNOSE ONLY -- never ships). Read-only observation of the refusal
// condition: nothing here changes what writeFrame() decides or when.
static uint32_t s_wf_last_ok_ms = 0;   // millis() of the last ACCEPTED frame
static uint16_t s_wf_consec = 0;       // refusals in an unbroken run
static uint16_t s_wf_seq = 0;          // refusals this boot
#endif

#define RECV_STATE_IDLE        0
#define RECV_STATE_HDR_FOUND   1
#define RECV_STATE_LEN1_FOUND  2
#define RECV_STATE_LEN2_FOUND  3

void ArduinoSerialInterface::enable() {
  _isEnabled = true;
  _state = RECV_STATE_IDLE;
  // #1283 (D2): sample the capacity now, while nothing has been queued yet. Learning
  // it only from writeFrame() would leave a window where the gate is unarmed, and a
  // transport kept backlogged from boot might never open it at all -- the fix would
  // then silently do nothing. writeFrame() still raises this if a larger free level
  // is ever observed.
  {
    const int writable = _serial->availableForWrite();
    if (writable > _max_writable_seen) _max_writable_seen = writable;
  }
#if defined(OFFBAND_USJ_CONSOLE)
  // #1093: seed the "fully drained" reference with the known HWCDC ring capacity BEFORE any
  // frame is written, so it can never be under-learned under sustained load (writeFrame's
  // running max only ever raises it, e.g. for a larger custom ring). Without this seed a
  // stream that keeps the ring backlogged from boot could learn a too-low capacity and fire
  // the terminator a hair early; with it, the terminator fires only when the ring is truly
  // empty. See CdcConsoleFlush.h for why the capacity cannot be measured live at init.
  if (_zlp_ring_capacity < HWCDC_DEFAULT_TX_RING) {
    _zlp_ring_capacity = HWCDC_DEFAULT_TX_RING;
  }
#endif
}
void ArduinoSerialInterface::disable() {
  _isEnabled = false;
#if defined(OFFBAND_USJ_CONSOLE)
  _zlp_pending = false;   // #1093: drop any deferred terminator so a re-enable starts clean
#endif
}

bool ArduinoSerialInterface::isConnected() const { 
  return true;   // no way of knowing, so assume yes
}

bool ArduinoSerialInterface::isWriteBusy() const {
  // #1283 (D2). This returned false unconditionally, which made every pacing gate in
  // MyMesh::checkSerialInterface() inert on USB -- including the one whose comment
  // reads "don't spam the Serial Interface too quickly!". The contact stream
  // therefore emitted one frame per main-loop pass regardless of what the endpoint
  // could ship, and #1299 measured the result: the TX FIFO falling 2-5 bytes short
  // of a 151-byte frame, over and over, at ~2000 frames/s.
  //
  // The threshold is a MAX frame, not the frame in hand: this predicate has no
  // length argument, and a gate that let through a frame it turns out cannot fit
  // would be no gate at all.
  const int writable = _serial->availableForWrite();
  if (writable < 0) return false;   // transport cannot say; writeFrame's own check stands

  // Read-only: the capacity is learned in enable() and writeFrame(), which are
  // non-const and already read this value. Keeping the predicate pure avoids a
  // read-modify-write on a const path shared by four stream gates.
  //
  // Gate ONLY once this transport has demonstrated it can reach the threshold.
  // ArduinoSerialInterface also backs plain UART bridges, where the TX buffer is
  // commonly 64 or 128 bytes -- smaller than a max frame. Gating unconditionally
  // there would report busy forever and stall every stream permanently, trading
  // dropped contacts for a dead companion. Those transports keep the old behavior,
  // which D1 (#1301) already makes safe by retrying rather than discarding.
  const int need = (int)MAX_FRAME_SIZE + 3;
  if (_max_writable_seen < need) return false;

  return writable < need;
}

size_t ArduinoSerialInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    // frame is too big!
    return 0;
  }

  uint8_t hdr[3];
  hdr[0] = '>';
  hdr[1] = (len & 0xFF);  // LSB
  hdr[2] = (len >> 8);    // MSB

  // #718: ALL OR NOTHING. This transport is deliberately non-blocking on a companion
  // USB build -- main.cpp sets Serial.setTxTimeoutMs(0) (#149) so a host that stops
  // draining cannot stall the loop and starve BLE servicing. A non-blocking write
  // therefore returns SHORT when the TX FIFO is full.
  //
  // Dropping bytes is correct for the debug mirror and CORRUPTING here: this is a
  // length-prefixed protocol. Emitting the header and then a partial payload leaves
  // the receiver counting toward bytes that never arrive, so it consumes the NEXT
  // frame's header as filler and the stream desyncs. Observed over USB serial as a
  // caplog download delivering 545 of an announced 1067 bytes with a complete
  // '>' 0xB0 0x00 0xC4 0x02 header spliced inside a payload.
  //
  // So: refuse rather than truncate. Returning 0 without touching the wire keeps the
  // stream parseable and lets the caller retry the SAME frame on a later pass, which
  // is non-blocking in exactly the way #149 requires.
  const size_t frame_len = 3 + len;
  const int writable = _serial->availableForWrite();
  // #1283 (D2): raise the learned empty-buffer capacity that isWriteBusy() gates on.
  // Here rather than in the predicate: this path is non-const and already has the
  // value in hand.
  if (writable > _max_writable_seen) _max_writable_seen = writable;
  if (writable >= 0 && (size_t)writable < frame_len) {
#if defined(OFFBAND_WRITEFRAME_REFUSAL_TRACE) && OFFBAND_WRITEFRAME_REFUSAL_TRACE
    // #1299 step 3: measure WHY the FIFO is short of room. `avail` and `need` say
    // whether it is starved or merely a hair short; `since_ok` separates a producer
    // that outran the endpoint (~0 ms) from a host that stalled (tens of ms);
    // `consec` says whether refusals burst or scatter.
    //
    // Safe to log here: on a USB companion g_meshLogMirror is false, so
    // mesh_log_line() reaches the capture ring ONLY and never this FIFO. Confirmed
    // empirically -- a flat-out host reader saw zero stray bytes between frames.
    s_wf_seq++;
    s_wf_consec++;
    mesh_log_line(MLOG_BOOT,
                  "[wf] refuse#%u avail=%d need=%u since_ok=%lums consec=%u\n",
                  (unsigned)s_wf_seq, writable, (unsigned)frame_len,
                  (unsigned long)(millis() - s_wf_last_ok_ms), (unsigned)s_wf_consec);
#endif
    return 0;   // caller MUST NOT treat this as sent
  }

  // These two short-write branches should be RARE rather than impossible: capacity was
  // just checked, but availableForWrite() is a snapshot and nothing here holds a lock,
  // so a concurrent writer (the debug console shares this Stream on some roles) can
  // consume room in between. Returning 0 is still right -- the caller retries, and the
  // receiver's decoder resyncs on the next '>' rather than trusting a bad length.
  if (_serial->write(hdr, 3) != 3) {
    return 0;
  }
  const size_t n = _serial->write(src, len);
  if (n != len) {
    return 0;   // refused: nothing reached the wire, so leave any prior _zlp_pending intact
  }
#if defined(OFFBAND_USJ_CONSOLE)
  // #1093: the ESP32 USB-Serial-JTAG console (HWCDC) emits no terminating zero-length
  // packet after a transfer whose on-wire length is a multiple of the 64-byte USB packet
  // size, so such a frame holds host-side until the next write -- the mechanism #1044
  // fixed for text-console replies. We must NOT drain it inline here: flushSerialConsole()
  // blocks (Serial.flush + up-to-2 ms wait), which would regress the #149 non-blocking
  // contract on this hot path. Instead ARM a deferred terminator; loop() emits the ZLP
  // once the TX ring has fully drained, without ever waiting (see CdcConsoleFlush.h).
  //
  // `writable` is the ring free space BEFORE this write, so its running maximum is the
  // empty-ring capacity -- the level availableForWrite() returns to once every queued byte
  // has reached the host. loop() uses it as the "fully drained" mark.
  if (writable > _zlp_ring_capacity) {
    _zlp_ring_capacity = writable;
  }
  // This frame's bytes have already begun a new USB transfer, which terminates any prior
  // held 64-multiple frame on the host -- so SET (not OR) the pending state from THIS
  // frame: re-arm iff it is itself a 64-multiple on the shared USB-Serial-JTAG console,
  // else clear (it self-terminates and has superseded any prior hold). Back-to-back
  // 64-multiple frames arm once and are terminated by a single ZLP after the whole batch
  // drains. A dedicated-UART bridge companion (_serial != Serial) never arms. Verified: #1093.
  if (isConsoleSharedWithProtocol() && (frame_len % USB_FS_BULK_MAX_PACKET) == 0) {
    _zlp_pending = true;
  } else {
    _zlp_pending = false;
  }
#endif
#if defined(OFFBAND_WRITEFRAME_REFUSAL_TRACE) && OFFBAND_WRITEFRAME_REFUSAL_TRACE
  s_wf_last_ok_ms = millis();   // #1299: the clock `since_ok` is measured against
  s_wf_consec = 0;
#endif
  return n;
}

void ArduinoSerialInterface::loop() {
#if defined(OFFBAND_USJ_CONSOLE)
  // #1093: emit the deferred USB-Serial-JTAG terminator armed by writeFrame(), non-blocking.
  // Fire only once the TX ring has fully drained (availableForWrite() back to the learned
  // empty-ring capacity) AND the FIFO is writable -- then the last packet on the wire is the
  // frame's own tail, so the ZLP terminates it rather than an early chunk. This is correct
  // even for several 64-multiple frames written back to back (one ZLP after the whole batch
  // drains). Until drained this defers to the next pass; a stalled or not-yet-connected host
  // simply leaves the data in the ring, so availableForWrite() stays below capacity and no
  // ZLP is emitted -- the path cannot wedge (#149).
  //
  // availableForWrite() is the SAME call writeFrame() already makes every frame (#718); it
  // is reached here only while a terminator is pending (a brief window after a 64-multiple
  // frame), and on this single-writer companion its tx_lock is uncontended so it returns
  // without blocking. emitConsoleZlpIfReady() never waits.
  if (_zlp_pending && _zlp_ring_capacity > 0
      && _serial->availableForWrite() >= _zlp_ring_capacity
      && emitConsoleZlpIfReady()) {
    _zlp_pending = false;
  }
#endif
}

size_t ArduinoSerialInterface::checkRecvFrame(uint8_t dest[]) {
  pollUsbLifecycle();
  while (_serial->available()) {
    int c = _serial->read();
    if (c < 0) break;

    switch (_state) {
      case RECV_STATE_IDLE:
        if (c == '<') {
          _state = RECV_STATE_HDR_FOUND;
        }
        break;
      case RECV_STATE_HDR_FOUND:
        _frame_len = (uint8_t)c;   // LSB
        _state = RECV_STATE_LEN1_FOUND;
        break;
      case RECV_STATE_LEN1_FOUND:
        _frame_len |= ((uint16_t)c) << 8;   // MSB
        rx_len = 0;
        _state = _frame_len > 0 ? RECV_STATE_LEN2_FOUND : RECV_STATE_IDLE;
        break;
      default:
        if (rx_len < MAX_FRAME_SIZE) {
          rx_buf[rx_len] = (uint8_t)c;   // rest of frame will be discarded if > MAX
        }
        rx_len++;
        if (rx_len >= _frame_len) {  // received a complete frame?
          if (_frame_len > MAX_FRAME_SIZE) _frame_len = MAX_FRAME_SIZE;    // truncate
          memcpy(dest, rx_buf, _frame_len);
          _state = RECV_STATE_IDLE;  // reset state, for next frame
          if (!_first_frame_logged && isConsoleSharedWithProtocol()) {
            // #1072: the client's first request reached us. A connect that
            // fails without this line never delivered a command at all.
            _first_frame_logged = true;
            mesh_log_line(MLOG_BOOT, "[usb] first-frame cmd=0x%02X len=%u\n",
                          (unsigned)dest[0], (unsigned)_frame_len);
          }
          return _frame_len;
        }
    }
  }
  return 0;
}

// #1072: USB link lifecycle for the companion whose framed protocol runs on
// Serial itself, so a failed connect can be told apart: the host never
// attached, it attached and sent nothing, or it sent a first command. Lines go
// through mesh_log_line, which a USB companion keeps off Serial (#1087): they
// reach the ring and UART0 only.
//
// This runs on every loop pass against the live protocol port, so it uses
// only side-effect-free reads, and what "up" means depends on the port:
//  - ESP32 USB-Serial-JTAG (HWCDC: RC32, RCC6). The peripheral exposes no DTR.
//    HWCDC::isPlugged() is its SOF-based bus state: a host is enumerating the
//    port. (bool)Serial is deliberately not used: while the host is idle it
//    flushes the TX FIFO as a probe, i.e. it would act on the protocol stream.
//  - nRF52 TinyUSB CDC. Serial.dtr() is the host's DTR: the port is open.
//    (bool)Serial is not used: it yields whenever the host is closed.
//  - Anything else: no attach state is readable; only the first-frame line.
//
// Readings are debounced, with flaps counted rather than hidden, by
// UsbLinkTracker (see its header).
#if defined(ESP32) && ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  #define USB_LINK_UP()    HWCDC::isPlugged()
  #define USB_LINK_UP_TXT  "bus attached"
  #define USB_LINK_DN_TXT  "bus detached"
#elif defined(NRF52_PLATFORM) && defined(USE_TINYUSB)
  #define USB_LINK_UP()    (Serial.dtr() != 0)
  #define USB_LINK_UP_TXT  "host open"
  #define USB_LINK_DN_TXT  "host close"
#endif

void ArduinoSerialInterface::pollUsbLifecycle() {
#if defined(USB_LINK_UP)
  if (!isConsoleSharedWithProtocol()) return;   // a UART bridge has no USB state
  uint16_t flaps = 0;
  const UsbLinkTracker::Event ev = _usb_link.update(USB_LINK_UP(), millis(), &flaps);
  if (ev == UsbLinkTracker::NONE) return;
  // Re-arm the first-frame line whenever the link went down, including inside
  // a blip -- never on a clean UP: a client can send its first frame within
  // the settle time, before the attach is confirmed.
  if (ev != UsbLinkTracker::UP) _first_frame_logged = false;
  switch (ev) {
    case UsbLinkTracker::UP:        mesh_log_line(MLOG_BOOT, "[usb] " USB_LINK_UP_TXT " flaps=%u\n", (unsigned)flaps); break;
    case UsbLinkTracker::DOWN:      mesh_log_line(MLOG_BOOT, "[usb] " USB_LINK_DN_TXT " flaps=%u\n", (unsigned)flaps); break;
    case UsbLinkTracker::BLIP_UP:   mesh_log_line(MLOG_BOOT, "[usb] " USB_LINK_UP_TXT " (blip) flaps=%u\n", (unsigned)flaps); break;
    case UsbLinkTracker::BLIP_DOWN: mesh_log_line(MLOG_BOOT, "[usb] " USB_LINK_DN_TXT " (blip) flaps=%u\n", (unsigned)flaps); break;
    case UsbLinkTracker::UNSTABLE:  mesh_log_line(MLOG_BOOT, "[usb] link unstable flaps=%u\n", (unsigned)flaps); break;
    default: break;
  }
#endif
}
