// src/helpers/wifi_observer/WifiBootstrap.cpp
#include "WifiBootstrap.h"
#include "WifiObserverConfig.h"

#ifdef CROSSWIRE_OBSERVER_BLE_COMPANION
// Plan 3 Task 10 (Strycher/LoRa#272): system channel for first-
// contact WiFi setup (welcome message on no-creds, IP + mDNS
// hostname on STA connect). Only compiled in for BLE-companion
// builds where the system channel exists.
#include "SystemChannelCli.h"
#endif

#if defined(CROSSWIRE_AP_SETUP_FORM_ENABLED) && CROSSWIRE_AP_SETUP_FORM_ENABLED
// Plan 3 Task 10b (Strycher/LoRa#272): secondary build-flag-gated
// softAP HTTP setup form. Coexists with the BLE system channel;
// whichever path the user uses first wins.
#include "ApSetupForm.h"
#endif

#include <cstdio>
#include <cstring>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <WiFi.h>
  #include <Preferences.h>
  #include <esp_coexist.h>   // esp_coex_preference_set() -- Meshtastic V4
                             // coex fix pattern (issue #2, #4; verified
                             // working on V4 with PIN 100776 pair test)
  #include <esp_wifi.h>      // esp_wifi_set_ps() -- companion to coex tuning
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
    // ----------------------------------------------------------------------
    // WiFi/BT coexistence: apply Meshtastic V4 coex fix pattern UNCONDITIONALLY
    // at boot, BEFORE any WiFi or BT runtime activity. Setting the arbiter
    // preference to BALANCE early ensures that whenever the BT controller
    // and (eventually) WiFi stack both activate, neither starves the other.
    //
    // Per issue #2 root cause (verified on V4):
    //   - Default arbiter is effectively WiFi-biased on many ESP-IDF versions
    //   - Without BALANCE preference, BLE advertisement slots get starved
    //   - On the CP2102 V3 SKU, this manifests as BT controller crashes
    //     once both stacks share the 2.4 GHz radio
    //
    // This call is safe to invoke even when WiFi is never started -- it
    // configures the coexistence arbiter's preference, not the radio itself.
    // ----------------------------------------------------------------------
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);

    // TODO(future Task): wire rescue button sampling.
    Preferences prefs;
    prefs.begin("wifi", /*readOnly=*/true);
    String ssid = prefs.getString("ssid", "");
    prefs.end();

    if (ssid.isEmpty()) {
        Serial.println("[WifiBootstrap] No saved WiFi creds; awaiting "
                       "system-channel CLI commands "
                       "(set wifi.ssid + set wifi.pwd).");
        state_ = WifiBootstrapState::ApMode;  // semantic kept; no softAP started
#ifdef CROSSWIRE_OBSERVER_BLE_COMPANION
        // Plan 3 Task 10 replaces the originally-planned softAP +
        // captive form path. The welcome message is enqueued and
        // posted as soon as MyMesh::loop() runs systemChannelDrain,
        // which happens shortly after begin() returns. No softAP,
        // no captive portal, no Wi-Fi switching on the user's
        // phone -- they type commands in the channel that's
        // already paired with their MeshCore app.
        crosswire::systemChannelPostStatus(
            "Welcome! To set WiFi: send 'set wifi.ssid YourSSID' "
            "then 'set wifi.pwd YourPSK' as messages in this channel. "
            "Use 'get wifi.status' to check.");
#endif
#if defined(CROSSWIRE_AP_SETUP_FORM_ENABLED) && CROSSWIRE_AP_SETUP_FORM_ENABLED
        // Plan 3 Task 10b: SECONDARY commissioning path. Start a
        // softAP + plain-HTTP form on :80 in addition to the system
        // channel post above. Both write to the same NVS keys;
        // whichever path the user uses first wins. The two paths
        // coexist intentionally so a user can pick whichever is
        // more convenient (BLE app vs. phone-on-AP browser).
        if (crosswire::apSetupFormStart()) {
            const char* ap_ssid = crosswire::apSetupFormSsid();
#ifdef CROSSWIRE_OBSERVER_BLE_COMPANION
            // Second system-channel message naming the AP SSID +
            // URL. The 5s rate limiter MAY drop this if the first
            // message has not yet been drained by the time we get
            // here; that's acceptable (the AP info is also on
            // Serial), but enqueue it anyway -- the limiter is on
            // the post path, not the queue path.
            if (ap_ssid != nullptr) {
                crosswire::systemChannelPostStatus(
                    "Alternate setup: connect to WiFi '%s' (no password) "
                    "then open http://192.168.4.1/ in a browser.",
                    ap_ssid);
            }
#endif
        }
#endif
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
        // Meshtastic V4 coex fix: yield 2.4 GHz radio between DTIM beacons
        // so BT controller can schedule advertising/connection slots.
        WiFi.setSleep(true);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
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
#ifdef CROSSWIRE_OBSERVER_BLE_COMPANION
                // Plan 3 Task 10 (Strycher/LoRa#272): post the IP +
                // mDNS hostname so the user immediately sees how to
                // reach the web UI without having to dig through
                // router admin. mDNS hostname format mirrors the
                // system-channel name derivation: "meshcore-<8hex>"
                // where the 8hex comes from the first 4 bytes of
                // the identity pubkey. The rate limiter in
                // systemChannelPostStatus protects against this
                // firing on every loop iteration; the transition
                // happens only on actual STA up.
                postStaConnectedStatus();
#endif
            } else if (millis() - last_attempt_ms_ > 10000) {
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
        case WifiBootstrapState::ApMode: {
#if defined(CROSSWIRE_AP_SETUP_FORM_ENABLED) && CROSSWIRE_AP_SETUP_FORM_ENABLED
            // Plan 3 Task 10b: service the secondary HTTP form (no-op
            // if start failed at begin()). Poll completion; on
            // success, tear down and reboot so the no-creds branch
            // isn't re-entered and STA bring-up runs cleanly from a
            // fresh boot. The "rebooting in 3 seconds" HTML page
            // returned to the browser is what this delay tracks --
            // we want the page to render before the radio drops.
            crosswire::apSetupFormService();
            if (crosswire::apSetupFormCompleted()) {
                Serial.println("[WifiBootstrap] AP form completed; "
                               "rebooting in 3s");
                crosswire::apSetupFormStop();
                delay(3000);
                ESP.restart();
            }
#endif
            break;
        }
        default:
            break;  // CliRescue / Boot / StaFailed
    }
}

bool WifiBootstrap::isStaConnected() const {
    return state_ == WifiBootstrapState::StaConnected;
}

#ifdef CROSSWIRE_OBSERVER_BLE_COMPANION
// Borrowed from WifiObserver -- the cached identity pubkey, set
// by wifiObserverSetMeshContext (called from main.cpp after
// the_mesh.begin). May be nullptr if STA happens to come up
// before main.cpp finishes wiring; in that case we post an
// IP-only message (no mDNS hint) rather than skip the post
// entirely. Forward-declared here to avoid pulling all of
// WifiObserver.h into the bootstrap surface.
extern const uint8_t* wifiObserverPubKey();

void WifiBootstrap::postStaConnectedStatus() {
    const uint8_t* pk = wifiObserverPubKey();
    if (pk != nullptr) {
        // mDNS hostname format: "meshcore-XXXXXXXX" where the 8
        // hex chars are the first 4 bytes of the identity pubkey.
        // Consistent with the system-channel name suffix so the
        // user sees the same identifier in both places.
        char hex[9];
        static const char H[] = "0123456789abcdef";
        for (int i = 0; i < 4; ++i) {
            hex[i * 2]     = H[(pk[i] >> 4) & 0x0F];
            hex[i * 2 + 1] = H[pk[i] & 0x0F];
        }
        hex[8] = '\0';
        crosswire::systemChannelPostStatus(
            "WiFi connected. IP %s -- https://meshcore-%s.local/",
            WiFi.localIP().toString().c_str(), hex);
    } else {
        crosswire::systemChannelPostStatus(
            "WiFi connected. IP %s",
            WiFi.localIP().toString().c_str());
    }
}
#endif  // CROSSWIRE_OBSERVER_BLE_COMPANION

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
