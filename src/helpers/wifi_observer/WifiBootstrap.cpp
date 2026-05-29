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

#include <cstdio>
#include <cstring>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <WiFi.h>          // still used for WiFi.mode(WIFI_OFF) teardown safety
  #include <Preferences.h>
  #include <esp_coexist.h>   // esp_coex_preference_set() -- Meshtastic V4
                             // coex fix pattern (issue #2, #4; verified
                             // working on V4 with PIN 100776 pair test)
  #include <esp_wifi.h>      // esp_wifi_init/start/connect/set_ps/sta_get_ap_info
  #include <esp_netif.h>     // esp_netif_init/create_default_wifi_sta/get_ip_info
  #include <esp_event.h>     // esp_event_loop_create_default()
  #include <esp_err.h>       // ESP_ERR_INVALID_STATE
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
        startStaDirect(ssid.c_str(), pwd.c_str());
    }
}

// ---------------------------------------------------------------------------
// startStaDirect -- bring up WiFi STA via esp_wifi DIRECTLY, bypassing
// arduino's WiFi.mode()/WiFi.begin() (Strycher/LoRa#318).
//
// WHY bypass arduino: arduino-esp32's wifiLowLevelInit() (WiFiGeneric.cpp:
// 674-681) hardcodes dynamic_rx_buf_num=32 + dynamic_tx_buf_num=32 at
// esp_wifi_init(), IGNORING sdkconfig. Those pools balloon under traffic to
// a high-water that stays mapped -- ~77 KB consumed at STA bringup on the
// internal-SRAM-only V3, starving NimBLE + observer + mbedTLS. arduino
// exposes no API to lower the counts (useStaticBuffers only flips to STATIC
// buffers, which need contiguous DMA RAM unavailable after NimBLE+observer
// fragment the heap -- verified crash-loop). The only path to a low DYNAMIC
// ceiling is to call esp_wifi_init() ourselves with a custom config.
//
// We KEEP dynamic buffers (tx_buf_type=1, fragmentation-tolerant) and just
// lower the counts 32 -> 8. esp_netif_create_default_wifi_sta() wires the
// DHCP client automatically via the default event loop, so no custom event
// handlers are needed -- loop() polls esp_wifi_sta_get_ap_info() +
// esp_netif_get_ip_info() for connection state.
//
// Tradeoff: arduino's WiFi.localIP()/WiFi.status() no longer work (they key
// off arduino's own lowLevelInitDone/_esp_wifi_started flags). All such
// callers in the observer env route through staLocalIp() / the
// WifiBootstrapState machine instead. The other WiFi.* call sites in the
// codebase (main.cpp:WiFi.begin, UITask:WiFi.localIP) are inside
// #ifdef WIFI_SSID, which the observer env does not define.
// ---------------------------------------------------------------------------
void WifiBootstrap::startStaDirect(const char* ssid, const char* pwd) {
    // Bring up the TCP/IP + event-loop substrate that arduino's tcpipInit()
    // would normally provide. Idempotent: ESP_ERR_INVALID_STATE means
    // something already initialized it (tolerated, not fatal).
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        Serial.printf("[WifiBootstrap] esp_netif_init failed: %d\n", (int)e);
        state_ = WifiBootstrapState::StaFailed;
        return;
    }
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        Serial.printf("[WifiBootstrap] esp_event_loop_create_default failed: %d\n", (int)e);
        state_ = WifiBootstrapState::StaFailed;
        return;
    }
    if (sta_netif_ == nullptr) {
        sta_netif_ = (void*)esp_netif_create_default_wifi_sta();
        if (sta_netif_ == nullptr) {
            Serial.println("[WifiBootstrap] create_default_wifi_sta failed");
            state_ = WifiBootstrapState::StaFailed;
            return;
        }
    }

    // Custom init config: dynamic buffers (fragmentation-tolerant) capped at
    // 8 instead of arduino's 32. See function header for full rationale.
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.static_rx_buf_num  = 4;
    cfg.dynamic_rx_buf_num = 8;    // was 32 (arduino hardcode)
    cfg.tx_buf_type        = 1;    // DYNAMIC -- not static (static crashes)
    cfg.static_tx_buf_num  = 0;    // must be 0 when tx_buf_type=1
    cfg.dynamic_tx_buf_num = 8;    // was 32 (arduino hardcode)
    cfg.cache_tx_buf_num   = 4;    // arduino's dynamic-mode value
    e = esp_wifi_init(&cfg);
    if (e != ESP_OK) {
        Serial.printf("[WifiBootstrap] esp_wifi_init failed: %d\n", (int)e);
        state_ = WifiBootstrapState::StaFailed;
        return;
    }

    // Store creds in RAM (not flash NVS -- we manage persistence via our own
    // "wifi" namespace; WIFI_STORAGE_RAM avoids a second copy in the wifi NVS).
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t wc = {};
    strncpy((char*)wc.sta.ssid,     ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char*)wc.sta.password, pwd,  sizeof(wc.sta.password) - 1);
    esp_wifi_set_config(WIFI_IF_STA, &wc);

    esp_wifi_start();
    // Meshtastic V4 coex fix: yield 2.4 GHz radio between DTIM beacons so the
    // BT controller can schedule advertising/connection slots. Must be set
    // after esp_wifi_start(). (Equivalent to arduino WiFi.setSleep(true).)
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_wifi_connect();
}

void WifiBootstrap::loop() {
    // Poll-based connection tracking (Strycher/LoRa#318): since we drive
    // esp_wifi directly (no arduino WiFi class), WiFi.status() is unusable.
    // "Connected" = STA has a DHCP-bound IP (what MQTT actually needs);
    // staLocalIp() returns false until then. Loss-of-link is detected via
    // esp_wifi_sta_get_ap_info(). esp_netif_create_default_wifi_sta() runs
    // the DHCP client automatically; no custom event handlers required.
    switch (state_) {
        case WifiBootstrapState::StaConnecting: {
            char ipbuf[16];
            if (staLocalIp(ipbuf, sizeof(ipbuf))) {
                state_ = WifiBootstrapState::StaConnected;
                Serial.printf("[WifiBootstrap] STA connected; IP=%s\n", ipbuf);
#ifdef CROSSWIRE_OBSERVER_BLE_COMPANION
                // Plan 3 Task 10 (Strycher/LoRa#272): post IP + mDNS
                // hostname to the system channel so the user sees how to
                // reach the web UI. Rate-limited inside systemChannelPostStatus;
                // fires only on this StaConnecting->StaConnected transition.
                postStaConnectedStatus();
#endif
            } else if (millis() - last_attempt_ms_ > 10000) {
                sta_retry_count_++;
                Serial.printf("[WifiBootstrap] STA retry #%u\n",
                              sta_retry_count_);
                esp_wifi_connect();   // re-initiate association
                last_attempt_ms_ = millis();
            }
            break;
        }
        case WifiBootstrapState::StaConnected: {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
                // Lost association. Drop back to connecting + re-initiate
                // (no auto-reconnect without arduino's WiFi class).
                state_ = WifiBootstrapState::StaConnecting;
                last_attempt_ms_ = millis();
                esp_wifi_connect();
            }
            break;
        }
        default:
            break;  // ApMode / CliRescue / Boot / StaFailed
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
    char ipbuf[16];
    if (!staLocalIp(ipbuf, sizeof(ipbuf))) return;  // no IP yet; skip
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
            ipbuf, hex);
    } else {
        crosswire::systemChannelPostStatus("WiFi connected. IP %s", ipbuf);
    }
}
#endif  // CROSSWIRE_OBSERVER_BLE_COMPANION

// ---------------------------------------------------------------------------
// staLocalIp -- format the STA's DHCP-bound IPv4 as "a.b.c.d". Returns false
// (out untouched) if no netif or no bound address yet. Strycher/LoRa#318:
// replaces arduino WiFi.localIP() now that we drive esp_netif directly.
// esp_ip4_addr_t.addr is network byte order with the first octet in the low
// byte; format manually to avoid depending on lwip IP2STR/IPSTR macros.
// ---------------------------------------------------------------------------
bool WifiBootstrap::staLocalIp(char* out, size_t out_len) const {
    if (sta_netif_ == nullptr || out == nullptr || out_len < 8) return false;
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info((esp_netif_t*)sta_netif_, &info) != ESP_OK) return false;
    uint32_t a = info.ip.addr;
    if (a == 0) return false;
    snprintf(out, out_len, "%u.%u.%u.%u",
             (unsigned)(a & 0xFF), (unsigned)((a >> 8) & 0xFF),
             (unsigned)((a >> 16) & 0xFF), (unsigned)((a >> 24) & 0xFF));
    return true;
}

WifiBootstrap& wifiBootstrap() {
    static WifiBootstrap inst;
    return inst;
}

#else  // !ARDUINO (host build for tests)

void WifiBootstrap::begin() {}
void WifiBootstrap::loop() {}
bool WifiBootstrap::isStaConnected() const { return false; }
bool WifiBootstrap::staLocalIp(char*, size_t) const { return false; }
WifiBootstrap& wifiBootstrap() {
    static WifiBootstrap inst;
    return inst;
}

#endif  // ARDUINO

}  // namespace crosswire
