// src/helpers/wifi_observer/WebApi.h
//
// REST API handlers for the web UI. One C function per route, matching
// the esp_http_server handler signature `esp_err_t (httpd_req_t*)`.
//
// Plan 3 Task 7 creates this header + a stubbed implementation so
// WebServer.cpp can register the URI table at link time without
// waiting on Task 8. Task 8 replaces each stub body with the real
// REST logic; this header is the contract Task 8 must satisfy.
//
// Handlers MUST consume + release the request body if any, set the
// response status / type, and return ESP_OK on success. Returning
// anything other than ESP_OK causes esp_http_server to drop the
// underlying socket (per esp_http_server.h docs).
//
// Auth + CSRF policy is enforced by helpers in WebServer.cpp; the
// handlers below assume Task 8 will continue to call those helpers
// rather than reimplementing the cookie / token dance per handler.
//
// Companion-only routes are listed here unconditionally; WebServer.cpp
// gates their *registration* on CROSSWIRE_OBSERVER_BLE_COMPANION. The
// stub bodies in WebApi.cpp compile on all variants.
//
// Strycher/LoRa#272.

#pragma once

#ifdef ARDUINO
  #include <esp_http_server.h>
#endif

namespace crosswire {
namespace WebApi {

#ifdef ARDUINO

// ---- Unauthenticated --------------------------------------------------------
esp_err_t handleLogin            (httpd_req_t* req);
esp_err_t handleLogout           (httpd_req_t* req);

// ---- Authenticated (auth helper enforced in WebServer.cpp) ------------------
esp_err_t handleChangePassword   (httpd_req_t* req);
esp_err_t handleGetStatus        (httpd_req_t* req);
esp_err_t handleGetBrokers       (httpd_req_t* req);
esp_err_t handleSetBroker        (httpd_req_t* req);
esp_err_t handleGetWifi          (httpd_req_t* req);
esp_err_t handleSetWifi          (httpd_req_t* req);
esp_err_t handleScanWifi         (httpd_req_t* req);
esp_err_t handleCli              (httpd_req_t* req);
esp_err_t handleGetBle           (httpd_req_t* req);    // companion-only
esp_err_t handleResetBlePin      (httpd_req_t* req);    // companion-only
esp_err_t handleFactoryReset     (httpd_req_t* req);
esp_err_t handleRegenerateCert   (httpd_req_t* req);
esp_err_t handleOta              (httpd_req_t* req);    // Plan 4 replaces; Plan 3 returns 501

#endif  // ARDUINO

}  // namespace WebApi
}  // namespace crosswire
