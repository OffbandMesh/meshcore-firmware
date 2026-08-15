// src/helpers/wifi_observer/ObserverCli.cpp
//
// Plan 2 v2 Task 10.
//
// #538 (Option A): this file provides CLI verbs for BOTH the observer and the
// repeater. The BROKER verbs (mqtt.* / set|get mqtt.broker.*), the typed config
// providers (observerConfigSet/Get), and the broker enumeration are common to
// both roles and stay unguarded. The observer-only verbs (wifi.*, display.*,
// web.*) pull the observer's wifiBootstrap + config providers and are guarded
// behind OFFBAND_OBSERVER, so the repeater compiles this file for the broker
// verbs alone, without that observer-pipeline dependency.

#include "ObserverCli.h"
#include "ConfigSchema.h"
#include "../config/ConfigDispatch.h"          // #364: role-agnostic config dispatch
// #538 (Option A): the wifi.*/display.* handlers pull the observer's WiFi
// bootstrap (wifiBootstrap), which the repeater has no equivalent for. Their CLI
// verbs are guarded behind OFFBAND_OBSERVER below, so the repeater compiles this
// file for the BROKER verbs only and never links the wifi/display providers.
#ifdef OFFBAND_OBSERVER
#include "../config/WifiConfigProvider.h"      // #370: wifi.* handlers (moved out of this file)
#include "../config/DisplayConfigProvider.h"   // #370: display.* handlers + NVS accessors (moved out)
#endif
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>   // #63: isxdigit/toupper for jwt_owner validation

#ifdef ARDUINO
  #include <Preferences.h>
  #include <WiFi.h>          // WiFi.localIP() for get wifi.status
#endif

namespace offband {

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
    if (v < 0 || v >= OFFBAND_MAX_BROKERS) return -1;
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
        case BrokerState::HeldNoHeap:  return "held(no-heap)";
        default:                       return "?";
    }
}

// #172: WIRE-safe state/error tokens for the OCFG_BROKERS dump (no parens/spaces,
// distinct from stateStr's human "held(no-clock)"). These MUST match the client
// contract (see #172 / OffbandConfigProtocol.h).
static const char* brokerStateWire(BrokerState s) {
    switch (s) {
        case BrokerState::Down:        return "down";
        case BrokerState::Connecting:  return "connecting";
        case BrokerState::Up:          return "up";
        case BrokerState::Backoff:     return "backoff";
        case BrokerState::HeldNoClock: return "held_no_clock";
        case BrokerState::HeldNoHeap:  return "held_no_heap";
        default:                       return "down";
    }
}
static const char* brokerErrorWire(BrokerErrorClass e) {
    switch (e) {
        case BrokerErrorClass::None:  return "none";
        case BrokerErrorClass::Tcp:   return "tcp";
        case BrokerErrorClass::Auth:  return "auth";
        case BrokerErrorClass::Tls:   return "tls";
        case BrokerErrorClass::Other: return "other";
        default:                      return "other";
    }
}

// ---------------------------------------------------------------------------
// Subcommand handlers
// ---------------------------------------------------------------------------

// "mqtt status" -- summary of pool + per-slot.
static bool handleStatus(char* reply, size_t reply_size, MqttBrokerPool& pool) {
    int n = snprintf(reply, reply_size, "mqtt: configured=%u enabled=%u up=%u\n",
                     pool.configuredCount(), pool.enabledCount(), pool.upCount());
    for (uint8_t slot = 0; slot < OFFBAND_MAX_BROKERS; ++slot) {
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
    if (!pool.reloadSlot((uint8_t)slot)) {
        // #181: NVS is updated, but the live client wasn't reconciled now (worker
        // queue full / not ready). Surface it instead of ACKing a clean toggle --
        // the change still takes effect at the next reboot (SAFELANE 6).
        snprintf(reply, reply_size,
                 "mqtt slot %d: %s saved, but live reload failed -- effective after reboot\n",
                 slot, enable ? "enabled" : "disabled");
        return true;
    }
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
    if (!writeGlobalIata(value)) {   // #181: surface NVS failure -- never ACK an unverified write
        snprintf(reply, reply_size, "ERROR: failed to save mqtt.iata (NVS write failed)\n");
        return true;
    }
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
    if (!writeStatusIntervalSec((uint16_t)v)) {   // #181: surface NVS failure
        snprintf(reply, reply_size, "ERROR: failed to save mqtt.status_interval (NVS write failed)\n");
        return true;
    }
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
    if (!pool.reloadSlot((uint8_t)slot)) {
        // #181: saved to NVS, but the cached cfg_ wasn't refreshed (worker queue
        // full / not ready) -- `mqtt status` shows stale until reboot. Surface it
        // rather than ACK a clean set (SAFELANE 6).
        snprintf(reply, reply_size,
                 "mqtt.broker.%d.%s saved, but live reload failed -- effective after reboot\n",
                 slot, key);
        return true;
    }

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

#ifdef OFFBAND_OBSERVER
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
#endif  // OFFBAND_OBSERVER (web handler -- observer-only, #538 Option A)

// ---------------------------------------------------------------------------
// Top-level dispatch
// ---------------------------------------------------------------------------

// "get mqtt.broker.<N>.<key>" -- #45: symmetric read for the
// existing "set mqtt.broker.<N>.<key>". Key vocabulary mirrors
// handleSetBrokerField. Secret fields (password) are write-only and refused
// here, per the wifi.pwd policy + the CLAUDE.md "never echo a secret" rule.
// #172/#173: reach the observer pool for live runtime + the device owner hex, so the
// runtime/resolved fields below are individually GETtable (not just in the dump) --
// the client editor's single-slot Refresh uses per-field get, not the pool dump.
MqttBrokerPool& wifiObserverPool();

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
    // #172/#173: runtime + resolved-default fields, individually GETtable so the
    // client's single-slot Refresh (per-field get) matches the OCFG_BROKERS dump.
    // Wire tokens identical to the dump (brokerStateWire/brokerErrorWire). A valid
    // slot is guaranteed here (readBrokerConfig above rejects out-of-range).
    else if (eq(key, "state"))         snprintf(reply, reply_size, "mqtt.broker.%d.state = %s\n",      slot, brokerStateWire(wifiObserverPool().broker((uint8_t)slot).runtime().state));
    else if (eq(key, "last_error"))    snprintf(reply, reply_size, "mqtt.broker.%d.last_error = %s\n", slot, brokerErrorWire(wifiObserverPool().broker((uint8_t)slot).runtime().last_error_class));
    else if (eq(key, "jwt_owner_resolved")) {
        // The owner used at connect: the explicit jwt_owner if set, else the device pubkey (#95).
        if (cfg.jwt_owner[0] != '\0') {
            snprintf(reply, reply_size, "mqtt.broker.%d.jwt_owner_resolved = %s\n", slot, cfg.jwt_owner);
        } else {
            char hex[72] = {0};
            wifiObserverPool().deviceOwnerHex(hex, sizeof(hex));
            snprintf(reply, reply_size, "mqtt.broker.%d.jwt_owner_resolved = %s\n", slot, hex);
        }
    }
    else if (eq(key, "iata_resolved")) {
        // The IATA used at connect: the explicit iata_override if set, else the global IATA.
        if (cfg.iata_override[0] != '\0') {
            snprintf(reply, reply_size, "mqtt.broker.%d.iata_resolved = %s\n", slot, cfg.iata_override);
        } else {
            char giata[8] = {0};
            readGlobalIata(giata, sizeof(giata));
            snprintf(reply, reply_size, "mqtt.broker.%d.iata_resolved = %s\n", slot, giata);
        }
    }
    else snprintf(reply, reply_size, "ERROR: unknown broker field '%s'\n", key);
    return true;
}

// "mqtt view <N>" -- #98: dump ALL stored config for one
// slot in a single reply (secrets redacted), so an operator can verify a slot
// without querying each field via `get mqtt.broker.<N>.<key>`. The rendering +
// redaction live in ConfigSchema::formatBrokerConfig (host-tested,
// test_observer_broker_format.py); this is just the NVS read + glue. Runtime
// state (state/retries) stays in `mqtt status` -- view is the stored CONFIG.
static bool handleViewBroker(char* reply, size_t reply_size, int slot) {
    BrokerConfig cfg;
    if (!readBrokerConfig((uint8_t)slot, cfg)) {
        snprintf(reply, reply_size, "ERROR: cannot read slot %d\n", slot);
        return true;
    }
    formatBrokerConfig((uint8_t)slot, cfg, reply, reply_size);
    return true;
}

// "mqtt clear <N>" -- #98: wipe one slot's stored config back
// to empty (url + every field blank, disabled), tearing down any live client.
// It clears the FIELDS, not the device. RECOVERY: a default slot (0-5) is
// re-seeded to its default on the NEXT reboot (populateDefaultBrokers fills
// empty slots); a custom slot (6-9) stays empty until reconfigured.
static bool handleClearBroker(char* reply, size_t reply_size,
                              MqttBrokerPool& pool, int slot) {
    if (!clearBrokerConfig((uint8_t)slot)) {
        snprintf(reply, reply_size, "ERROR: cannot clear slot %d\n", slot);
        return true;
    }
    pool.reloadSlot((uint8_t)slot);  // empty url -> worker tears down any client
    snprintf(reply, reply_size,
             "mqtt slot %d cleared. Default slots re-seed at next reboot; "
             "custom slots stay empty.\n", slot);
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
                         OFFBAND_MAX_BROKERS - 1);
                return true;
            }
            return handleEnableSet(reply, reply_size, pool, slot, true);
        }
        const char* dis_rest = skipPrefix(rest, "disable");
        if (dis_rest != nullptr) {
            int slot = parseSlot(dis_rest);
            if (slot < 0) {
                snprintf(reply, reply_size, "ERROR: usage: mqtt disable <0..%d>\n",
                         OFFBAND_MAX_BROKERS - 1);
                return true;
            }
            return handleEnableSet(reply, reply_size, pool, slot, false);
        }
        const char* view_rest = skipPrefix(rest, "view");
        if (view_rest != nullptr) {
            int slot = parseSlot(view_rest);
            if (slot < 0) {
                snprintf(reply, reply_size, "ERROR: usage: mqtt view <0..%d>\n",
                         OFFBAND_MAX_BROKERS - 1);
                return true;
            }
            return handleViewBroker(reply, reply_size, slot);
        }
        const char* clr_rest = skipPrefix(rest, "clear");
        if (clr_rest != nullptr) {
            int slot = parseSlot(clr_rest);
            if (slot < 0) {
                snprintf(reply, reply_size, "ERROR: usage: mqtt clear <0..%d>\n",
                         OFFBAND_MAX_BROKERS - 1);
                return true;
            }
            return handleClearBroker(reply, reply_size, pool, slot);
        }
        snprintf(reply, reply_size,
                 "ERROR: unknown mqtt subcommand "
                 "(status | view <N> | enable <N> | disable <N> | clear <N>)\n");
        return true;
    }

#ifdef OFFBAND_OBSERVER   // #538 (Option A): display/wifi verbs are observer-only
    // "display ..." commands -- #141: display always-on toggle.
    const char* disp_rest = skipPrefix(cmd, "display");
    if (disp_rest != nullptr) {
        if (eq(disp_rest, "always on")) return handleDisplayAlwaysOn(reply, reply_size, true);
        // "normal" restores the default 15 s timeout. "always off" is accepted
        // as an alias for it: it reads literally, but there is no force-dark
        // mode, so we redirect it to normal rather than reject it (#141).
        if (eq(disp_rest, "normal") ||
            eq(disp_rest, "always off")) return handleDisplayAlwaysOn(reply, reply_size, false);
        // #148: display rotation (0/180) + flip toggle.
        if (eq(disp_rest, "flip")) return handleDisplayFlip(reply, reply_size);
        const char* rot_rest = skipPrefix(disp_rest, "rotate");
        if (rot_rest != nullptr) {
            if (eq(rot_rest, "0"))   return handleDisplayRotate(reply, reply_size, 0);
            if (eq(rot_rest, "180")) return handleDisplayRotate(reply, reply_size, 180);
            snprintf(reply, reply_size, "ERROR: display rotate supports 0 or 180\n");
            return true;
        }
        snprintf(reply, reply_size,
                 "ERROR: usage: display always on | display normal | display rotate <0|180> | display flip\n");
        return true;
    }

    // "wifi ..." commands -- #45: namespace-subcommand
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
        // #689/#696: "wifi clear" / "wifi clear pwd" / "wifi clear all".
        // skipPrefix returns the remainder after the subcommand (empty
        // string when bare), which handleClearWifi treats as "pwd".
        const char* clr_rest = skipPrefix(rest, "clear");
        if (clr_rest != nullptr) {
            return handleClearWifi(reply, reply_size, clr_rest);
        }
        snprintf(reply, reply_size,
                 "ERROR: unknown wifi subcommand "
                 "(status | enable | disable | clear)\n");
        return true;
    }
#endif  // OFFBAND_OBSERVER (display/wifi verbs -- #538 Option A)

    // "get wifi.<field>" -- Plan 3 Task 10 (Strycher/LoRa#272). This
    // is the first `get` verb the observer CLI handles; all earlier
    // observer commands were `set`/`mqtt.*` only. CliPassthrough
    // (Plan 3 Task 4) accepts any leading `get ` or `set ` line, so
    // we can pick up the `get` branch here without changing the
    // allowlist surface.
    rest = skipPrefix(cmd, "get");
    if (rest != nullptr) {
#ifdef OFFBAND_OBSERVER   // #538 (Option A): get wifi.* is observer-only
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
#endif  // OFFBAND_OBSERVER (get wifi.* -- #538 Option A)
        // "get mqtt.broker.<N>.<key>" -- #45 symmetric read (mirrors the
        // "set mqtt.broker.<N>.<key>" parse below).
        if (strncmp(rest, "mqtt.broker.", 12) == 0) {
            const char* p = rest + 12;
            int slot = parseSlot(p);
            if (slot < 0) {
                snprintf(reply, reply_size,
                         "ERROR: usage: get mqtt.broker.<0..%d>.<key>\n",
                         OFFBAND_MAX_BROKERS - 1);
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
#ifdef OFFBAND_OBSERVER   // #538 (Option A): set web.*/wifi.* are observer-only
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
#endif  // OFFBAND_OBSERVER (set web.*/wifi.* -- #538 Option A)
    if (strncmp(rest, "mqtt.broker.", 12) == 0) {
        const char* p = rest + 12;
        int slot = parseSlot(p);
        if (slot < 0) {
            snprintf(reply, reply_size, "ERROR: usage: set mqtt.broker.<0..%d>.<key> <value>\n",
                     OFFBAND_MAX_BROKERS - 1);
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

// ===========================================================================
// Typed config dispatch (Epic F / #165) -- the wire path's set/get backend.
// ===========================================================================
// The Offband config command (CMD_OFFBAND_CONFIG; OffbandConfigProtocol.h)
// calls configSet/configGet instead of re-parsing a CLI string. Each key routes
// straight to the SAME static handler dispatchObserverCli uses, so the
// ConfigSchema/NVS logic, validation, and secret redaction stay single-source
// at the handler layer -- the CLI grammar never enters the wire path.
// dispatchObserverCli (the _sys string front-end) is intentionally unchanged.
//   reply : NUL-terminated human text (the same the CLI returns).
//   return: true if the key was handled (incl. an ERROR reply); false if the
//           key is unknown to the observer config surface.

// #364 (Epic #300 item 1): the parse LOGIC moved to the shared dispatcher
// (helpers/config/ConfigDispatch.h) so every role parses config values
// identically. These two thin adapters bind the observer's own constant
// (OFFBAND_MAX_BROKERS) and keep the dispatch chain below byte-for-byte
// unchanged -- behaviour is identical to the former local implementations.
static inline bool parseConfigBool(const char* v, bool& out) {
    return config::parseBool(v, out);
}
static inline bool parseBrokerKey(const char* key, int& out_slot, const char*& out_field) {
    return config::parseIndexedKey(key, "mqtt.broker.", OFFBAND_MAX_BROKERS,
                                   out_slot, out_field);
}

// #364: the observer's config-SET provider, registered with the shared
// dispatcher at the bottom of this file. Formerly the public
// configSet(..., MqttBrokerPool&). The body is UNCHANGED; the only difference is
// that the pool is taken from wifiObserverPool() instead of a parameter, because
// the role-agnostic dispatcher must not know about MqttBrokerPool.
//
// Key ORDER below is load-bearing and must not be reordered: exact
// `wifi.enabled` is tested before the generic `wifi.` prefix (else the on/off
// switch is silently written as a wifi field named "enabled"), and the exact
// `mqtt.*` keys before the `mqtt.broker.` family.
static bool observerConfigSet(const char* key, const char* value, char* reply, size_t reply_size) {
    if (key == nullptr || value == nullptr || reply == nullptr || reply_size == 0) return false;
    reply[0] = '\0';
    MqttBrokerPool& pool = wifiObserverPool();

    if (eq(key, "mqtt.iata"))            return handleSetIata(reply, reply_size, value);
    if (eq(key, "mqtt.status_interval")) return handleSetStatusInterval(reply, reply_size, value);

    // #370: display.* and wifi.* moved to their own providers
    // (config/DisplayConfigProvider, config/WifiConfigProvider). The shared
    // dispatcher routes those key spaces there; this provider owns only mqtt.*.

    {
        int slot; const char* field;
        if (parseBrokerKey(key, slot, field)) {
            // F6 (#166): `enabled` and `clear` are ACTIONS, not persisted fields.
            // handleSetBrokerField has no enabled-set and force-disables the slot
            // on any field write (#53), so route these to their own handlers --
            // this is what lets the client write `enabled` LAST as the activation
            // guard and wipe a slot via `clear`. Everything else is a real field.
            if (eq(field, "enabled")) {
                bool on;
                if (!parseConfigBool(value, on)) {
                    snprintf(reply, reply_size, "ERROR: mqtt.broker.%d.enabled expects 0|1\n", slot);
                    return true;
                }
                return handleEnableSet(reply, reply_size, pool, slot, on);
            }
            if (eq(field, "clear"))
                return handleClearBroker(reply, reply_size, pool, slot);
            return handleSetBrokerField(reply, reply_size, pool, slot, field, value);
        }
        if (strncmp(key, "mqtt.broker.", 12) == 0) {
            snprintf(reply, reply_size, "ERROR: bad broker key '%s' (mqtt.broker.<0..%d>.<field>)\n",
                     key, OFFBAND_MAX_BROKERS - 1);
            return true;
        }
    }

    return false;  // unknown key -- not part of the observer config surface
}

// #364: the observer's config-GET provider (registered at the bottom of this
// file). Formerly the public configGet(). Body unchanged. Secrets stay
// write-only here (wifi.pwd / broker password|jwt_token).
static bool observerConfigGet(const char* key, char* reply, size_t reply_size) {
    if (key == nullptr || reply == nullptr || reply_size == 0) return false;
    reply[0] = '\0';

    // #370: wifi.* and display.* GETs are served by their own providers now.

    if (eq(key, "mqtt.iata")) {
        char iata[8] = {0};
        readGlobalIata(iata, sizeof(iata));
        snprintf(reply, reply_size, "mqtt.iata = %s\n", iata[0] ? iata : "(unset)");
        return true;
    }
    if (eq(key, "mqtt.status_interval")) {
        snprintf(reply, reply_size, "mqtt.status_interval = %u\n", (unsigned)readStatusIntervalSec());
        return true;
    }

    {
        int slot; const char* field;
        if (parseBrokerKey(key, slot, field))
            return handleGetBrokerField(reply, reply_size, slot, field);
        if (strncmp(key, "mqtt.broker.", 12) == 0) {
            snprintf(reply, reply_size, "ERROR: bad broker key '%s'\n", key);
            return true;
        }
    }

    return false;  // unknown key
}

// ---------------------------------------------------------------------------
// Broker-pool enumeration (Epic F / F3, #162) -- backs the OCFG_BROKERS read.
// ---------------------------------------------------------------------------
int configBrokerSlotCount() { return OFFBAND_MAX_BROKERS; }

bool configBrokerSlotPopulated(uint8_t slot) {
    BrokerConfig cfg;
    return readBrokerConfig(slot, cfg) && cfg.url[0] != '\0';
}

// Render a populated slot's non-secret config as wire "key=value\n" lines (the
// OCFG_BROKER_KV payload bodies). transport/auth_type as the string enum names
// (matching the SET grammar); password redacted to "(set)"/"(unset)"; jwt_token
// omitted (not a config key). Returns bytes written (0 if the slot is empty).
// Caller passes a buffer large enough for a full slot (~700 B) and splits the
// result on '\n', emitting one BROKER_KV frame per line.
size_t configRenderBrokerSlot(uint8_t slot, char* out, size_t out_size,
                              const BrokerRuntimeState* rt,
                              const char* owner_default_hex,
                              int32_t ring_lag,
                              bool ring_lapped) {
    if (out == nullptr || out_size == 0) return 0;
    out[0] = '\0';
    BrokerConfig cfg;
    if (!readBrokerConfig(slot, cfg) || cfg.url[0] == '\0') return 0;
    size_t n = 0;
    // Clamp the accumulation: snprintf returns the WOULD-BE length, so on
    // truncation w_ can exceed the remaining space -- never let n pass out_size
    // (else the next call's `out_size - n` underflows, size_t -> UB).
#define BKV(...) do { \
    if (n < out_size) { \
        int w_ = snprintf(out + n, out_size - n, __VA_ARGS__); \
        if (w_ > 0) n += ((size_t)w_ < out_size - n) ? (size_t)w_ : (out_size - n - 1); \
    } \
} while (0)
    BKV("enabled=%d\n",       cfg.enabled ? 1 : 0);
    BKV("url=%s\n",           cfg.url);
    BKV("port=%u\n",          (unsigned)cfg.port);
    BKV("transport=%s\n",     transportStr(cfg.transport));
    BKV("auth_type=%s\n",     authStr(cfg.auth_type));
    BKV("username=%s\n",      cfg.username);
    BKV("password=%s\n",      cfg.password[0] ? "(set)" : "(unset)");
    BKV("topic_prefix=%s\n",  cfg.topic_prefix);
    BKV("iata_override=%s\n", cfg.iata_override);
    BKV("jwt_audience=%s\n",  cfg.jwt_audience);
    BKV("jwt_refresh=%u\n",   (unsigned)cfg.jwt_refresh_sec);
    BKV("jwt_owner=%s\n",     cfg.jwt_owner);
    BKV("jwt_email=%s\n",     cfg.jwt_email);
    BKV("ca_cert=%s\n",       cfg.ca_cert_name);
    // #172: live runtime state + last-error (additive; passed from the pool). Lets
    // the app show the REAL connect result, not just the config `enabled` flag.
    // Old clients ignore unknown keys.
    if (rt != nullptr) {
        BKV("state=%s\n",      brokerStateWire(rt->state));
        BKV("last_error=%s\n", brokerErrorWire(rt->last_error_class));
    }
    // #175: ring send-backlog + lap flag. Non-zero lag = messages published while
    // this broker was rotated out / down and not yet drained; lapped = the ring
    // overran its cursor and the overrun was lost (best-effort overflow). Omitted
    // when ring_lag < 0 (caller had no pool handle).
    if (ring_lag >= 0) {
        BKV("ring_lag=%ld\n",    (long)ring_lag);
        BKV("ring_lapped=%s\n",  ring_lapped ? "yes" : "no");
    }
    // #173: resolved-default placeholders (additive). Emitted ONLY when the raw
    // field is blank, carrying the value the firmware actually uses at connect, so
    // the client shows it as a hint WITHOUT writing it back as an explicit override
    // -- the raw key above stays blank and remains the source of truth for writes.
    if (cfg.jwt_owner[0] == '\0' && owner_default_hex != nullptr && owner_default_hex[0] != '\0') {
        BKV("jwt_owner_resolved=%s\n", owner_default_hex);
    }
    if (cfg.iata_override[0] == '\0') {
        char giata[8] = {0};
        if (readGlobalIata(giata, sizeof(giata)) && giata[0] != '\0') {
            BKV("iata_resolved=%s\n", giata);
        }
    }
#undef BKV
    return n;   // clamped written length (== strlen(out))
}

// ---------------------------------------------------------------------------
// #364 (Epic #300 item 1): register the observer as a config provider.
//
// File-scope registrar -> runs during static initialisation. The dispatcher's
// provider table is POD in .bss (zero-initialised before ANY dynamic
// initialiser), so this is link-order safe, and there is nothing for a future
// role to forget to call from main().
//
// This registration is the ONLY thing that couples the observer to the shared
// dispatcher. The repeater (#301/#305) adds its own registrar for its own keys
// without touching this file.
// ---------------------------------------------------------------------------
// __attribute__((used)): belt-and-braces against the compiler/linker dropping an
// object whose only purpose is its constructor's side effect. NOT a fix for an
// observed defect -- the #364 review (BLOCKER-1) verified the registrar's
// static-init ctor is present in .init_array on the linked ESP32-S3 image, its
// whole body one call to config::registerProvider. ESP-IDF's linker script KEEPs
// .init_array, and this TU is always pulled in (dispatchObserverCli /
// configBrokerSlotCount are referenced elsewhere), so it cannot be dropped as an
// unextracted archive member either. Kept as free insurance against a future
// toolchain/flag change, since the failure mode -- every config key answering
// "unknown config key" on a deployed fleet -- is severe. (#370: the wifi/display
// providers carry the same `used` insurance in their own TUs.)
// #366: the observer's key-space manifest, for registration-time overlap
// detection. Must mirror what observerConfigSet/observerConfigGet actually
// claim (see the if-chain above). #370: the observer now owns ONLY `mqtt.*`;
// `wifi.` moved to WifiConfigProvider and `display.` to DisplayConfigProvider,
// each declaring its own prefix. The overlap detector fires if any two
// providers claim the same key space (e.g. #301's `wifi.mode` under `wifi.`).
// Keep this in sync when adding/removing an observer config key.
static const char* const kObserverConfigPrefixes[] = {
    "mqtt.iata",
    "mqtt.status_interval",
    "mqtt.broker.",
};

static __attribute__((used)) config::ProviderRegistrar _observer_config_provider(
    &observerConfigSet, &observerConfigGet, "observer",
    kObserverConfigPrefixes,
    (int)(sizeof(kObserverConfigPrefixes) / sizeof(kObserverConfigPrefixes[0])));

}  // namespace offband
