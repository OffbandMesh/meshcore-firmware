// src/helpers/wifi_observer/CrashLog.cpp
//
// Implementation of the RTC_NOINIT-backed crash log ring buffer.
// See CrashLog.h for the full rationale.

#include "CrashLog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <Wire.h>          // i2cScan() bus probing
  #include <Preferences.h>   // NVS-backed boot counter
  #include <esp_attr.h>      // RTC_NOINIT_ATTR
  #include <esp_system.h>    // esp_reset_reason(), esp_register_shutdown_handler()
  #include <esp_log.h>       // esp_log_set_vprintf()
  #include <esp_err.h>       // esp_err_to_name (stop-gap)
  #include <esp_wifi.h>      // esp_wifi_stop (stop-gap auto-recovery)
  #include <esp_bt.h>        // esp_bt_controller_disable (stop-gap auto-recovery)
  #include <esp_phy_init.h>  // esp_phy_erase_cal_data_in_nvs (stop-gap auto-recovery)
  #include <freertos/FreeRTOS.h>
  #include <freertos/portmacro.h>  // portENTER_CRITICAL
  #include <freertos/task.h> // uxTaskGetStackHighWaterMark()
#endif

namespace crosswire {

// ---------------------------------------------------------------------------
// Reset-reason mapping
// ---------------------------------------------------------------------------
const char* resetReasonString(int reason) {
#ifdef ARDUINO
    switch ((esp_reset_reason_t)reason) {
        case ESP_RST_UNKNOWN:    return "UNKNOWN";
        case ESP_RST_POWERON:    return "POWERON";
        case ESP_RST_EXT:        return "EXT_PIN";
        case ESP_RST_SW:         return "SW_RESET";
        case ESP_RST_PANIC:      return "PANIC";
        case ESP_RST_INT_WDT:    return "INT_WDT";
        case ESP_RST_TASK_WDT:   return "TASK_WDT";
        case ESP_RST_WDT:        return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP:  return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:   return "BROWNOUT";
        case ESP_RST_SDIO:       return "SDIO";
        default:                 return "UNKNOWN";
    }
#else
    (void)reason;
    return "HOST_BUILD";
#endif
}

// ---------------------------------------------------------------------------
// Ring buffer storage (RTC_NOINIT memory)
// ---------------------------------------------------------------------------
// Layout:
//   [header (16 B)] [data (kCrashLogDataSize B)]
// Total: 4096 B in RTC slow memory.

static constexpr uint32_t kCrashLogMagic    = 0xCAFEF00DU;
static constexpr size_t   kCrashLogTotal    = 4096;
static constexpr size_t   kCrashLogDataSize = kCrashLogTotal - 16;  // 4080 B
static constexpr size_t   kCrashLogLineMax  = 240;                  // truncation cap

struct CrashLogHeader {
    uint32_t magic;        // kCrashLogMagic when valid
    uint32_t write_index;  // next byte to write within data[]
    uint32_t wrapped;      // 0 or 1; signals data[] wrapped at least once
    uint32_t reserved;     // pad to 16 B
};

#ifdef ARDUINO
// RTC_NOINIT_ATTR puts this in RTC slow memory, NOT cleared by soft reset.
// The compiler does NOT initialize NOINIT variables; whatever was there
// before the reset is what we see. That's the whole point.
RTC_NOINIT_ATTR static CrashLogHeader s_header;
RTC_NOINIT_ATTR static char           s_data[kCrashLogDataSize];

// Critical section spinlock for thread-safe writes.
static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
#else
// Host build: just provide stubs so the unit harness compiles.
static CrashLogHeader s_header;
static char           s_data[kCrashLogDataSize];
#endif

static bool s_begin_called = false;

// ---------------------------------------------------------------------------
// Internal: write n bytes into the ring buffer starting at write_index.
// Wraps around if needed. Updates write_index + wrapped. Caller MUST hold
// the critical section.
// ---------------------------------------------------------------------------
static void writeToRing(const char* src, size_t n) {
    if (n == 0) return;
    if (n > kCrashLogDataSize) {
        // Truncate from the right: keep only the last kCrashLogDataSize bytes.
        src += (n - kCrashLogDataSize);
        n = kCrashLogDataSize;
    }
    size_t wi = s_header.write_index % kCrashLogDataSize;
    size_t first_chunk = kCrashLogDataSize - wi;
    if (first_chunk >= n) {
        memcpy(&s_data[wi], src, n);
        s_header.write_index = (wi + n) % kCrashLogDataSize;
        if (wi + n >= kCrashLogDataSize) s_header.wrapped = 1;
    } else {
        memcpy(&s_data[wi], src, first_chunk);
        memcpy(&s_data[0],  src + first_chunk, n - first_chunk);
        s_header.write_index = n - first_chunk;
        s_header.wrapped = 1;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void crashLogBegin() {
    if (s_begin_called) return;
    s_begin_called = true;

#ifdef ARDUINO
    // Was the previous boot's buffer valid?
    bool valid = (s_header.magic == kCrashLogMagic) &&
                 (s_header.write_index < kCrashLogDataSize);
    if (valid) {
        // Dump previous-boot contents to serial.
        Serial.println();
        Serial.println("=========================================================");
        Serial.println("=== CRASH LOG FROM PREVIOUS BOOT (RTC_NOINIT survived) ===");
        Serial.printf("=== reset_reason=%d (%s), buffer wrapped=%u ===\n",
                      (int)esp_reset_reason(),
                      resetReasonString(esp_reset_reason()),
                      (unsigned)s_header.wrapped);
        Serial.println("=========================================================");

        // Print the buffer in order: if wrapped, start at write_index
        // (= oldest byte); else start at 0.
        size_t start = s_header.wrapped ? s_header.write_index : 0;
        size_t count = s_header.wrapped ? kCrashLogDataSize : s_header.write_index;
        for (size_t i = 0; i < count; ++i) {
            char c = s_data[(start + i) % kCrashLogDataSize];
            if (c == '\0') continue;  // skip embedded nulls
            Serial.write(c);
        }
        Serial.println();
        Serial.println("=========================================================");
        Serial.println("=== END CRASH LOG (this boot's writes start here)     ===");
        Serial.println("=========================================================");
        Serial.println();
    } else {
        // Fresh power-on (or deep-sleep wake, or corrupted RTC memory).
        Serial.println("[CrashLog] fresh boot; no previous-boot log to recover.");
    }

    // Re-initialize header for THIS boot's writes. The data[] is
    // intentionally NOT cleared -- the wrap logic + start-from-write_index
    // handles "empty" correctly because write_index = 0 + wrapped = 0
    // means "print bytes [0..0)" = print nothing.
    portENTER_CRITICAL(&s_log_mux);
    s_header.magic       = kCrashLogMagic;
    s_header.write_index = 0;
    s_header.wrapped     = 0;
    s_header.reserved    = 0;
    portEXIT_CRITICAL(&s_log_mux);

    // v2: install hooks immediately so ESP-IDF logs from THIS boot onward
    // are captured, and any reset path triggers a last-gasp dump.
    crashLogInstallEspLogHook();
    crashLogInstallShutdownHandler();

    // v6: initialize boot counter + heartbeat state.
    heartbeatBegin();
#endif
}

void crashLogf(const char* fmt, ...) {
    if (!s_begin_called) return;  // pre-init writes go nowhere

    char line[kCrashLogLineMax + 1];
    int  prefix_len = 0;

#ifdef ARDUINO
    // Auto-prepend "[<millis>] " for ordering context.
    prefix_len = snprintf(line, sizeof(line), "[%lu] ",
                          (unsigned long)millis());
#else
    prefix_len = snprintf(line, sizeof(line), "[host] ");
#endif

    if (prefix_len < 0 || prefix_len >= (int)sizeof(line)) {
        prefix_len = 0;  // shouldn't happen, but defend
        line[0] = '\0';
    }

    va_list ap;
    va_start(ap, fmt);
    int body_len = vsnprintf(line + prefix_len, sizeof(line) - prefix_len, fmt, ap);
    va_end(ap);

    if (body_len < 0) return;
    size_t total = prefix_len + body_len;
    if (total >= sizeof(line)) total = sizeof(line) - 1;  // truncation
    // Append newline if not already present.
    if (total > 0 && line[total - 1] != '\n' && total < sizeof(line) - 1) {
        line[total++] = '\n';
        line[total]   = '\0';
    }

#ifdef ARDUINO
    // Write to serial (live monitoring path).
    Serial.write((const uint8_t*)line, total);

    // Write to ring buffer (crash-survival path).
    portENTER_CRITICAL(&s_log_mux);
    writeToRing(line, total);
    portEXIT_CRITICAL(&s_log_mux);
#else
    // Host build: just write to stdout.
    fputs(line, stdout);
    writeToRing(line, total);
#endif
}

void crashLogDump() {
#ifdef ARDUINO
    Serial.println("--- crashLogDump (current buffer) ---");
    size_t start = s_header.wrapped ? s_header.write_index : 0;
    size_t count = s_header.wrapped ? kCrashLogDataSize : s_header.write_index;
    for (size_t i = 0; i < count; ++i) {
        char c = s_data[(start + i) % kCrashLogDataSize];
        if (c == '\0') continue;
        Serial.write(c);
    }
    Serial.println("--- end ---");
#endif
}

void crashLogClear() {
    portENTER_CRITICAL(&s_log_mux);
    s_header.magic       = kCrashLogMagic;
    s_header.write_index = 0;
    s_header.wrapped     = 0;
    portEXIT_CRITICAL(&s_log_mux);
}

// ---------------------------------------------------------------------------
// CrashLog v2: ESP-IDF log capture + shutdown handler + heap stats
// ---------------------------------------------------------------------------

#ifdef ARDUINO

// Recursion guard: if ESP_LOG is called from inside our handler (e.g., a
// driver logs while we're writing to its serial), we'd loop forever.
// FreeRTOS task-local storage would be cleaner; thread_local on this
// static is sufficient for the single-task setup() case + good enough
// for the multi-task runtime case (worst harm is dropped log lines).
static thread_local bool s_in_vprintf = false;

static int crashlog_vprintf(const char* fmt, va_list ap) {
    if (s_in_vprintf) return 0;  // recursion guard
    s_in_vprintf = true;

    char line[256];
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    if (n < 0) { s_in_vprintf = false; return 0; }
    size_t len = (size_t)n;
    if (len >= sizeof(line)) len = sizeof(line) - 1;  // truncation

    // Write to serial (live monitoring path).
    Serial.write((const uint8_t*)line, len);

    // Write to ring buffer (crash-survival path). Only if begin() ran;
    // pre-begin ESP-IDF logs (during framework init) don't have a buffer
    // to write into yet -- they still hit Serial above.
    if (s_begin_called) {
        portENTER_CRITICAL(&s_log_mux);
        writeToRing(line, len);
        portEXIT_CRITICAL(&s_log_mux);
    }

    s_in_vprintf = false;
    return n;
}

void crashLogInstallEspLogHook() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    // Returns the PREVIOUS vprintf; we ignore it (= replace, don't chain).
    esp_log_set_vprintf(crashlog_vprintf);

    // Boost BT-stack log levels so Bluedroid pair-state-machine events
    // appear in our captured stream. Default arduino-esp32 ships these
    // at WARN; bumping to DEBUG surfaces handshake/bond/disconnect
    // events that the user-visible "drops out on connect" failure
    // produces. esp_log_level_set("*", ...) only affects categories
    // whose code wasn't compiled with a higher minimum level, so this
    // is best-effort -- some BT_LOG categories are sdkconfig-gated.
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("BT", ESP_LOG_DEBUG);
    esp_log_level_set("BT_LOG", ESP_LOG_DEBUG);
    esp_log_level_set("BT_HCI", ESP_LOG_DEBUG);
    esp_log_level_set("BT_GATT", ESP_LOG_DEBUG);
    esp_log_level_set("BT_BTM", ESP_LOG_DEBUG);
    esp_log_level_set("BT_SMP", ESP_LOG_DEBUG);
    esp_log_level_set("BT_L2CAP", ESP_LOG_DEBUG);
    esp_log_level_set("BT_GAP", ESP_LOG_DEBUG);
    esp_log_level_set("NimBLE", ESP_LOG_DEBUG);

    crashLogf("[CrashLog] ESP-IDF log capture installed; BT log level boosted to DEBUG");
}

static void crashlog_shutdown_handler(void) {
    // Last-gasp dump before reset finalizes. Fires on panic, watchdog,
    // ESP.restart(), and similar soft-reset paths.
    Serial.println();
    Serial.println("=== CRASHLOG SHUTDOWN HANDLER FIRED ===");
    crashLogDump();
    Serial.println("=== END SHUTDOWN DUMP ===");
    Serial.flush();
}

void crashLogInstallShutdownHandler() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    esp_err_t err = esp_register_shutdown_handler(crashlog_shutdown_handler);
    if (err != ESP_OK) {
        crashLogf("[CrashLog] esp_register_shutdown_handler failed: %d", (int)err);
    } else {
        crashLogf("[CrashLog] shutdown handler registered");
    }
}

// Loop phase tracking (CrashLog v3).
static volatile const char** s_loop_phase_ptr = nullptr;
static volatile uint32_t*    s_loop_iter_ptr  = nullptr;

void loopPhaseSet(volatile const char** phase_ptr, volatile uint32_t* iter_ptr) {
    s_loop_phase_ptr = phase_ptr;
    s_loop_iter_ptr  = iter_ptr;
}

// ---------------------------------------------------------------------------
// Heartbeat + boot counter state
// ---------------------------------------------------------------------------

// Boot counter persisted in RTC_NOINIT. Uses its own magic separate
// from the ring buffer's so we can validate independently.
static constexpr uint32_t kBootCounterMagic = 0xB007C001U;

struct BootCounterState {
    uint32_t magic;
    uint32_t counter;
};

RTC_NOINIT_ATTR static BootCounterState s_boot_state;

// Sub-loop visit tracking. Each sub-loop sets its bit on entry;
// heartbeat reads + zeroes. portMUX serialization for thread safety.
static volatile uint8_t s_subloop_flags = 0;
static volatile uint32_t s_loop_iter_delta = 0;
static portMUX_TYPE s_hb_mux = portMUX_INITIALIZER_UNLOCKED;

// Heartbeat timing
static uint32_t s_last_hb_ms = 0;

// NVS-backed boot counter: persists across ALL reset paths including
// brown-out (which RTC_NOINIT does NOT survive on this CP2102 V3 SKU,
// as evidenced by user observation correlating BLE pair attempts with
// OLED flips that we wrongly read as "no reboot" because RTC counter
// stayed at 1).
//
// Tradeoff: NVS write costs ~5-15ms per boot (single flash write) but
// gives definitive reboot-count evidence that's survivable across the
// brown-out reset class that BLE radio current spikes can trigger.

static uint32_t s_nvs_boot_count = 0;
static uint32_t s_prev_boot_uptime_s = 0;
static ::Preferences s_boot_prefs;  // explicit global namespace to avoid any future conflicts

void heartbeatBegin() {
    // RTC_NOINIT counter (kept for telemetry but no longer the primary truth):
    if (s_boot_state.magic == kBootCounterMagic) {
        s_boot_state.counter++;
    } else {
        s_boot_state.magic   = kBootCounterMagic;
        s_boot_state.counter = 1;
    }

    // NVS-backed counter (survives ALL resets including brown-out):
    bool ok = s_boot_prefs.begin("cw_boot", /*readOnly=*/false);
    if (ok) {
        s_nvs_boot_count       = s_boot_prefs.getUInt("count", 0) + 1;
        s_prev_boot_uptime_s   = s_boot_prefs.getUInt("last_up_s", 0);
        s_boot_prefs.putUInt("count", s_nvs_boot_count);
        s_boot_prefs.putUInt("last_up_s", 0);  // reset; will be updated periodically
        s_boot_prefs.end();
    } else {
        crashLogf("[boot] WARN: NVS cw_boot namespace open failed; falling back to RTC counter");
        s_nvs_boot_count = bootCounterValue();
    }

    crashLogf("[boot] nvs_count=%u rtc_count=%u prev_boot_lasted=%us reset_reason=%d (%s)",
              (unsigned)s_nvs_boot_count,
              (unsigned)bootCounterValue(),
              (unsigned)s_prev_boot_uptime_s,
              (int)esp_reset_reason(),
              resetReasonString((int)esp_reset_reason()));

    // Stop-gap rapid-reboot detection (#265). Runs AFTER the [boot] log so the
    // diagnostic trace shows boot context first, then stop-gap decisions. If
    // OperatorPromptMode is returned, we never return to caller -- chip is
    // wedged and operator needs to physically power-cycle.
    StopGapState sg_state = stopGapCheckRapidReboot(s_prev_boot_uptime_s, s_nvs_boot_count);
    if (sg_state == StopGapState::OperatorPromptMode ||
        sg_state == StopGapState::RapidDetected) {
        // RapidDetected = auto-recovery disabled by build flag; same handling
        // as OperatorPromptMode (idle indefinitely, render PROMPT).
        stopGapEnterOperatorPromptMode();  // [[noreturn]]
    }
    // StopGapState::AutoRecoveryAttempted means esp_restart() was called inside
    // stopGapCheckRapidReboot and we never reached this point. Anything else
    // (Normal) means proceed with boot.

    s_last_hb_ms = 0;
    portENTER_CRITICAL(&s_hb_mux);
    s_subloop_flags    = 0;
    s_loop_iter_delta  = 0;
    portEXIT_CRITICAL(&s_hb_mux);
}

uint32_t bootCounterValue() {
    // Now returns the NVS-backed count if available, else RTC fallback.
    if (s_nvs_boot_count > 0) return s_nvs_boot_count;
    return (s_boot_state.magic == kBootCounterMagic) ? s_boot_state.counter : 0;
}

// Periodically (called from heartbeatTick) save current uptime to NVS
// so the next boot can report "previous boot lasted Ns" - critical for
// distinguishing "long stable run then reset" from "fast crash cycle".
static uint32_t s_last_uptime_save_ms = 0;
static void maybeSaveUptime(uint32_t now_ms) {
    // Save every 5 seconds (matches stats cadence; balances flash wear)
    if (now_ms - s_last_uptime_save_ms < 5000) return;
    s_last_uptime_save_ms = now_ms;
    ::Preferences p;
    if (p.begin("cw_boot", /*readOnly=*/false)) {
        p.putUInt("last_up_s", now_ms / 1000);
        p.end();
    }
}

void subloopMark(uint8_t which) {
    portENTER_CRITICAL(&s_hb_mux);
    s_subloop_flags |= which;
    portEXIT_CRITICAL(&s_hb_mux);
}

void loopIterTick() {
    portENTER_CRITICAL(&s_hb_mux);
    s_loop_iter_delta++;
    portEXIT_CRITICAL(&s_hb_mux);
}

void heartbeatTick(uint32_t now_ms) {
    if (now_ms - s_last_hb_ms < 1000) return;
    s_last_hb_ms = now_ms;

    // Atomic snapshot + reset.
    uint8_t  flags;
    uint32_t delta_iter;
    portENTER_CRITICAL(&s_hb_mux);
    flags        = s_subloop_flags;
    delta_iter   = s_loop_iter_delta;
    s_subloop_flags    = 0;
    s_loop_iter_delta  = 0;
    portEXIT_CRITICAL(&s_hb_mux);

    crashLogf("[hb] up=%us iter=+%u boot=%u phases=[W:%c M:%c S:%c U:%c] free_heap=%u",
              (unsigned)(now_ms / 1000),
              (unsigned)delta_iter,
              (unsigned)bootCounterValue(),
              (flags & SUBLOOP_WIFI)    ? 'Y' : 'N',
              (flags & SUBLOOP_MESH)    ? 'Y' : 'N',
              (flags & SUBLOOP_SENSORS) ? 'Y' : 'N',
              (flags & SUBLOOP_UI)      ? 'Y' : 'N',
              (unsigned)ESP.getFreeHeap());

    // Persist current uptime to NVS so next boot can report "previous
    // boot lasted Ns" -- definitive evidence of cycle period.
    maybeSaveUptime(now_ms);
}

void i2cScan(int sda_pin, int scl_pin, const char* label) {
    // Re-init Wire on the specified pins if provided (must be done from
    // a task safe context). Don't disturb existing setup if pins are -1.
    if (sda_pin >= 0 && scl_pin >= 0) {
        Wire.end();
        Wire.begin(sda_pin, scl_pin);
    }
    crashLogf("[i2cscan:%s] starting scan on sda=%d scl=%d",
              label ? label : "?",
              sda_pin >= 0 ? sda_pin : -1,
              scl_pin >= 0 ? scl_pin : -1);
    uint8_t found_count = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            crashLogf("[i2cscan:%s] FOUND device at 0x%02X", label ? label : "?", addr);
            found_count++;
        }
    }
    crashLogf("[i2cscan:%s] scan complete; %u device(s) responded",
              label ? label : "?", found_count);
}

void crashLogHeapStats(const char* tag) {
    // Snapshot loop phase + iteration if registered.
    const char* phase = "?";
    uint32_t    iter  = 0;
    if (s_loop_phase_ptr != nullptr && *s_loop_phase_ptr != nullptr) {
        phase = (const char*)*s_loop_phase_ptr;
    }
    if (s_loop_iter_ptr != nullptr) {
        iter = *s_loop_iter_ptr;
    }
    crashLogf("[stats:%s] heap_free=%u heap_min=%u stack_hw=%u loop_iter=%u phase='%s'",
              tag ? tag : "?",
              (unsigned)ESP.getFreeHeap(),
              (unsigned)ESP.getMinFreeHeap(),
              (unsigned)uxTaskGetStackHighWaterMark(nullptr),
              (unsigned)iter,
              phase);
}

// ---------------------------------------------------------------------------
// Stop-gap rapid-reboot detection + recovery (Strycher/LoRa#265)
// ---------------------------------------------------------------------------
// See CrashLog.h header block for design rationale. Two NVS keys (in the
// existing cw_boot namespace) drive a small state machine:
//   - rapid_n     uint32  consecutive boots where prev_boot_lasted_s < RAPID_THRESH_S
//   - recov_at    uint32  nvs_boot_count at which auto-recovery was last attempted
//                         (sentinel kStopGapRecoveryNeverAttempted = never)
//
// stopGapDecide() is the PURE state-machine logic, no I/O, host-testable.
// Defined at the BOTTOM of this file OUTSIDE the #ifdef ARDUINO blocks so
// it's compiled for both ARDUINO and host builds.
//
// stopGapCheckRapidReboot() is the I/O wrapper (ARDUINO-only) that reads/writes
// NVS, logs via crashLogf, and dispatches to recovery / operator-prompt.

namespace {
constexpr const char*   kKeyConsecutiveRapidBoots   = "rapid_n";
constexpr const char*   kKeyRecoveryAttemptedAtBoot = "recov_at";
}  // anonymous namespace

#if CROSSWIRE_STOPGAP_AUTO_RECOVER_ENABLE
// Internal: execute the half-empirical auto-recovery sequence. Calls
// esp_restart() at the end and never returns. Designed to be called from
// stopGapCheckRapidReboot AFTER the recovery-attempted flag is persisted
// to NVS so the post-restart boot can detect "already tried recovery."
//
// HALF-EMPIRICAL: these ESP-IDF APIs are designed for clean teardown, not
// for clearing wedged hardware state. May or may not clear the specific
// wedge being seen in the field. The operator-prompt fallback covers the
// case where this doesn't work.
static void stopGapAttemptAutoRecovery() {
    crashLogf("=========================================================");
    crashLogf("[stopgap-recovery] HALF-EMPIRICAL AUTO-RECOVERY -- attempting");
    crashLogf("[stopgap-recovery] See Strycher/LoRa#264 (root cause) + #265 (this stop-gap)");
    crashLogf("=========================================================");

    crashLogf("[stopgap-recovery] Step 1/4: esp_phy_erase_cal_data_in_nvs()");
    esp_err_t err = esp_phy_erase_cal_data_in_nvs();
    crashLogf("[stopgap-recovery]   result: %s (0x%x)",
              (err == ESP_OK) ? "ESP_OK" : esp_err_to_name(err),
              (unsigned)err);

    crashLogf("[stopgap-recovery] Step 2/4: esp_wifi_stop() (safe no-op if not started)");
    err = esp_wifi_stop();
    crashLogf("[stopgap-recovery]   result: 0x%x", (unsigned)err);

    crashLogf("[stopgap-recovery] Step 3/4: esp_bt_controller_disable() + deinit if enabled");
    esp_bt_controller_status_t bt_st = esp_bt_controller_get_status();
    crashLogf("[stopgap-recovery]   bt_controller_status=%d", (int)bt_st);
    if (bt_st == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
    }

    crashLogf("[stopgap-recovery] Step 4/4: esp_restart() with PHY cal cleared + radios torn down");
    Serial.flush();
    delay(100);
    esp_restart();
    // Never reached.
}
#endif  // CROSSWIRE_STOPGAP_AUTO_RECOVER_ENABLE

StopGapState stopGapCheckRapidReboot(uint32_t prev_boot_lasted_s,
                                     uint32_t nvs_boot_count) {
#if !CROSSWIRE_STOPGAP_DETECT_ENABLE
    (void)prev_boot_lasted_s;
    (void)nvs_boot_count;
    return StopGapState::Normal;
#else
    ::Preferences p;
    if (!p.begin("cw_boot", /*readOnly=*/false)) {
        crashLogf("[stopgap] WARN: NVS cw_boot open failed; skipping detection");
        return StopGapState::Normal;
    }

    // Build inputs from NVS + build-flag thresholds, then call the PURE
    // decision function (host-testable; see scripts/test_observer_stopgap.py).
    StopGapDecideInputs in;
    in.consecutive_rapid_in     = p.getUInt(kKeyConsecutiveRapidBoots, 0);
    in.recovery_attempted_at_in = p.getUInt(kKeyRecoveryAttemptedAtBoot,
                                            kStopGapRecoveryNeverAttempted);
    in.prev_boot_lasted_s       = prev_boot_lasted_s;
    in.nvs_boot_count           = nvs_boot_count;
    in.rapid_thresh_s           = CROSSWIRE_STOPGAP_RAPID_THRESH_S;
    in.rapid_count              = CROSSWIRE_STOPGAP_RAPID_COUNT;
    in.fatal_count              = CROSSWIRE_STOPGAP_FATAL_COUNT;
    in.auto_recover_enabled     = (CROSSWIRE_STOPGAP_AUTO_RECOVER_ENABLE != 0);

    StopGapDecision dec = stopGapDecide(in);

    // Persist decision's new state back to NVS (only on actual change to
    // minimize flash wear).
    if (dec.consecutive_rapid_out != in.consecutive_rapid_in) {
        p.putUInt(kKeyConsecutiveRapidBoots, dec.consecutive_rapid_out);
    }
    if (dec.recovery_attempted_at_out != in.recovery_attempted_at_in) {
        p.putUInt(kKeyRecoveryAttemptedAtBoot, dec.recovery_attempted_at_out);
    }
    p.end();

    crashLogf("[stopgap] consecutive=%u (prev_lasted=%us thresh=%us count=%u/fatal=%u recov_at=%s state=%d)",
              (unsigned)dec.consecutive_rapid_out,
              (unsigned)prev_boot_lasted_s,
              (unsigned)CROSSWIRE_STOPGAP_RAPID_THRESH_S,
              (unsigned)CROSSWIRE_STOPGAP_RAPID_COUNT,
              (unsigned)CROSSWIRE_STOPGAP_FATAL_COUNT,
              (dec.recovery_attempted_at_out == kStopGapRecoveryNeverAttempted)
                  ? "never"
                  : "set",
              (int)dec.state);

    // Dispatch on decision.
    switch (dec.state) {
        case StopGapState::Normal:
            return StopGapState::Normal;

        case StopGapState::RapidDetected:
            crashLogf("[stopgap] RAPID-REBOOT DETECTED (consecutive=%u) but AUTO_RECOVER disabled at build; falling through to operator-prompt mode.",
                      (unsigned)dec.consecutive_rapid_out);
            return StopGapState::RapidDetected;

        case StopGapState::AutoRecoveryAttempted:
#if CROSSWIRE_STOPGAP_AUTO_RECOVER_ENABLE
            crashLogf("[stopgap] RAPID-REBOOT DETECTED (consecutive=%u). Attempting half-empirical auto-recovery.",
                      (unsigned)dec.consecutive_rapid_out);
            stopGapAttemptAutoRecovery();  // calls esp_restart(), never returns
#endif
            // Unreached in practice when AUTO_RECOVER_ENABLE=1; if disabled,
            // stopGapDecide would not have returned AutoRecoveryAttempted.
            return StopGapState::AutoRecoveryAttempted;

        case StopGapState::OperatorPromptMode:
            crashLogf("[stopgap] FATAL: rapid-reboot persists (consecutive=%u, fatal_threshold=%u, recovery_already_attempted=%s).",
                      (unsigned)dec.consecutive_rapid_out,
                      (unsigned)CROSSWIRE_STOPGAP_FATAL_COUNT,
                      (in.recovery_attempted_at_in != kStopGapRecoveryNeverAttempted) ? "yes" : "no");
            return StopGapState::OperatorPromptMode;
    }
    return StopGapState::Normal;  // unreachable; satisfy compiler
#endif  // CROSSWIRE_STOPGAP_DETECT_ENABLE
}

[[noreturn]] void stopGapEnterOperatorPromptMode() {
    crashLogf("=========================================================");
    crashLogf("[stopgap] OPERATOR ACTION REQUIRED -- POWER-CYCLE DEVICE");
    crashLogf("[stopgap] Continuous reboot detected. Auto-recovery: %s.",
#if CROSSWIRE_STOPGAP_AUTO_RECOVER_ENABLE
              "attempted, did not clear the loop"
#else
              "DISABLED at build time"
#endif
              );
    crashLogf("[stopgap] PHYSICALLY UNPLUG AND REPLUG USB to clear the wedged peripheral state.");
    crashLogf("[stopgap] User config (broker, BLE pairings, IATA) preserved across all recovery paths.");
    crashLogf("[stopgap] See Strycher/LoRa#264 (root) + #265 (this stop-gap) for context.");
    crashLogf("=========================================================");

    // Idle forever. vTaskDelay() yields to FreeRTOS scheduler so watchdog
    // is fed implicitly. Render the message every 5 seconds so the
    // operator sees it whenever they attach a serial monitor.
    while (true) {
        crashLogf("[stopgap] WEDGED: POWER-CYCLE REQUIRED (uptime=%lus, free_heap=%u)",
                  (unsigned long)(millis() / 1000),
                  (unsigned)ESP.getFreeHeap());
        // TODO(#265): render to OLED if available. Currently Serial-only
        // because CrashLog doesn't have a direct OLED handle -- needs to
        // be plumbed through (probably via a callback registration API
        // that main.cpp invokes during setup before crashLogBegin runs).
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

#else  // !ARDUINO host build

void crashLogInstallEspLogHook() {}
void crashLogInstallShutdownHandler() {}
void crashLogHeapStats(const char*) {}
void loopPhaseSet(volatile const char**, volatile uint32_t*) {}
void i2cScan(int, int, const char*) {}
void heartbeatBegin() {}
void subloopMark(uint8_t) {}
void loopIterTick() {}
void heartbeatTick(uint32_t) {}
uint32_t bootCounterValue() { return 0; }

// Stop-gap host stubs (for host-runnable tests of detection logic)
StopGapState stopGapCheckRapidReboot(uint32_t /*prev_boot_lasted_s*/,
                                     uint32_t /*nvs_boot_count*/) {
    return StopGapState::Normal;
}
[[noreturn]] void stopGapEnterOperatorPromptMode() {
    // Host build: do not loop forever. Test harness will mock this.
    // Use abort() so misbehaving tests are loud rather than silent.
    abort();
}

#endif

}  // namespace crosswire
