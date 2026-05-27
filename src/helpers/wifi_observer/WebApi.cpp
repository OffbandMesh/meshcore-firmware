// src/helpers/wifi_observer/WebApi.cpp
//
// STUBS (Plan 3 Task 7). Each handler returns 501 Not Implemented so
// WebServer.cpp can register the URI table and the firmware links +
// boots end-to-end at the Task 7 checkpoint. Task 8 replaces every
// stub body with the real REST logic; the header (WebApi.h) is the
// stable contract Task 8 must satisfy.
//
// Returning ESP_OK is mandatory even for 501 -- a non-OK return causes
// esp_http_server to drop the underlying socket, which would mask the
// "stub responded" outcome we want to verify in Task 12 smoke tests.
//
// Strycher/LoRa#272.

#include "WebApi.h"

#ifdef ARDUINO

namespace crosswire {
namespace WebApi {

namespace {

// Centralized 501 body so swapping one stub at a time (Task 8) does
// not require touching this helper. Sets status + content type, sends
// a short JSON envelope, returns ESP_OK.
esp_err_t sendStub(httpd_req_t* req, const char* route) {
    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    char body[128];
    // Keep the message machine-parseable so Task 12 smoke tests can
    // assert "stub" presence per-route.
    int n = snprintf(body, sizeof(body),
                     "{\"error\":\"not_implemented\",\"route\":\"%s\",\"stub\":true}\n",
                     route);
    if (n < 0 || (size_t)n >= sizeof(body)) n = (int)sizeof(body) - 1;
    httpd_resp_send(req, body, n);
    return ESP_OK;
}

}  // namespace

// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleLogin(httpd_req_t* req)          { return sendStub(req, "/api/login"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleLogout(httpd_req_t* req)         { return sendStub(req, "/api/logout"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleChangePassword(httpd_req_t* req) { return sendStub(req, "/api/change_password"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleGetStatus(httpd_req_t* req)      { return sendStub(req, "/api/status"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleGetBrokers(httpd_req_t* req)     { return sendStub(req, "/api/brokers"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleSetBroker(httpd_req_t* req)      { return sendStub(req, "/api/brokers/{N}"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleGetWifi(httpd_req_t* req)        { return sendStub(req, "/api/wifi"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleSetWifi(httpd_req_t* req)        { return sendStub(req, "/api/wifi"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleScanWifi(httpd_req_t* req)       { return sendStub(req, "/api/wifi/scan"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleCli(httpd_req_t* req)            { return sendStub(req, "/api/cli"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleGetBle(httpd_req_t* req)         { return sendStub(req, "/api/ble"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleResetBlePin(httpd_req_t* req)    { return sendStub(req, "/api/ble/reset_pin"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleFactoryReset(httpd_req_t* req)   { return sendStub(req, "/api/factory_reset"); }
// Stub for Task 7 link; Task 8 replaces.
esp_err_t handleRegenerateCert(httpd_req_t* req) { return sendStub(req, "/api/regenerate_cert"); }
// Plan 4 implements OTA; Plan 3 explicitly returns 501 per Task 7 spec.
// Task 8 leaves this alone -- the 501 reply is the final-form Plan 3 behavior.
esp_err_t handleOta(httpd_req_t* req)            { return sendStub(req, "/api/ota"); }

}  // namespace WebApi
}  // namespace crosswire

#endif  // ARDUINO
