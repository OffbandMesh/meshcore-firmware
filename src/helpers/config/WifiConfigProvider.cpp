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
#include "../wifi_observer/WifiBootstrap.h"   // wifi.status reads WifiBootstrap
                                              // (observer bring-up; see header --
                                              // #365 supplies its own off-observer)
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
  #include <Preferences.h>
  #include <WiFi.h>   // WiFi.localIP() for wifi.status
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
        String s = p.getString("ssid", "");
        p.end();
        snprintf(reply, reply_size, "wifi.ssid = %s\n",
                 s.isEmpty() ? "(unset)" : s.c_str());
#else
        snprintf(reply, reply_size, "wifi.ssid = (host build)\n");
#endif
        return true;
    }
    if (config::strEq(field, "status")) {
#ifdef ARDUINO
        // Reach into the WifiBootstrap state via the singleton.
        // Render a single human-readable line summarizing the
        // current STA state + IP when connected.
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
        } else {
            snprintf(reply, reply_size, "wifi.status = %s\n", st);
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
