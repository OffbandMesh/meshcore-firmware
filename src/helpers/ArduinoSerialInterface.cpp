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

// #1315: the bounded window a COMMITTED frame may spend finishing itself, in ms.
//
// 2 ms is measured, not chosen by taste. #1299 instrumented every refusal on this
// transport and found the FIFO shortfall clears in UNDER A MILLISECOND every time
// (`since_ok=0-1ms`, `consec=1` -- the next pass always succeeded). 2 ms therefore
// covers the observed recovery with margin while matching the longest wait already
// accepted anywhere on this transport (flushSerialConsole's up-to-2 ms, #1093).
//
// This window opens ONLY after a header has reached the wire, which the capacity
// check makes rare. The common path stays exactly as non-blocking as #149 requires.
#define COMMITTED_COMPLETE_MS   2UL

// A spin whose only exit is a clock hangs if the clock ever stops. millis() is driven by
// an interrupt-backed counter and a stall is not expected -- but "not expected" is how a
// 2 ms best-effort window becomes a dead companion, and this path runs with a frame half
// on the wire. So every spin below ALSO counts attempts and gives up, turning a clock
// fault into one reported malformed frame instead of a lockup. 4096 non-progressing
// attempts is far beyond what 2 ms of real time can produce and costs nothing when the
// clock behaves (the deadline fires first, every time).
#define COMMITTED_SPIN_LIMIT    4096u

size_t ArduinoSerialInterface::pushBounded(const uint8_t* p, size_t len, uint32_t deadline) {
  size_t done = 0;
  unsigned idle = 0;
  while (done < len) {
    // write() is non-blocking here (main.cpp sets Serial.setTxTimeoutMs(0), #149), so a
    // full FIFO returns short rather than stalling -- that is what makes spinning on it
    // bounded by OUR clock instead of the host's. No delay()/yield(): handing the main
    // loop away mid-frame is precisely what must not happen while a frame is committed.
    const size_t w = _serial->write(p + done, len - done);
    done += w;
    if (done >= len) break;
    idle = w ? 0 : idle + 1;
    if (idle >= COMMITTED_SPIN_LIMIT) break;
    if ((int32_t)(millis() - deadline) >= 0) break;
  }
  return done;
}

size_t ArduinoSerialInterface::padBounded(size_t count, uint32_t deadline) {
  // Filler for the tail of a frame whose declared length we can no longer honor with real
  // data. Static const so it costs no stack on a path that is already degraded.
  //
  // 0xFF, NOT 0x00, and the difference is not cosmetic. If the payload short-wrote at
  // n == 0 the ENTIRE payload is filler, including byte 0 -- the response code. Zero
  // filler would present as RESP_CODE_OK (MyMesh.cpp:152 defines it as 0), so the client
  // would read a spurious SUCCESS reply to whatever it last asked. 0xFF is not a response
  // code at all (they run 0..22 plus the Offband 0xC0..0xC6 block), so a fully-filled
  // frame decodes as an unknown code the client ignores. Found by adversarial review.
  static const uint8_t FILLER[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  size_t done = 0;
  unsigned idle = 0;
  while (done < count) {
    const size_t want = (count - done) < sizeof(FILLER) ? (count - done) : sizeof(FILLER);
    const size_t w = _serial->write(FILLER, want);
    done += w;
    if (done >= count) break;
    idle = w ? 0 : idle + 1;
    if (idle >= COMMITTED_SPIN_LIMIT) break;
    if ((int32_t)(millis() - deadline) >= 0) break;
  }
  return done;
}

void ArduinoSerialInterface::reportCommittedLoss(const char* where, size_t on_wire,
                                                 size_t declared) {
  // NEVER let this line reach the FIFO we have just failed to write. mesh_log_line()'s
  // sink mirrors to Serial when g_meshLogMirror is set (MeshLog.cpp:199 writes it raw),
  // and on the roles where Serial IS the protocol line that splices console text into the
  // stream -- turning a one-frame loss into the desync this change exists to prevent.
  //
  // But suppress ONLY THE MIRROR, never the capture. An earlier revision returned early
  // here, which silenced the report exactly when the shared console was mirroring -- i.e.
  // in the case most likely to PRODUCE this failure, since the mirror's own
  // Serial.availableForWrite() check makes it the concurrent FIFO consumer that causes
  // the short write in the first place. Diagnostics that vanish when you turn diagnostics
  // on are worse than none. Found by adversarial review.
  //
  // Single-threaded main-loop path, and the flag is only read inside mesh_log_line's own
  // sink; the save/restore cannot be observed torn by anything that matters here.
  const bool suppress_mirror = isConsoleSharedWithProtocol() && g_meshLogMirror;
  if (suppress_mirror) g_meshLogMirror = false;

  // MLOG_ERROR, not MLOG_DEBUG: real data left the device malformed.
  //
  // This DOES reach a non-diag build, contrary to what this comment said before hardware
  // verification corrected it. Capture is off by default (#428), but the 0xC4 caplog
  // download is compiled unconditionally and the client can enable capture at runtime
  // (MyMesh.cpp:2394 and :2103), so the tester path is: turn capture on, reproduce,
  // download. What this line does NOT do is survive a reset -- the capture ring is plain
  // RAM. A loss that only matters after a reboot is #1316's crashlog sink, which is why
  // that one writes both rings. Deliberately NOT doing the same here: this is writeFrame's
  // hot path under the #149 non-blocking contract, and crashLogf takes a critical section.
  mesh_log_line(MLOG_ERROR,
                "[wf] COMMITTED FRAME LOST at %s: %u of %u bytes on the wire (#1315)\n",
                where, (unsigned)on_wire, (unsigned)declared);

  if (suppress_mirror) g_meshLogMirror = true;
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

  // #1315: PAST THIS POINT THE FRAME IS COMMITTED.
  //
  // A short write here is RARE rather than impossible: capacity was checked microseconds
  // ago, but availableForWrite() is a snapshot and nothing holds a lock, so a concurrent
  // writer can consume room in between -- the debug console shares this Stream on the
  // USB-serial companion roles (isConsoleSharedWithProtocol()).
  //
  // Both branches below used to `return 0` under a comment claiming nothing reached the
  // wire. That was FALSE, and the falsehood became load-bearing: #1301 taught every
  // stream caller to retry a 0, so a retry here wrote a whole frame BEHIND a partial one
  // -- the exact desync the #718 comment above warns about. The header carries the
  // length, so the moment it is out the receiver is already counting, and there is no
  // un-writing it.
  //
  // So: finish what we started inside ONE bounded window, and pad to the declared length
  // if the transport will not take the rest. A well-formed frame carrying garbage costs
  // the receiver ONE frame; a desync costs every frame after it, and we do not control
  // the client's decoder. That asymmetry is the whole argument. Owner decision 2026-10-02.
  //
  // Padding is BEST EFFORT, not a guarantee: it shares the one deadline, so it can only
  // fill from whatever room exists at that instant. A transport that takes nothing more
  // still leaves a short frame on the wire -- see the "wedged" native test. That residual
  // case is reported, not hidden, and it is strictly better than the pre-#1315 retry.
  const uint32_t deadline = millis() + COMMITTED_COMPLETE_MS;
  size_t on_wire = 0;   // bytes of THIS frame that actually reached the transport
  // Whether the RECEIVER gets the payload we were asked to send. Tracked as a flag rather
  // than inferred from on_wire, because a fully padded frame has on_wire == frame_len:
  // the right number of bytes, the wrong bytes. Keying the counter off the length missed
  // exactly the commonest degraded case.
  bool degraded = false;

  size_t hdr_done = _serial->write(hdr, 3);
  if (hdr_done != 3) {
    hdr_done += pushBounded(hdr + hdr_done, 3 - hdr_done, deadline);
  }
  on_wire = hdr_done;

  if (hdr_done == 0) {
    // NOTHING reached the wire, so the frame is not committed after all -- the transport
    // simply refused at the last moment (availableForWrite() said yes, write() said no).
    // This is the ordinary retryable refusal and must stay one: returning `len` here would
    // silently discard a frame that could still be delivered next pass, and bumping the
    // loss counter would blame a loss that did not happen.
    return 0;
  }

  if (hdr_done != 3) {
    // The transport took 1-2 header bytes and then would not take another for the whole
    // window. We can neither finish the frame nor retract the fragment, so the receiver
    // will mis-read the next frame's header. Nothing available here prevents that; the
    // only honest action is to say so and NOT make it worse with a second fragment.
    //
    // This path must NOT return early past the ZLP tail below. An earlier revision did,
    // and it stranded a PREVIOUS frame's deferred terminator: frame A (a 64-multiple) set
    // _zlp_pending, frame B failed here and skipped the tail, then frame C cleared
    // _zlp_pending as "superseded" -- so A's ZLP never fired and A sat in the host's USB
    // buffer indefinitely. A failed write of one frame silently un-delivered an unrelated
    // earlier one. Found by adversarial review; the tail now runs on every committed path.
    degraded = true;
    reportCommittedLoss("header", on_wire, frame_len);
  } else {
    size_t n = _serial->write(src, len);
    if (n != len) {
      n += pushBounded(src + n, len - n, deadline);
    }
    on_wire = 3 + n;
    if (n != len) {
      // Pad so the payload matches the length the header already declared. The receiver
      // gets a parseable frame whose tail is filler, discards or mis-renders that one
      // frame, and stays in sync for everything after it.
      const size_t filled = padBounded(len - n, deadline);
      on_wire = 3 + n + filled;
      degraded = true;
      reportCommittedLoss(filled == len - n ? "payload-padded" : "payload-short",
                          on_wire, frame_len);
    }
  }
  // A committed frame is consumed either way: the caller must never re-send it, so the
  // return below is `len` regardless of how much of it was real. The loss is visible
  // through the log and through _committed_loss_count, not through this return value --
  // because a caller that retried would write a second frame behind a partial one, which
  // is the corruption being removed. Callers that need to know whether the stream degraded
  // read the counter; see the header.
  if (degraded) _committed_loss_count++;
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
  //
  // #1315: keyed off `on_wire`, the bytes that ACTUALLY went out, not off `frame_len`.
  // On a degraded committed frame those differ, and the host terminates transfers by what
  // it received -- so a padded frame (on_wire == frame_len) decides exactly as before,
  // while a short one decides on its real length instead of a length that never arrived.
  if (isConsoleSharedWithProtocol() && (on_wire % USB_FS_BULK_MAX_PACKET) == 0) {
    _zlp_pending = true;
  } else {
    _zlp_pending = false;
  }
#endif
#if defined(OFFBAND_WRITEFRAME_REFUSAL_TRACE) && OFFBAND_WRITEFRAME_REFUSAL_TRACE
  s_wf_last_ok_ms = millis();   // #1299: the clock `since_ok` is measured against
  s_wf_consec = 0;
#endif
  // `len`, not the byte count: see the committed-frame note above. A committed frame is
  // always consumed, so the caller advances its cursor exactly once either way.
  return len;
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
