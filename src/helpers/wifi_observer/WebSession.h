// src/helpers/wifi_observer/WebSession.h
//
// In-memory session table (up to CROSSWIRE_MAX_WEB_SESSIONS) +
// client-IP login-throttling. Sessions are cookie-bound:
//   Cookie: cw_sid=<32-hex-char-id>; HttpOnly; Secure; SameSite=Strict
//   Cookie: cw_csrf=<32-hex-char-csrf>; Secure; SameSite=Strict
// (csrf cookie is NOT HttpOnly because JS reads it to put into the
// X-Csrf-Token header; HttpOnly would break the double-submit pattern.)
//
// 30-min idle timeout (hardcoded in v1; corporate hardening makes
// it configurable).
//
// LIFETIME: returned WebSession* pointers are valid until EITHER
// webSessionDestroy() is called for that sid OR webSessionTick()
// expires the session as idle. Callers (the web handler) must not
// retain a pointer across a yield/await -- re-look-up by cookie on
// each request.
//
// THREAD-SAFETY: same single-task ownership discipline as WebCertStore.
// All entry points are intended to be called from the WifiObserver
// loop task (the same task that runs the eventual web server handler
// and the periodic GC). No internal locking; do not call from other
// tasks or ISRs.

#pragma once
#include "WifiObserverConfig.h"
#include <stdint.h>
#include <stddef.h>

namespace crosswire {

constexpr uint32_t kSessionIdleTimeoutMs = 30 * 60 * 1000;  // 30 min
constexpr uint8_t  kThrottleMaxFails    = 5;
constexpr uint32_t kThrottleWindowMs    = 60 * 1000;        // 60s
constexpr uint32_t kThrottleLockoutMs   = 5 * 60 * 1000;    // 5 min

#ifndef CROSSWIRE_MAX_WEB_SESSIONS
  #define CROSSWIRE_MAX_WEB_SESSIONS 4
#endif

struct WebSession {
    bool     used                = false;
    char     sid[33]             = {0};   // 32 hex chars + NUL
    char     csrf[33]            = {0};
    uint32_t ip                  = 0;     // network-byte-order
    uint32_t last_seen_ms        = 0;
    bool     password_change_required = false;
};

// Returns the session if cookie matches a live entry and not idle-timed-out.
// Updates last_seen on hit. Returns nullptr on miss / timeout.
WebSession* webSessionLookup(const char* sid_cookie, uint32_t now_ms);

// Creates a new session for the given client IP. Generates a fresh
// random sid + csrf token. Returns nullptr if the table is full
// (oldest session is NOT evicted -- the user re-logs in).
WebSession* webSessionCreate(uint32_t client_ip, uint32_t now_ms,
                             bool require_password_change);

// Force-end a session (logout, password change rotation).
void webSessionDestroy(const char* sid);

// Throttling. Call recordLoginFailure on every bad login attempt.
// isLockedOut returns true if this IP should be rejected.
bool webSessionIsLockedOut(uint32_t client_ip, uint32_t now_ms);
void webSessionRecordLoginFailure(uint32_t client_ip, uint32_t now_ms);
void webSessionRecordLoginSuccess(uint32_t client_ip);

// Periodic GC: expires idle sessions + ages throttle counters.
// Called from WifiObserver::wifiObserverLoop().
void webSessionTick(uint32_t now_ms);

}  // namespace crosswire
