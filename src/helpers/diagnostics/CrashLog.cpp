// src/helpers/diagnostics/CrashLog.cpp
//
// Implementation of the RTC_NOINIT-backed crash log ring buffer.
// See CrashLog.h for the full rationale.

#include "CrashLog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if defined(OFFBAND_CRASHLOG_ESP32)
  #include <Arduino.h>
  #include <Wire.h>          // i2cScan() bus probing
  #include <Preferences.h>   // NVS-backed boot counter
  #include <esp_attr.h>      // RTC_NOINIT_ATTR
  #include <esp_system.h>    // esp_reset_reason(), esp_register_shutdown_handler()
  #include <esp_log.h>       // esp_log_set_vprintf()
  #include <freertos/FreeRTOS.h>
  #include <freertos/portmacro.h>  // portENTER_CRITICAL
  #include <freertos/task.h> // uxTaskGetStackHighWaterMark()
#elif defined(OFFBAND_CRASHLOG_NRF52)
  #include <Arduino.h>       // Serial, millis(); .noinit ring from #361
#endif

namespace offband {

// Reset-reason provider hook (#376). Board registers its decoder; unset is safe.
static ResetReasonHook s_reset_reason_hook = nullptr;
void crashLogSetResetReasonHook(ResetReasonHook hook) { s_reset_reason_hook = hook; }

// ---------------------------------------------------------------------------
// Reset-reason mapping
// ---------------------------------------------------------------------------
const char* resetReasonString(int reason) {
#if defined(OFFBAND_CRASHLOG_ESP32)
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
#elif defined(OFFBAND_CRASHLOG_NRF52)
    // No esp_reset_reason enum on nRF52. Delegate to the board's decoder if it
    // registered one (#376 decision 2); otherwise say so honestly.
    (void)reason;
    return s_reset_reason_hook ? s_reset_reason_hook() : "n/a";
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
static constexpr size_t   kCrashLogTotal    = OFFBAND_CRASHLOG_RING_BYTES;  // #376: overridable
static constexpr size_t   kCrashLogDataSize = kCrashLogTotal - 16;  // header is 16 B
static constexpr size_t   kCrashLogLineMax  = 240;                  // truncation cap

struct CrashLogHeader {
    uint32_t magic;        // kCrashLogMagic when valid
    uint32_t write_index;  // next byte to write within data[]
    uint32_t wrapped;      // 0 or 1; signals data[] wrapped at least once
    uint32_t reserved;     // pad to 16 B
};

// Retained storage. OFFBAND_RETAINED = RTC_NOINIT_ATTR (ESP32) / .noinit (nRF52)
// / nothing (host). Contents are NOT zeroed by C-runtime startup, so a previous
// boot's log survives a soft/watchdog reset. The compiler does not initialize
// these; whatever survived the reset is what we read -- that is the point.
OFFBAND_RETAINED static CrashLogHeader s_header;
OFFBAND_RETAINED static char           s_data[kCrashLogDataSize];

// Thread-safe write guard. ESP32 uses a portMUX spinlock; nRF52 (FreeRTOS via
// the Adafruit core) masks interrupts around the short critical region; host is
// single-threaded and needs neither.
#if defined(OFFBAND_CRASHLOG_ESP32)
  static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
  static portMUX_TYPE s_hb_mux  = portMUX_INITIALIZER_UNLOCKED;
  #define OFFBAND_CL_ENTER(mux) portENTER_CRITICAL(mux)
  #define OFFBAND_CL_EXIT(mux)  portEXIT_CRITICAL(mux)
#elif defined(OFFBAND_CRASHLOG_NRF52)
  // Dummy mux tokens so shared call sites compile unchanged; the guard is a
  // brief interrupt mask (writes are a memcpy of <=241 bytes).
  static int s_log_mux = 0;
  static int s_hb_mux  = 0;
  #define OFFBAND_CL_ENTER(mux) do { (void)(mux); noInterrupts(); } while (0)
  #define OFFBAND_CL_EXIT(mux)  do { (void)(mux); interrupts();   } while (0)
#else
  static int s_log_mux = 0;
  static int s_hb_mux  = 0;
  #define OFFBAND_CL_ENTER(mux) do { (void)(mux); } while (0)
  #define OFFBAND_CL_EXIT(mux)  do { (void)(mux); } while (0)
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

// Platform reset-reason code. ESP32 has the esp_reset_reason() enum; nRF52's
// reason is carried by the board hook (its raw code is provider-defined, so 0
// here and the string comes from resetReasonString via the hook).
static int currentResetReasonCode() {
#if defined(OFFBAND_CRASHLOG_ESP32)
    return (int)esp_reset_reason();
#else
    return 0;
#endif
}

// Snapshot of the PREVIOUS boot's ring, captured at crashLogBegin() before this
// boot overwrites it. Held so the dump can be re-emitted a few seconds later,
// once a serial monitor plugged in AFTER the reset has had time to attach --
// the boot-time print alone is lost to a not-yet-connected host (#378). Plain
// static (not retained): it only needs to live for this boot.
#ifndef OFFBAND_CRASHLOG_HOST
static char   s_prev_snapshot[kCrashLogDataSize];
static size_t s_prev_len     = 0;
static bool   s_prev_pending = false;   // a deferred re-dump is still owed

// ---------------------------------------------------------------------------
// #741/#756: CrashLog must NEVER block the boot it exists to diagnose.
//
// THE BUG THIS PREVENTS. On a native-USB-CDC board (15 of them -- RC32, HV4,
// HV4-R8, tracker_v2, E213, E290, T190, EoRa-S3, S3-zero, meshnology_w12,
// station-g2/g3, t3_s3, t_beam_1w, t_beams3) `Serial` is HWCDC. Its write loop
// (framework-arduinoespressif32/cores/esp32/HWCDC.cpp:440-470) is:
//
//     uint32_t tries = tx_timeout_ms;      // we had set this to 0
//     while (connected && to_send) {
//         if (last_toSend == to_send) { tries--; delay(1); }   // 0-1 = 4294967295
//         if (tries == 0) { connected = false; }               // now unreachable
//     }
//
// With tx_timeout_ms == 0 the counter UNDERFLOWS and the escape hatch can never
// fire -- roughly 50 days of 1 ms iterations. The trigger is CDC reporting
// "connected" (plugged in and enumerated) while nothing DRAINS: a wall charger,
// a power bank, a car USB socket. Observed on rc32-bench-1 blocking for 49.9
// minutes inside crashLogBegin, resuming the instant a host opened the port.
//
// Cruelly, attaching a console to investigate DRAINS the buffer and unblocks the
// board, so it always "worked when you looked at it" (#702, days lost).
//
// THE RULE: a truncated crash log is strictly better than a device that never
// boots. Write what fits, drop the rest, never wait.
// ---------------------------------------------------------------------------

// Bounded, non-blocking. Returns bytes actually written; callers must not care.
//   no host attached -> drop everything
//   host attached    -> write at most availableForWrite(), drop the remainder
//
// `if (Serial)` is the right test on all three transports we ship:
//   ESP32 HWCDC              -> isCDC_Connected()
//   nRF52 Adafruit_USBD_CDC  -> tud_cdc_n_connected()
//   HardwareSerial (UART)    -> true once begun (writing to a pin is free anyway)
static size_t crashLogSerialWrite(const char* data, size_t len) {
    if (len == 0) return 0;
    if (!Serial) return 0;                       // no host -> drop, never wait
    int room = Serial.availableForWrite();
    if (room <= 0) return 0;                     // no space -> drop, never wait
    if (len > (size_t)room) len = (size_t)room;  // bound to what fits
    return Serial.write((const uint8_t*)data, len);
}

static void crashLogSerialLine(const char* s) {
    crashLogSerialWrite(s, strlen(s));
    crashLogSerialWrite("\r\n", 2);
}

// Emit `count` bytes of ring content, bounded and non-blocking, skipping the
// embedded NULs that pad an unwrapped ring. `at(i)` supplies byte i so the two
// callers can differ in indexing (linear snapshot vs wrapping ring) without
// duplicating the transmit logic.
//
// #756 review (Gemini): the NUL skip is NOT new -- both dumps have always
// stripped them (`if (c != '\0')`). My first pass at chunking
// emitPreviousBootDump dropped that filter, which would have started emitting
// raw NULs into terminals that previously never saw them, AND left the two
// dumps behaving differently. One helper, original semantics, both callers.
template <typename AtFn>
// Returns the number of bytes that actually reached the transport. #952: the
// caller must distinguish "dumped, possibly truncated" from "wrote nothing at
// all", because only the latter is worth retrying.
static size_t crashLogEmitRing(size_t count, AtFn at) {
    size_t i = 0;
    size_t written = 0;
    while (i < count) {
        int room = Serial.availableForWrite();
        if (room <= 0) break;                       // full -> stop, never wait

        char chunk[64];
        size_t n = 0;
        const size_t cap = (sizeof(chunk) < (size_t)room) ? sizeof(chunk) : (size_t)room;
        while (n < cap && i < count) {
            char c = at(i);
            ++i;
            if (c == '\0') continue;                // padding, not content
            chunk[n++] = c;
        }
        if (n == 0) continue;                       // whole chunk was padding
        size_t w = crashLogSerialWrite(chunk, n);
        if (w == 0) break;                          // no progress -> stop
        written += w;
    }
    return written;
}

// Returns true if any of the previous boot's ring actually reached the wire.
//
// #952: `Serial` being connected does NOT mean the transport can accept bytes --
// availableForWrite() can be 0 with a host attached and not draining. Reporting
// real progress lets the caller keep the one-shot pending flag alive instead of
// spending it on a write that emitted nothing. Same defect class as the issue
// this fixes, one layer down.
static bool emitPreviousBootDump(const char* why) {
    if (s_prev_len == 0) { return false; }
    // Cheap early-out: if nobody is listening there is nothing to emit TO, and
    // the whole point is not to spend boot time on it.
    if (!Serial) return false;

    char hdr[160];
    crashLogSerialLine("");
    crashLogSerialLine("=========================================================");
    snprintf(hdr, sizeof(hdr), "=== CRASH LOG FROM PREVIOUS BOOT (%s) ===", why);
    crashLogSerialLine(hdr);
    snprintf(hdr, sizeof(hdr), "=== reset_reason=%d (%s) ===",
             currentResetReasonCode(), resetReasonString(currentResetReasonCode()));
    crashLogSerialLine(hdr);
    crashLogSerialLine("=========================================================");

    // Was a per-byte Serial.write() over up to 4080 bytes -- the call site that
    // blocked. Now chunked and bounded: each chunk writes only what fits and the
    // rest is dropped. A partial dump is the acceptable outcome; a wedged boot
    // is not. The deferred re-dump (#378/#463) does get a second chance later
    // once a monitor has attached -- as of #952 that is actually true; before it,
    // the pending flag was spent at T+5 s whether or not anyone was listening.
    const size_t written = crashLogEmitRing(s_prev_len, [](size_t i) { return s_prev_snapshot[i]; });

    crashLogSerialLine("");
    crashLogSerialLine("=== END CRASH LOG ======================================");

    // A TRUNCATED dump counts as delivered -- retrying would duplicate what the
    // reader already has. Only a dump that emitted NOTHING is worth another go.
    // Narrow cost: if the transport fills between the header lines and the ring,
    // the header can print twice. Cosmetic, and strictly better than the loss
    // this issue exists to prevent.
    return written > 0;
}
#endif

void crashLogBegin() {
    if (s_begin_called) return;
    s_begin_called = true;

#ifndef OFFBAND_CRASHLOG_HOST
    // Was the previous boot's buffer valid?
    bool valid = (s_header.magic == kCrashLogMagic) &&
                 (s_header.write_index < kCrashLogDataSize);
    if (valid) {
        // Snapshot the previous-boot ring IN ORDER (oldest first) before this
        // boot overwrites it, so it can be re-emitted later for a late-connecting
        // monitor (#378). If wrapped, order starts at write_index; else at 0.
        size_t start = s_header.wrapped ? s_header.write_index : 0;
        size_t count = s_header.wrapped ? kCrashLogDataSize : s_header.write_index;
        if (count > kCrashLogDataSize) count = kCrashLogDataSize;
        for (size_t i = 0; i < count; ++i) {
            s_prev_snapshot[i] = s_data[(start + i) % kCrashLogDataSize];
        }
        s_prev_len     = count;
        s_prev_pending = true;                 // a deferred re-dump is owed
        // Return deliberately ignored: best-effort immediate print for a monitor
        // that was already attached. If it emitted nothing, s_prev_pending stays
        // true and crashLogTick retries once a host appears (#952).
        (void)emitPreviousBootDump("retained RAM survived");
    } else {
        // Fresh power-on (or brown-out / corrupted retained memory).
        s_prev_len     = 0;
        s_prev_pending = false;
        crashLogSerialLine("[CrashLog] fresh boot; no previous-boot log to recover.");
    }

    // Re-initialize header for THIS boot's writes. The data[] is
    // intentionally NOT cleared -- the wrap logic + start-from-write_index
    // handles "empty" correctly because write_index = 0 + wrapped = 0
    // means "print bytes [0..0)" = print nothing.
    OFFBAND_CL_ENTER(&s_log_mux);
    s_header.magic       = kCrashLogMagic;
    s_header.write_index = 0;
    s_header.wrapped     = 0;
    s_header.reserved    = 0;
    OFFBAND_CL_EXIT(&s_log_mux);

    // v2: install hooks immediately so ESP-IDF logs from THIS boot onward
    // are captured, and any reset path triggers a last-gasp dump.
    // (No-ops on nRF52: no ESP-IDF log stream, no shutdown-handler registry.)
    crashLogInstallEspLogHook();
    crashLogInstallShutdownHandler();

    // v6: initialize boot counter + heartbeat state. (nRF52: minimal, see below.)
    heartbeatBegin();
#endif
}

void crashLogf(const char* fmt, ...) {
    // #181: NEVER drop a line on the floor pre-begin. The crash logger silently
    // losing a line is the one failure that erases the very evidence the log
    // exists to capture (SAFELANE 6/1). Before crashLogBegin(), still emit to the
    // live path (Serial/stdout); only the RTC ring -- which begin() initializes --
    // is skipped until ready (mirrors crashlog_vprintf's pre-begin handling).
    char line[kCrashLogLineMax + 1];
    int  prefix_len = 0;

#ifndef OFFBAND_CRASHLOG_HOST
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

#ifndef OFFBAND_CRASHLOG_HOST
    // Live monitoring path -- ALWAYS, even before begin() (the ring isn't ready
    // yet, but the line must not vanish).
    crashLogSerialWrite(line, total);

    // Crash-survival ring -- only once begin() has initialized the header.
    if (s_begin_called) {
        OFFBAND_CL_ENTER(&s_log_mux);
        writeToRing(line, total);
        OFFBAND_CL_EXIT(&s_log_mux);
    }
#else
    // Host build: just write to stdout.
    fputs(line, stdout);
    writeToRing(line, total);
#endif
}

void crashLogDump() {
#ifndef OFFBAND_CRASHLOG_HOST
    crashLogSerialLine("--- crashLogDump (current buffer) ---");
    size_t start = s_header.wrapped ? s_header.write_index : 0;
    size_t count = s_header.wrapped ? kCrashLogDataSize : s_header.write_index;
    // #756: was a per-byte Serial.write() over the whole ring -- same unbounded
    // blocking shape as emitPreviousBootDump(). Chunked and bounded; stops
    // rather than waits. crashLogDump() is also called from the shutdown
    // handler, where blocking would stall the reset itself.
    crashLogEmitRing(count, [start](size_t i) {
        return s_data[(start + i) % kCrashLogDataSize];
    });
    crashLogSerialLine("--- end ---");
#endif
}

void crashLogClear() {
    OFFBAND_CL_ENTER(&s_log_mux);
    s_header.magic       = kCrashLogMagic;
    s_header.write_index = 0;
    s_header.wrapped     = 0;
    OFFBAND_CL_EXIT(&s_log_mux);
}

// #378/#952: call once per loop. Re-emits the previous-boot crash dump ONE more
// time, once the defer delay has elapsed AND a host is actually attached, so a
// serial monitor connected AFTER the reset (the normal field case -- you plug in
// to a node you found wedged/rebooted) still sees it. The boot-time print alone
// is lost to a host that has not attached yet.
//
// The `Serial` term is load-bearing, not defensive. Without it the flag was
// cleared at T+5 s regardless, emitPreviousBootDump returned early on !Serial,
// and the dump was gone -- see crashLogShouldEmitDeferred for the full account.
void crashLogTick(uint32_t now_ms) {
#ifndef OFFBAND_CRASHLOG_HOST
    if (crashLogShouldEmitDeferred(s_prev_pending, now_ms, static_cast<bool>(Serial))) {
        // Spend the one-shot flag only if the dump actually went out. A host that
        // is attached but not draining leaves availableForWrite() at 0; clearing
        // regardless would lose the log for the same reason the pre-#952 code did,
        // just one layer deeper.
        if (emitPreviousBootDump("deferred re-dump for late serial connect")) {
            s_prev_pending = false;
        }
    }
#else
    (void)now_ms;
#endif
}

// ---------------------------------------------------------------------------
// CrashLog v2: ESP-IDF log capture + shutdown handler + heap stats
// ---------------------------------------------------------------------------
// ESP32-ONLY. These lean on ESP-IDF facilities with no nRF52 equivalent:
// esp_log_set_vprintf (BT/Bluedroid log capture), esp_register_shutdown_handler,
// NVS Preferences, ESP.getFreeHeap, uxTaskGetStackHighWaterMark. On nRF52 the
// retained ring + crashLogf above are the breadcrumb substrate; the richer
// sub-loop/heartbeat instrumentation is a later #350 increment. nRF52 + host
// therefore share the stub set at the `#else` below.
// ---------------------------------------------------------------------------

#if defined(OFFBAND_CRASHLOG_ESP32)

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
    crashLogSerialWrite(line, len);

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
    //
    // #756: same defect class as the boot path, and arguably worse here -- a
    // blocking write in a shutdown handler stalls the RESET itself, turning a
    // clean reboot into a wedge. Bounded writes throughout, and NO Serial.flush():
    // flush waits for the host to drain, which is exactly the wait we must never
    // perform. If nobody is listening there is nothing to flush to.
    crashLogSerialLine("");
    crashLogSerialLine("=== CRASHLOG SHUTDOWN HANDLER FIRED ===");
    crashLogDump();
    crashLogSerialLine("=== END SHUTDOWN DUMP ===");
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
// heartbeat reads + zeroes. s_hb_mux (defined once, up top) serializes.
static volatile uint8_t s_subloop_flags = 0;
static volatile uint32_t s_loop_iter_delta = 0;

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
        // #181: the boot counter IS crash-cycle evidence -- a silent put failure
        // would freeze the count and mask a reboot loop (SAFELANE 6). Surface it.
        if (s_boot_prefs.putUInt("count", s_nvs_boot_count) == 0) {
            crashLogf("[boot] WARN: NVS cw_boot 'count' write FAILED (count=%u not persisted)",
                      (unsigned)s_nvs_boot_count);
        }
        s_boot_prefs.putUInt("last_up_s", 0);  // reset; maybeSaveUptime overwrites within 5s
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
    // #181: feeds "prev boot lasted Ns" -- the crash-cycle-PERIOD evidence. Runs
    // every 5s, so a silent failure would erase that evidence indefinitely. Log a
    // failure ONCE and re-arm on the next success, rather than flooding the 4KB
    // ring with a repeating warning (SAFELANE 6).
    static bool s_uptime_save_warned = false;
    ::Preferences p;
    if (!p.begin("cw_boot", /*readOnly=*/false)) {
        if (!s_uptime_save_warned) {
            crashLogf("[boot] WARN: uptime save -- NVS cw_boot open FAILED (suppressing repeats)");
            s_uptime_save_warned = true;
        }
        return;
    }
    size_t wrote = p.putUInt("last_up_s", now_ms / 1000);
    p.end();
    if (wrote == 0) {
        if (!s_uptime_save_warned) {
            crashLogf("[boot] WARN: uptime save -- NVS 'last_up_s' write FAILED (suppressing repeats)");
            s_uptime_save_warned = true;
        }
        return;
    }
    s_uptime_save_warned = false;  // re-arm: a recovered write re-logs the next failure
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

    uint32_t heap = ESP.getFreeHeap();

    // #183: build the heartbeat line ONCE (same [millis] prefix crashLogf would add)
    // and emit it to Serial every second for live monitoring -- but write it to the
    // RTC crash ring only PERIODICALLY or on a real anomaly. A 1 Hz ring write floods
    // the 4 KB ring (~50 s to wrap) and evicts the crash diagnostics the ring exists
    // to preserve across a reboot. At one ring beat per 30 s the ring holds ~25 min of
    // uptime markers AND keeps real events readable for the whole boot.
    char line[180];
    int n = snprintf(line, sizeof(line),
              "[%lu] [hb] up=%us iter=+%u boot=%u phases=[W:%c M:%c S:%c U:%c] free_heap=%u\n",
              (unsigned long)millis(),
              (unsigned)(now_ms / 1000),
              (unsigned)delta_iter,
              (unsigned)bootCounterValue(),
              (flags & SUBLOOP_WIFI)    ? 'Y' : 'N',
              (flags & SUBLOOP_MESH)    ? 'Y' : 'N',
              (flags & SUBLOOP_SENSORS) ? 'Y' : 'N',
              (flags & SUBLOOP_UI)      ? 'Y' : 'N',
              (unsigned)heap);
    if (n > 0) {
        size_t total = ((size_t)n < sizeof(line)) ? (size_t)n : sizeof(line) - 1;
        crashLogSerialWrite(line, total);   // live monitoring, every second

        // Ring-write only when the beat carries new signal: every 30 s, or the moment
        // free heap drops sharply (>8 KB since the last beat) -- a memory-pressure
        // anomaly worth preserving. (A hung subloop is still covered: the next periodic
        // beat shows the N flags, and the shutdown handler dumps the ring on the
        // watchdog reset.)
        static uint32_t s_last_hb_ring_ms = 0;
        static uint32_t s_last_hb_heap    = 0;
        bool heap_drop = (s_last_hb_heap != 0) && (heap + 8192u < s_last_hb_heap);
        bool periodic  = (s_last_hb_ring_ms == 0) || (now_ms - s_last_hb_ring_ms >= 30000u);
        if (periodic || heap_drop) {
            portENTER_CRITICAL(&s_log_mux);
            writeToRing(line, total);
            portEXIT_CRITICAL(&s_log_mux);
            s_last_hb_ring_ms = now_ms;
        }
        s_last_hb_heap = heap;
    }

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

#else  // nRF52 + host: ESP-IDF extras are stubbed (see block header). The
       // retained ring + crashLogf (above) are the working breadcrumb substrate
       // on nRF52; sub-loop/heartbeat instrumentation is a later #350 increment.

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

#endif

}  // namespace offband
