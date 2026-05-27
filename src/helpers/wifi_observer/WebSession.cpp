// src/helpers/wifi_observer/WebSession.cpp
#include "WebSession.h"

#include <string.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <esp_random.h>
  #include <mbedtls/constant_time.h>
#endif

namespace crosswire {

#ifdef ARDUINO

// ---------------------------------------------------------------------------
// Session table
// ---------------------------------------------------------------------------
static WebSession s_sessions[CROSSWIRE_MAX_WEB_SESSIONS];

// ---------------------------------------------------------------------------
// Throttle table
// ---------------------------------------------------------------------------
// 16 IP slots is enough to defeat single-source brute-force on a LAN
// (an attacker would need to rotate among >16 source IPs to keep a
// brute-force pipeline alive) but small enough to RAM-budget. Eviction
// policy on overflow: drop the entry with the oldest first_fail_ms.
struct ThrottleEntry {
    bool     used               = false;
    uint32_t ip                 = 0;
    uint8_t  failure_count      = 0;
    uint32_t first_fail_ms      = 0;
    uint32_t lockout_until_ms   = 0;
};

static constexpr size_t kThrottleSlots = 16;
static ThrottleEntry s_throttle[kThrottleSlots];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Fill `out` with `len` hex chars (lower-case) + NUL terminator at out[len].
// Uses esp_random() (hardware RNG on ESP32-S3) as the entropy source.
static void fillHexRandom(char* out, size_t len) {
    static const char kHex[] = "0123456789abcdef";
    // Process in 4-byte chunks from one esp_random() call each.
    size_t i = 0;
    while (i < len) {
        uint32_t r = esp_random();
        for (int b = 0; b < 4 && i < len; ++b) {
            uint8_t byte = (uint8_t)(r >> (b * 8));
            if (i < len) out[i++] = kHex[(byte >> 4) & 0xF];
            if (i < len) out[i++] = kHex[byte & 0xF];
        }
    }
    out[len] = '\0';
}

// Constant-time compare of two 32-char sids. Returns true on match.
// Length check is done up front (timing-leak of length is not useful
// here -- sid length is a public constant). The byte comparison itself
// uses mbedtls_ct_memcmp so we don't short-circuit on the first
// mismatched character (a strncmp-based lookup is a cookie-guess
// timing oracle).
static bool sidEqualsConstantTime(const char* a, const char* b) {
    if (!a || !b) return false;
    if (strlen(a) != 32 || strlen(b) != 32) return false;
    return mbedtls_ct_memcmp(a, b, 32) == 0;
}

static ThrottleEntry* findThrottleEntry(uint32_t ip) {
    for (size_t i = 0; i < kThrottleSlots; ++i) {
        if (s_throttle[i].used && s_throttle[i].ip == ip) return &s_throttle[i];
    }
    return nullptr;
}

// Find a slot for a new throttle entry for `ip`. Prefers an unused slot;
// if all in use, evicts the entry with the oldest first_fail_ms.
static ThrottleEntry* allocThrottleEntry(uint32_t ip) {
    ThrottleEntry* oldest = &s_throttle[0];
    for (size_t i = 0; i < kThrottleSlots; ++i) {
        if (!s_throttle[i].used) {
            s_throttle[i] = ThrottleEntry{};
            s_throttle[i].used = true;
            s_throttle[i].ip   = ip;
            return &s_throttle[i];
        }
        if (s_throttle[i].first_fail_ms < oldest->first_fail_ms) {
            oldest = &s_throttle[i];
        }
    }
    // Full: evict oldest.
    Serial.printf("[WebSession] throttle table full, evicting ip=0x%08x\n",
                  (unsigned)oldest->ip);
    *oldest = ThrottleEntry{};
    oldest->used = true;
    oldest->ip   = ip;
    return oldest;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

WebSession* webSessionLookup(const char* sid_cookie, uint32_t now_ms) {
    if (!sid_cookie) return nullptr;
    if (strlen(sid_cookie) != 32) return nullptr;
    for (size_t i = 0; i < CROSSWIRE_MAX_WEB_SESSIONS; ++i) {
        if (!s_sessions[i].used) continue;
        if (!sidEqualsConstantTime(s_sessions[i].sid, sid_cookie)) continue;
        // Match -- check idle timeout.
        uint32_t elapsed = now_ms - s_sessions[i].last_seen_ms;
        if (elapsed > kSessionIdleTimeoutMs) {
            Serial.printf("[WebSession] session %.8s... timed out (idle %u ms)\n",
                          s_sessions[i].sid, (unsigned)elapsed);
            s_sessions[i] = WebSession{};
            return nullptr;
        }
        s_sessions[i].last_seen_ms = now_ms;
        return &s_sessions[i];
    }
    return nullptr;
}

WebSession* webSessionCreate(uint32_t client_ip, uint32_t now_ms,
                             bool require_password_change) {
    for (size_t i = 0; i < CROSSWIRE_MAX_WEB_SESSIONS; ++i) {
        if (s_sessions[i].used) continue;
        s_sessions[i] = WebSession{};
        s_sessions[i].used = true;
        fillHexRandom(s_sessions[i].sid,  32);
        fillHexRandom(s_sessions[i].csrf, 32);
        s_sessions[i].ip                       = client_ip;
        s_sessions[i].last_seen_ms             = now_ms;
        s_sessions[i].password_change_required = require_password_change;
        Serial.printf("[WebSession] created session %.8s... for ip=0x%08x\n",
                      s_sessions[i].sid, (unsigned)client_ip);
        return &s_sessions[i];
    }
    Serial.println("[WebSession] WARN: session table full, refusing new session");
    return nullptr;
}

void webSessionDestroy(const char* sid) {
    if (!sid || strlen(sid) != 32) return;
    for (size_t i = 0; i < CROSSWIRE_MAX_WEB_SESSIONS; ++i) {
        if (!s_sessions[i].used) continue;
        if (!sidEqualsConstantTime(s_sessions[i].sid, sid)) continue;
        Serial.printf("[WebSession] destroyed session %.8s...\n", s_sessions[i].sid);
        s_sessions[i] = WebSession{};
        return;
    }
}

bool webSessionIsLockedOut(uint32_t client_ip, uint32_t now_ms) {
    ThrottleEntry* e = findThrottleEntry(client_ip);
    if (!e) return false;
    if (e->lockout_until_ms == 0) return false;
    // int32 subtraction handles millis() wraparound: if now is past
    // the lockout deadline, the difference is positive.
    if ((int32_t)(now_ms - e->lockout_until_ms) >= 0) return false;
    return true;
}

void webSessionRecordLoginFailure(uint32_t client_ip, uint32_t now_ms) {
    ThrottleEntry* e = findThrottleEntry(client_ip);
    if (!e) {
        e = allocThrottleEntry(client_ip);
        e->failure_count = 0;
        e->first_fail_ms = now_ms;
    } else {
        // If the prior window has elapsed, restart the counter window.
        if ((now_ms - e->first_fail_ms) > kThrottleWindowMs) {
            e->failure_count = 0;
            e->first_fail_ms = now_ms;
            e->lockout_until_ms = 0;
        }
    }
    e->failure_count++;
    if (e->failure_count >= kThrottleMaxFails) {
        e->lockout_until_ms = now_ms + kThrottleLockoutMs;
        Serial.printf("[WebSession] ip=0x%08x locked out for %u ms (%u fails in window)\n",
                      (unsigned)client_ip,
                      (unsigned)kThrottleLockoutMs,
                      (unsigned)e->failure_count);
    }
}

void webSessionRecordLoginSuccess(uint32_t client_ip) {
    ThrottleEntry* e = findThrottleEntry(client_ip);
    if (!e) return;
    // Successful login clears the throttle state for this IP. (Note: a
    // locked-out IP cannot reach this path because the login handler
    // checks isLockedOut first.)
    *e = ThrottleEntry{};
}

void webSessionTick(uint32_t now_ms) {
    // Expire idle sessions.
    for (size_t i = 0; i < CROSSWIRE_MAX_WEB_SESSIONS; ++i) {
        if (!s_sessions[i].used) continue;
        if ((now_ms - s_sessions[i].last_seen_ms) > kSessionIdleTimeoutMs) {
            Serial.printf("[WebSession] GC expiring idle session %.8s...\n",
                          s_sessions[i].sid);
            s_sessions[i] = WebSession{};
        }
    }
    // Age throttle counters: drop entries whose lockout has expired AND
    // whose failure window is well past (2x window -- belt + suspenders).
    for (size_t i = 0; i < kThrottleSlots; ++i) {
        if (!s_throttle[i].used) continue;
        bool lockout_expired = (s_throttle[i].lockout_until_ms == 0) ||
                               ((int32_t)(now_ms - s_throttle[i].lockout_until_ms) >= 0);
        bool window_stale    = (now_ms - s_throttle[i].first_fail_ms) > (kThrottleWindowMs * 2);
        if (lockout_expired && window_stale) {
            s_throttle[i] = ThrottleEntry{};
        }
    }
}

#else  // !ARDUINO

// Host-build stubs so the file is harmless if the env doesn't define
// ARDUINO (e.g., a future native unit test that includes this header
// transitively). The behaviors are intentionally degenerate so tests
// that exercise these paths fail loudly rather than silently.

WebSession* webSessionLookup(const char*, uint32_t) { return nullptr; }
WebSession* webSessionCreate(uint32_t, uint32_t, bool) { return nullptr; }
void webSessionDestroy(const char*) {}
bool webSessionIsLockedOut(uint32_t, uint32_t) { return false; }
void webSessionRecordLoginFailure(uint32_t, uint32_t) {}
void webSessionRecordLoginSuccess(uint32_t) {}
void webSessionTick(uint32_t) {}

#endif  // ARDUINO

}  // namespace crosswire
