// src/helpers/wifi_observer/ObserverCli.cpp
//
// Plan 2 v2 Task 10.

#include "ObserverCli.h"
#include "ConfigSchema.h"
#include "WifiBootstrap.h"   // Plan 3 Task 10: get wifi.status walks
                             // WifiBootstrap::state()
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>   // #63: isxdigit/toupper for jwt_owner validation

#ifdef ARDUINO
  #include <Preferences.h>
  #include <WiFi.h>          // WiFi.localIP() for get wifi.status
#endif

namespace crosswire {

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------

static bool eq(const char* a, const char* b) {
    return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}

// Find first non-space after skipping `prefix`. Returns nullptr if
// the string does not start with prefix (followed by space or NUL).
static const char* skipPrefix(const char* s, const char* prefix) {
    if (s == nullptr || prefix == nullptr) return nullptr;
    size_t pl = strlen(prefix);
    if (strncmp(s, prefix, pl) != 0) return nullptr;
    if (s[pl] == '\0') return s + pl;
    if (s[pl] != ' ')  return nullptr;
    s += pl;
    while (*s == ' ') s++;
    return s;
}

// Parse a uint between 0 and 255 from the START of `s`. Returns -1 on
// invalid input.
static int parseSlot(const char* s) {
    if (s == nullptr || *s < '0' || *s > '9') return -1;
    int v = (int)strtol(s, nullptr, 10);
    if (v < 0 || v >= CROSSWIRE_MAX_BROKERS) return -1;
    return v;
}

// Map string to BrokerTransport enum. Returns false on unknown value.
static bool parseTransport(const char* s, BrokerTransport& out) {
    if (eq(s, "tcp")) { out = BrokerTransport::Tcp; return true; }
    if (eq(s, "tls")) { out = BrokerTransport::Tls; return true; }
    if (eq(s, "wss")) { out = BrokerTransport::Wss; return true; }
    return false;
}

static bool parseAuthType(const char* s, BrokerAuthType& out) {
    if (eq(s, "none"))  { out = BrokerAuthType::None;  return true; }
    if (eq(s, "basic")) { out = BrokerAuthType::Basic; return true; }
    if (eq(s, "jwt"))   { out = BrokerAuthType::Jwt;   return true; }
    return false;
}

static const char* transportStr(BrokerTransport t) {
    switch (t) {
        case BrokerTransport::Tcp: return "tcp";
        case BrokerTransport::Tls: return "tls";
        case BrokerTransport::Wss: return "wss";
        default:                   return "?";
    }
}

static const char* authStr(BrokerAuthType a) {
    switch (a) {
        case BrokerAuthType::None:  return "none";
        case BrokerAuthType::Basic: return "basic";
        case BrokerAuthType::Jwt:   return "jwt";
        default:                    return "?";
    }
}

static const char* stateStr(BrokerState s) {
    switch (s) {
        case BrokerState::Down:        return "down";
        case BrokerState::Connecting:  return "connecting";
        case BrokerState::Up:          return "up";
        case BrokerState::Backoff:     return "backoff";
        case BrokerState::HeldNoClock: return "held(no-clock)";
        default:                       return "?";
    }
}

// ---------------------------------------------------------------------------
// Subcommand handlers
// ---------------------------------------------------------------------------

// "mqtt status" -- summary of pool + per-slot.
static bool handleStatus(char* reply, size_t reply_size, MqttBrokerPool& pool) {
    int n = snprintf(reply, reply_size, "mqtt: configured=%u enabled=%u up=%u\n",
                     pool.configuredCount(), pool.enabledCount(), pool.upCount());
    for (uint8_t slot = 0; slot < CROSSWIRE_MAX_BROKERS; ++slot) {
        const MqttBroker& b = pool.broker(slot);
        if (!b.isConfigured()) continue;
        if (n < 0 || (size_t)n >= reply_size) break;
        const BrokerConfig& cfg = b.config();
        const BrokerRuntimeState& rt = b.runtime();
        n += snprintf(reply + n, reply_size - (size_t)n,
                      "  [%u] %s %s/%s url=%s state=%s retries=%u",
                      slot,
                      cfg.enabled ? "ENABLED" : "disabled",
                      transportStr(cfg.transport), authStr(cfg.auth_type),
                      cfg.url, stateStr(rt.state), (unsigned)rt.retry_count);
        // #63: jwt slots show whether the owner/email identity claims are set
        // (Y/N only -- values via the set echo; keeps the line frame-sized).
        if (cfg.auth_type == BrokerAuthType::Jwt && n >= 0 && (size_t)n < reply_size) {
            n += snprintf(reply + n, reply_size - (size_t)n, " own=%c eml=%c",
                          cfg.jwt_owner[0] ? 'Y' : 'N',
                          cfg.jwt_email[0] ? 'Y' : 'N');
        }
        // #66: on TLS/WSS slots surface the trust source (and JWT audience
        // presence). cert=NONE on a wss slot is the smoking gun for a missing
        // CA -- TLS handshakes then fail into permanent backoff while every
        // other visible field looks correct.
        if ((cfg.transport == BrokerTransport::Tls ||
             cfg.transport == BrokerTransport::Wss) &&
            n >= 0 && (size_t)n < reply_size) {
            n += snprintf(reply + n, reply_size - (size_t)n, " cert=%s",
                          cfg.ca_cert_name[0] ? cfg.ca_cert_name : "NONE");
            if (cfg.auth_type == BrokerAuthType::Jwt &&
                n >= 0 && (size_t)n < reply_size) {
                n += snprintf(reply + n, reply_size - (size_t)n, " aud=%c",
                              cfg.jwt_audience[0] ? 'Y' : 'N');
            }
        }
        if (n >= 0 && (size_t)n < reply_size) {
            n += snprintf(reply + n, reply_size - (size_t)n, "\n");
        }
    }
    return true;
}

// "mqtt enable <N>" / "mqtt disable <N>"
static bool handleEnableSet(char* reply, size_t reply_size, MqttBrokerPool& pool,
                            int slot, bool enable) {
    BrokerConfig cfg;
    if (!readBrokerConfig((uint8_t)slot, cfg)) {
        snprintf(reply, reply_size, "ERROR: cannot read slot %d\n", slot);
        return true;
    }
    cfg.enabled = enable;
    if (!writeBrokerConfig((uint8_t)slot, cfg)) {
        snprintf(reply, reply_size, "ERROR: cannot write slot %d\n", slot);
        return true;
    }
    pool.reloadSlot((uint8_t)slot);
    snprintf(reply, reply_size, "mqtt slot %d: %s\n",
             slot, enable ? "enabled" : "disabled");
    return true;
}

// "set mqtt.iata <code>"
static bool handleSetIata(char* reply, size_t reply_size, const char* value) {
    if (value == nullptr || *value == '\0') {
        snprintf(reply, reply_size, "ERROR: usage: set mqtt.iata <code>\n");
        return true;
    }
    writeGlobalIata(value);
    snprintf(reply, reply_size, "mqtt.iata = %s\n", value);
    return true;
}

// "set mqtt.status_interval <sec>"
static bool handleSetStatusInterval(char* reply, size_t reply_size, const char* value) {
    if (value == nullptr || *value == '\0') {
        snprintf(reply, reply_size, "ERROR: usage: set mqtt.status_interval <10..3600>\n");
        return true;
    }
    long v = strtol(value, nullptr, 10);
    if (v < 10 || v > 3600) {
        snprintf(reply, reply_size, "ERROR: status_interval %ld out of range [10, 3600]\n", v);
        return true;
    }
    writeStatusIntervalSec((uint16_t)v);
    snprintf(reply, reply_size, "mqtt.status_interval = %ld\n", v);
    return true;
}

// "set mqtt.broker.<N>.<key> <value>"
// Returns true if handled (reply written), false to bubble up unknown.
static bool handleSetBrokerField(char* reply, size_t reply_size,
                                 MqttBrokerPool& pool,
                                 int slot, const char* key, const char* value) {
    if (slot < 0 || key == nullptr || value == nullptr) {
        snprintf(reply, reply_size, "ERROR: usage: set mqtt.broker.<N>.<key> <value>\n");
        return true;
    }
    BrokerConfig cfg;
    if (!readBrokerConfig((uint8_t)slot, cfg)) {
        snprintf(reply, reply_size, "ERROR: cannot read slot %d\n", slot);
        return true;
    }
    const bool was_enabled = cfg.enabled;  // #53: auto-disable if currently live

    bool sensitive = false;  // password/jwt fields elided from reply
    if (eq(key, "url")) {
        strncpy(cfg.url, value, sizeof(cfg.url) - 1);
        cfg.url[sizeof(cfg.url) - 1] = '\0';
    } else if (eq(key, "port")) {
        long v = strtol(value, nullptr, 10);
        if (v < 1 || v > 65535) {
            snprintf(reply, reply_size, "ERROR: port %ld out of range\n", v);
            return true;
        }
        cfg.port = (uint16_t)v;
    } else if (eq(key, "transport")) {
        BrokerTransport t;
        if (!parseTransport(value, t)) {
            snprintf(reply, reply_size, "ERROR: transport must be tcp|tls|wss\n");
            return true;
        }
        cfg.transport = t;
    } else if (eq(key, "auth_type")) {
        BrokerAuthType a;
        if (!parseAuthType(value, a)) {
            snprintf(reply, reply_size, "ERROR: auth_type must be none|basic|jwt\n");
            return true;
        }
        cfg.auth_type = a;
    } else if (eq(key, "username")) {
        strncpy(cfg.username, value, sizeof(cfg.username) - 1);
        cfg.username[sizeof(cfg.username) - 1] = '\0';
    } else if (eq(key, "password")) {
        strncpy(cfg.password, value, sizeof(cfg.password) - 1);
        cfg.password[sizeof(cfg.password) - 1] = '\0';
        sensitive = true;
    } else if (eq(key, "jwt_audience")) {
        strncpy(cfg.jwt_audience, value, sizeof(cfg.jwt_audience) - 1);
        cfg.jwt_audience[sizeof(cfg.jwt_audience) - 1] = '\0';
    } else if (eq(key, "jwt_refresh")) {
        long v = strtol(value, nullptr, 10);
        if (v < 60 || v > 86400) {
            snprintf(reply, reply_size, "ERROR: jwt_refresh %ld out of range [60, 86400]\n", v);
            return true;
        }
        cfg.jwt_refresh_sec = (uint32_t)v;
    } else if (eq(key, "jwt_owner")) {
        // #63: JWT "owner" claim -- owner pubkey, exactly 64 hex chars.
        // "" clears the claim. Normalized to uppercase (broker convention,
        // matches the device publicKey claim).
        size_t len = strlen(value);
        if (len != 0 && len != 64) {
            snprintf(reply, reply_size, "ERROR: jwt_owner must be 64 hex chars (got %u)\n",
                     (unsigned)len);
            return true;
        }
        for (size_t i = 0; i < len; ++i) {
            if (!isxdigit((unsigned char)value[i])) {
                snprintf(reply, reply_size, "ERROR: jwt_owner must be 64 hex chars\n");
                return true;
            }
        }
        for (size_t i = 0; i < len; ++i) {
            cfg.jwt_owner[i] = (char)toupper((unsigned char)value[i]);
        }
        cfg.jwt_owner[len] = '\0';
    } else if (eq(key, "jwt_email")) {
        // #63: JWT "email" claim. "" clears it.
        if (strlen(value) >= sizeof(cfg.jwt_email)) {
            snprintf(reply, reply_size, "ERROR: jwt_email too long (max %u)\n",
                     (unsigned)(sizeof(cfg.jwt_email) - 1));
            return true;
        }
        strncpy(cfg.jwt_email, value, sizeof(cfg.jwt_email) - 1);
        cfg.jwt_email[sizeof(cfg.jwt_email) - 1] = '\0';
    } else if (eq(key, "iata_override")) {
        strncpy(cfg.iata_override, value, sizeof(cfg.iata_override) - 1);
        cfg.iata_override[sizeof(cfg.iata_override) - 1] = '\0';
    } else if (eq(key, "topic_prefix")) {
        strncpy(cfg.topic_prefix, value, sizeof(cfg.topic_prefix) - 1);
        cfg.topic_prefix[sizeof(cfg.topic_prefix) - 1] = '\0';
    } else if (eq(key, "ca_cert")) {
        strncpy(cfg.ca_cert_name, value, sizeof(cfg.ca_cert_name) - 1);
        cfg.ca_cert_name[sizeof(cfg.ca_cert_name) - 1] = '\0';
    } else {
        snprintf(reply, reply_size, "ERROR: unknown broker field '%s'\n", key);
        return true;
    }

    // #53: never reconfigure a LIVE broker. If the slot was enabled, force it
    // disabled so the change lands on a quiescent slot; the user re-enables
    // explicitly. A set on an already-disabled slot just writes NVS (there is
    // no live client to touch -- instant, no worker round-trip).
    if (was_enabled) cfg.enabled = false;

    if (!writeBrokerConfig((uint8_t)slot, cfg)) {
        snprintf(reply, reply_size, "ERROR: write slot %d failed\n", slot);
        return true;
    }

    // #70/#67: refresh the broker's cached cfg_ so `mqtt status` reflects the
    // new value immediately. Previously only the was_enabled path reloaded (via
    // the live-client teardown); a set on an already-DISABLED slot wrote NVS but
    // never refreshed cfg_, so status kept showing the boot-time config until the
    // slot was enabled or the device rebooted. reloadSlot() is async + worker-
    // owned (the reconciling_[] guard serializes it against loopTask), so this is
    // race-safe; for a disabled slot the worker just re-reads NVS into cfg_ with
    // no client touch.
    pool.reloadSlot((uint8_t)slot);

    if (was_enabled) {
        snprintf(reply, reply_size,
                 "mqtt.broker.%d.%s set; slot disabled -- 'mqtt enable %d' to apply\n",
                 slot, key, slot);
    } else {
        snprintf(reply, reply_size, "mqtt.broker.%d.%s = %s\n",
                 slot, key, sensitive ? "<redacted>" : value);
    }
    return true;
}

// "set wifi.ssid <s>"  / "set wifi.pwd <s>"  / "get wifi.ssid"
// / "get wifi.status" -- Plan 3 Task 10 (Strycher/LoRa#272).
//
// First-contact WiFi setup is driven through the BLE system channel
// (SystemChannelCli) by typing CLI commands as channel messages.
// Those commands route through the cliPassthrough allowlist into
// dispatchObserverCli, which is this function. Both set and get
// halves write/read NVS namespace "wifi" with keys ssid + pwd; the
// status reply enumerates WifiBootstrapState so the user can confirm
// state after a set sequence.
//
// PSK redacted from reply per CLAUDE.md security note: the set-pwd
// branch acknowledges "wifi.pwd = (set, length=N)" without echoing
// the value, and get-pwd is unsupported.
static bool handleSetWifiField(char* reply, size_t reply_size,
                               const char* field, const char* value) {
    if (field == nullptr || value == nullptr) {
        snprintf(reply, reply_size,
                 "ERROR: usage: set wifi.ssid <s> | set wifi.pwd <s>\n");
        return true;
    }
    if (!eq(field, "ssid") && !eq(field, "pwd")) {
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
    if (eq(field, "pwd")) {
        // Never echo the PSK in any code path.
        snprintf(reply, reply_size,
                 "wifi.pwd set (length=%u). Reboot or run "
                 "'wifi status' after STA retry.\n",
                 (unsigned)strlen(value));
    } else {
        snprintf(reply, reply_size, "wifi.ssid = %s\n", value);
    }
    return true;
}

static bool handleGetWifi(char* reply, size_t reply_size, const char* field) {
    if (field == nullptr) {
        snprintf(reply, reply_size,
                 "ERROR: usage: get wifi.ssid | wifi status\n");
        return true;
    }
    if (eq(field, "pwd")) {
        // Refuse to ever read the PSK back. There is no legitimate
        // workflow where surfacing the saved PSK to a remote caller
        // is the right answer.
        snprintf(reply, reply_size,
                 "ERROR: wifi.pwd is write-only\n");
        return true;
    }
    if (eq(field, "ssid")) {
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
    if (eq(field, "status")) {
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

// "set web.allow_initial <on|off>" -- recovery override that re-allows
// the derived initial password even after the user has set their own.
// Cleared automatically after the next successful login via
// webAuthSetPassword(). Writes NVS namespace "web" key "allow_initial".
static bool handleSetWebAllowInitial(char* reply, size_t reply_size,
                                     const char* value) {
    if (value == nullptr || (!eq(value, "on") && !eq(value, "off"))) {
        snprintf(reply, reply_size,
                 "ERROR: usage: set web.allow_initial <on|off>\n");
        return true;
    }
    uint8_t on = eq(value, "on") ? 1 : 0;
#ifdef ARDUINO
    Preferences p;
    if (!p.begin("web", /*readOnly=*/false)) {
        snprintf(reply, reply_size,
                 "ERROR: cannot open NVS namespace 'web'\n");
        return true;
    }
    p.putUChar("allow_initial", on);
    p.end();
#else
    (void)on;  // host build: no NVS
#endif
    snprintf(reply, reply_size, "web.allow_initial = %s\n", value);
    return true;
}

// ---------------------------------------------------------------------------
// Top-level dispatch
// ---------------------------------------------------------------------------

// "wifi enable" / "wifi disable" -- Strycher/Crosswire#45. Persists an NVS
// policy flag in namespace "wifi" (key "enabled", default true) that
// WifiBootstrap::begin() honors at boot. Reboot-to-apply by design: we do
// NOT tear down a live STA here, because the observer's MQTT uplink and this
// very _sys channel ride that WiFi link -- the flag only gates the next
// boot's STA attempt.
static bool handleSetWifiEnabled(char* reply, size_t reply_size, bool enabled) {
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

// "get mqtt.broker.<N>.<key>" -- Strycher/Crosswire#45: symmetric read for the
// existing "set mqtt.broker.<N>.<key>". Key vocabulary mirrors
// handleSetBrokerField. Secret fields (password) are write-only and refused
// here, per the wifi.pwd policy + the CLAUDE.md "never echo a secret" rule.
static bool handleGetBrokerField(char* reply, size_t reply_size,
                                 int slot, const char* key) {
    if (slot < 0 || key == nullptr) {
        snprintf(reply, reply_size, "ERROR: usage: get mqtt.broker.<N>.<key>\n");
        return true;
    }
    BrokerConfig cfg;
    if (!readBrokerConfig((uint8_t)slot, cfg)) {
        snprintf(reply, reply_size, "ERROR: cannot read slot %d\n", slot);
        return true;
    }
    if (eq(key, "password")) {
        snprintf(reply, reply_size,
                 "ERROR: mqtt.broker.%d.password is write-only\n", slot);
        return true;
    }
    if      (eq(key, "url"))           snprintf(reply, reply_size, "mqtt.broker.%d.url = %s\n", slot, cfg.url);
    else if (eq(key, "port"))          snprintf(reply, reply_size, "mqtt.broker.%d.port = %u\n", slot, (unsigned)cfg.port);
    else if (eq(key, "transport"))     snprintf(reply, reply_size, "mqtt.broker.%d.transport = %s\n", slot, transportStr(cfg.transport));
    else if (eq(key, "auth_type"))     snprintf(reply, reply_size, "mqtt.broker.%d.auth_type = %s\n", slot, authStr(cfg.auth_type));
    else if (eq(key, "username"))      snprintf(reply, reply_size, "mqtt.broker.%d.username = %s\n", slot, cfg.username);
    else if (eq(key, "enabled"))       snprintf(reply, reply_size, "mqtt.broker.%d.enabled = %d\n", slot, cfg.enabled ? 1 : 0);
    else if (eq(key, "topic_prefix"))  snprintf(reply, reply_size, "mqtt.broker.%d.topic_prefix = %s\n", slot, cfg.topic_prefix);
    else if (eq(key, "iata_override")) snprintf(reply, reply_size, "mqtt.broker.%d.iata_override = %s\n", slot, cfg.iata_override);
    else if (eq(key, "ca_cert"))       snprintf(reply, reply_size, "mqtt.broker.%d.ca_cert = %s\n", slot, cfg.ca_cert_name);
    else if (eq(key, "jwt_audience"))  snprintf(reply, reply_size, "mqtt.broker.%d.jwt_audience = %s\n", slot, cfg.jwt_audience);
    else if (eq(key, "jwt_refresh"))   snprintf(reply, reply_size, "mqtt.broker.%d.jwt_refresh = %u\n", slot, (unsigned)cfg.jwt_refresh_sec);
    else if (eq(key, "jwt_owner"))     snprintf(reply, reply_size, "mqtt.broker.%d.jwt_owner = %s\n", slot, cfg.jwt_owner);
    else if (eq(key, "jwt_email"))     snprintf(reply, reply_size, "mqtt.broker.%d.jwt_email = %s\n", slot, cfg.jwt_email);
    else snprintf(reply, reply_size, "ERROR: unknown broker field '%s'\n", key);
    return true;
}

bool dispatchObserverCli(const char* cmd, char* reply, size_t reply_size,
                         MqttBrokerPool& pool) {
    if (cmd == nullptr || reply == nullptr || reply_size == 0) return false;
    reply[0] = '\0';

    // "mqtt ..." commands
    const char* rest = skipPrefix(cmd, "mqtt");
    if (rest != nullptr) {
        if (eq(rest, "status") || *rest == '\0') {
            return handleStatus(reply, reply_size, pool);
        }
        const char* en_rest = skipPrefix(rest, "enable");
        if (en_rest != nullptr) {
            int slot = parseSlot(en_rest);
            if (slot < 0) {
                snprintf(reply, reply_size, "ERROR: usage: mqtt enable <0..%d>\n",
                         CROSSWIRE_MAX_BROKERS - 1);
                return true;
            }
            return handleEnableSet(reply, reply_size, pool, slot, true);
        }
        const char* dis_rest = skipPrefix(rest, "disable");
        if (dis_rest != nullptr) {
            int slot = parseSlot(dis_rest);
            if (slot < 0) {
                snprintf(reply, reply_size, "ERROR: usage: mqtt disable <0..%d>\n",
                         CROSSWIRE_MAX_BROKERS - 1);
                return true;
            }
            return handleEnableSet(reply, reply_size, pool, slot, false);
        }
        snprintf(reply, reply_size, "ERROR: unknown mqtt subcommand\n");
        return true;
    }

    // "wifi ..." commands -- Strycher/Crosswire#45: namespace-subcommand
    // grammar aligned with "mqtt status/enable/disable". `status` moves from
    // the verb-first "get wifi.status" to "wifi status"; the dotted form is
    // kept as a backward-compat alias in the `get` branch below.
    rest = skipPrefix(cmd, "wifi");
    if (rest != nullptr) {
        if (eq(rest, "status") || *rest == '\0') {
            return handleGetWifi(reply, reply_size, "status");
        }
        if (eq(rest, "enable"))  return handleSetWifiEnabled(reply, reply_size, true);
        if (eq(rest, "disable")) return handleSetWifiEnabled(reply, reply_size, false);
        snprintf(reply, reply_size,
                 "ERROR: unknown wifi subcommand (status | enable | disable)\n");
        return true;
    }

    // "get wifi.<field>" -- Plan 3 Task 10 (Strycher/LoRa#272). This
    // is the first `get` verb the observer CLI handles; all earlier
    // observer commands were `set`/`mqtt.*` only. CliPassthrough
    // (Plan 3 Task 4) accepts any leading `get ` or `set ` line, so
    // we can pick up the `get` branch here without changing the
    // allowlist surface.
    rest = skipPrefix(cmd, "get");
    if (rest != nullptr) {
        if (strncmp(rest, "wifi.", 5) == 0) {
            const char* field = rest + 5;
            // Extract just the field name; ignore any trailing args
            // (get takes no value).
            char fbuf[16];
            size_t fi = 0;
            while (field[fi] && field[fi] != ' ' && fi + 1 < sizeof(fbuf)) {
                fbuf[fi] = field[fi];
                ++fi;
            }
            fbuf[fi] = '\0';
            return handleGetWifi(reply, reply_size, fbuf);
        }
        // "get mqtt.broker.<N>.<key>" -- #45 symmetric read (mirrors the
        // "set mqtt.broker.<N>.<key>" parse below).
        if (strncmp(rest, "mqtt.broker.", 12) == 0) {
            const char* p = rest + 12;
            int slot = parseSlot(p);
            if (slot < 0) {
                snprintf(reply, reply_size,
                         "ERROR: usage: get mqtt.broker.<0..%d>.<key>\n",
                         CROSSWIRE_MAX_BROKERS - 1);
                return true;
            }
            while (*p >= '0' && *p <= '9') p++;  // past slot digit(s)
            if (*p != '.') {
                snprintf(reply, reply_size,
                         "ERROR: missing .<key> after broker slot\n");
                return true;
            }
            p++;  // past '.'
            char key[32];
            size_t ki = 0;
            while (*p && *p != ' ' && ki + 1 < sizeof(key)) key[ki++] = *p++;
            key[ki] = '\0';
            return handleGetBrokerField(reply, reply_size, slot, key);
        }
        // Unknown `get` key. Return false so CliPassthrough falls
        // through to its "unknown:" reply rather than us claiming
        // ownership.
        return false;
    }

    // "set mqtt.<...>" commands
    rest = skipPrefix(cmd, "set");
    if (rest == nullptr) return false;  // not ours

    // Expect "mqtt.iata <code>" or "mqtt.status_interval <sec>"
    // or "mqtt.broker.<N>.<key> <value>"
    if (strncmp(rest, "mqtt.iata", 9) == 0) {
        const char* v = rest + 9;
        while (*v == ' ') v++;
        return handleSetIata(reply, reply_size, v);
    }
    if (strncmp(rest, "mqtt.status_interval", 20) == 0) {
        const char* v = rest + 20;
        while (*v == ' ') v++;
        return handleSetStatusInterval(reply, reply_size, v);
    }
    if (strncmp(rest, "web.allow_initial", 17) == 0) {
        const char* v = rest + 17;
        while (*v == ' ') v++;
        return handleSetWebAllowInitial(reply, reply_size, v);
    }
    // "set wifi.ssid <s>" / "set wifi.pwd <s>" -- Plan 3 Task 10.
    if (strncmp(rest, "wifi.", 5) == 0) {
        const char* p = rest + 5;
        char field[16];
        size_t fi = 0;
        while (*p && *p != ' ' && fi + 1 < sizeof(field)) {
            field[fi++] = *p++;
        }
        field[fi] = '\0';
        while (*p == ' ') ++p;   // value starts here
        return handleSetWifiField(reply, reply_size, field, p);
    }
    if (strncmp(rest, "mqtt.broker.", 12) == 0) {
        const char* p = rest + 12;
        int slot = parseSlot(p);
        if (slot < 0) {
            snprintf(reply, reply_size, "ERROR: usage: set mqtt.broker.<0..%d>.<key> <value>\n",
                     CROSSWIRE_MAX_BROKERS - 1);
            return true;
        }
        // Skip past the slot digit(s)
        while (*p >= '0' && *p <= '9') p++;
        if (*p != '.') {
            snprintf(reply, reply_size, "ERROR: missing .<key> after broker slot\n");
            return true;
        }
        p++;  // past '.'
        // Extract key (up to space)
        char key[32];
        size_t ki = 0;
        while (*p && *p != ' ' && ki + 1 < sizeof(key)) key[ki++] = *p++;
        key[ki] = '\0';
        while (*p == ' ') p++;  // value starts here
        return handleSetBrokerField(reply, reply_size, pool, slot, key, p);
    }

    // Not an observer command
    return false;
}

}  // namespace crosswire
