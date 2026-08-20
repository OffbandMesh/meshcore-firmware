// src/helpers/config/WifiConfigProvider.cpp
//
// #370: the shared `wifi.*` config provider. See WifiConfigProvider.h.
//
// Handler bodies are relocated VERBATIM from ObserverCli.cpp (byte-identical
// reply strings; the only change is `eq(...)` -> `config::strEq(...)`, which is
// behaviourally identical -- both are NULL-safe strcmp==0, proven by the #364
// host parity harness). This keeps the firmware<->client wire contract
// (#143/#160) unchanged.

#include "WifiConfigProvider.h"
#include "ConfigDispatch.h"
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
  #include <Preferences.h>
#include "../prefs/PrefsRead.h"   // #899: prefStr() -- type-checked optional read
  #include <WiFi.h>   // WiFi.status()/localIP() for wifi.status
#endif

// #684: wifi.status reports ONE uniform vocabulary across every WiFi-capable
// role (owner D1/D3, 2026-08-01). On an OBSERVER build the authoritative source
// is the WifiBootstrap STA/AP bring-up machine. A non-observer build (companion
// #365, repeater #301) runs no such machine, so it derives the SAME words from
// the raw WiFi driver + stored creds. The observer header is pulled ONLY on
// observer builds -- that is exactly what lets WifiConfigProvider link with zero
// wifi_observer/ sources off-observer (the gap #370 left; unblocks #462).
#ifdef OFFBAND_OBSERVER
  #include "../wifi_observer/WifiBootstrap.h"
#endif

namespace offband {

// ---------------------------------------------------------------------------
// Handlers (relocated from ObserverCli.cpp -- bodies unchanged)
// ---------------------------------------------------------------------------

bool handleSetWifiField(char* reply, size_t reply_size,
                        const char* field, const char* value) {
    if (field == nullptr || value == nullptr) {
        snprintf(reply, reply_size,
                 "ERROR: usage: set wifi.ssid <s> | set wifi.pwd <s>\n");
        return true;
    }
    if (!config::strEq(field, "ssid") && !config::strEq(field, "pwd")) {
        snprintf(reply, reply_size,
                 "ERROR: unknown wifi field '%s' "
                 "(supported: ssid, pwd)\n", field);
        return true;
    }
    // Reject empty values: an empty SSID is never useful and would
    // collide with the no-creds detection in WifiBootstrap::begin.
    if (value[0] == '\0') {
        snprintf(reply, reply_size,
                 "ERROR: empty value for wifi.%s\n", field);
        return true;
    }
#ifdef ARDUINO
    Preferences p;
    if (!p.begin("wifi", /*readOnly=*/false)) {
        snprintf(reply, reply_size,
                 "ERROR: cannot open NVS namespace 'wifi'\n");
        return true;
    }
    p.putString(field, value);
    p.end();
#endif
    if (config::strEq(field, "pwd")) {
        // Never echo the PSK in any code path.
        snprintf(reply, reply_size,
                 "wifi.pwd set (%u chars entered). Reboot or run "
                 "'wifi status' after STA retry.\n",
                 (unsigned)strlen(value));
    } else {
        snprintf(reply, reply_size, "wifi.ssid = %s\n", value);
    }
    return true;
}

bool handleClearWifi(char* reply, size_t reply_size, const char* what) {
    // #689/#696: the only way to un-set WiFi creds. `set wifi.pwd ""` is
    // deliberately rejected (empty SSID collides with the no-creds
    // detection in WifiBootstrap::begin), which left a stored PSK
    // permanently stuck -- and a stored PSK raises the STA scan-auth
    // threshold to WPA2, so the node can never join an OPEN network
    // again (#692). Before this, the only escape was a full NVS wipe,
    // which also destroys the device identity.
    //
    // Default (no argument) clears the password only: that is the
    // recoverable case, and dropping the SSID too would silently send
    // the node back to "awaiting setup" when the user asked for less.
    bool clear_ssid = false;
    if (what == nullptr || what[0] == '\0' || config::strEq(what, "pwd")) {
        clear_ssid = false;
    } else if (config::strEq(what, "all")) {
        clear_ssid = true;
    } else {
        snprintf(reply, reply_size,
                 "ERROR: usage: wifi clear [pwd|all]\n");
        return true;
    }
#ifdef ARDUINO
    Preferences p;
    if (!p.begin("wifi", /*readOnly=*/false)) {
        snprintf(reply, reply_size,
                 "ERROR: cannot open NVS namespace 'wifi'\n");
        return true;
    }
    // remove(), NOT putString("") -- per #98, writing an empty string does
    // not reliably clear an ESP32 NVS key, which would leave the very
    // stale-PSK state this command exists to escape.
    //
    // The return value MUST be checked: this command exists to rescue a
    // device stranded by a stale PSK, so reporting success on a failed
    // erase would send the user away believing they are fixed when the
    // node is still unable to join an open AP.
    //
    // But remove() also returns false when the key was simply ABSENT
    // (nvs_erase_key -> ESP_ERR_NVS_NOT_FOUND), which is the common case:
    // a device that never had a PSK, or a second `wifi clear`. Treating
    // that as failure would report a scary NVS error for a no-op. isKey()
    // separates the two: only a key that exists and refuses to erase is
    // a real failure.
    const bool pwd_erase_failed  = p.isKey("pwd")  && !p.remove("pwd");
    const bool ssid_erase_failed = clear_ssid && p.isKey("ssid") && !p.remove("ssid");
    p.end();
    if (pwd_erase_failed || ssid_erase_failed) {
        snprintf(reply, reply_size,
                 "ERROR: could not clear wifi.%s (NVS erase failed)\n",
                 pwd_erase_failed ? "pwd" : "ssid");
        return true;
    }
#endif
    if (clear_ssid) {
        snprintf(reply, reply_size,
                 "wifi cleared (ssid + pwd). Reboot to apply.\n");
    } else {
        snprintf(reply, reply_size,
                 "wifi.pwd cleared. Reboot to join an open network.\n");
    }
    return true;
}

bool handleGetWifi(char* reply, size_t reply_size, const char* field) {
    if (field == nullptr) {
        snprintf(reply, reply_size,
                 "ERROR: usage: get wifi.ssid | wifi status\n");
        return true;
    }
    if (config::strEq(field, "pwd")) {
        // Refuse to ever read the PSK back. There is no legitimate
        // workflow where surfacing the saved PSK to a remote caller
        // is the right answer.
        snprintf(reply, reply_size,
                 "ERROR: wifi.pwd is write-only\n");
        return true;
    }
    if (config::strEq(field, "ssid")) {
#ifdef ARDUINO
        Preferences p;
        if (!p.begin("wifi", /*readOnly=*/true)) {
            snprintf(reply, reply_size,
                     "ERROR: cannot open NVS namespace 'wifi'\n");
            return true;
        }
        String s = offband::prefStr(p, "ssid");
        p.end();
        snprintf(reply, reply_size, "wifi.ssid = %s\n",
                 s.isEmpty() ? "(unset)" : s.c_str());
#else
        snprintf(reply, reply_size, "wifi.ssid = (host build)\n");
#endif
        return true;
    }
    if (config::strEq(field, "status")) {
#if defined(ARDUINO) && defined(OFFBAND_OBSERVER)
        // Observer: authoritative state from the WifiBootstrap STA/AP machine.
        // Render a single human-readable line summarizing the current STA state
        // + IP when connected.
        auto state = wifiBootstrap().state();
        const char* st = "?";
        switch (state) {
            case WifiBootstrapState::Boot:          st = "Boot";          break;
            case WifiBootstrapState::CliRescue:     st = "CliRescue";     break;
            case WifiBootstrapState::ApMode:        st = "AwaitingSetup"; break;
            case WifiBootstrapState::StaConnecting: st = "StaConnecting"; break;
            case WifiBootstrapState::StaConnected:  st = "StaConnected";  break;
            case WifiBootstrapState::StaFailed:     st = "StaFailed";     break;
        }
        if (state == WifiBootstrapState::StaConnected) {
            snprintf(reply, reply_size, "wifi.status = %s ip=%s\n",
                     st, WiFi.localIP().toString().c_str());
        } else if (state == WifiBootstrapState::StaConnecting) {
            // #696: the field cannot read the serial log -- a remote
            // reporter only ever sees this line. A bare "StaConnecting"
            // is indistinguishable between "SSID never matched" and
            // "PSK rejected", which is what stalled #692. Carry the
            // retry count and the last disconnect reason here.
            const uint8_t reason = wifiBootstrap().lastDisconnectReason();
            const char* rname = WifiBootstrap::disconnectReasonName(reason);
            if (reason == 0) {
                snprintf(reply, reply_size,
                         "wifi.status = %s retry=%u (no disconnect event yet)\n",
                         st, (unsigned)wifiBootstrap().staRetryCount());
            } else {
                snprintf(reply, reply_size,
                         "wifi.status = %s retry=%u last_reason=%u(%s)\n",
                         st, (unsigned)wifiBootstrap().staRetryCount(),
                         (unsigned)reason, rname ? rname : "UNKNOWN");
            }
        } else {
            snprintf(reply, reply_size, "wifi.status = %s\n", st);
        }
#elif defined(ARDUINO)
        // Non-observer (companion #365 / repeater #301): no WifiBootstrap
        // machine. Derive the SAME vocabulary from stored creds + the raw WiFi
        // driver, so the meshcore-client#375 contract holds unchanged:
        //   no SSID stored           -> AwaitingSetup   (not provisioned)
        //   WL_CONNECTED             -> StaConnected ip=<addr>
        //   NO_SSID/FAILED/LOST      -> StaFailed
        //   otherwise (idle/dscn/scn)-> StaConnecting
        String ssid;
        {
            Preferences p;
            if (p.begin("wifi", /*readOnly=*/true)) { ssid = offband::prefStr(p, "ssid"); p.end(); }
        }
        if (ssid.isEmpty()) {
            snprintf(reply, reply_size, "wifi.status = AwaitingSetup\n");
        } else {
            wl_status_t ws = WiFi.status();
            if (ws == WL_CONNECTED) {
                snprintf(reply, reply_size, "wifi.status = StaConnected ip=%s\n",
                         WiFi.localIP().toString().c_str());
            } else if ((WiFi.getMode() & WIFI_MODE_STA) == 0 ||
                       ws == WL_NO_SSID_AVAIL || ws == WL_CONNECT_FAILED ||
                       ws == WL_CONNECTION_LOST) {
                // Not connected AND not actively attempting: either STA was never
                // brought up (creds stored but WiFi.begin() not called -- the
                // on-demand / burst-WiFi repeater #301 case), or a terminal
                // failure. Report StaFailed rather than the false-positive
                // StaConnecting (Gemini 2.5 review, #684). getMode() returns
                // WIFI_MODE_NULL when WiFi is uninitialised -- safe.
                snprintf(reply, reply_size, "wifi.status = StaFailed\n");
            } else {
                // STA is up and working toward a link (transient IDLE /
                // DISCONNECTED / SCAN during an active attempt).
                snprintf(reply, reply_size, "wifi.status = StaConnecting\n");
            }
        }
#else
        snprintf(reply, reply_size, "wifi.status = (host build)\n");
#endif
        return true;
    }
    snprintf(reply, reply_size,
             "ERROR: unknown wifi field '%s' (supported: ssid, status)\n",
             field);
    return true;
}

bool handleSetWifiEnabled(char* reply, size_t reply_size, bool enabled) {
#ifdef ARDUINO
    Preferences p;
    if (!p.begin("wifi", /*readOnly=*/false)) {
        snprintf(reply, reply_size, "ERROR: cannot open NVS namespace 'wifi'\n");
        return true;
    }
    p.putBool("enabled", enabled);
    p.end();
#endif
    snprintf(reply, reply_size, "wifi.enabled = %d (reboot to apply)\n",
             enabled ? 1 : 0);
    return true;
}

// ---------------------------------------------------------------------------
// Provider set/get -- relocated verbatim from observerConfigSet/Get's wifi
// branches. Key ORDER is load-bearing: exact `wifi.enabled` MUST be tested
// before the generic `wifi.` prefix, else the boolean is written to NVS as a
// wifi field literally named "enabled".
// ---------------------------------------------------------------------------
namespace {

bool wifiConfigSet(const char* key, const char* value, char* reply, size_t reply_size) {
    if (key == nullptr || value == nullptr || reply == nullptr || reply_size == 0) return false;

    if (config::strEq(key, "wifi.enabled")) {
        bool on;
        if (!config::parseBool(value, on)) { snprintf(reply, reply_size, "ERROR: wifi.enabled expects 0|1\n"); return true; }
        return handleSetWifiEnabled(reply, reply_size, on);
    }
    // #689/#696: wire-path parity for the clear. MUST be tested before the
    // generic "wifi." prefix below for the same reason wifi.enabled is --
    // otherwise this falls through to handleSetWifiField and writes an NVS
    // key literally named "clear" instead of removing the password.
    if (config::strEq(key, "wifi.clear"))
        return handleClearWifi(reply, reply_size, value);
    if (strncmp(key, "wifi.", 5) == 0)            // wifi.ssid / wifi.pwd
        return handleSetWifiField(reply, reply_size, key + 5, value);

    return false;  // not a wifi key
}

bool wifiConfigGet(const char* key, char* reply, size_t reply_size) {
    if (key == nullptr || reply == nullptr || reply_size == 0) return false;

    if (config::strEq(key, "wifi.enabled")) {
#ifdef ARDUINO
        Preferences p; bool en = true;
        if (p.begin("wifi", /*readOnly=*/true)) { en = p.getBool("enabled", true); p.end(); }
        snprintf(reply, reply_size, "wifi.enabled = %d\n", en ? 1 : 0);
#else
        snprintf(reply, reply_size, "wifi.enabled = (host build)\n");
#endif
        return true;
    }
    if (strncmp(key, "wifi.", 5) == 0)             // wifi.ssid (wifi.pwd -> write-only error)
        return handleGetWifi(reply, reply_size, key + 5);

    return false;  // not a wifi key
}

// #366: this provider owns exactly the `wifi.` prefix. The overlap detector
// fires at registration if another provider also claims it (e.g. a second role
// registering `wifi.mode` under `wifi.` -- #301's exact hazard).
const char* const kWifiPrefixes[] = { "wifi." };

// Self-register during static init (POD table in .bss -> link-order safe; see
// ConfigDispatch.h). __attribute__((used)) is free insurance against a future
// toolchain dropping the registrar object (#364 review BLOCKER-1).
__attribute__((used)) config::ProviderRegistrar _wifi_config_provider(
    &wifiConfigSet, &wifiConfigGet, "wifi",
    kWifiPrefixes, (int)(sizeof(kWifiPrefixes) / sizeof(kWifiPrefixes[0])));

}  // namespace

}  // namespace offband
