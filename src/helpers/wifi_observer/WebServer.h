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

namespace crosswire {

// Start the HTTPS server. Returns true if listening on 0.0.0.0:443.
// Idempotent: re-call after cert regeneration to swap cert.
bool webServerStart();

// Stop + free. Safe to call when not running.
void webServerStop();

bool webServerIsRunning();

}  // namespace crosswire
