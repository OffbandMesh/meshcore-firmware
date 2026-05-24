// src/helpers/wifi_observer/WifiBootstrap.cpp
#include "WifiBootstrap.h"
#include "WifiObserverConfig.h"

#include <cstdio>
#include <cstring>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <WiFi.h>
  #include <Preferences.h>
#endif

namespace crosswire {

// -------------------------------------------------------------------------
// SSID derivation -- pure function, host-testable.
// -------------------------------------------------------------------------
void WifiBootstrap::deriveApSsid(const uint8_t* mac_bytes, char* out,
                                 size_t out_len) {
    // Format: "Crosswire-Observer-XXXXXX" where XXXXXX = last 6 hex
    // chars of MAC. Total: 19 (prefix) + 6 (hex) + 1 (NUL) = 26 bytes.
    const size_t needed = 26;
    if (out_len < needed) {
        if (out_len > 0) out[0] = '\0';
        return;
    }
    // Last 3 bytes of MAC -> 6 hex chars.
    static const char* H = "0123456789ABCDEF";
    const uint8_t* tail = mac_bytes + 3;
    char hex[7];
    for (int i = 0; i < 3; ++i) {
        hex[i * 2]     = H[(tail[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = H[tail[i] & 0x0F];
    }
    hex[6] = '\0';
    snprintf(out, out_len, "%s%s", CROSSWIRE_AP_SSID_PREFIX, hex);
}

// -------------------------------------------------------------------------
// Lifecycle (Arduino target only; host build skips).
// -------------------------------------------------------------------------
#ifdef ARDUINO

void WifiBootstrap::begin() {
    // TODO(Plan 1 close-out flash / future Task): wire rescue button sampling.
    //   - Sample a board-specific GPIO pin for CROSSWIRE_CLI_RESCUE_BOOT_WINDOW_MS.
    //   - If held: state_ = CliRescue; enter serial CLI loop.
    // Plan 1 skeleton: skip rescue, fall through to NVS check.

    Preferences prefs;
    prefs.begin("wifi", /*readOnly=*/true);
    String ssid = prefs.getString("ssid", "");
    prefs.end();

    if (ssid.isEmpty()) {
        // TODO(Plan 3): start AP mode + HTTP setup form. For Plan 1,
        // log and stay in ApMode state without serving anything.
        // Devices flashed in Plan 1 without saved creds will sit in
        // this state until creds are set via serial CLI.
        Serial.println("[WifiBootstrap] No saved WiFi creds; staying in "
                       "AP mode (Plan 3 will add setup form).");
        state_ = WifiBootstrapState::ApMode;
    } else {
        Serial.printf("[WifiBootstrap] Saved WiFi SSID=%s; attempting STA.\n",
                      ssid.c_str());
        state_ = WifiBootstrapState::StaConnecting;
        last_attempt_ms_ = millis();
        String pwd;
        {
            Preferences p2;
            p2.begin("wifi", /*readOnly=*/true);
            pwd = p2.getString("pwd", "");
            p2.end();
        }
        // PSK redacted from logs per CLAUDE.md security note.
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pwd.c_str());
    }
}

void WifiBootstrap::loop() {
    switch (state_) {
        case WifiBootstrapState::StaConnecting: {
            if (WiFi.status() == WL_CONNECTED) {
                state_ = WifiBootstrapState::StaConnected;
                Serial.printf("[WifiBootstrap] STA connected; IP=%s\n",
                              WiFi.localIP().toString().c_str());
            } else if (millis() - last_attempt_ms_ > 10000) {
                // 10s retry interval. Plan 1: retry indefinitely.
                // Plan 2+: honor wifi.sta_retry_limit.
                sta_retry_count_++;
                Serial.printf("[WifiBootstrap] STA retry #%u\n",
                              sta_retry_count_);
                WiFi.reconnect();
                last_attempt_ms_ = millis();
            }
            break;
        }
        case WifiBootstrapState::StaConnected: {
            if (WiFi.status() != WL_CONNECTED) {
                state_ = WifiBootstrapState::StaConnecting;
                last_attempt_ms_ = millis();
            }
            break;
        }
        default:
            break;  // ApMode / CliRescue / Boot / StaFailed (Plan 1 stubs)
    }
}

bool WifiBootstrap::isStaConnected() const {
    return state_ == WifiBootstrapState::StaConnected;
}

WifiBootstrap& wifiBootstrap() {
    static WifiBootstrap inst;
    return inst;
}

#else  // !ARDUINO (host build for tests)

void WifiBootstrap::begin() {}
void WifiBootstrap::loop() {}
bool WifiBootstrap::isStaConnected() const { return false; }
WifiBootstrap& wifiBootstrap() {
    static WifiBootstrap inst;
    return inst;
}

#endif  // ARDUINO

}  // namespace crosswire
