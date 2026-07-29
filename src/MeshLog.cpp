#include "MeshLog.h"
#include "CaptureRing.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Static capture ring (BSS — no heap; see design-of-record 4.2). Non-retained:
// contents are lost on any reset, by design (live-only capture).
#ifndef OFFBAND_CAPLOG_RING_BYTES
  #if defined(ESP32)
    #define OFFBAND_CAPLOG_RING_BYTES 16384   // conservative; final size pending heap check (#393 D3)
  #else
    #define OFFBAND_CAPLOG_RING_BYTES 8192    // nRF52/STM32 tighter RAM
  #endif
#endif

// Max formatted line length (incl. timestamp prefix); longer lines truncated.
#define MLOG_LINE_MAX 240

static uint8_t     g_storage[OFFBAND_CAPLOG_RING_BYTES];
static CaptureRing g_ring(g_storage, sizeof(g_storage));
// volatile: read lock-free from the hot path / macro; written by the CLI task.
// A byte-wide store is atomic on ARM, so volatile (visibility) is sufficient
// for these control flags — a race at worst drops or adds a single log line.
static volatile uint8_t g_max_level = MLOG_DEBUG;

// Definition of the fast-path flag declared in MeshLog.h.
volatile bool g_meshLogEnabled = false;

// Live-serial mirror (#411): when a routed line is captured, also echo it to the
// live serial console -- but ONLY where the console is not the framed companion
// protocol line. Default true (repeater/observer/BLE-companion/dev: console is
// free); a USB-serial companion sets it false at boot (meshLogSetMirror) so the
// capture never corrupts its protocol line. Gated by g_meshLogEnabled + level,
// so it is the client-settable runtime replacement for the old compile MESH_DEBUG.
volatile bool g_meshLogMirror = true;

// Short critical section around ring mutation so a producer on another task
// (e.g. the BLE task) can't interleave with the main loop mid-append. The sink
// must be called from task context, not a hard ISR.
//
//  - ESP32: a portMUX spinlock covers both cores.
//  - nRF52: the Adafruit core is FreeRTOS-based; taskENTER_CRITICAL masks only
//    up to configMAX_SYSCALL_INTERRUPT_PRIORITY, leaving the SoftDevice's
//    high-priority radio interrupts running. noInterrupts()/__disable_irq()
//    would starve the SoftDevice and can hard-fault BLE — do NOT use it here.
//  - Other (STM32, native): a plain interrupt lock suffices.
#if defined(ESP32)
  static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
  #define MLOG_ENTER() portENTER_CRITICAL(&g_mux)
  #define MLOG_EXIT()  portEXIT_CRITICAL(&g_mux)
#elif defined(NRF52_PLATFORM)
  #include <FreeRTOS.h>
  #include <task.h>
  #define MLOG_ENTER() taskENTER_CRITICAL()
  #define MLOG_EXIT()  taskEXIT_CRITICAL()
#else
  #define MLOG_ENTER() noInterrupts()
  #define MLOG_EXIT()  interrupts()
#endif

void meshLogSetEnabled(bool enabled) { g_meshLogEnabled = enabled; }
bool meshLogIsEnabled() { return g_meshLogEnabled; }
void meshLogSetMirror(bool on) { g_meshLogMirror = on; }
bool meshLogMirrorEnabled() { return g_meshLogMirror; }
void meshLogSetLevel(uint8_t max_level) { g_max_level = max_level; }
uint8_t meshLogGetLevel() { return g_max_level; }
size_t meshLogCapacity() { return g_ring.capacity(); }

void meshLogClear() {
  MLOG_ENTER();
  g_ring.clear();
  MLOG_EXIT();
}

size_t meshLogBytesUsed() {
  MLOG_ENTER();
  size_t n = g_ring.bytesUsed();
  MLOG_EXIT();
  return n;
}

size_t meshLogSnapshot(uint8_t* out, size_t out_cap, size_t offset) {
  MLOG_ENTER();
  size_t n = g_ring.snapshot(out, out_cap, offset);
  MLOG_EXIT();
  return n;
}

void meshLogDumpSerial() {
  // Read in small chunks, each under a brief lock, writing to Serial between
  // locks so the critical section stays short (Serial writes can block).
  uint8_t chunk[128];
  size_t offset = 0;
  for (;;) {
    MLOG_ENTER();
    size_t n = g_ring.snapshot(chunk, sizeof(chunk), offset);
    MLOG_EXIT();
    if (n == 0) break;
    Serial.write(chunk, n);
    offset += n;
  }
}

void mesh_log_line(uint8_t level, const char* fmt, ...) {
  // Cheap early-out: disabled by default, so stock builds pay one branch.
  if (!g_meshLogEnabled || level > g_max_level) return;

  // Format OUTSIDE the critical section (stack-frugal: one bounded buffer, no
  // heap). Timestamp prefix gives every captured line timing context.
  char line[MLOG_LINE_MAX];
  int p = snprintf(line, sizeof(line), "[%lu] ", (unsigned long)millis());
  if (p < 0) return;
  if ((size_t)p >= sizeof(line)) p = sizeof(line) - 1;

  va_list args;
  va_start(args, fmt);
  int m = vsnprintf(line + p, sizeof(line) - p, fmt, args);
  va_end(args);
  if (m < 0) return;

  // snprintf/vsnprintf always NUL-terminate within bounds, so strlen() yields
  // the true content length whether or not the line was truncated — robust
  // against off-by-one in the return-value arithmetic, and never appends the
  // NUL byte itself.
  size_t total = strlen(line);

  MLOG_ENTER();
  g_ring.append(reinterpret_cast<const uint8_t*>(line), total);
  MLOG_EXIT();

  // Live serial mirror (#411) -- outside the lock. Only where the console is free
  // of the framed protocol (g_meshLogMirror); a USB-serial companion keeps this
  // false so nothing raw hits its protocol line.
  //
  // #447: the mirror MUST be non-blocking. A plain Serial.write() blocks when the
  // TX buffer fills with no reader (USB-CDC plugged to a host with no monitor, or a
  // saturated UART) -- on an Observer/repeater/BLE-companion (mirror ON) that stalls
  // the main loop and drops WiFi/TCP/BLE. #428's boot auto-resume makes it fire at
  // boot. So drop the mirror line when the TX buffer can't take it in full: the
  // capture ring already holds it (append above), so the downloadable capture is
  // unaffected -- only the best-effort live echo is skipped under back-pressure.
  if (g_meshLogMirror && Serial.availableForWrite() >= (int)total) {
    Serial.write(reinterpret_cast<const uint8_t*>(line), total);
  }
}
