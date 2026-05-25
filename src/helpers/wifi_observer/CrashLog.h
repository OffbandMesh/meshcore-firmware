// src/helpers/wifi_observer/CrashLog.h
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
#include "WifiObserverConfig.h"
#include <stddef.h>
#include <stdint.h>

namespace crosswire {

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

}  // namespace crosswire
