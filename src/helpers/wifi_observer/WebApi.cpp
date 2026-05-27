// src/helpers/wifi_observer/WebApi.cpp
//
// REST endpoint handlers for the Crosswire observer web UI.
// Replaces the Task 7 stubs with real logic. Each handler follows the
// pattern documented in WebApi.h: middleware (auth + csrf) is enforced
// in WebServer.cpp's trampolines; handlers here pick up the session
// via webServerCurrentSession() and apply the 423-Locked password-
// change-required check via the requirePwOkay() helper.
//
// JSON strategy: hand-rolled snprintf, matching MqttPayload.cpp's
// style. Payloads stay small (<1 KB on every route except /api/status
// and /api/wifi/scan, both of which fit comfortably in 4 KB). For
// PARSING incoming JSON bodies we use a minimal extractor (extractJsonString
// / extractJsonBool / extractJsonNumber) that handles flat objects
// `{"key":"value", "k2":42, "k3":true}` -- all five POST handlers
// (/api/login, /api/change_password, /api/brokers/<N>, /api/wifi,
// /api/cli) conform to that shape, so a full recursive-descent parser
// would be over-engineering.
//
// Companion-only handlers (/api/ble, /api/ble/reset_pin) are wrapped
// in #ifdef CROSSWIRE_OBSERVER_BLE_COMPANION in BOTH the header and
// this file. WebServer.cpp's registration block uses the same gate.
//
// Strycher/LoRa#272 (Plan 3 Task 8).

#include "WebApi.h"

#ifdef ARDUINO

#include "WifiObserverConfig.h"
#include "WebServer.h"        // webServerCurrentSession, webServerClientIp, deferred ops
#include "WebSession.h"
#include "WebAuth.h"
#include "WebCertStore.h"
#include "ConfigSchema.h"
#include "MqttBrokerPool.h"
#include "MqttPayload.h"      // buildStatusJson, MqttPayloadCtx
#include "WifiObserver.h"     // wifiObserverPool() singleton
#include "CliPassthrough.h"

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_random.h>
#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_wifi.h>          // esp_wifi_scan_get_ap_num / records
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace crosswire {
namespace WebApi {

// ===========================================================================
// Borrowed pubkey (set by webApiInitIdentityPubkey from main.cpp post-mesh)
// ===========================================================================
static const uint8_t* s_identity_pubkey = nullptr;

void webApiInitIdentityPubkey(const uint8_t* pubkey_32_bytes) {
    s_identity_pubkey = pubkey_32_bytes;
}

namespace {

// ===========================================================================
// Common response helpers
// ===========================================================================

esp_err_t sendJsonStatus(httpd_req_t* req, const char* status_line,
                         const char* json_body, size_t body_len) {
    httpd_resp_set_status(req, status_line);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, json_body, (ssize_t)body_len);
    return ESP_OK;
}

esp_err_t sendJson200(httpd_req_t* req, const char* json_body) {
    return sendJsonStatus(req, "200 OK", json_body, strlen(json_body));
}

esp_err_t sendError(httpd_req_t* req, const char* status_line, const char* code) {
    // Minimal {"error":"<code>"} envelope. Distinct from sendJson200 so
    // every error site looks identical in a tail/grep audit.
    char body[96];
    int n = snprintf(body, sizeof(body), "{\"error\":\"%s\"}\n", code);
    if (n < 0 || (size_t)n >= sizeof(body)) n = (int)sizeof(body) - 1;
    return sendJsonStatus(req, status_line, body, (size_t)n);
}

esp_err_t send400(httpd_req_t* req, const char* code = "bad_request") {
    return sendError(req, "400 Bad Request", code);
}
esp_err_t send401(httpd_req_t* req, const char* code = "unauthorized") {
    return sendError(req, "401 Unauthorized", code);
}
esp_err_t send404(httpd_req_t* req, const char* code = "not_found") {
    return sendError(req, "404 Not Found", code);
}
esp_err_t send423(httpd_req_t* req) {
    return sendError(req, "423 Locked", "password_change_required");
}
esp_err_t send429(httpd_req_t* req) {
    return sendError(req, "429 Too Many Requests", "rate_limited");
}
esp_err_t send500(httpd_req_t* req, const char* code = "internal_error") {
    return sendError(req, "500 Internal Server Error", code);
}
esp_err_t send501(httpd_req_t* req, const char* code) {
    return sendError(req, "501 Not Implemented", code);
}
esp_err_t send503(httpd_req_t* req, const char* code) {
    return sendError(req, "503 Service Unavailable", code);
}

// 423-gate: applied to every authenticated handler EXCEPT
// /api/change_password and /api/logout. Returns true if the caller
// should continue; false if the response was sent (caller returns ESP_OK).
bool requirePwOkay(httpd_req_t* req, WebSession* sess) {
    if (!sess) {
        // Should never happen at call sites that ran requireAuth first,
        // but defense-in-depth: 401 rather than silent pass.
        send401(req);
        return false;
    }
    if (sess->password_change_required) {
        send423(req);
        return false;
    }
    return true;
}

// ===========================================================================
// Body reading
// ===========================================================================
//
// httpd_req_recv may return fewer bytes than requested in a single call
// (TLS record boundaries, slow client, etc.) -- the call must be looped
// until req->content_len bytes have been read or recv returns <= 0.

constexpr size_t kMaxBodyBytes = 2048;  // generous; broker config is the largest legit body

// Read the request body into buf (NUL-terminated). Returns the number of
// bytes read (excluding the NUL), or -1 on error / oversize.
// Caller is responsible for handling 400 on -1.
ssize_t readBody(httpd_req_t* req, char* buf, size_t buf_size) {
    if (!req || !buf || buf_size == 0) return -1;
    if (req->content_len == 0) { buf[0] = '\0'; return 0; }
    if (req->content_len >= buf_size) return -1;  // oversize

    size_t remaining = req->content_len;
    size_t off       = 0;
    while (remaining > 0) {
        int got = httpd_req_recv(req, buf + off, remaining);
        if (got <= 0) {
            // 0 = client closed mid-body; -1 = error / timeout. Either way
            // we cannot continue. (HTTPD_SOCK_ERR_TIMEOUT and friends are
            // negative; caller surfaces 400.)
            if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;  // retry once
            return -1;
        }
        off       += (size_t)got;
        remaining -= (size_t)got;
    }
    buf[off] = '\0';
    return (ssize_t)off;
}

// ===========================================================================
// Minimal JSON value extractors
// ===========================================================================
//
// Flat-object parsers only -- no nesting, no arrays, no escape decoding
// beyond \" \\ \/ \n \r \t. Each handler that needs a JSON body posts
// a single-level object, so this surface is sufficient. If a future
// route needs nested input, build that surface in a new WebJson module
// rather than expanding these helpers.

// Skip whitespace in s; return pointer to first non-WS char (or '\0').
const char* skipWs(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') ++s;
    return s;
}

// Find `"key"` (with surrounding quotes) at top level of s; return pointer
// to the character AFTER the closing quote. nullptr if not found at depth 0.
// "Top level" here means we ignore matches that occur inside string
// values -- we walk the object respecting quote toggling, but we do NOT
// validate nested braces.
// First-wins on duplicate keys (safer; cannot be hidden by later injection).
// Note: browser JSON.parse takes last-wins, so server and client disagree
// on malformed input. Acceptable trade-off for an admin-auth surface.
const char* findKey(const char* s, const char* key) {
    if (!s || !key) return nullptr;
    size_t key_len = strlen(key);
    bool in_string = false;
    bool escape    = false;
    const char* p  = s;
    while (*p) {
        char c = *p;
        if (escape) { escape = false; ++p; continue; }
        if (c == '\\' && in_string) { escape = true; ++p; continue; }
        if (c == '"') {
            if (!in_string) {
                // candidate key
                if (strncmp(p + 1, key, key_len) == 0 && p[1 + key_len] == '"') {
                    return p + 2 + key_len;  // past closing quote
                }
                in_string = true;
            } else {
                in_string = false;
            }
            ++p;
            continue;
        }
        ++p;
    }
    return nullptr;
}

// Find the start of the value (after `"key":`). Returns nullptr if not found.
const char* findValueStart(const char* json, const char* key) {
    const char* after_key = findKey(json, key);
    if (!after_key) return nullptr;
    after_key = skipWs(after_key);
    if (*after_key != ':') return nullptr;
    return skipWs(after_key + 1);
}

// Decode a JSON string at *p (must point at opening '"'). Writes into out
// up to out_size-1 bytes + NUL. Returns true on success.
bool decodeJsonString(const char* p, char* out, size_t out_size) {
    if (!p || *p != '"' || !out || out_size == 0) return false;
    ++p;  // skip opening "
    size_t oi = 0;
    while (*p && *p != '"') {
        if (oi + 1 >= out_size) return false;  // overflow
        if (*p == '\\') {
            ++p;
            if (!*p) return false;
            char esc = *p;
            char repl;
            switch (esc) {
                case '"':  repl = '"';  break;
                case '\\': repl = '\\'; break;
                case '/':  repl = '/';  break;
                case 'n':  repl = '\n'; break;
                case 'r':  repl = '\r'; break;
                case 't':  repl = '\t'; break;
                case 'b':  repl = '\b'; break;
                case 'f':  repl = '\f'; break;
                case 'u':
                    // Skip \uXXXX -- we don't decode UTF-16 escapes in the
                    // few config fields this handler accepts. If a future
                    // user surfaces a real \u<hex> need, add proper decoding.
                    // TODO: Consider full UTF-16 (surrogate-pair-aware)
                    // decoding in a future iteration; current impl loses
                    // Unicode SSIDs / topic prefixes that arrive \u-escaped
                    // from JSON.stringify (every char in the escaped run
                    // collapses to a single '?').
                    if (p[1] && p[2] && p[3] && p[4]) {
                        out[oi++] = '?';
                        p += 5;
                        continue;
                    }
                    return false;
                default:   return false;  // unknown escape
            }
            out[oi++] = repl;
            ++p;
        } else {
            out[oi++] = *p;
            ++p;
        }
    }
    if (*p != '"') return false;  // unterminated
    out[oi] = '\0';
    return true;
}

bool extractJsonString(const char* json, const char* key,
                       char* out, size_t out_size) {
    const char* v = findValueStart(json, key);
    if (!v) return false;
    return decodeJsonString(v, out, out_size);
}

// Returns true if key exists and its value is `true` (literal). false on
// `false`, missing, or anything else. *found_out (optional) reports presence.
bool extractJsonBool(const char* json, const char* key, bool* found_out = nullptr) {
    const char* v = findValueStart(json, key);
    if (found_out) *found_out = false;
    if (!v) return false;
    if (strncmp(v, "true", 4) == 0)  { if (found_out) *found_out = true; return true; }
    if (strncmp(v, "false", 5) == 0) { if (found_out) *found_out = true; return false; }
    return false;
}

// Extract an unsigned integer. Returns true on success.
bool extractJsonUint(const char* json, const char* key, uint32_t* out) {
    const char* v = findValueStart(json, key);
    if (!v || !out) return false;
    if (*v < '0' || *v > '9') return false;
    char* end = nullptr;
    unsigned long n = strtoul(v, &end, 10);
    if (end == v) return false;
    *out = (uint32_t)n;
    return true;
}

// ===========================================================================
// JSON escape (output side)
// ===========================================================================
// Re-use MqttPayload::escapeJsonString to avoid drift -- it's already
// the canonical escape in this module and handles the same character
// set we need (", \, \n, \r, \t, control chars -> '_').

void escapeForJson(const char* input, char* output, size_t output_size) {
    escapeJsonString(input, output, output_size);
}

// ===========================================================================
// Cookie + auth helpers (handlers that issue or rotate sessions)
// ===========================================================================

// Two Set-Cookie headers per session: cw_sid (HttpOnly) + cw_csrf (JS-readable).
// esp_http_server supports multiple values for the same header name via
// repeated httpd_resp_set_hdr calls (it appends; max_resp_headers default
// is 8 and we use 2). The session struct holds the literal sid + csrf
// values; we format them into Cookie attributes here.
//
// SECURE attribute: the server is HTTPS-only (port 443 via httpd_ssl_start),
// so Secure on every issued cookie is correct.
// SAMESITE=Strict: this is a same-origin SPA; cross-site requests should
// not carry the cookie at all. CSRF is the double-submit JS-readable token.
//
// Header lifetime: httpd_resp_set_hdr stores the caller's pointer; the
// data must remain valid until httpd_resp_send returns. setSessionCookies
// is always invoked synchronously just before httpd_resp_send within the
// same handler frame, so stack-local buffers (lifetime = the caller's
// stack frame, which outlives the send) are sufficient AND remove the
// foot-gun of function-static buffers that would silently corrupt under
// concurrent dispatch if esp_http_server's worker count were ever raised.
//
// Cookie attribute strings (Path/HttpOnly/Secure/SameSite) are pulled
// from the kCw{Sid,Csrf}CookieAttrs constants in WebSession.h so all
// issue/clear sites format identically -- otherwise the browser treats
// them as different cookies and sessions silently break.

void setSessionCookies(httpd_req_t* req, const WebSession& sess) {
    char sid_cookie[128];
    char csrf_cookie[128];
    snprintf(sid_cookie, sizeof(sid_cookie),
             "cw_sid=%s; Max-Age=1800; %s",
             sess.sid, kCwSidCookieAttrs);
    snprintf(csrf_cookie, sizeof(csrf_cookie),
             "cw_csrf=%s; Max-Age=1800; %s",
             sess.csrf, kCwCsrfCookieAttrs);
    httpd_resp_set_hdr(req, "Set-Cookie", sid_cookie);
    httpd_resp_set_hdr(req, "Set-Cookie", csrf_cookie);
}

// Clear both cookies (logout, post-password-change rotation when we
// destroy without re-issuing).
void clearSessionCookies(httpd_req_t* req) {
    char sid_clear[128];
    char csrf_clear[128];
    snprintf(sid_clear, sizeof(sid_clear),
             "cw_sid=; Max-Age=0; %s", kCwSidCookieAttrs);
    snprintf(csrf_clear, sizeof(csrf_clear),
             "cw_csrf=; Max-Age=0; %s", kCwCsrfCookieAttrs);
    httpd_resp_set_hdr(req, "Set-Cookie", sid_clear);
    httpd_resp_set_hdr(req, "Set-Cookie", csrf_clear);
}

// Re-resolve the cw_sid cookie value (for explicit logout / rotate
// destroy). Returns false if no cookie. The sid buffer must be at
// least 33 bytes.
bool readSidCookieFromReq(httpd_req_t* req, char* sid, size_t sid_size) {
    if (!req || !sid || sid_size < 33) return false;
    sid[0] = '\0';
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (hdr_len == 0 || hdr_len > 256) return false;
    char buf[257];
    if (httpd_req_get_hdr_value_str(req, "Cookie", buf, sizeof(buf)) != ESP_OK) {
        return false;
    }
    buf[256] = '\0';
    // Find "cw_sid=" in the cookie header (we duplicate logic from
    // WebServer.cpp's extractCookieValue rather than expose it -- the
    // surface here is small and the duplication is one ~15-line loop).
    const char* needle = "cw_sid=";
    const char* p      = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ';') ++p;
        if (!*p) break;
        if (strncmp(p, needle, 7) == 0) {
            p += 7;
            const char* end = p;
            while (*end && *end != ';') ++end;
            size_t n = (size_t)(end - p);
            if (n >= sid_size) return false;
            memcpy(sid, p, n);
            sid[n] = '\0';
            return n > 0;
        }
        while (*p && *p != ';') ++p;
    }
    return false;
}

}  // namespace (helpers)

// ===========================================================================
//
//  HANDLERS
//
// ===========================================================================

// ---------------------------------------------------------------------------
// POST /api/login
// ---------------------------------------------------------------------------
// Body: {"password":"..."}
// Lockout-first -> verify -> issue (sid+csrf cookies) -> 200 {"ok":true,
//   "password_change_required":<bool>}.
// On failure: record + 401 (with throttle threshold respected).
esp_err_t handleLogin(httpd_req_t* req) {
    // Read body.
    char body[256];
    ssize_t blen = readBody(req, body, sizeof(body));
    if (blen < 0) return send400(req);

    // Throttle check first -- a locked-out IP gets 429 BEFORE we even
    // peek at the password, so timing-side-channels do not leak password
    // length / parse path during lockout.
    uint32_t client_ip = webServerClientIp(req);
    uint32_t now       = (uint32_t)millis();
    if (webSessionIsLockedOut(client_ip, now)) {
        return send429(req);
    }

    // Parse body.
    char pw[96];
    if (!extractJsonString(body, "password", pw, sizeof(pw))) {
        // Treat malformed body as a failed attempt for throttling -- a
        // probe sending garbage shouldn't escape the rate limit by
        // simply omitting the field.
        webSessionRecordLoginFailure(client_ip, now);
        return send400(req, "missing_password");
    }

    // Verify.
    bool needs_change = false;
    bool ok = webAuthVerifyPassword(pw, &needs_change);
    // Wipe local copy of plaintext ASAP. Constant-time isn't needed
    // post-verify (already-rejected vs already-accepted), just hygiene.
    memset(pw, 0, sizeof(pw));

    if (!ok) {
        webSessionRecordLoginFailure(client_ip, now);
        return send401(req, "bad_credentials");
    }

    // Success: clear throttle, create session.
    webSessionRecordLoginSuccess(client_ip);
    WebSession* sess = webSessionCreate(client_ip, now, needs_change);
    if (!sess) {
        return send500(req, "session_table_full");
    }

    setSessionCookies(req, *sess);
    char body_out[80];
    int n = snprintf(body_out, sizeof(body_out),
                     "{\"ok\":true,\"password_change_required\":%s}\n",
                     needs_change ? "true" : "false");
    return sendJsonStatus(req, "200 OK", body_out, (size_t)n);
}

// ---------------------------------------------------------------------------
// POST /api/logout
// ---------------------------------------------------------------------------
// Destroys the cw_sid session (if any) + clears cookies.
// Intentionally auth-free so a stale-cookie client can clean up without
// 401-looping; the destroy is a no-op if the sid is unknown.
esp_err_t handleLogout(httpd_req_t* req) {
    char sid[33];
    if (readSidCookieFromReq(req, sid, sizeof(sid))) {
        webSessionDestroy(sid);
    }
    clearSessionCookies(req);
    return sendJson200(req, "{\"ok\":true}\n");
}

// ---------------------------------------------------------------------------
// POST /api/change_password
// ---------------------------------------------------------------------------
// Body: {"new_password":"..."} (we don't require old_password because
// the session itself proves the user authenticated; the old password
// has already been verified via the cookie that authorized this request).
//
// Validation:
//   - len >= 8
//   - != derived initial password (forward OR reverse)
// Effect:
//   - webAuthSetPassword(new_pw)
//   - destroy current session, create fresh one (rotate sid+csrf)
//   - new session has password_change_required=false
esp_err_t handleChangePassword(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    // NOTE: we do NOT call requirePwOkay() here -- the whole point of
    // this route is to clear the password_change_required flag.

    char body[256];
    ssize_t blen = readBody(req, body, sizeof(body));
    if (blen < 0) return send400(req);

    char new_pw[96];
    if (!extractJsonString(body, "new_password", new_pw, sizeof(new_pw))) {
        return send400(req, "missing_new_password");
    }
    size_t new_len = strlen(new_pw);
    if (new_len < 8) {
        memset(new_pw, 0, sizeof(new_pw));
        return send400(req, "new_password_too_short");
    }

    // Reject the derived initial password (forward + reverse). This is
    // a UX guardrail -- WebAuth would accept setting any value here,
    // but reusing the derived initial defeats the rotate-on-first-login
    // discipline.
    char derived[16];
    if (webAuthDeriveInitialPassword(derived, sizeof(derived))) {
        size_t dlen = strlen(derived);
        if (new_len == dlen && memcmp(new_pw, derived, dlen) == 0) {
            memset(new_pw, 0, sizeof(new_pw));
            return send400(req, "new_password_equals_initial");
        }
        // Reverse order: <pk6><pin>. Built from the same components
        // but we don't have direct access here; cheap check is to flip
        // the halves of derived (which is <pin><pk6>, both 6 chars).
        if (dlen == 12) {
            char reverse[13];
            memcpy(reverse,     derived + 6, 6);
            memcpy(reverse + 6, derived,     6);
            reverse[12] = '\0';
            if (new_len == 12 && memcmp(new_pw, reverse, 12) == 0) {
                memset(new_pw, 0, sizeof(new_pw));
                return send400(req, "new_password_equals_initial");
            }
        }
    }

    bool wrote = webAuthSetPassword(new_pw);
    memset(new_pw, 0, sizeof(new_pw));
    if (!wrote) return send500(req, "password_persist_failed");

    // Rotate the session: destroy current, create fresh (no
    // password_change_required this time). The fresh session inherits
    // the client IP from the existing one.
    uint32_t client_ip = sess->ip;
    webSessionDestroy(sess->sid);
    WebSession* fresh = webSessionCreate(client_ip, (uint32_t)millis(),
                                         /*require_password_change=*/false);
    if (!fresh) {
        // Unusual -- table was non-full a microsecond ago. Bail to
        // login UI.
        clearSessionCookies(req);
        return send500(req, "session_rotate_failed");
    }
    setSessionCookies(req, *fresh);
    return sendJson200(req, "{\"ok\":true}\n");
}

// ---------------------------------------------------------------------------
// GET /api/status
// ---------------------------------------------------------------------------
// Reuses MqttBrokerPool::buildWebStatusJson which calls the same
// buildStatusJson the MQTT /status publish path uses. Output is therefore
// byte-equivalent to what the broker pool publishes to its /status topic,
// satisfying the Plan 3 spec ("/api/status: 200 JSON via the same
// StatusPayload::buildStatusJson already wired in Plan 2 -- reused
// unchanged"). The golden-test lock documented in MqttPayload.h now
// extends to the HTTPS GET path: the two cannot drift.
//
// If no snapshot has been built yet (first boot, before the pool's first
// publishStatusIfDue tick), returns 503 so the SPA can retry/poll.
esp_err_t handleGetStatus(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    // buildStatusJson realistic payload is ~600 bytes; 1 KB headroom.
    char buf[1024];
    int n = wifiObserverPool().buildWebStatusJson(buf, sizeof(buf));
    if (n < 0) {
        // -1 means either overflow OR no snapshot yet. Snapshot-not-yet is
        // by far the more common case (overflow on a 1KB buffer for a
        // ~600B payload would indicate a bug, not a runtime condition).
        return send503(req, "status_not_ready");
    }
    return sendJsonStatus(req, "200 OK", buf, (size_t)n);
}

// ---------------------------------------------------------------------------
// GET /api/brokers
// ---------------------------------------------------------------------------
// Enumerates all configured slots. Masks sensitive fields:
//   - password -> "***" if set, "" if empty
//   - jwt_token -> "***" if set, "" if empty
// Other fields returned verbatim (after JSON escape).
esp_err_t handleGetBrokers(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    // Chunk response since the payload may exceed any single small buffer.
    // Use httpd_resp_send_chunk; total payload roughly N*250 bytes worst case.
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send_chunk(req, "[", 1);

    char chunk[640];
    for (uint8_t i = 0; i < CROSSWIRE_MAX_BROKERS; ++i) {
        BrokerConfig cfg;
        readBrokerConfig(i, cfg);

        char url_e[256];
        char user_e[128];
        char prefix_e[64];
        char io_e[16];
        char aud_e[192];
        char ca_e[48];
        escapeForJson(cfg.url,           url_e,    sizeof(url_e));
        escapeForJson(cfg.username,      user_e,   sizeof(user_e));
        escapeForJson(cfg.topic_prefix,  prefix_e, sizeof(prefix_e));
        escapeForJson(cfg.iata_override, io_e,     sizeof(io_e));
        escapeForJson(cfg.jwt_audience,  aud_e,    sizeof(aud_e));
        escapeForJson(cfg.ca_cert_name,  ca_e,     sizeof(ca_e));

        const char* pw_mask  = (cfg.password[0]  != '\0') ? "***" : "";
        const char* jwt_mask = (cfg.jwt_token[0] != '\0') ? "***" : "";

        int n = snprintf(chunk, sizeof(chunk),
                         "%s{"
                         "\"slot\":%u,"
                         "\"enabled\":%s,"
                         "\"url\":\"%s\","
                         "\"port\":%u,"
                         "\"transport\":%u,"
                         "\"auth_type\":%u,"
                         "\"username\":\"%s\","
                         "\"password\":\"%s\","
                         "\"jwt_token\":\"%s\","
                         "\"topic_prefix\":\"%s\","
                         "\"iata_override\":\"%s\","
                         "\"jwt_audience\":\"%s\","
                         "\"jwt_refresh_sec\":%lu,"
                         "\"ca_cert_name\":\"%s\""
                         "}",
                         (i == 0) ? "" : ",",
                         (unsigned)i,
                         cfg.enabled ? "true" : "false",
                         url_e,
                         (unsigned)cfg.port,
                         (unsigned)cfg.transport,
                         (unsigned)cfg.auth_type,
                         user_e,
                         pw_mask,
                         jwt_mask,
                         prefix_e,
                         io_e,
                         aud_e,
                         (unsigned long)cfg.jwt_refresh_sec,
                         ca_e);
        if (n < 0 || (size_t)n >= sizeof(chunk)) {
            // Per-slot truncation: send what we have so far, abort the rest.
            httpd_resp_send_chunk(req, "]", 1);
            httpd_resp_send_chunk(req, nullptr, 0);
            return ESP_OK;
        }
        httpd_resp_send_chunk(req, chunk, (ssize_t)n);
    }
    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, nullptr, 0);  // end-of-chunks
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/brokers/<N>
// ---------------------------------------------------------------------------
// Parse N from the URI, parse JSON body into a BrokerConfig, validate,
// writeBrokerConfig, mqttBrokerPool().reloadSlot(N).
//
// Password / jwt_token semantics: if the body OMITS the field, we
// preserve the existing NVS value (so the UI can resend the masked
// "***" placeholder without overwriting the real secret). If the body
// contains the field and it is NOT "***", we use the supplied value
// (including empty string to clear).
esp_err_t handleSetBroker(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    // Parse slot from URI: "/api/brokers/<N>"
    const char* uri = req->uri;
    const char* prefix = "/api/brokers/";
    size_t prefix_len = strlen(prefix);
    if (strncmp(uri, prefix, prefix_len) != 0) {
        return send404(req, "bad_uri");
    }
    const char* slot_str = uri + prefix_len;
    if (*slot_str < '0' || *slot_str > '9') {
        return send400(req, "bad_slot");
    }
    char* end = nullptr;
    long slot_l = strtol(slot_str, &end, 10);
    if (slot_l < 0 || slot_l >= CROSSWIRE_MAX_BROKERS) {
        return send400(req, "slot_out_of_range");
    }
    uint8_t slot = (uint8_t)slot_l;
    // Accept exactly "/api/brokers/<N>" or "/api/brokers/<N>/" -- nothing
    // further. Trailing path segments (e.g. "/api/brokers/3/foo") would
    // otherwise silently route to slot 3, masking client bugs.
    if (*end != '\0' && !(*end == '/' && end[1] == '\0')) {
        return send404(req, "bad_uri");
    }

    // Read body.
    char body[kMaxBodyBytes];
    ssize_t blen = readBody(req, body, sizeof(body));
    if (blen < 0) return send400(req);

    // Load existing config so we can preserve omitted secrets.
    BrokerConfig cfg;
    readBrokerConfig(slot, cfg);

    // Parse top-level fields.
    bool found_bool;
    bool new_enabled = extractJsonBool(body, "enabled", &found_bool);
    if (found_bool) cfg.enabled = new_enabled;

    char tmp[512];
    if (extractJsonString(body, "url", tmp, sizeof(tmp))) {
        strncpy(cfg.url, tmp, sizeof(cfg.url));
        cfg.url[sizeof(cfg.url) - 1] = '\0';
    }
    uint32_t u;
    if (extractJsonUint(body, "port", &u))      cfg.port = (uint16_t)u;
    if (extractJsonUint(body, "transport", &u)) {
        if (u > 2) return send400(req, "bad_transport");
        cfg.transport = (BrokerTransport)u;
    }
    if (extractJsonUint(body, "auth_type", &u)) {
        if (u > 2) return send400(req, "bad_auth_type");
        cfg.auth_type = (BrokerAuthType)u;
    }
    if (extractJsonString(body, "username", tmp, sizeof(tmp))) {
        strncpy(cfg.username, tmp, sizeof(cfg.username));
        cfg.username[sizeof(cfg.username) - 1] = '\0';
    }
    if (extractJsonString(body, "password", tmp, sizeof(tmp))) {
        // "***" sentinel = "keep existing" (so the UI can echo the mask).
        if (strcmp(tmp, "***") != 0) {
            strncpy(cfg.password, tmp, sizeof(cfg.password));
            cfg.password[sizeof(cfg.password) - 1] = '\0';
        }
    }
    if (extractJsonString(body, "jwt_token", tmp, sizeof(tmp))) {
        if (strcmp(tmp, "***") != 0) {
            strncpy(cfg.jwt_token, tmp, sizeof(cfg.jwt_token));
            cfg.jwt_token[sizeof(cfg.jwt_token) - 1] = '\0';
        }
    }
    if (extractJsonString(body, "topic_prefix", tmp, sizeof(tmp))) {
        strncpy(cfg.topic_prefix, tmp, sizeof(cfg.topic_prefix));
        cfg.topic_prefix[sizeof(cfg.topic_prefix) - 1] = '\0';
    }
    if (extractJsonString(body, "iata_override", tmp, sizeof(tmp))) {
        strncpy(cfg.iata_override, tmp, sizeof(cfg.iata_override));
        cfg.iata_override[sizeof(cfg.iata_override) - 1] = '\0';
    }
    if (extractJsonString(body, "jwt_audience", tmp, sizeof(tmp))) {
        strncpy(cfg.jwt_audience, tmp, sizeof(cfg.jwt_audience));
        cfg.jwt_audience[sizeof(cfg.jwt_audience) - 1] = '\0';
    }
    if (extractJsonUint(body, "jwt_refresh_sec", &u)) {
        cfg.jwt_refresh_sec = u;
    }
    if (extractJsonString(body, "ca_cert_name", tmp, sizeof(tmp))) {
        strncpy(cfg.ca_cert_name, tmp, sizeof(cfg.ca_cert_name));
        cfg.ca_cert_name[sizeof(cfg.ca_cert_name) - 1] = '\0';
    }

    // Cross-field validation.
    if (cfg.enabled && cfg.url[0] == '\0') {
        return send400(req, "enabled_requires_url");
    }

    // auth_type vs credentials cross-validation. Runs against the MERGED
    // cfg (post "***" keep-existing replacement), so a UI that resends
    //   {auth_type: 1, username: "u", password: "***"}
    // validates clean as long as the previously-stored password was non-
    // empty. Returns a structured 400 naming the missing credential
    // class so the SPA can surface a specific error.
    const char* missing_required = nullptr;
    switch (cfg.auth_type) {
        case BrokerAuthType::None:
            // No credentials required; do not enforce anything.
            break;
        case BrokerAuthType::Basic:
            if (cfg.username[0] == '\0' || cfg.password[0] == '\0') {
                missing_required = "basic_username_password";
            }
            break;
        case BrokerAuthType::Jwt:
            if (cfg.jwt_token[0] == '\0') {
                missing_required = "jwt_token";
            }
            break;
    }
    if (missing_required != nullptr) {
        // Two-key envelope; same shape family as the simple
        // {"error":"<code>"} the other 400 sites in this file emit.
        // Named err_body (not body) to avoid shadowing the outer
        // request-body buffer declared above.
        char err_body[96];
        int bn = snprintf(err_body, sizeof(err_body),
                          "{\"error\":\"auth_credentials_missing\",\"required\":\"%s\"}\n",
                          missing_required);
        if (bn < 0 || (size_t)bn >= sizeof(err_body)) bn = (int)sizeof(err_body) - 1;
        return sendJsonStatus(req, "400 Bad Request", err_body, (size_t)bn);
    }

    // Persist + reload.
    if (!writeBrokerConfig(slot, cfg)) {
        return send500(req, "broker_write_failed");
    }
    bool reloaded = wifiObserverPool().reloadSlot(slot);
    // reloadSlot can return false if the pool was never begin()'d (WiFi
    // not yet up). That's not a failure mode for the config write -- the
    // pool will pick up the new config on its next begin path. Return
    // 200 either way; include a reloaded flag for visibility.
    char body_out[80];
    int n = snprintf(body_out, sizeof(body_out),
                     "{\"ok\":true,\"reloaded\":%s}\n",
                     reloaded ? "true" : "false");
    return sendJsonStatus(req, "200 OK", body_out, (size_t)n);
}

// ---------------------------------------------------------------------------
// GET /api/wifi
// ---------------------------------------------------------------------------
// Returns the currently-saved SSID (NOT the password) + current connection
// state.
esp_err_t handleGetWifi(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    Preferences p;
    p.begin("wifi", /*readOnly=*/true);
    String saved_ssid = p.getString("ssid", "");
    p.end();

    char ssid_e[80];
    escapeForJson(saved_ssid.c_str(), ssid_e, sizeof(ssid_e));

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "{\"saved_ssid\":\"%s\",\"connected\":%s,"
                     "\"current_ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d}\n",
                     ssid_e,
                     (WiFi.status() == WL_CONNECTED) ? "true" : "false",
                     WiFi.SSID().c_str(),
                     WiFi.localIP().toString().c_str(),
                     WiFi.RSSI());
    return sendJsonStatus(req, "200 OK", buf, (size_t)n);
}

// ---------------------------------------------------------------------------
// POST /api/wifi
// ---------------------------------------------------------------------------
// Body: {"ssid":"...","psk":"..."}
// Writes to NVS (namespace "wifi", keys "ssid"/"pwd" -- matching
// WifiBootstrap's begin path), then schedules an async reconnect + reboot.
// We reboot rather than just reconnect because WifiBootstrap caches the
// SSID at begin(); without a reboot, WiFi.reconnect() would attempt to
// reconnect using the previously-active creds (in-memory) rather than
// the freshly-written NVS values.
esp_err_t handleSetWifi(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    char body[512];
    ssize_t blen = readBody(req, body, sizeof(body));
    if (blen < 0) return send400(req);

    char ssid[64];
    char psk[96];
    if (!extractJsonString(body, "ssid", ssid, sizeof(ssid))) {
        return send400(req, "missing_ssid");
    }
    // PSK may be empty (open networks are allowed).
    if (!extractJsonString(body, "psk", psk, sizeof(psk))) {
        psk[0] = '\0';
    }
    if (ssid[0] == '\0') return send400(req, "empty_ssid");

    // Persist. WifiBootstrap reads "wifi" namespace, keys "ssid" + "pwd".
    Preferences p;
    if (!p.begin("wifi", /*readOnly=*/false)) {
        memset(psk, 0, sizeof(psk));
        return send500(req, "nvs_open_failed");
    }
    p.putString("ssid", ssid);
    p.putString("pwd",  psk);
    p.end();
    memset(psk, 0, sizeof(psk));

    // Schedule the reboot AFTER the response flushes. 2s reconnect probe
    // + 5s reboot is enough to ship the 200 cleanly.
    webServerScheduleWifiReconnect(2000);
    webServerScheduleReboot(5000);

    return sendJson200(req,
                       "{\"ok\":true,\"message\":\"will_reboot_to_apply\","
                       "\"reboot_in_ms\":5000}\n");
}

// ---------------------------------------------------------------------------
// GET /api/wifi/scan
// ---------------------------------------------------------------------------
// Returns a list of {ssid, rssi, channel, encryption}.
//
// We use WiFi.scanNetworks(async=true) to avoid blocking the handler.
// The first call returns -1 (or -2 = SCAN_RUNNING). Subsequent calls
// either return the count (scan done) or -1 (still running).
//
// To give the UI a useful response on the first call, we cache the last
// successful scan result and return it (with a "stale":true flag) while
// a new scan is in flight. Cache TTL = 30s; older than that and we force
// a fresh scan + return whatever's cached (or [] if no cache).
esp_err_t handleScanWifi(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    static uint32_t s_last_scan_ms = 0;
    static bool     s_scan_in_flight = false;
    constexpr uint32_t kCacheTtlMs = 30000;

    uint32_t now = (uint32_t)millis();
    int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        s_scan_in_flight = true;
        n = -1;  // surface as stale below
    } else if (n == WIFI_SCAN_FAILED) {
        n = -1;
    } else {
        s_scan_in_flight = false;
        // n >= 0 means a result set is sitting in the WiFi driver.
        s_last_scan_ms = now;
    }

    // Kick off a fresh scan if no scan is running AND cache is stale
    // (or we've never scanned).
    if (!s_scan_in_flight) {
        bool stale = (s_last_scan_ms == 0) || (now - s_last_scan_ms > kCacheTtlMs);
        if (stale) {
            // hidden=true so we see hidden SSIDs too; async=true returns
            // immediately. Don't block on this call.
            WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true,
                              /*passive=*/false, /*max_ms_per_chan=*/300);
            s_scan_in_flight = true;
        }
    }

    // Build response with whatever's in the driver cache.
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    char head[64];
    int hn = snprintf(head, sizeof(head),
                      "{\"in_flight\":%s,\"networks\":[",
                      s_scan_in_flight ? "true" : "false");
    httpd_resp_send_chunk(req, head, hn);

    if (n > 0) {
        char chunk[256];
        for (int i = 0; i < n; ++i) {
            String ssid = WiFi.SSID(i);
            char ssid_e[80];
            escapeForJson(ssid.c_str(), ssid_e, sizeof(ssid_e));
            int cn = snprintf(chunk, sizeof(chunk),
                              "%s{\"ssid\":\"%s\",\"rssi\":%d,"
                              "\"channel\":%d,\"encryption\":%u}",
                              (i == 0) ? "" : ",",
                              ssid_e,
                              (int)WiFi.RSSI(i),
                              (int)WiFi.channel(i),
                              (unsigned)WiFi.encryptionType(i));
            if (cn < 0 || (size_t)cn >= sizeof(chunk)) break;
            httpd_resp_send_chunk(req, chunk, (ssize_t)cn);
        }
    }
    httpd_resp_send_chunk(req, "]}\n", 3);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /api/cli
// ---------------------------------------------------------------------------
// Body: {"line":"..."} -> {"result":"ok|denied|unknown","response":"..."}.
// Pass through to CliPassthrough which enforces the allowlist.
esp_err_t handleCli(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    char body[512];
    ssize_t blen = readBody(req, body, sizeof(body));
    if (blen < 0) return send400(req);

    char line[256];
    if (!extractJsonString(body, "line", line, sizeof(line))) {
        return send400(req, "missing_line");
    }

    char response[512];
    CliResult rc = cliPassthroughExecute(line, response, sizeof(response));
    const char* rc_str =
        (rc == CliResult::Ok)      ? "ok" :
        (rc == CliResult::Denied)  ? "denied" : "unknown";

    char escaped[1024];
    escapeForJson(response, escaped, sizeof(escaped));

    char out[1280];
    int n = snprintf(out, sizeof(out),
                     "{\"result\":\"%s\",\"response\":\"%s\"}\n",
                     rc_str, escaped);
    if (n < 0 || (size_t)n >= sizeof(out)) return send500(req, "cli_truncated");
    return sendJsonStatus(req, "200 OK", out, (size_t)n);
}

// ---------------------------------------------------------------------------
// GET /api/ble (companion-only)
// POST /api/ble/reset_pin (companion-only)
// ---------------------------------------------------------------------------
#ifdef CROSSWIRE_OBSERVER_BLE_COMPANION

// NVS slot for runtime-overridden PIN. We write here from reset_pin;
// WebAuth currently reads BLE_PIN_CODE (the build-time macro) -- a
// future Plan task may wire WebAuth to prefer this NVS value. For
// now the handler persists it so the surface is there for that pickup.
static const char* kNvsWeb        = "web";
static const char* kKeyLastBlePin = "last_ble_pin";

esp_err_t handleGetBle(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    // pubkey_hex: 64 lowercase hex chars from s_identity_pubkey (set
    // by webApiInitIdentityPubkey from main.cpp post-the_mesh.begin).
    char pubkey_hex[65];
    if (s_identity_pubkey != nullptr) {
        static const char H[] = "0123456789abcdef";
        for (int i = 0; i < 32; ++i) {
            pubkey_hex[i * 2]     = H[(s_identity_pubkey[i] >> 4) & 0xF];
            pubkey_hex[i * 2 + 1] = H[ s_identity_pubkey[i]       & 0xF];
        }
        pubkey_hex[64] = '\0';
    } else {
        pubkey_hex[0] = '\0';
    }

    // current_pin: prefer NVS-stored override, fall back to BLE_PIN_CODE.
    char pin_str[8];
    Preferences p;
    bool have_override = false;
    if (p.begin(kNvsWeb, /*readOnly=*/true)) {
        uint32_t stored = p.getULong(kKeyLastBlePin, 0xFFFFFFFFu);
        p.end();
        if (stored != 0xFFFFFFFFu && stored < 1000000u) {
            snprintf(pin_str, sizeof(pin_str), "%06u", (unsigned)stored);
            have_override = true;
        }
    }
    if (!have_override) {
#ifdef BLE_PIN_CODE
        snprintf(pin_str, sizeof(pin_str), "%06u", (unsigned)BLE_PIN_CODE);
#else
        snprintf(pin_str, sizeof(pin_str), "------");
#endif
    }

    // paired_count: MeshCore's BLE companion stack doesn't expose a
    // bond-count accessor we can call from here without a heavy include.
    // Returning -1 as a sentinel "unavailable" -- the UI shows "n/a".
    // File-level note: when the BLE companion stack gains a public
    // getBondedCount(), replace -1 with the real value.
    int paired_count = -1;

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "{\"pubkey_hex\":\"%s\",\"current_pin\":\"%s\","
                     "\"paired_count\":%d,\"pin_overridden\":%s}\n",
                     pubkey_hex, pin_str, paired_count,
                     have_override ? "true" : "false");
    return sendJsonStatus(req, "200 OK", buf, (size_t)n);
}

esp_err_t handleResetBlePin(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    // Drain any body (the UI may POST {}); ignore its content.
    char body[64];
    (void)readBody(req, body, sizeof(body));

    // 6-digit pin uniformly in [0, 999999]. esp_random returns 32 bits;
    // mod by 1_000_000 is mildly biased toward low values (2^32 / 10^6
    // = 4294.967, so digits in [0, 4294 * 1e6 % 2^32) are ~4295 times
    // hit vs the rest ~4294 times). For a 6-digit PIN that's an
    // immaterial bias; we don't need rejection sampling here.
    uint32_t pin = esp_random() % 1000000u;

    Preferences p;
    if (!p.begin(kNvsWeb, /*readOnly=*/false)) {
        return send500(req, "nvs_open_failed");
    }
    p.putULong(kKeyLastBlePin, pin);
    p.end();

    Serial.printf("[WebApi] BLE PIN reset to %06u (NVS write only; reboot to apply)\n",
                  (unsigned)pin);

    char buf[96];
    int n = snprintf(buf, sizeof(buf),
                     "{\"ok\":true,\"new_pin\":\"%06u\","
                     "\"note\":\"reboot_to_apply\"}\n",
                     (unsigned)pin);
    return sendJsonStatus(req, "200 OK", buf, (size_t)n);
}

#endif  // CROSSWIRE_OBSERVER_BLE_COMPANION

// ---------------------------------------------------------------------------
// POST /api/factory_reset
// ---------------------------------------------------------------------------
// Erase all observer-owned NVS namespaces + the LittleFS web cert, then
// schedule a reboot.
// Idempotent: missing namespaces are no-ops.
esp_err_t handleFactoryReset(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    static const char* kNamespaces[] = {
        "wifi", "mqtt", "mqtt_b0", "mqtt_b1", "mqtt_b2",
        "mqtt_b3", "mqtt_b4", "mqtt_b5", "observer", "web",
        nullptr
    };
    for (int i = 0; kNamespaces[i] != nullptr; ++i) {
        nvs_handle_t h;
        if (nvs_open(kNamespaces[i], NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
            Serial.printf("[WebApi] factory_reset: erased NVS namespace '%s'\n",
                          kNamespaces[i]);
        }
    }

    // Erase LittleFS web cert + key (WebCertStore will regen on next boot).
    if (LittleFS.exists("/web_cert.pem")) LittleFS.remove("/web_cert.pem");
    if (LittleFS.exists("/web_key.pem"))  LittleFS.remove("/web_key.pem");

    webServerScheduleReboot(3000);
    return sendJson200(req,
                       "{\"ok\":true,\"message\":\"factory_reset_scheduled\","
                       "\"reboot_in_ms\":3000}\n");
}

// ---------------------------------------------------------------------------
// POST /api/regenerate_cert
// ---------------------------------------------------------------------------
// Regenerate the self-signed TLS cert. The browser will see a "new cert"
// warning on next connect (different fingerprint). We CANNOT live-swap
// the cert -- esp_https_server doesn't support it -- so the cert store
// regenerate is followed by a server restart, scheduled deferred so the
// response can flush first.
//
// Implementation detail: we don't call webServerStart() inline here.
// Plan 3 Task 11 will wire the WifiObserver loop to handle a cert-
// regenerated flag -- for the Plan 3 build, the simplest correct path
// is to schedule a full reboot (~3s) so the device comes back up with
// the new cert via the normal WebCertStore::begin flow.
esp_err_t handleRegenerateCert(httpd_req_t* req) {
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    // Regenerate using the same key type the server started with
    // (currently EC P-256; see WebServer.cpp start path).
    bool ok = webCertStoreRegenerate(TlsKeyType::EcP256);
    if (!ok) return send500(req, "cert_regenerate_failed");

    // Schedule reboot to bring server up with new cert. 3 seconds is
    // enough to flush the response + let the browser see it.
    webServerScheduleReboot(3000);
    return sendJson200(req,
                       "{\"ok\":true,\"message\":\"cert_regenerated\","
                       "\"reboot_in_ms\":3000,"
                       "\"note\":\"browser_will_show_new_cert_warning\"}\n");
}

// ---------------------------------------------------------------------------
// POST /api/ota
// ---------------------------------------------------------------------------
// Plan 4 implements; Plan 3 returns 501 with a structured envelope.
// We keep this handler updated (vs the old sendStub) so the response
// shape matches the rest of the API.
esp_err_t handleOta(httpd_req_t* req) {
    // Auth still required (route is wired through authPostTrampoline);
    // re-confirm the session for the 423 gate so an authenticated user
    // mid-password-change doesn't even see a 501 -- they see the 423
    // pointing them at change_password first.
    WebSession* sess = webServerCurrentSession(req);
    if (!sess) return send401(req);
    if (!requirePwOkay(req, sess)) return ESP_OK;

    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    const char body[] = "{\"error\":\"not_implemented\",\"plan\":\"4\","
                        "\"route\":\"/api/ota\"}\n";
    httpd_resp_send(req, body, sizeof(body) - 1);
    return ESP_OK;
}

}  // namespace WebApi
}  // namespace crosswire

#else  // !ARDUINO

// Host-build: provide the init function as a no-op so a transitive
// include of this header from a future host test doesn't fail to link.
namespace crosswire {
namespace WebApi {
void webApiInitIdentityPubkey(const uint8_t*) {}
}  // namespace WebApi
}  // namespace crosswire

#endif  // ARDUINO
