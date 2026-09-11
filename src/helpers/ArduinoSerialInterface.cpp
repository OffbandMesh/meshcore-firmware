#include "ArduinoSerialInterface.h"
#include "../MeshLog.h"

#define RECV_STATE_IDLE        0
#define RECV_STATE_HDR_FOUND   1
#define RECV_STATE_LEN1_FOUND  2
#define RECV_STATE_LEN2_FOUND  3

void ArduinoSerialInterface::enable() { 
  _isEnabled = true;
  _state = RECV_STATE_IDLE;
}
void ArduinoSerialInterface::disable() {
  _isEnabled = false;
}

bool ArduinoSerialInterface::isConnected() const { 
  return true;   // no way of knowing, so assume yes
}

bool ArduinoSerialInterface::isWriteBusy() const {
  return false;
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
  if (writable >= 0 && (size_t)writable < frame_len) {
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
  return n == len ? n : 0;
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
