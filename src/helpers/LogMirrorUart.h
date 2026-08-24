#pragma once

// #888 -- portable raw-UART log mirror.
//
// WHAT THIS IS
// ------------
// A second place the device can emit text: a plain UART wire, separate from
// whatever `Serial` happens to be. It exists because on most boards `Serial` is
// USB, and USB dies with the chip -- so every log ever captured that way came
// from a boot that SUCCEEDED. A wire does not care whether the chip is healthy,
// so it can witness the boots that matter.
//
// It writes hardware registers directly. No driver, no RTOS, no Arduino core,
// no logging framework -- so it is usable from the very first executable
// instruction of the application, including from a constructor(101) that runs
// ahead of every other C++ static constructor in the image.
//
// HISTORY, because it explains the shape
// --------------------------------------
// This began (#740/#763) as ESP32-only code living inside BootBeacon.h, and the
// MeshLog call site was gated on ARDUINO_USB_CDC_ON_BOOT. That was never a real
// portability boundary -- every MCU in this fleet can write bytes to a UART
// without a driver. It was one board's problem (a UART-bridge board where
// `Serial` IS the mirror UART, so mirroring double-prints) solved by disabling
// the feature nearly everywhere. #888 inverts that: the back end is per-chip and
// additive, and the "my console is that wire" case is a per-board declaration.
//
// ADDING A CHIP FAMILY
// --------------------
// Implement ONE function for your platform below:
//
//     static inline void offband_log_mirror_putc(char c)
//
// plus a lazy init if the peripheral needs one, and a flush. Everything else --
// the string helpers, the beacon, the MeshLog mirror -- is platform-neutral and
// comes for free. If your platform has no back end yet you get a loud #error
// naming exactly that, never a silent no-op (#740 review: a diagnostic that
// quietly does nothing is worse than one that refuses to build).
//
// THE ONE SAFETY RULE
// -------------------
// Every wait on hardware MUST be finitely bounded. A diagnostic that can hang
// the board it is observing is not a diagnostic (SAFELANE 11.8). The ESP32 arm
// learned this the hard way: its first version spun on a FIFO-count read,
// assuming an unclocked peripheral reads 0. A gated peripheral commonly reads
// all-ones instead, which made the wait permanent. Worst case here is losing
// bytes; it is never the board.

// ---------------------------------------------------------------------------
// #888 RENAME TRIPWIRE. OFFBAND_MESHLOG_UART0 became OFFBAND_LOG_MIRROR_UART.
// A variant left on the old name would still COMPILE and would simply have no
// mirror -- i.e. the diagnostic that exists to catch silent failures would fail
// silently. Refuse to build instead. (Gemini review, #944 finding 5.1. The
// rename itself is complete: no .ini/.h/.cpp in the tree sets the old name.)
// ---------------------------------------------------------------------------
#if defined(OFFBAND_MESHLOG_UART0)
  #error "OFFBAND_MESHLOG_UART0 was renamed to OFFBAND_LOG_MIRROR_UART (#888). Update this env's build_flags -- the old name is inert and would leave this board with no log mirror."
#endif

// ---------------------------------------------------------------------------
// Umbrella: the raw writer has two consumers -- the boot beacon
// (OFFBAND_BOOT_BEACON) and the MeshLog mirror (OFFBAND_LOG_MIRROR_UART).
// Either one alone pulls in the primitives.
// ---------------------------------------------------------------------------
#if defined(OFFBAND_BOOT_BEACON) || (defined(OFFBAND_LOG_MIRROR_UART) && OFFBAND_LOG_MIRROR_UART)
  #define OFFBAND_LOG_MIRROR_ACTIVE 1
#endif

#if defined(OFFBAND_LOG_MIRROR_ACTIVE)

// ---------------------------------------------------------------------------
// #944 finding 2.1 -- SIZE THE BOUND TO THE WAIT IT IS BOUNDING.
//
// Two different waits happen in this file and they are orders of magnitude
// apart, so one magic number cannot serve both:
//
//   * A SINGLE BYTE (nRF52 ENDTX, and the TXSTOPPED handshake). The legitimate
//     wait is one character time -- 1.04 ms at 9600, the slowest baud in the
//     table below, and 87 us at the 115200 the bench actually runs.
//   * A FIFO DRAIN (the ESP32 arm). That legitimately waits for up to a full
//     128-byte hardware FIFO to clear, ~11 ms at 115200 and ~133 ms at 9600,
//     so it keeps the large guard on purpose. Shrinking THAT one would drop
//     bytes on a healthy board, which is the opposite of the fix.
//
// The per-byte guard was also 2,000,000 -- roughly 125 ms of spinning at one
// peripheral read per ~62 ns, i.e. >1000x the wait it bounds. That matters
// because CrashLog runs from fault and shutdown paths: a stall that long can
// let a watchdog turn a recoverable fault into a hard reset and lose the dump.
//
// 2^17 leaves ~8x headroom over a 9600-baud byte while capping the worst-case
// stall near 8 ms. Losing a byte is acceptable; wedging the board is not.
// ---------------------------------------------------------------------------
#define OFFBAND_LOG_MIRROR_BYTE_GUARD 131072UL

// ---------------------------------------------------------------------------
// #887: BOUND ON A SEQUENCE OF WRITES, not just on one byte.
//
// Each back end's putc() spins on a hardware flag with a large guard counter, so
// one byte to a dead peripheral is bounded but slow. That was adequate while
// every caller wrote a line at a time. #953 sends the crash ring -- up to 4080
// bytes -- down this path, and a per-byte bound multiplied by 4080 is not a
// bound: the estimated worst case is tens of minutes of stalled boot. That is
// the failure class #741/#756 exist to prevent.
//
// Count CONSECUTIVE timeouts and give up after a few. The counter resets to zero
// on any successful byte, which is the important half: a transient fault costs a
// few bytes, not the channel. An earlier attempt latched the transport dead for
// the whole boot and was rejected for exactly that -- it turned a momentary
// problem into permanent loss of the wire you depend on when things go wrong.
//
// Worst case becomes threshold x guard rather than 4080 x guard.
#ifndef OFFBAND_LOG_MIRROR_TIMEOUT_GIVEUP
  #define OFFBAND_LOG_MIRROR_TIMEOUT_GIVEUP 5
#endif
static uint8_t offband_log_mirror_timeouts = 0;

// True while the transport is still considered usable. Callers that need to know
// whether their bytes reached a wire -- rather than merely being handed over --
// must ask, because putc() cannot report per-byte success.
static inline bool offband_log_mirror_ok(void) {
  return offband_log_mirror_timeouts < OFFBAND_LOG_MIRROR_TIMEOUT_GIVEUP;
}

#include <stdint.h>
#include <stddef.h>

// Baud. Applies to every back end; each maps it to whatever its hardware wants.
#ifndef OFFBAND_LOG_MIRROR_BAUD
  #define OFFBAND_LOG_MIRROR_BAUD 115200
#endif

// ===========================================================================
// ESP32 back end -- raw UART TX FIFO register writes.
//
// Behaviour here is deliberately UNCHANGED from the #740/#763 implementation.
// RC32's diag envs are the regression check and their output must stay
// byte-identical.
// ===========================================================================
#if defined(ESP32)

#include "soc/soc.h"        // READ_PERI_REG / WRITE_PERI_REG
#include "soc/uart_reg.h"   // UART_FIFO_REG / UART_STATUS_REG / UART_TXFIFO_CNT_*

// Which UART peripheral. 0 = UART0. On the RC32 that is GPIO43 (U0TXD) =
// carrier header pin 12, where the external sniffer clips on. Deliberately NOT
// USB-Serial-JTAG -- the whole point is to not be USB.
#ifndef OFFBAND_LOG_MIRROR_ESP32_UART
  #define OFFBAND_LOG_MIRROR_ESP32_UART 0
#endif

// ESP32-S3 TX FIFO is 128 bytes. Leave headroom rather than filling it exactly;
// this is a diagnostic, not a throughput path.
#ifndef OFFBAND_LOG_MIRROR_FIFO_HIGHWATER
  #define OFFBAND_LOG_MIRROR_FIFO_HIGHWATER 100
#endif

// ---------------------------------------------------------------------------
// #977 (#944 gate finding 1.1) -- NOT REENTRANT. READ BEFORE LOGGING FROM AN ISR.
//
// Both back ends share one transmit path: the nRF52 arm has a single static
// byte_buf plus the shared TXD.PTR/TASKS_STARTTX registers, and the ESP32 arm
// writes the FIFO register with no mutual exclusion. If an interrupt preempts a
// write in flight and itself logs, it overwrites the byte and restarts the
// transfer -- the preempted character is lost or garbled.
//
// This is NOT reachable in the tree as it stands [verified 2026-08-24, #944]:
// the only attachInterrupt() in src/ is setPmuFlag() in
// helpers/esp32/TBeamBoard.cpp, which sets a bool and does not log, and there
// are no IRAM_ATTR or bare interrupt handlers anywhere. Nothing logs from an
// ISR. The one live preemption path is a fault handler firing mid-putc, which
// costs one mirrored character BEFORE the crash dump -- the right trade there.
//
// It was left unfixed deliberately. The obvious fix -- wrapping putc in
// __disable_irq() -- puts the bounded spin loop inside a critical section, so it
// would trade an unreachable race for a reachable multi-millisecond stall with
// interrupts off, on the very fault path finding 2.1 above is about.
//
// SO: if you ever add an ISR that logs, this stops being theoretical. Fix it
// then, and fix it by making the wait short enough to sit in a critical section
// (or by queueing from ISR context) -- not by wrapping the loop as it stands.
// ---------------------------------------------------------------------------
static inline void offband_log_mirror_putc(char c) {
  if (!offband_log_mirror_ok()) return;         // #887: too many in a row

  uint32_t guard = 2000000;   // BOUNDED -- see "THE ONE SAFETY RULE" above
  bool drained = false;
  while (guard--) {
    uint32_t cnt = (READ_PERI_REG(UART_STATUS_REG(OFFBAND_LOG_MIRROR_ESP32_UART))
                    >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT_V;
    if (cnt <= OFFBAND_LOG_MIRROR_FIFO_HIGHWATER) { drained = true; break; }
  }
  if (!drained) {                               // FIFO never drained; nothing is
    offband_log_mirror_timeouts++;              // reading this wire
    return;                                     // (pre-#887 this wrote anyway)
  }
  offband_log_mirror_timeouts = 0;              // a good byte clears the count
  WRITE_PERI_REG(UART_FIFO_REG(OFFBAND_LOG_MIRROR_ESP32_UART), (uint32_t)(uint8_t)c);
}

// Block until TX has drained. Serial.begin() software-resets the UART
// controller, which FLUSHES the hardware FIFO -- so a line emitted immediately
// before it can be truncated mid-transmission. Call before any known UART
// reconfiguration. (Gemini review, #740.)
static inline void offband_log_mirror_flush(void) {
  uint32_t guard = 2000000;
  while (guard--) {
    if ((((READ_PERI_REG(UART_STATUS_REG(OFFBAND_LOG_MIRROR_ESP32_UART)) >> UART_TXFIFO_CNT_S)
          & UART_TXFIFO_CNT_V)) == 0) break;
  }
}

// ===========================================================================
// nRF52 back end -- UARTE + EasyDMA.
//
// Different model from the ESP32: there is no TX FIFO register to poke. UARTE
// transmits from a RAM buffer via TXD.PTR / TXD.MAXCNT / TASKS_STARTTX and
// signals completion on EVENTS_ENDTX. A single static byte satisfies EasyDMA's
// "must be in RAM" requirement and needs no heap, so this still works from a
// static constructor.
//
// INSTANCE CHOICE -- UARTE1 by default, and this matters:
//   Serial1 is bound to NRF_UARTE0 by the Adafruit core (Uart.cpp), always.
//   Serial2 is bound to NRF_UARTE1 but ONLY under
//   `#if defined(PIN_SERIAL2_RX) && defined(PIN_SERIAL2_TX)`.
// A variant that does not define PIN_SERIAL2_* therefore leaves UARTE1 entirely
// unbound, and a dedicated instance means a later Serial1.begin() CANNOT steal
// the wire mid-boot. That is not hypothetical: RC32 lost a session to exactly
// that shape (#702), where GPS init reassigned the sniffer pin partway through
// setup() and the mirror went silent.
// ===========================================================================
#elif defined(NRF52_PLATFORM) || defined(NRF52840_XXAA) || defined(NRF52832_XXAA)

#include <nrf.h>
// Pulled in so OFFBAND_LOG_MIRROR_TX_PIN can be written as a variant symbol
// (PIN_SERIAL1_TX) rather than a bare number -- a raw pin number in an env is
// exactly the kind of thing that silently rots when a pin map changes. This is
// #defines and extern declarations only, so it is safe from early init.
#include <variant.h>

// Which UARTE instance. 1 = NRF_UARTE1 (unbound unless the variant defines
// PIN_SERIAL2_*). Override to 0 only if you know Serial1 is unused AND will
// stay unused for the life of the build.
#ifndef OFFBAND_LOG_MIRROR_NRF_UARTE
  #define OFFBAND_LOG_MIRROR_NRF_UARTE 1
#endif

#if OFFBAND_LOG_MIRROR_NRF_UARTE == 0
  #define OFFBAND_LOG_MIRROR_NRF_DEV NRF_UARTE0
#else
  #define OFFBAND_LOG_MIRROR_NRF_DEV NRF_UARTE1
#endif

#ifndef OFFBAND_LOG_MIRROR_TX_PIN
  #error "OFFBAND_LOG_MIRROR_TX_PIN must name the pin the mirror transmits on."
#endif

// Bounds-check the pin at COMPILE time. OFFBAND_LOG_MIRROR_TX_PIN is an integer
// handed in by an env, and it indexes g_ADigitalPinMap below. A typo would read
// past the end of that array and write whatever it found into PSEL.TXD -- which
// at best selects nothing and at worst points the UARTE at a pin some other
// peripheral is driving. Catch it in the build, not on a bench.
// (Gemini review, #888.)
#if OFFBAND_LOG_MIRROR_TX_PIN >= PINS_COUNT
  #error "OFFBAND_LOG_MIRROR_TX_PIN is out of range for this variant's pin map (>= PINS_COUNT)."
#endif

// Baud -> BAUDRATE register value. These are the hardware's magic constants;
// they are not a simple divisor, so an unsupported value is a build error
// rather than a silently wrong bit rate.
#if   OFFBAND_LOG_MIRROR_BAUD == 9600
  #define OFFBAND_LOG_MIRROR_NRF_BAUD 0x00275000UL
#elif OFFBAND_LOG_MIRROR_BAUD == 19200
  #define OFFBAND_LOG_MIRROR_NRF_BAUD 0x004EA000UL
#elif OFFBAND_LOG_MIRROR_BAUD == 38400
  #define OFFBAND_LOG_MIRROR_NRF_BAUD 0x009D5000UL
#elif OFFBAND_LOG_MIRROR_BAUD == 57600
  #define OFFBAND_LOG_MIRROR_NRF_BAUD 0x00EBF000UL
#elif OFFBAND_LOG_MIRROR_BAUD == 115200
  #define OFFBAND_LOG_MIRROR_NRF_BAUD 0x01D60000UL
#elif OFFBAND_LOG_MIRROR_BAUD == 230400
  #define OFFBAND_LOG_MIRROR_NRF_BAUD 0x03B00000UL
#elif OFFBAND_LOG_MIRROR_BAUD == 460800
  #define OFFBAND_LOG_MIRROR_NRF_BAUD 0x07400000UL
#elif OFFBAND_LOG_MIRROR_BAUD == 921600
  #define OFFBAND_LOG_MIRROR_NRF_BAUD 0x0F000000UL
#else
  #error "OFFBAND_LOG_MIRROR_BAUD: unsupported rate. Add its BAUDRATE constant here."
#endif

// Arduino pin number -> nRF pin (port<<5 | pin), which is also the PSEL
// encoding. Declared rather than pulling in Arduino.h, so this header stays
// usable from the earliest init.
extern "C" const uint32_t g_ADigitalPinMap[];

// Configure once; (re)enable on demand.
//
// Split this way deliberately. flush() fully DISABLES the peripheral so it stops
// drawing current, which matters because this channel is meant to be usable on
// battery with no host attached. But disabling and re-enabling around every byte
// would be needless churn on the line, so the enable is a cheap register test
// rather than an unconditional write, and the pin/baud configuration happens
// exactly once. (Gemini review, #888 — the finding was right that nothing ever
// disabled it; the per-byte enable/disable it proposed is not.)
static inline void offband_log_mirror_nrf_init(void) {
  static bool configured = false;     // zero-initialised, so safe pre-ctor
  NRF_UARTE_Type* u = OFFBAND_LOG_MIRROR_NRF_DEV;

  // #887: the test-and-set has to be atomic. Writers reach this from more than
  // one context -- MeshLog from any task, CrashLog from a BLE callback, the
  // beacon from a constructor -- so two of them can both see `configured` false
  // on first use and both run the configuration block. Cheap outer test first,
  // so the steady-state cost after configuration stays a single read.
  //
  // PRIMASK is saved and restored rather than calling interrupts()
  // unconditionally: this can be entered from a context that ALREADY has
  // interrupts disabled (a crash path, a critical section), and re-enabling them
  // there would be worse than the race being fixed.
  if (!configured) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (!configured) {                // re-test now that we are alone
      u->ENABLE      = 0;             // PSEL/BAUDRATE are only safe to set while disabled
      u->PSEL.TXD    = g_ADigitalPinMap[OFFBAND_LOG_MIRROR_TX_PIN];
      u->PSEL.RXD    = 0xFFFFFFFF;    // TX only -- we never listen
      u->PSEL.CTS    = 0xFFFFFFFF;
      u->PSEL.RTS    = 0xFFFFFFFF;
      u->CONFIG      = 0;             // no parity, no flow control
      u->BAUDRATE    = OFFBAND_LOG_MIRROR_NRF_BAUD;
      configured     = true;          // LAST, so nobody skips init mid-configure
    }
    __set_PRIMASK(primask);
  }

  if (u->ENABLE != 8) u->ENABLE = 8;  // 8 = UARTE enabled
}

// ---------------------------------------------------------------------------
// #977 (#944 gate finding 1.1) -- NOT REENTRANT. READ BEFORE LOGGING FROM AN ISR.
//
// Both back ends share one transmit path: the nRF52 arm has a single static
// byte_buf plus the shared TXD.PTR/TASKS_STARTTX registers, and the ESP32 arm
// writes the FIFO register with no mutual exclusion. If an interrupt preempts a
// write in flight and itself logs, it overwrites the byte and restarts the
// transfer -- the preempted character is lost or garbled.
//
// This is NOT reachable in the tree as it stands [verified 2026-08-24, #944]:
// the only attachInterrupt() in src/ is setPmuFlag() in
// helpers/esp32/TBeamBoard.cpp, which sets a bool and does not log, and there
// are no IRAM_ATTR or bare interrupt handlers anywhere. Nothing logs from an
// ISR. The one live preemption path is a fault handler firing mid-putc, which
// costs one mirrored character BEFORE the crash dump -- the right trade there.
//
// It was left unfixed deliberately. The obvious fix -- wrapping putc in
// __disable_irq() -- puts the bounded spin loop inside a critical section, so it
// would trade an unreachable race for a reachable multi-millisecond stall with
// interrupts off, on the very fault path finding 2.1 above is about.
//
// SO: if you ever add an ISR that logs, this stops being theoretical. Fix it
// then, and fix it by making the wait short enough to sit in a critical section
// (or by queueing from ISR context) -- not by wrapping the loop as it stands.
// ---------------------------------------------------------------------------
static inline void offband_log_mirror_putc(char c) {
  if (!offband_log_mirror_ok()) return;         // #887: too many in a row
  offband_log_mirror_nrf_init();

  // EasyDMA source must be in RAM. `static volatile` puts it there and avoids
  // any stack lifetime question while DMA is in flight.
  static volatile uint8_t byte_buf;
  byte_buf = (uint8_t)c;

  NRF_UARTE_Type* u = OFFBAND_LOG_MIRROR_NRF_DEV;
  u->EVENTS_ENDTX = 0;
  u->TXD.PTR      = (uint32_t)&byte_buf;
  u->TXD.MAXCNT   = 1;
  u->TASKS_STARTTX = 1;

  // BOUNDED. If the peripheral was never enabled, or its clock is gated,
  // EVENTS_ENDTX never fires -- so this must not be a bare `while`. Losing a
  // byte is acceptable; wedging the board under diagnosis is not.
  uint32_t guard = OFFBAND_LOG_MIRROR_BYTE_GUARD;   // #944 2.1 -- one byte, not 125 ms
  while (guard-- && u->EVENTS_ENDTX == 0) { }
  if (u->EVENTS_ENDTX == 0) {                   // #887: never completed
    offband_log_mirror_timeouts++;
    return;
  }
  offband_log_mirror_timeouts = 0;              // a good byte clears the count
  u->EVENTS_ENDTX = 0;

  // Deliberately NO TASKS_STOPTX here. STOPTX completes asynchronously and is
  // signalled by EVENTS_TXSTOPPED; issuing STARTTX for the next byte while a
  // stop is still in flight is not a defined sequence. Consecutive STARTTX
  // transactions are, and each one has already completed at this point because
  // we waited for its ENDTX. So the UARTE simply stays running between bytes,
  // which is also what a log stream wants. It is stopped in flush().
}

// Stop the transmitter and RELEASE the peripheral.
//
// Each putc already waits for its own ENDTX, so nothing is outstanding and there
// is no buffer to drain -- what this actually does is give the hardware back:
// STOPTX (the counterpart to the omitted per-byte STOPTX), then ENABLE = 0 so
// the UARTE stops drawing current. init() brings it back on demand.
//
// ⚠ Note for anyone measuring current: MeshLog's mirror path calls
// offband_log_mirror_write() per line and never calls flush(), so on a
// mirror-only build the UARTE stays enabled once the first line is emitted.
// That is a deliberate trade -- continuous availability over idle current -- and
// the knob to change it is right here. A build that cares should call flush()
// when it is done logging.
static inline void offband_log_mirror_flush(void) {
  NRF_UARTE_Type* u = OFFBAND_LOG_MIRROR_NRF_DEV;
  if (u->ENABLE != 8) return;         // never enabled: nothing to stop or release

  u->EVENTS_TXSTOPPED = 0;
  u->TASKS_STOPTX = 1;

  // BOUNDED, same reasoning as putc: a peripheral whose clock is gated will
  // never raise TXSTOPPED, and this must not become the hang it exists to catch.
  uint32_t guard = OFFBAND_LOG_MIRROR_BYTE_GUARD;   // #944 2.1 -- short handshake
  while (guard-- && u->EVENTS_TXSTOPPED == 0) { }
  u->EVENTS_TXSTOPPED = 0;

  u->ENABLE = 0;
}

// ===========================================================================
// No back end for this platform yet.
//
// This is deliberately a hard error and not a silent stub. #740's review made
// that call and it was right: someone debugging their instrument while it
// quietly does nothing is a worse outcome than a build that refuses and tells
// them precisely what is missing.
// ===========================================================================
#else

#error "OFFBAND_LOG_MIRROR_UART / OFFBAND_BOOT_BEACON: no raw-UART back end for this MCU yet. Implement offband_log_mirror_putc() + offband_log_mirror_flush() for it in src/helpers/LogMirrorUart.h -- every MCU in this fleet can write a UART without a driver, so this is a gap to fill, not a boundary. RP2040 and STM32 are tracked under epic #887."

#endif  // per-platform back end

// ---------------------------------------------------------------------------
// Platform-neutral helpers. Every back end gets these for free.
// ---------------------------------------------------------------------------

// Raw, unprefixed. Used by the MeshLog mirror, which emits lines MeshLog has
// already formatted (it applies its own "[millis] " prefix) -- so it must NOT
// get the "[BEACON] " tag. That tag exists so a host-side capture can tell
// instrument output from application output on one shared wire, and
// mislabelling app logs as beacons would defeat it.
static inline void offband_log_mirror_write(const char* s, size_t n) {
  if (!s) return;
  while (n--) offband_log_mirror_putc(*s++);
}

static inline void offband_log_mirror_puts(const char* s) {
  if (!s) return;
  while (*s) offband_log_mirror_putc(*s++);
}

#else  // mirror inactive

// Macros, not empty inline functions. An inline function that no translation
// unit calls emits -Wunused-function, which breaks a -Werror build -- and this
// header reaches every env in the fleet. A macro generates no symbol at all.
#define offband_log_mirror_putc(c)      ((void)0)
#define offband_log_mirror_write(s, n)  ((void)0)
#define offband_log_mirror_puts(s)      ((void)0)
#define offband_log_mirror_flush()      ((void)0)

#endif  // OFFBAND_LOG_MIRROR_ACTIVE
