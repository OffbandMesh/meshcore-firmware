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

// Portable printf-format attribute. GCC/Clang understand
// __attribute__((format(printf, fmt_idx, args_idx))); MSVC ignores it.
// Used on crashLogf() below so the compiler validates format-string args
// when GCC/Clang is the compiler. Defined here so it's available to any
// future declarations in this header that need the same hint.
#if defined(__GNUC__) || defined(__clang__)
  #define CROSSWIRE_PRINTF_FMT(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
  #define CROSSWIRE_PRINTF_FMT(fmt_idx, args_idx)
#endif

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
void crashLogf(const char* fmt, ...) CROSSWIRE_PRINTF_FMT(1, 2);

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

// ---------------------------------------------------------------------------
// Stop-gap rapid-reboot detection + recovery (Strycher/LoRa#265)
// ---------------------------------------------------------------------------
// Defense against the post-flash wedged-peripheral-state symptom documented
// in Strycher/LoRa#264. Most ESP32-S3 peripherals are NOT reset by soft
// reset (esp_restart, RTS-pin reset from esptool) per ESP-IDF documented
// behavior; only physical USB power-cycle clears internal capacitance +
// peripheral register state. After certain runtime triggers, the device
// can enter a continuous 3-5s reboot loop that no reflash recovers.
//
// This stop-gap does NOT fix the root cause (separate investigation under
// LoRa-t7i). It detects the loop pattern via NVS-tracked consecutive
// rapid-reboot count and:
//   1. Optionally attempts half-empirical auto-recovery (PHY cal erase +
//      radio teardown + restart). Disabled by default until bench-validated.
//   2. Falls back to operator-prompt mode (idle forever, render clear
//      POWER-CYCLE message to Serial + OLED). Guaranteed to break the
//      silent-reboot-loop symptom even if auto-recovery fails or is off.
//
// User config (broker URLs, BLE pairings, IATA, WiFi creds) is NEVER touched.
// Only PHY calibration cache (recoverable, recalibrates) + stop-gap's own
// internal counters in the cw_boot NVS namespace.
//
// Build flags (default values shown):
//   CROSSWIRE_STOPGAP_DETECT_ENABLE          1   // detection + operator-prompt
//   CROSSWIRE_STOPGAP_AUTO_RECOVER_ENABLE    0   // auto-recovery (off until bench-validated)
//   CROSSWIRE_STOPGAP_RAPID_THRESH_S         10  // boot lasting < N seconds counts as "rapid"
//   CROSSWIRE_STOPGAP_RAPID_COUNT            3   // N consecutive rapid boots trips detection
//   CROSSWIRE_STOPGAP_FATAL_COUNT            6   // N consecutive rapid boots forces operator-prompt
//
// Set via build_flags in the env's platformio.ini, e.g.:
//   -D CROSSWIRE_STOPGAP_AUTO_RECOVER_ENABLE=1
//   -D CROSSWIRE_STOPGAP_RAPID_COUNT=2  (more aggressive detection)

#ifndef CROSSWIRE_STOPGAP_DETECT_ENABLE
  #define CROSSWIRE_STOPGAP_DETECT_ENABLE 1
#endif
#ifndef CROSSWIRE_STOPGAP_AUTO_RECOVER_ENABLE
  #define CROSSWIRE_STOPGAP_AUTO_RECOVER_ENABLE 0
#endif
#ifndef CROSSWIRE_STOPGAP_RAPID_THRESH_S
  #define CROSSWIRE_STOPGAP_RAPID_THRESH_S 10
#endif
#ifndef CROSSWIRE_STOPGAP_RAPID_COUNT
  #define CROSSWIRE_STOPGAP_RAPID_COUNT 3
#endif
#ifndef CROSSWIRE_STOPGAP_FATAL_COUNT
  #define CROSSWIRE_STOPGAP_FATAL_COUNT 6
#endif

// Result of stopGapCheckRapidReboot / stopGapDecide. Caller (heartbeatBegin ->
// setup chain) MUST honor OperatorPromptMode by NOT continuing normal init.
enum class StopGapState {
    Normal,                 // no rapid-reboot pattern; proceed with boot
    RapidDetected,          // detection tripped but auto-recovery DISABLED; caller falls through to OperatorPromptMode handling
    AutoRecoveryAttempted,  // auto-recovery just executed (and called esp_restart); not normally reached by caller
    OperatorPromptMode,     // OPERATOR ACTION REQUIRED; caller must invoke stopGapEnterOperatorPromptMode()
};

// Sentinel value for kKeyRecoveryAttemptedAtBoot meaning "never attempted."
// Exposed so host-runnable tests can construct StopGapDecideInputs.
constexpr uint32_t kStopGapRecoveryNeverAttempted = 0xFFFFFFFFu;

// Pure inputs to the stop-gap state machine. No I/O dependency.
struct StopGapDecideInputs {
    // Persisted state (from NVS) at start of this boot:
    uint32_t consecutive_rapid_in;       // current consecutive_rapid_boots counter
    uint32_t recovery_attempted_at_in;   // kStopGapRecoveryNeverAttempted = no prior attempt

    // This boot's runtime context:
    uint32_t prev_boot_lasted_s;         // 0 = never recorded (very first boot)
    uint32_t nvs_boot_count;             // monotonic boot-id for marking "recovery attempted at"

    // Configuration thresholds (from build flags in production):
    uint32_t rapid_thresh_s;             // boot lasting < this counts as "rapid"
    uint32_t rapid_count;                // N consecutive rapid trips detection
    uint32_t fatal_count;                // N consecutive rapid forces OperatorPromptMode
    bool     auto_recover_enabled;       // true = try auto-recovery; false = jump to operator-prompt
};

// Pure outputs. Caller writes consecutive_rapid_out + recovery_attempted_at_out
// back to NVS and then acts on state.
struct StopGapDecision {
    StopGapState state;                  // next action for caller
    uint32_t     consecutive_rapid_out;  // value to persist to NVS rapid_n
    uint32_t     recovery_attempted_at_out;  // value to persist to NVS recov_at (kStopGapRecoveryNeverAttempted to clear)
};

// PURE LOGIC -- no I/O, no globals, no Arduino dependencies. Host-testable.
// Given the persisted state + this boot's context + thresholds, decides what
// action the stop-gap should take and what to write back to NVS.
StopGapDecision stopGapDecide(const StopGapDecideInputs& in);

// I/O wrapper: reads NVS, calls stopGapDecide, writes back NVS, logs via
// crashLogf, and (if auto-recovery is requested) calls esp_restart. Used from
// heartbeatBegin AFTER the [boot] log line. Returns the caller-visible state.
StopGapState stopGapCheckRapidReboot(uint32_t prev_boot_lasted_s,
                                     uint32_t nvs_boot_count);

// Enter operator-prompt mode. Renders POWER-CYCLE REQUIRED message to
// Serial every 5 seconds and idles forever (vTaskDelay so watchdog feeds).
// NEVER RETURNS. Caller MUST stop normal init when invoking this.
//
// Future enhancement: render to OLED if available (currently Serial-only).
[[noreturn]] void stopGapEnterOperatorPromptMode();

}  // namespace crosswire
