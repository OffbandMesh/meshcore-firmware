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
  // reply path (SAFELANE rule 8); on timeout we flush best-effort, which is harmless.
  uint32_t t0 = micros();
  while (!usb_serial_jtag_ll_txfifo_writable() && (uint32_t)(micros() - t0) < 2000) { }
  usb_serial_jtag_ll_txfifo_flush();            // zero-length packet ends the USB transfer
#else
  Serial.flush();
#endif
}
