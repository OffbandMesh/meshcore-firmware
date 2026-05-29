// src/helpers/wifi_observer/WifiBootstrap.h
//
// Boot-time WiFi bootstrap for the Crosswire observer. Owns the
// AP-mode-vs-STA-vs-CLI-rescue decision and the AP-mode HTTP setup
// form. Once WiFi is up, signals readiness via isStaConnected().

#pragma once
#include "WifiObserverConfig.h"
#include <stdint.h>
#include <stddef.h>

namespace crosswire {

enum class WifiBootstrapState : uint8_t {
    Boot,           // before begin() called
    CliRescue,      // serial CLI mode; never returns
    ApMode,         // serving setup form (Plan 3 wires the form; Plan 1 stub)
    StaConnecting,  // creds saved; attempting STA
    StaConnected,   // STA up; safe to start MQTT
    StaFailed,      // STA failed beyond sta_retry_limit
};

class WifiBootstrap {
public:
    // Reads NVS, samples the rescue button, decides initial state.
    // Called from setup() before BLE / LoRa init.
    void begin();

    // Drives the state machine. Call every iteration of the main loop.
    void loop();

    // True once STA is connected and the MQTT subsystem may start.
    bool isStaConnected() const;

    // Format the current STA IPv4 as "a.b.c.d" into `out`. Returns false
    // (and leaves `out` untouched) if no IP is bound yet. Replaces the
    // arduino WiFi.localIP() callers now that WifiBootstrap drives
    // esp_wifi directly (Strycher/LoRa#318) -- arduino's getters key off
    // its own init flags which we bypass. Host build returns false.
    bool staLocalIp(char* out, size_t out_len) const;

    // Returns "Crosswire-Observer-XXXXXX" using the last 6 hex chars
    // of the device MAC. Static so it can be unit-tested without WiFi.
    // mac_bytes must point to 6 bytes (BSSID / WiFi.macAddress() raw).
    static void deriveApSsid(const uint8_t* mac_bytes, char* out, size_t out_len);

    WifiBootstrapState state() const { return state_; }

private:
    WifiBootstrapState state_ = WifiBootstrapState::Boot;
    uint32_t sta_retry_count_ = 0;
    uint32_t last_attempt_ms_ = 0;

    // STA netif handle from esp_netif_create_default_wifi_sta(), stored as
    // void* so this header stays includable by host-test builds (no
    // esp_netif.h dependency). Cast back to esp_netif_t in the .cpp.
    // Strycher/LoRa#318: WifiBootstrap drives esp_wifi/esp_netif directly
    // to cap WiFi buffer counts; this handle backs staLocalIp() + the
    // connection-state poll in loop().
    void* sta_netif_ = nullptr;

#ifdef ARDUINO
    // Bring up STA via esp_wifi directly with a custom low-buffer config,
    // bypassing arduino's WiFi.mode()/begin() (Strycher/LoRa#318). ARDUINO
    // only; the host build's begin() stub never calls it.
    void startStaDirect(const char* ssid, const char* pwd);
#endif

#ifdef CROSSWIRE_OBSERVER_BLE_COMPANION
    // Plan 3 Task 10 (Strycher/LoRa#272): post the IP + mDNS
    // hostname onto the system channel on STA-connect transition.
    // Pulls the pubkey from wifiObserverPubKey() so the mDNS hex
    // matches the system-channel name derivation. No-op if the
    // pubkey is not yet available (very early STA-up before
    // wifiObserverSetMeshContext has been called -- vanishingly
    // unlikely in practice because main.cpp wires context right
    // after the_mesh.begin, well before WiFi associates, but
    // guarded anyway).
    void postStaConnectedStatus();
#endif
};

// Singleton accessor. The whole subsystem assumes one instance.
WifiBootstrap& wifiBootstrap();

}  // namespace crosswire
