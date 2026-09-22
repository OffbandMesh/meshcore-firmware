#pragma once
// #1035 -- the "right" flush for the ESP32 USB-Serial-JTAG console.
//
// On the USB-Serial-JTAG console (Arduino HWCDC), a reply whose on-wire length is an
// exact multiple of the 64-byte USB max packet size is emitted as full 64-byte packets
// with NO terminating short/zero-length packet. The host (e.g. Windows usbser.sys) then
// holds the final packet in its own buffer until the next write -- documented by
// Espressif as data getting "stuck in host memory" (ESP-IDF USB-Serial-JTAG console
// guide). It is host-side and intermittent; the bytes are delayed, not lost. See #1035.
//
// Draining the ring with Serial.flush() does NOT fix this -- HWCDC::flush() only empties
// the TX ring and never emits the terminating zero-length packet (ZLP). The fix is the
// Espressif-documented recipe from hal/usb_serial_jtag_ll.h:
//
//   "To send a zero-length packet, call usb_serial_jtag_ll_txfifo_flush() again when
//    usb_serial_jtag_ll_txfifo_writable() returns true."
//
// This is the same primitive the (unreleased) arduino-esp32 master HWCDC ISR uses; we
// invoke it from our code at the console-reply boundary instead of waiting for the
// framework. On transports without USB-Serial-JTAG this degrades to a plain flush.
//
// A ZLP is preferred over appending a byte: a trailing character would change the exact
// reply bytes a host tool matches against; a zero-length packet only terminates the USB
// transfer and leaves the payload byte-for-byte unchanged.
#include <Arduino.h>

// Gate on the chip HAVING the USB-Serial-JTAG peripheral AND `Serial` actually being its
// USB-CDC console (ARDUINO_USB_CDC_ON_BOOT). A board can have the peripheral yet route
// `Serial` to a UART bridge -- e.g. Heltec V3 (esp32-s3 + CP2102), where Serial is UART0;
// there the ZLP bug does not occur and we must not poke the idle JTAG FIFO. On such boards
// this degrades to a plain Serial.flush(). (#1035, Gemini review.)
#if defined(ARDUINO_ARCH_ESP32)
  #include "soc/soc_caps.h"
  #if SOC_USB_SERIAL_JTAG_SUPPORTED && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    #include "hal/usb_serial_jtag_ll.h"
    #define OFFBAND_USJ_CONSOLE 1
  #endif
#endif

// USB Full-Speed bulk max packet size. A CDC transfer whose on-wire length is an exact
// multiple of this is emitted as full packets with NO terminating short packet, so the
// host holds the final packet until the next write -- the #1035/#1093 hold. Named so the
// framing math that decides "does this frame need a terminator?" is not a bare 64.
static constexpr size_t USB_FS_BULK_MAX_PACKET = 64;

// HWCDC's default TX ring size: arduino-esp32 HWCDC::begin() calls setTxBufferSize(256) and
// the companion never overrides it, so 256 IS the ring capacity. ArduinoSerialInterface
// seeds its "fully drained" reference with this so the reference can never be under-learned
// under sustained load (a boot banner dirties the ring before the interface can measure it
// empty, so it cannot be read live at init -- see the #1093 review). If a future board did
// configure a LARGER ring, the running max in writeFrame() raises the reference to match; a
// SMALLER ring would leave the reference too high, which merely no-ops the terminator (a
// stale host-side hold, never a truncation) -- fail-safe, not fail-corrupt.
static constexpr int HWCDC_DEFAULT_TX_RING = 256;

// Drain the console TX and emit the terminating zero-length packet so a reply whose
// length is a multiple of 64 is delivered immediately instead of held host-side (#1035).
static inline void flushSerialConsole() {
#if defined(OFFBAND_USJ_CONSOLE)
  Serial.flush();                               // drain the HWCDC TX ring to the FIFO
  // The last real 64-byte packet may still be draining from the FIFO. Wait (bounded) for
  // the FIFO to become writable, THEN emit the terminating zero-length packet -- the LL
  // header says to flush "when usb_serial_jtag_ll_txfifo_writable() returns true". Checking
  // once and skipping raced with the in-flight packet and left ~1% of 64-multiple replies
  // unterminated (#1035). The 2 ms bound means an unread/stalled host can never wedge the
  // reply path; on timeout we flush best-effort, which is harmless.
  uint32_t t0 = micros();
  while (!usb_serial_jtag_ll_txfifo_writable() && (uint32_t)(micros() - t0) < 2000) { }
  usb_serial_jtag_ll_txfifo_flush();            // zero-length packet ends the USB transfer
#else
  Serial.flush();
#endif
}

// Non-blocking deferred terminator for the framed-protocol hot path (#1093).
//
// flushSerialConsole() is fit for the console-reply boundary but NOT for writeFrame(): its
// Serial.flush() + up-to-2 ms busy-wait would reintroduce a stall on a path that #149
// deliberately keeps non-blocking (a stalled USB host must never wedge the loop and starve
// BLE/radio). This variant NEVER waits: it emits the terminating zero-length packet iff the
// TX FIFO is already writable -- i.e. the host has accepted the last real packet -- and
// otherwise does nothing so the caller retries on the next loop pass. Returns true iff the
// ZLP was emitted (or there is nothing to terminate off USB-Serial-JTAG), so the caller can
// clear its pending flag.
//
// PRECONDITION (enforced by the caller, ArduinoSerialInterface::loop): the TX ring has
// fully drained -- availableForWrite() is back to the ring capacity. Only then is the FIFO's
// last packet the frame's own tail, so the ZLP terminates the FINAL packet rather than an
// early chunk; this is what makes the single writable() check sufficient and correct even
// for several 64-multiple frames written back to back. With the ring empty the HWCDC ISR is
// in its empty-ring path, which does NOT touch this flush register (it only disables its own
// interrupt), so there is no cross-core race and no critical section is needed. Detecting
// drain from the ring (a cross-core-safe, framework-stable signal) rather than from the
// ISR's interrupt-enable side effects is deliberate -- see the #1093 review history.
static inline bool emitConsoleZlpIfReady() {
#if defined(OFFBAND_USJ_CONSOLE)
  if (usb_serial_jtag_ll_txfifo_writable()) {
    usb_serial_jtag_ll_txfifo_flush();          // zero-length packet ends the USB transfer
    return true;
  }
  return false;                                 // FIFO not writable yet -- retry next pass
#else
  return true;                                  // no USB-Serial-JTAG => nothing to terminate
#endif
}
