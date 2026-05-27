// src/helpers/wifi_observer/WebServer.cpp
//
// HTTPS server lifecycle + route dispatcher. See WebServer.h for
// design notes. Strycher/LoRa#272 (Plan 3 Task 7).

#include "WebServer.h"
#include "WifiObserverConfig.h"

#include <string.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <WiFi.h>
  #include <esp_err.h>
  #include <esp_http_server.h>
  #include <esp_https_server.h>
  #include <esp_system.h>          // esp_restart()
  #include <lwip/sockets.h>        // getpeername + sockaddr_in
  // See WebAuth.cpp -- mbedtls 2.x ships constant_time.h without
  // extern "C" guards; force C linkage so the C++-mangled reference
  // matches libmbedcrypto's C symbol.
  extern "C" {
  #include <mbedtls/constant_time.h>
  }

  #include "WebCertStore.h"
  #include "WebSession.h"
  #include "WebApi.h"
  #include "WebUiAssets.h"
#endif

namespace crosswire {

#ifdef ARDUINO

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
// httpd_handle is the running server (NULL if stopped).
//
// s_cert_pem / s_key_pem are pointers into WebCertStore's heap-allocated
// PEM buffers. WebCertStore guarantees these pointers remain valid until
// webCertStoreShutdown() or webCertStoreRegenerate() is called -- both of
// which we sequence around webServerStop()/webServerStart() ourselves, so
// the lifetime requirement of httpd_ssl_config_t (which only reads the
// pointers during httpd_ssl_start; see esp_https_server.h docstring "Does
// not have to stay valid after calling this function") is comfortably met.
// We still cache them here for the regenerate-swap path and for diagnostics.
static httpd_handle_t s_httpd      = nullptr;
static const char*    s_cert_pem   = nullptr;
static const char*    s_key_pem    = nullptr;

// Start-failure backoff state. Without this, a permanent cert-gen failure
// (e.g., heap fragmentation -> mbedtls esp-sha buffer alloc fails) causes
// the wifiObserverLoop's `if (!webServerIsRunning()) webServerStart();`
// gate to retry every loop tick (~4-5 Hz), which:
//   1. spams Serial with the same failure messages,
//   2. fragments heap further on every attempt, and
//   3. starves the BLE stack of allocator headroom -> BLE goes dead.
// Discovered post-deploy on xiao-bench-1 2026-05-27 (Strycher/LoRa#XXX):
// after a manual reboot the system stayed in BLE-dead state for 7+ min
// of retry loop before user could investigate. Fix: per-failure
// exponential backoff (30s -> 60s -> 120s -> ... cap 5min); cleared on
// successful start OR explicit restart (regenerate-cert path).
static bool     s_start_in_backoff      = false;
static uint32_t s_start_next_retry_ms   = 0;     // millis() deadline
static uint32_t s_start_retry_count     = 0;     // attempts since last success
static const uint32_t kStartMinRetryMs  = 30000;   // 30s after first failure
static const uint32_t kStartMaxRetryMs  = 300000;  // cap at 5 min

// ---------------------------------------------------------------------------
// Cookie + header parsing helpers
// ---------------------------------------------------------------------------

// Defensive cap on Cookie header length we'll allocate on the stack.
// 256 bytes is generous for the cookies we issue (cw_sid + cw_csrf are
// each ~50 bytes with attributes); a longer header is either a probe
// or a misconfigured client and we should reject early.
static constexpr size_t kMaxCookieHeader   = 256;
static constexpr size_t kSidHexLen         = 32;
static constexpr size_t kCsrfHexLen        = 32;
static constexpr size_t kMaxCsrfHeader     = 64;

// Parse a cookie header value, find `name=`, and copy the value into
// `out` (NUL-terminated). Returns true on a clean parse, false on
// not-found / malformed / overflow. Does NOT do percent-decoding -- our
// sids are hex literals.
static bool extractCookieValue(const char* header, const char* name,
                               char* out, size_t out_len) {
    if (!header || !name || !out || out_len == 0) return false;
    out[0] = '\0';

    const size_t name_len = strlen(name);
    const char*  p        = header;

    // Cookie header is a "; "-separated list of name=value pairs. We
    // walk it tolerant of whitespace and the trailing-no-semicolon case.
    while (*p) {
        // Skip leading whitespace + separators.
        while (*p == ' ' || *p == '\t' || *p == ';') ++p;
        if (!*p) break;

        // Does this pair start with our target name + '='?
        bool match = (strncmp(p, name, name_len) == 0) && (p[name_len] == '=');

        // Advance past the name=, regardless.
        const char* val_start = nullptr;
        if (match) {
            val_start = p + name_len + 1;
            p         = val_start;
        }

        // Walk to the next ';' or end-of-string -- that's the end of
        // this pair's value (whether we're matching or skipping).
        const char* val_end = p;
        while (*val_end && *val_end != ';') ++val_end;

        if (match) {
            size_t vlen = (size_t)(val_end - val_start);
            if (vlen >= out_len) {
                // Overflow guard: don't truncate a sid silently.
                out[0] = '\0';
                return false;
            }
            memcpy(out, val_start, vlen);
            out[vlen] = '\0';
            return true;
        }

        p = val_end;
    }
    return false;
}

// Read the Cookie: header (if any) and return the cw_sid= value in
// `out_sid` (NUL-terminated). Returns false if no cookie or no sid.
static bool readSidCookie(httpd_req_t* req, char* out_sid, size_t out_len) {
    if (!req || !out_sid || out_len == 0) return false;
    out_sid[0] = '\0';

    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (hdr_len == 0 || hdr_len > kMaxCookieHeader) return false;

    char buf[kMaxCookieHeader + 1];
    if (httpd_req_get_hdr_value_str(req, "Cookie", buf, sizeof(buf)) != ESP_OK) {
        return false;
    }
    buf[kMaxCookieHeader] = '\0';  // belt + suspenders

    return extractCookieValue(buf, "cw_sid", out_sid, out_len);
}

// Read the X-Csrf-Token: header. Returns false if missing / oversize.
static bool readCsrfHeader(httpd_req_t* req, char* out, size_t out_len) {
    if (!req || !out || out_len == 0) return false;
    out[0] = '\0';
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "X-Csrf-Token");
    if (hdr_len == 0 || hdr_len > kMaxCsrfHeader) return false;
    return httpd_req_get_hdr_value_str(req, "X-Csrf-Token", out, out_len) == ESP_OK;
}

// ---------------------------------------------------------------------------
// Auth + CSRF middleware
// ---------------------------------------------------------------------------

// On 401: clear the cw_sid cookie and send a short JSON envelope.
// The cleared cookie carries Max-Age=0 + same path so the browser
// drops its stale sid; the next request comes in without one and the
// login UI can ask for credentials cleanly.
static esp_err_t sendUnauthorized(httpd_req_t* req) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    // Cookie attributes pulled from the kCwSidCookieAttrs constant in
    // WebSession.h so this clear stays bit-identical to the issue/clear
    // sites in WebApi.cpp; drift here would make the browser treat the
    // cleared cookie as a different cookie and the stale sid would
    // survive the 401.
    char sid_clear[128];
    snprintf(sid_clear, sizeof(sid_clear),
             "cw_sid=; Max-Age=0; %s", kCwSidCookieAttrs);
    httpd_resp_set_hdr(req, "Set-Cookie", sid_clear);
    const char body[] = "{\"error\":\"unauthorized\"}\n";
    httpd_resp_send(req, body, sizeof(body) - 1);
    return ESP_OK;
}

static esp_err_t sendForbidden(httpd_req_t* req, const char* reason) {
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    char body[96];
    int n = snprintf(body, sizeof(body),
                     "{\"error\":\"forbidden\",\"reason\":\"%s\"}\n",
                     reason ? reason : "csrf");
    if (n < 0 || (size_t)n >= sizeof(body)) n = (int)sizeof(body) - 1;
    httpd_resp_send(req, body, n);
    return ESP_OK;
}

// Look up the session by cw_sid cookie. On miss / timeout / no cookie,
// send 401 (with cookie clear) and return nullptr. Callers MUST check
// the return value before touching it.
//
// NOTE: WebSession::webSessionLookup is constant-time wrt the sid
// comparison; the only timing variance here is the cookie-header-parse
// loop above, which is bounded + uniform.
static WebSession* requireAuth(httpd_req_t* req) {
    char sid[kSidHexLen + 1];
    if (!readSidCookie(req, sid, sizeof(sid))) {
        sendUnauthorized(req);
        return nullptr;
    }
    WebSession* sess = webSessionLookup(sid, (uint32_t)millis());
    if (!sess) {
        sendUnauthorized(req);
        return nullptr;
    }
    return sess;
}

// Verify the X-Csrf-Token header matches the session's csrf token.
// Constant-time compare via mbedtls_ct_memcmp (same primitive WebAuth
// + WebSession use). On mismatch, send 403 and return false.
//
// Strict length match required -- the csrf token is a 32-hex-char
// literal we generated; anything else is a tampered request.
static bool requireCsrf(httpd_req_t* req, WebSession* sess) {
    if (!sess) return false;
    char hdr[kMaxCsrfHeader + 1];
    if (!readCsrfHeader(req, hdr, sizeof(hdr))) {
        sendForbidden(req, "csrf_missing");
        return false;
    }
    if (strlen(hdr) != kCsrfHexLen || strlen(sess->csrf) != kCsrfHexLen) {
        sendForbidden(req, "csrf_malformed");
        return false;
    }
    if (mbedtls_ct_memcmp(hdr, sess->csrf, kCsrfHexLen) != 0) {
        sendForbidden(req, "csrf_mismatch");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Static asset handlers (PROGMEM)
// ---------------------------------------------------------------------------

namespace {

// Generic PROGMEM asset serve: sets Content-Type, sends the byte slice.
// httpd_resp_send copies the bytes into its internal TLS buffer before
// returning, so passing a pointer into rodata/flash is safe and does
// not require keeping the buffer alive past this call.
esp_err_t sendAsset(httpd_req_t* req,
                    const char* bytes, size_t len,
                    const char* content_type) {
    if (!bytes || !content_type) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    httpd_resp_set_type(req, content_type);
    // Cache for 5 minutes on the placeholder bundle. Task 9 may tune
    // this once the real bundles ship; the value is intentionally
    // short during Plan 3 development to avoid sticky browser caches
    // masking iteration. Public so any LAN intermediary can cache too.
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=300");
    httpd_resp_send(req, bytes, (ssize_t)len);
    return ESP_OK;
}

esp_err_t serveIndex(httpd_req_t* req) {
    return sendAsset(req,
                     WebUiAssets::index_html,
                     WebUiAssets::index_html_len,
                     WebUiAssets::index_html_content_type);
}
esp_err_t serveAppJs(httpd_req_t* req) {
    return sendAsset(req,
                     WebUiAssets::app_js,
                     WebUiAssets::app_js_len,
                     WebUiAssets::app_js_content_type);
}
esp_err_t serveAppCss(httpd_req_t* req) {
    return sendAsset(req,
                     WebUiAssets::app_css,
                     WebUiAssets::app_css_len,
                     WebUiAssets::app_css_content_type);
}

}  // namespace

// ---------------------------------------------------------------------------
// Middleware-wrapping trampolines
// ---------------------------------------------------------------------------
// esp_http_server takes a plain function pointer per URI. To avoid
// rewriting every WebApi handler to call requireAuth+requireCsrf
// itself, we register thin trampolines that do the middleware here
// and then dispatch to the bare handler. Task 8 keeps WebApi handlers
// middleware-free; if the policy for a given route changes (e.g.,
// "logout no longer requires CSRF"), it's a one-line change here.

namespace {

// State-changing authenticated routes (POST): auth + csrf, then dispatch.
// `H` is the bare WebApi handler.
template <esp_err_t (*H)(httpd_req_t*)>
esp_err_t authPostTrampoline(httpd_req_t* req) {
    WebSession* sess = requireAuth(req);
    if (!sess) return ESP_OK;
    if (!requireCsrf(req, sess)) return ESP_OK;
    return H(req);
}

// Read-only authenticated routes (GET): auth, then dispatch. No CSRF
// because the request isn't state-changing -- a cookie-bound GET with
// no body has no replayable side effect.
template <esp_err_t (*H)(httpd_req_t*)>
esp_err_t authGetTrampoline(httpd_req_t* req) {
    WebSession* sess = requireAuth(req);
    if (!sess) return ESP_OK;
    return H(req);
}

}  // namespace

// ---------------------------------------------------------------------------
// URI registration
// ---------------------------------------------------------------------------

// Helper: register one URI, log + return false on failure.
static bool registerOne(httpd_handle_t h, const char* uri, httpd_method_t method,
                        esp_err_t (*handler)(httpd_req_t*)) {
    httpd_uri_t entry = {};
    entry.uri      = uri;
    entry.method   = method;
    entry.handler  = handler;
    entry.user_ctx = nullptr;
    esp_err_t rc = httpd_register_uri_handler(h, &entry);
    if (rc != ESP_OK) {
        Serial.printf("[WebServer] register_uri %s %d -> err=%d\n",
                      uri, (int)method, (int)rc);
        return false;
    }
    return true;
}

static bool registerRoutes(httpd_handle_t h) {
    bool ok = true;

    // ---- Static UI (unauthenticated; the login page lives here) -----------
    ok &= registerOne(h, "/",         HTTP_GET,  &serveIndex);
    ok &= registerOne(h, "/app.js",   HTTP_GET,  &serveAppJs);
    ok &= registerOne(h, "/app.css",  HTTP_GET,  &serveAppCss);

    // ---- Unauthenticated REST ---------------------------------------------
    // /api/login: no cookie required (this IS the cookie-issuing route).
    // /api/logout: deliberately auth-free so a stale cookie can be cleared
    // without 401-loop pain; the handler still rotates / drops the session.
    ok &= registerOne(h, "/api/login",  HTTP_POST, &WebApi::handleLogin);
    ok &= registerOne(h, "/api/logout", HTTP_POST, &WebApi::handleLogout);

    // ---- Authenticated state-changing (POST: auth + csrf) -----------------
    ok &= registerOne(h, "/api/change_password",
                      HTTP_POST, &authPostTrampoline<&WebApi::handleChangePassword>);
    // NOTE: spec text says `POST /api/brokers/<N>` (path-templated index).
    // esp_http_server's default uri_match_fn (strcmp) does NOT do path
    // params -- a request like POST /api/brokers/3 needs the wildcard
    // match function. Register the parent path with wildcard; the handler
    // parses N out of req->uri at dispatch time. We set the server's
    // uri_match_fn to httpd_uri_match_wildcard in the start path.
    ok &= registerOne(h, "/api/brokers",
                      HTTP_GET,  &authGetTrampoline<&WebApi::handleGetBrokers>);
    ok &= registerOne(h, "/api/brokers/*",
                      HTTP_POST, &authPostTrampoline<&WebApi::handleSetBroker>);
    ok &= registerOne(h, "/api/wifi",
                      HTTP_GET,  &authGetTrampoline<&WebApi::handleGetWifi>);
    ok &= registerOne(h, "/api/wifi",
                      HTTP_POST, &authPostTrampoline<&WebApi::handleSetWifi>);
    ok &= registerOne(h, "/api/wifi/scan",
                      HTTP_GET,  &authGetTrampoline<&WebApi::handleScanWifi>);
    ok &= registerOne(h, "/api/cli",
                      HTTP_POST, &authPostTrampoline<&WebApi::handleCli>);

    // ---- Read-only authenticated GETs -------------------------------------
    ok &= registerOne(h, "/api/status",
                      HTTP_GET,  &authGetTrampoline<&WebApi::handleGetStatus>);

    // ---- Companion-only BLE routes ----------------------------------------
#ifdef CROSSWIRE_OBSERVER_BLE_COMPANION
    ok &= registerOne(h, "/api/ble",
                      HTTP_GET,  &authGetTrampoline<&WebApi::handleGetBle>);
    ok &= registerOne(h, "/api/ble/reset_pin",
                      HTTP_POST, &authPostTrampoline<&WebApi::handleResetBlePin>);
#endif

    // ---- Destructive admin -------------------------------------------------
    ok &= registerOne(h, "/api/factory_reset",
                      HTTP_POST, &authPostTrampoline<&WebApi::handleFactoryReset>);
    ok &= registerOne(h, "/api/regenerate_cert",
                      HTTP_POST, &authPostTrampoline<&WebApi::handleRegenerateCert>);

    // ---- Plan-4 placeholder (Plan 3 returns 501 from the WebApi stub) -----
    ok &= registerOne(h, "/api/ota",
                      HTTP_POST, &authPostTrampoline<&WebApi::handleOta>);

    return ok;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool webServerIsRunning() {
    return s_httpd != nullptr;
}

// ---------------------------------------------------------------------------
// Public helpers exposed to WebApi (Task 8 Option Y)
// ---------------------------------------------------------------------------

// Resolve session from the cw_sid cookie on an in-flight request.
// The trampoline ran requireAuth first, so a non-null return is the
// normal path here. The nullptr guard exists for safety; handlers
// MUST still null-check before deref.
WebSession* webServerCurrentSession(httpd_req_t* req) {
    if (!req) return nullptr;
    char sid[kSidHexLen + 1];
    if (!readSidCookie(req, sid, sizeof(sid))) return nullptr;
    return webSessionLookup(sid, (uint32_t)millis());
}

// Resolve client IPv4 address (network byte order) via the underlying
// socket. esp_http_server exposes the socket fd via httpd_req_to_sockfd;
// getpeername gives us the sockaddr. Returns 0 on any failure.
//
// IPv6 note: this build is STA-only IPv4 today. If/when IPv6 lands,
// the throttle table will need a wider key -- file as follow-up if so.
uint32_t webServerClientIp(httpd_req_t* req) {
    if (!req) return 0;
    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0) return 0;
    struct sockaddr_in6 addr = {};
    socklen_t addr_len = sizeof(addr);
    if (getpeername(sockfd, (struct sockaddr*)&addr, &addr_len) < 0) {
        return 0;
    }
    if (addr.sin6_family == AF_INET) {
        const struct sockaddr_in* a4 = reinterpret_cast<const struct sockaddr_in*>(&addr);
        return a4->sin_addr.s_addr;  // already network byte order
    }
    // IPv4-mapped IPv6 (::ffff:a.b.c.d) -- pull the trailing 4 bytes.
    // sin6_addr.s6_addr[10..11] == 0xFF,0xFF marks the v4-mapped form.
    if (addr.sin6_family == AF_INET6 &&
        addr.sin6_addr.s6_addr[10] == 0xFF &&
        addr.sin6_addr.s6_addr[11] == 0xFF) {
        uint32_t v4;
        memcpy(&v4, &addr.sin6_addr.s6_addr[12], 4);
        return v4;  // network byte order
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Deferred operations (called from WebApi handlers, executed on loop tick)
// ---------------------------------------------------------------------------
// HTTPS handlers cannot synchronously WiFi.disconnect() or ESP.restart()
// without killing the response mid-flush. Each handler schedules the
// destructive op + returns 200; webServerLoopTick (called from
// wifiObserverLoop) runs the op once the grace period elapses.

static bool     s_wifi_reconnect_pending = false;
static uint32_t s_wifi_reconnect_at_ms   = 0;
static bool     s_reboot_pending         = false;
static uint32_t s_reboot_at_ms           = 0;

void webServerScheduleWifiReconnect(uint32_t delay_ms) {
    s_wifi_reconnect_pending = true;
    s_wifi_reconnect_at_ms   = (uint32_t)millis() + delay_ms;
    Serial.printf("[WebServer] WiFi reconnect scheduled in %u ms\n",
                  (unsigned)delay_ms);
}

void webServerScheduleReboot(uint32_t delay_ms) {
    s_reboot_pending = true;
    s_reboot_at_ms   = (uint32_t)millis() + delay_ms;
    Serial.printf("[WebServer] Reboot scheduled in %u ms\n",
                  (unsigned)delay_ms);
}

void webServerLoopTick() {
    uint32_t now = (uint32_t)millis();
    if (s_wifi_reconnect_pending &&
        (int32_t)(now - s_wifi_reconnect_at_ms) >= 0) {
        s_wifi_reconnect_pending = false;
        Serial.println("[WebServer] applying deferred WiFi reconnect");
        // Re-read NVS creds via WifiBootstrap on next tick: simplest path
        // is disconnect + begin(), which WifiBootstrap's StaConnecting state
        // already handles. But WifiBootstrap caches state; calling .begin()
        // again is the cleanest re-entry.
        WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
        // Force WifiBootstrap to re-read NVS + re-attempt. Avoid re-entering
        // its begin() (which re-runs the coex pref + mode set) -- the
        // simpler path is to nudge WiFi.begin() with the now-current NVS
        // creds. WifiBootstrap.loop() will then see WL_CONNECTED transition.
        // (Note: a future iteration may add WifiBootstrap::reapplyCreds()
        // for an explicit re-load API; for now we lean on Arduino's
        // WiFi.begin(ssid, pwd) discovering creds from prior config.)
        // The simplest correct primitive: WiFi.reconnect() if creds are
        // still in volatile state; otherwise the next StaConnecting tick
        // re-attempts. WifiBootstrap already re-attempts every 10s on
        // failure, so a passive WiFi.disconnect is sufficient to trigger
        // a fresh handshake with whatever creds NVS now holds (which
        // WifiBootstrap.begin pulled at boot; the new POST /api/wifi
        // wrote them but didn't re-bootstrap).
        //
        // BUG NOTE: this path will reconnect to the OLD SSID until the
        // next reboot, because WifiBootstrap caches ssid/pwd at begin().
        // The POST /api/wifi handler writes NVS; rebooting is the
        // simplest way to pick up the new SSID. The handler combines
        // schedule_wifi_reconnect WITH schedule_reboot for that reason
        // -- the reconnect line above is belt-and-suspenders, the
        // reboot is the real apply path.
        WiFi.reconnect();
    }
    if (s_reboot_pending && (int32_t)(now - s_reboot_at_ms) >= 0) {
        s_reboot_pending = false;
        Serial.println("[WebServer] applying deferred reboot");
        esp_restart();   // does not return
    }
}

void webServerStop() {
    if (s_httpd) {
        httpd_ssl_stop(s_httpd);
        s_httpd = nullptr;
    }
    // We intentionally do NOT call webCertStoreShutdown() here. The cert
    // store outlives the server: a cert regenerate path stops the server,
    // calls webCertStoreRegenerate(), then re-starts. Shutting the store
    // down on every stop would force a regenerate on every restart.
    s_cert_pem = nullptr;
    s_key_pem  = nullptr;
}

bool webServerStart() {
    // Idempotency: a re-start (e.g., post-cert-regen) drops the running
    // instance first. esp_https_server has no live cert swap. A restart
    // also clears the start-failure backoff -- the caller has explicitly
    // signaled "try again now" by going through the regenerate path.
    if (s_httpd) {
        Serial.println("[WebServer] restart: stopping existing instance");
        webServerStop();
        s_start_in_backoff    = false;
        s_start_retry_count   = 0;
        s_start_next_retry_ms = 0;
    }

    // Backoff gate: if a prior attempt failed and the retry deadline
    // hasn't elapsed, silently bail. This prevents the wifiObserverLoop
    // gate from chewing through heap with per-tick retries when cert gen
    // is permanently broken. See module-static-state comment above for
    // the failure cascade (BLE starvation) this prevents.
    uint32_t now_ms = millis();
    if (s_start_in_backoff && (int32_t)(now_ms - s_start_next_retry_ms) < 0) {
        return false;
    }

    auto schedule_backoff = [&](const char* reason) {
        // Exponential backoff with cap. retry_count is the FAILURE count
        // before this attempt; first failure -> 30s, second -> 60s, etc.
        uint32_t shift = s_start_retry_count > 4 ? 4 : s_start_retry_count;
        uint32_t delay_ms = kStartMinRetryMs << shift;
        if (delay_ms > kStartMaxRetryMs) delay_ms = kStartMaxRetryMs;
        s_start_next_retry_ms = now_ms + delay_ms;
        s_start_retry_count++;
        s_start_in_backoff    = true;
        Serial.printf("[WebServer] start failed (%s); attempt %u; "
                      "next retry in %u ms\n",
                      reason,
                      (unsigned)s_start_retry_count,
                      (unsigned)delay_ms);
    };

    // Load cert+key. WebCertStore guarantees the returned pointers stay
    // valid until shutdown / regenerate is called -- a future cert swap
    // is the only thing that invalidates them, and we sequence that via
    // webServerStop() in the regenerate handler.
    //
    // Key type: EC P-256 for fast generation (~50ms vs ~5-8s for RSA-2048)
    // and small cert (~250B). Modern browsers accept it; corporate-hardening
    // iteration may add an option to choose RSA-2048.
    if (!webCertStoreBegin(TlsKeyType::EcP256, &s_cert_pem, &s_key_pem)) {
        schedule_backoff("cert load/gen");
        return false;
    }
    if (!s_cert_pem || !s_key_pem) {
        schedule_backoff("cert store returned null");
        return false;
    }

    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();

    // Wildcard URI matching: needed so `/api/brokers/3` reaches the
    // `/api/brokers/*` handler. Default match is strcmp.
    conf.httpd.uri_match_fn = httpd_uri_match_wildcard;

    // The spec lists 17 routes; one of them (/api/brokers as GET vs
    // /api/brokers/* as POST) collapses to a 2-handler pair sharing
    // the same uri *prefix*, plus /api/wifi GET vs POST sharing one
    // uri string. Net result: 17 (uri, method) handler entries.
    // Default max_uri_handlers=8 is too small; raise to a comfortable
    // ceiling with headroom for Plan 4's OTA-status route.
    conf.httpd.max_uri_handlers = 24;

    // Default response header slots = 8; we use 2 (Set-Cookie, Cache-Control
    // or Content-Type). 8 is fine.

    // Default port_secure = 443. Explicit anyway to make the contract
    // obvious in `grep`.
    conf.port_secure = 443;

    // Cert + key wiring. `cacert_pem` is the misnamed "server cert" slot
    // in arduino-esp32's bundled esp_https_server (~v1.x ESP-IDF v4.4);
    // the header even documents the rename pending for IDF v5.0. The
    // PEMs are NUL-terminated; pass strlen+1 so mbedtls's PEM parser
    // (which scans for "-----END CERTIFICATE-----" inside the buffer)
    // sees the terminator -- mbedtls 2.x requires the length to include
    // the NUL or it returns MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT.
    conf.cacert_pem  = reinterpret_cast<const uint8_t*>(s_cert_pem);
    conf.cacert_len  = strlen(s_cert_pem) + 1;
    conf.prvtkey_pem = reinterpret_cast<const uint8_t*>(s_key_pem);
    conf.prvtkey_len = strlen(s_key_pem) + 1;

    esp_err_t rc = httpd_ssl_start(&s_httpd, &conf);
    if (rc != ESP_OK || !s_httpd) {
        Serial.printf("[WebServer] httpd_ssl_start failed: err=%d\n", (int)rc);
        s_httpd = nullptr;
        schedule_backoff("httpd_ssl_start");
        return false;
    }

    if (!registerRoutes(s_httpd)) {
        Serial.println("[WebServer] route registration FAILED; stopping");
        httpd_ssl_stop(s_httpd);
        s_httpd = nullptr;
        schedule_backoff("registerRoutes");
        return false;
    }

    // Success: clear the backoff latch so future restarts are clean.
    s_start_in_backoff    = false;
    s_start_retry_count   = 0;
    s_start_next_retry_ms = 0;

    Serial.println("[WebServer] listening on 0.0.0.0:443 (HTTPS)");
    return true;
}

#else  // !ARDUINO

// Host-build stubs. Same shape as WebCertStore.cpp -- returning false
// makes it obvious in a hypothetical host test that no server was
// actually brought up.
bool webServerStart()     { return false; }
void webServerStop()      {}
bool webServerIsRunning() { return false; }
void webServerScheduleWifiReconnect(uint32_t) {}
void webServerScheduleReboot(uint32_t) {}
void webServerLoopTick() {}

#endif  // ARDUINO

}  // namespace crosswire
