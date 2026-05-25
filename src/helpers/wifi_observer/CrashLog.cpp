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
  #include <esp_attr.h>      // RTC_NOINIT_ATTR
  #include <esp_system.h>    // esp_reset_reason()
  #include <freertos/FreeRTOS.h>
  #include <freertos/portmacro.h>  // portENTER_CRITICAL
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

}  // namespace crosswire
