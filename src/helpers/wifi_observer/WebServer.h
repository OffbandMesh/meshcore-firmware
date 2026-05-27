// src/helpers/wifi_observer/WebServer.h
//
// HTTPS server on port 443 in STA mode. Owns the route table and
// request lifecycle. Routes dispatch into WebApi (REST handlers) or
// serve PROGMEM assets from WebUiAssets.
//
// Built on esp_https_server (ESP-IDF native, bundled with arduino-esp32).
//
// LIFECYCLE: webServerStart() is idempotent -- if already running, it
// stops first and re-loads cert+key before re-starting. This is the
// canonical way to swap in a freshly-regenerated cert (Task 8's
// /api/regenerate_cert handler calls webCertStoreRegenerate() then
// webServerStart()). esp_https_server does NOT expose live cert swap.
//
// THREAD-SAFETY: same single-task ownership discipline as the other
// wifi_observer modules. esp_https_server runs its own task internally
// for accept(), but URI handlers are dispatched on that task; our
// handlers reach into WebSession / WebAuth / WebCertStore which all
// assume single-task ownership. The implication: the WifiObserver loop
// task must NOT call into WebSession / WebAuth concurrently with an
// active HTTPS request. Plan 3 Task 11 handles this by quiescing the
// loop's session GC tick during the (very rare) cert regenerate
// window; routine GC ticks are safe because they only touch entries
// whose sids no in-flight handler can reach.
//
// Strycher/LoRa#272.

#pragma once

#ifdef ARDUINO
  #include <esp_http_server.h>
#endif

namespace crosswire {

// Forward decls -- avoid pulling WebSession.h into every includer.
struct WebSession;

// Start the HTTPS server. Returns true if listening on 0.0.0.0:443.
// Idempotent: re-call after cert regeneration to swap cert.
bool webServerStart();

// Stop + free. Safe to call when not running.
void webServerStop();

bool webServerIsRunning();

// Schedule a deferred WiFi reconnect (used by POST /api/wifi to apply
// new creds AFTER the response is flushed -- doing it inline kills the
// TLS socket mid-response). The flag is consumed by webServerLoopTick(),
// which the WifiObserver loop calls on every iteration. delay_ms is
// the grace period (typ. 5000) before WiFi.disconnect/begin runs, giving
// the response time to reach the client.
void webServerScheduleWifiReconnect(uint32_t delay_ms);

// Schedule a deferred reboot (used by POST /api/factory_reset). Same
// reasoning as the wifi reconnect: ESP.restart() can't fire inside the
// HTTPS handler without killing the response. Consumed by webServerLoopTick().
void webServerScheduleReboot(uint32_t delay_ms);

// Per-iteration tick the WifiObserver loop calls. Drives deferred
// reconnect + reboot timers. No-op when nothing is scheduled.
void webServerLoopTick();

#ifdef ARDUINO

// Resolve the session for an in-flight HTTPS request via its cw_sid
// cookie. Returns nullptr on miss / timeout / no cookie. Used by
// Task 8 WebApi handlers that need to reach session state (csrf,
// password_change_required, ip) without re-implementing the cookie-
// parse + constant-time lookup the trampoline already runs. This is
// Option Y from the Task 8 spec: trampoline auth still runs first
// (and 401s on miss), so by the time a handler calls this it should
// always succeed -- the nullptr return path exists for defense in
// depth (e.g., a future handler reachable without the trampoline).
WebSession* webServerCurrentSession(httpd_req_t* req);

// Resolve the client's peer IPv4 address (network byte order) for an
// in-flight request. Returns 0 on failure -- callers should treat 0
// as "unknown" rather than as a real IP (0.0.0.0 cannot route to us).
// Used by login throttling and session creation.
uint32_t webServerClientIp(httpd_req_t* req);

#endif

}  // namespace crosswire
