// src/helpers/wifi_observer/ObserverCli.cpp
//
// Plan 2 v2 Task 10.

#include "ObserverCli.h"
#include "ConfigSchema.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef ARDUINO
  #include <Preferences.h>
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
        case BrokerState::Down:       return "down";
        case BrokerState::Connecting: return "connecting";
        case BrokerState::Up:         return "up";
        case BrokerState::Backoff:    return "backoff";
        default:                      return "?";
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
                      "  [%u] %s %s/%s url=%s state=%s retries=%u\n",
                      slot,
                      cfg.enabled ? "ENABLED" : "disabled",
                      transportStr(cfg.transport), authStr(cfg.auth_type),
                      cfg.url, stateStr(rt.state), (unsigned)rt.retry_count);
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

    if (!writeBrokerConfig((uint8_t)slot, cfg)) {
        snprintf(reply, reply_size, "ERROR: write slot %d failed\n", slot);
        return true;
    }
    pool.reloadSlot((uint8_t)slot);
    snprintf(reply, reply_size, "mqtt.broker.%d.%s = %s\n",
             slot, key, sensitive ? "<redacted>" : value);
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
