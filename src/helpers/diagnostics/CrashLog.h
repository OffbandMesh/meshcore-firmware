// src/helpers/diagnostics/CrashLog.h
//
// Panic-survival ring buffer using RTC_NOINIT memory on ESP32-S3.
//
// The buffer lives in RTC slow memory (RTC_NOINIT_ATTR), which is
// preserved across soft resets (watchdog, panic, ESP.restart(), brown-
// out) but cleared on hard power-on/deep-sleep wakeup. A magic number
// in the header distinguishes "this survived a reset" (dump it to
// serial as a post-mortem) from "fresh power-on" (initialize empty).
//
// crashLogf(...) writes to both the ring buffer AND Serial in one
// call -- so whatever's the last thing written before the chip resets
// is recoverable on the next boot. Critical for diagnosing silent
// failures (BOR, fast WDT) where the panic dump never reaches the
// host because the reset is hardware-level and instantaneous.
//
// Capacity: 4 KB total = ~50 short log lines (avg 80 char/line).
// Storage cost: 4 KB of RTC slow memory (out of ~8 KB total on S3).
// Performance: each write takes a critical section + memcpy + Serial
//   write; ~5-15 microseconds for a typical line.
//
// Plan 1 scope: ring buffer + serial mirror + boot-time dump.
// Plan 2+: optional MQTT publish of crash log + CLI commands to
//   inspect/clear it on demand.

#pragma once
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Platform selector (#350 / #376)
// ---------------------------------------------------------------------------
// CrashLog was written ESP32-only, gated behind `#ifdef ARDUINO`. But ARDUINO
// is defined on nRF52 too, so that guard is wrong for a role-neutral logger.
// Select an EXPLICIT platform instead. ESP32 keeps every prior code path
// byte-for-byte; nRF52 gets the portable core; anything else is the host build.
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
  #define OFFBAND_CRASHLOG_ESP32 1
#elif defined(ARDUINO_ARCH_NRF52) || defined(NRF52) || defined(NRF52_PLATFORM)
  #define OFFBAND_CRASHLOG_NRF52 1
#else
  #define OFFBAND_CRASHLOG_HOST 1
#endif

// Retained-memory placement. The ring + boot counter live here so they survive
// a reset without being re-initialized by C-runtime startup.
//   ESP32 : RTC slow memory (RTC_NOINIT_ATTR) -- separate from main RAM,
//           survives deep sleep as well as soft/watchdog reset.
//   nRF52 : the .noinit section added in #361 -- main SRAM, survives soft and
//           watchdog reset but NOT a power cut. (Accepted trade, #350.)
//           NB (ld 2.29, #363): .noinit is GC'd unless REFERENCED. The ring IS
//           referenced by live code, so it is retained; do not add unreferenced
//           retained globals without a `used` anchor.
//   host  : plain static (tests only; no retention semantics).
#if defined(OFFBAND_CRASHLOG_ESP32)
  #include <esp_attr.h>
  #define OFFBAND_RETAINED RTC_NOINIT_ATTR
#elif defined(OFFBAND_CRASHLOG_NRF52)
  #define OFFBAND_RETAINED __attribute__((section(".noinit")))
#else
  #define OFFBAND_RETAINED
#endif

// Ring size in bytes (header + data). 4 KB default; a RAM-tight variant may
// override with -D OFFBAND_CRASHLOG_RING_BYTES=<n>. On nRF52840 (the whole
// fleet) 4 KB is ~1.7% of app RAM; ~2% of a repeater's free RAM.
#ifndef OFFBAND_CRASHLOG_RING_BYTES
  #define OFFBAND_CRASHLOG_RING_BYTES 4096
#endif

namespace offband {

// Reset-reason provider hook (#376, design decision 2: "delegate to the board").
// CrashLog is board-agnostic, so instead of threading a board handle through the
// API (which would ripple into integration code other sessions are editing), the
// board registers its decoder here. When set, CrashLog reports the reset reason
// via this hook; when null, it falls back to a built-in decode (ESP32) or "n/a"
// (nRF52). The hook returns a short human-readable string; the raw code is
// provider-defined. Wiring is a one-liner the board/example adds; unset is safe.
typedef const char* (*ResetReasonHook)();
void crashLogSetResetReasonHook(ResetReasonHook hook);

// ---------------------------------------------------------------------------
// Reset-reason string mapping (Stage A)
// ---------------------------------------------------------------------------
// Returns a short human-readable name for the given esp_reset_reason()
// enum value. Safe to call before crashLogBegin(); no buffer access.
// Returns "UNKNOWN" for unrecognized values rather than nullptr so
// printf-style callers don't need a null check.
const char* resetReasonString(int reason);

// ---------------------------------------------------------------------------
// Ring buffer lifecycle (Stage B)
// ---------------------------------------------------------------------------

// Call as the FIRST thing in setup() after Serial.begin(). Reads the
// RTC-NOINIT header; if the magic number matches (= previous boot's
// log survived the reset), dumps the saved buffer to Serial with a
// clear "=== CRASH LOG FROM PREVIOUS BOOT ===" header. Then resets
// the write index so this boot's logs start fresh in the buffer.
//
// On fresh power-on / deep-sleep wake, RTC_NOINIT memory contains
// garbage; magic won't match; nothing is dumped; buffer is zeroed
// and marked valid for THIS boot's writes.
//
// Idempotent: calling more than once does nothing after the first call.
void crashLogBegin();

// printf-style logger. Writes the formatted line to BOTH:
//   - the RTC_NOINIT ring buffer (survives soft reset)
//   - Serial (live monitoring)
// Each call appends one line with auto-prepended "[<millis>] " prefix.
// Lines longer than ~240 chars are truncated. Critical-section-guarded
// for thread safety from concurrent tasks (main loop, BLE callbacks).
//
// Format string must be a string literal or PROGMEM string; format
// args follow standard printf rules. Compiler validates via the
// __attribute__((format(printf, 1, 2))) hint.
void crashLogf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Dump current buffer contents to Serial on demand (without clearing).
// Useful from a CLI command or post-event inspection.
void crashLogDump();

// Clear the ring buffer (rewrite magic, reset write index). Does NOT
// clear the underlying bytes; just marks the buffer empty so subsequent
// reads see nothing prior. CLI / settings command on demand.
void crashLogClear();

// How long after boot the deferred re-dump becomes eligible. The delay exists
// so a host that WAS attached through the reset has time to re-enumerate and
// settle before the dump lands.
static const uint32_t kCrashLogDeferMs = 5000u;

// #952: is the deferred previous-boot dump eligible to be emitted right now?
//
// Pure, no I/O, compiled on host AND target so it can be unit-tested -- the
// same host-testable-seam pattern as selectEvictionVictim / selectTlsPromotion.
//
// THE BUG THIS ENCODES. The predicate used to be `pending && now >= 5000`, and
// crashLogTick cleared `pending` BEFORE attempting the write. emitPreviousBootDump
// then returns early when nothing is listening -- so on a board with no host
// attached at exactly T+5 s, the one chance was spent writing into a closed
// transport and could never come back. On a native-USB board, which re-enumerates
// after every reset, that is a race no human wins by hand: the RC52 case in #889
// lost a reset reason exactly this way.
//
// `host_attached` closes it. The dump is spent only when it can actually land,
// which is what #378 intended and what the comment in emitPreviousBootDump has
// always claimed. There is deliberately no upper bound -- the snapshot is a
// static buffer that costs nothing to keep, and a dump delivered late is
// strictly better than one delivered to nobody. It is clearly labelled as
// coming from the previous boot, so it cannot be mistaken for live output.
inline bool crashLogShouldEmitDeferred(bool pending, uint32_t now_ms, bool host_attached) {
  return pending && now_ms >= kCrashLogDeferMs && host_attached;
}

// Call once per loop iteration (pass millis()). Re-emits the previous-boot crash
// dump ONE more time, once at least kCrashLogDeferMs has passed AND a host is
// actually attached, so a serial monitor connected AFTER the reset -- the normal
// field case, you plug in to a node you found wedged or rebooted -- still sees
// it, no matter how long it took to get there (#378, #952). The boot-time print
// alone is lost to a host that has not attached yet. No-op once it has fired,
// and on a fresh boot with nothing to report.
void crashLogTick(uint32_t now_ms);

// ---------------------------------------------------------------------------
// CrashLog v2 additions (SAFELANE "no silent failure" enforcement)
// ---------------------------------------------------------------------------

// Install the ESP-IDF log capture hook via esp_log_set_vprintf().
// After this call, EVERY ESP_LOGE / ESP_LOGW / ESP_LOGI / ESP_LOGD / ESP_LOGV
// from any ESP-IDF component, NimBLE, Bluedroid, Wire, WiFi driver, etc.
// is routed through CrashLog's ring buffer in addition to Serial. Critical
// for diagnosing crashes in code we don't directly own (which is the
// majority of the stack).
//
// Idempotent: calling more than once does nothing after first call.
// Called automatically by crashLogBegin().
void crashLogInstallEspLogHook();

// Register a shutdown handler that flushes the CrashLog buffer to Serial
// on ANY reset path (panic, watchdog, ESP.restart()). Last-gasp visibility
// before the chip resets.
//
// Idempotent. Called automatically by crashLogBegin().
void crashLogInstallShutdownHandler();

// Snapshot current heap + stack into CrashLog with a caller-supplied tag.
// Use periodically (e.g., every 1-5 seconds in a loop) to detect slow
// memory exhaustion / leak patterns building toward a crash.
void crashLogHeapStats(const char* tag);

// Register a "current loop phase" pointer + iteration counter that the
// periodic stats printer reads. main.cpp loop() updates *phase_ptr at
// each step and increments *iter_ptr each iteration. If a sub-loop
// hangs, successive stats samples show the same phase string +
// frozen iteration count = exact deadlock localization.
//
// Both pointers must point to volatile storage (otherwise compiler may
// cache reads). The pointers themselves are stored in CrashLog state.
void loopPhaseSet(volatile const char** phase_ptr, volatile uint32_t* iter_ptr);

// Scan an I2C bus on specified pins, log every address (0x08-0x77)
// that ACKs. Surfaces ground truth about what's on the bus -- critical
// for variant identification when board-specific pin assignments may
// differ from the env's compile-time defaults.
//
// Pass -1, -1 for sda/scl to use the bus's currently-configured pins.
// label is a short string included in log lines ("board", "env", etc.).
void i2cScan(int sda_pin, int scl_pin, const char* label);

// ---------------------------------------------------------------------------
// Heartbeat + boot counter (CrashLog v6: positive-evidence health proof)
// ---------------------------------------------------------------------------
// Goal: prove the main loop is actually running AND each sub-loop is
// being reached, NOT just that "my stats line printed every 5s." Per
// user methodology critique: absence of negative evidence != proof of
// health. This produces POSITIVE evidence in the form of a comprehensive
// heartbeat line every ~1 second.

constexpr uint8_t SUBLOOP_WIFI    = 1 << 0;
constexpr uint8_t SUBLOOP_MESH    = 1 << 1;
constexpr uint8_t SUBLOOP_SENSORS = 1 << 2;
constexpr uint8_t SUBLOOP_UI      = 1 << 3;

// Initialize heartbeat state. Reads + increments the boot counter from
// RTC_NOINIT memory. Called automatically from crashLogBegin().
void heartbeatBegin();

// Called by main.cpp loop() before each sub-loop, with the bit for that
// sub-loop. Heartbeat-tick reads the cumulative-since-last-tick flags
// to determine which sub-loops actually ran in the past ~1s window.
void subloopMark(uint8_t which);

// Increment loop iteration counter. Called by main.cpp loop() once per
// outer iteration. Heartbeat reports delta-iter since last tick.
void loopIterTick();

// Emit heartbeat line if >= 1000ms since last emit. Call frequently
// (from main loop). The line format:
//   [hb] up=Ns iter=+N boot=N phases=[W:Y M:Y S:Y U:Y]
// W = wifi_observer, M = the_mesh, S = sensors, U = ui_task.
// Y/N flags reset after emit so each line covers the past 1s window.
void heartbeatTick(uint32_t now_ms);

// Boot counter value (loaded/incremented by heartbeatBegin).
// Persists across soft resets via RTC_NOINIT; resets on power-on /
// deep-sleep wake / esptool hard reset.
uint32_t bootCounterValue();

}  // namespace offband
