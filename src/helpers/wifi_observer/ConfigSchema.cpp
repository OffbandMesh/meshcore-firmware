// src/helpers/wifi_observer/ConfigSchema.cpp
#include "ConfigSchema.h"

#ifdef ARDUINO
  #include <Arduino.h>
  #include <Preferences.h>
#else
  // Host build: provide a thin Preferences shim so the file compiles
  // for host-runnable tests. The shim is in scripts/test_*.py harness.
  #include <cstdio>
  #include <cstring>
#endif

namespace crosswire {

void mqttBrokerNamespace(uint8_t broker_index, char* out, size_t out_len) {
    // "mqtt_b0".."mqtt_b5" -- 7 chars, well under 15-char NVS limit.
    if (broker_index >= CROSSWIRE_MAX_BROKERS || out_len < 8) {
        if (out_len > 0) out[0] = '\0';
        return;
    }
    snprintf(out, out_len, "mqtt_b%u", broker_index);
}

#ifdef ARDUINO

bool readGlobalIata(char* out, size_t out_len) {
    Preferences p;
    p.begin(kNvsMqtt, /*readOnly=*/true);
    String v = p.getString(kKeyMqttIata, "");
    p.end();
    if (v.isEmpty()) {
        if (out_len > 0) out[0] = '\0';
        return false;
    }
    strncpy(out, v.c_str(), out_len);
    out[out_len - 1] = '\0';
    return true;
}

void writeGlobalIata(const char* iata) {
    Preferences p;
    p.begin(kNvsMqtt, /*readOnly=*/false);
    p.putString(kKeyMqttIata, iata);
    p.end();
}

uint16_t readStatusIntervalSec() {
    Preferences p;
    p.begin(kNvsMqtt, /*readOnly=*/true);
    uint16_t v = p.getUShort(kKeyMqttStatusInterval, kDefaultStatusIntervalSec);
    p.end();
    // Clamp to valid range; defends against legacy values outside [10, 3600].
    if (v < kMinStatusIntervalSec) v = kMinStatusIntervalSec;
    if (v > kMaxStatusIntervalSec) v = kMaxStatusIntervalSec;
    return v;
}

void writeStatusIntervalSec(uint16_t seconds) {
    if (seconds < kMinStatusIntervalSec) seconds = kMinStatusIntervalSec;
    if (seconds > kMaxStatusIntervalSec) seconds = kMaxStatusIntervalSec;
    Preferences p;
    p.begin(kNvsMqtt, /*readOnly=*/false);
    p.putUShort(kKeyMqttStatusInterval, seconds);
    p.end();
}

bool readBrokerConfig(uint8_t slot, BrokerConfig& out) {
    if (slot >= CROSSWIRE_MAX_BROKERS) return false;
    char ns[16];
    mqttBrokerNamespace(slot, ns, sizeof(ns));
    Preferences p;
    p.begin(ns, /*readOnly=*/true);
    out.enabled   = p.getBool(kKeyBrokerEnabled, false);
    String url    = p.getString(kKeyBrokerUrl, "");
    strncpy(out.url, url.c_str(), sizeof(out.url));
    out.url[sizeof(out.url) - 1] = '\0';
    out.transport = (BrokerTransport)p.getUChar(kKeyBrokerTransport, (uint8_t)BrokerTransport::Tcp);
    uint16_t default_port;
    switch (out.transport) {
        case BrokerTransport::Tls: default_port = kDefaultTlsPort; break;
        case BrokerTransport::Wss: default_port = kDefaultWssPort; break;
        default:                   default_port = kDefaultTcpPort; break;
    }
    out.port      = p.getUShort(kKeyBrokerPort, default_port);
    out.auth_type = (BrokerAuthType)p.getUChar(kKeyBrokerAuthType, (uint8_t)BrokerAuthType::None);
    String u  = p.getString(kKeyBrokerUsername, "");
    String pw = p.getString(kKeyBrokerPassword, "");
    String jw = p.getString(kKeyBrokerJwtToken, "");
    String tp = p.getString(kKeyBrokerTopicPrefix, kDefaultTopicPrefix);
    String io = p.getString(kKeyBrokerIataOverride, "");
    strncpy(out.username,      u.c_str(),  sizeof(out.username));      out.username[sizeof(out.username)-1] = '\0';
    strncpy(out.password,      pw.c_str(), sizeof(out.password));      out.password[sizeof(out.password)-1] = '\0';
    strncpy(out.jwt_token,     jw.c_str(), sizeof(out.jwt_token));     out.jwt_token[sizeof(out.jwt_token)-1] = '\0';
    strncpy(out.topic_prefix,  tp.c_str(), sizeof(out.topic_prefix));  out.topic_prefix[sizeof(out.topic_prefix)-1] = '\0';
    strncpy(out.iata_override, io.c_str(), sizeof(out.iata_override)); out.iata_override[sizeof(out.iata_override)-1] = '\0';
    // Plan 2 v2 additions
    String ja = p.getString(kKeyBrokerJwtAudience, "");
    out.jwt_refresh_sec = p.getULong(kKeyBrokerJwtRefresh, 3600);
    String cc = p.getString(kKeyBrokerCaCertName, "");
    strncpy(out.jwt_audience,  ja.c_str(), sizeof(out.jwt_audience));  out.jwt_audience[sizeof(out.jwt_audience)-1]  = '\0';
    strncpy(out.ca_cert_name,  cc.c_str(), sizeof(out.ca_cert_name));  out.ca_cert_name[sizeof(out.ca_cert_name)-1]  = '\0';
    // #63 additions: JWT identity claims
    String jo = p.getString(kKeyBrokerJwtOwner, "");
    String je = p.getString(kKeyBrokerJwtEmail, "");
    strncpy(out.jwt_owner,     jo.c_str(), sizeof(out.jwt_owner));     out.jwt_owner[sizeof(out.jwt_owner)-1]         = '\0';
    strncpy(out.jwt_email,     je.c_str(), sizeof(out.jwt_email));     out.jwt_email[sizeof(out.jwt_email)-1]         = '\0';
    p.end();
    return true;
}

bool writeBrokerConfig(uint8_t slot, const BrokerConfig& cfg) {
    if (slot >= CROSSWIRE_MAX_BROKERS) return false;
    char ns[16];
    mqttBrokerNamespace(slot, ns, sizeof(ns));
    Preferences p;
    p.begin(ns, /*readOnly=*/false);
    p.putBool   (kKeyBrokerEnabled,      cfg.enabled);
    p.putString (kKeyBrokerUrl,          cfg.url);
    p.putUShort (kKeyBrokerPort,         cfg.port);
    p.putUChar  (kKeyBrokerTransport,    (uint8_t)cfg.transport);
    p.putUChar  (kKeyBrokerAuthType,     (uint8_t)cfg.auth_type);
    p.putString (kKeyBrokerUsername,     cfg.username);
    p.putString (kKeyBrokerPassword,     cfg.password);
    p.putString (kKeyBrokerJwtToken,     cfg.jwt_token);
    p.putString (kKeyBrokerTopicPrefix,  cfg.topic_prefix);
    p.putString (kKeyBrokerIataOverride, cfg.iata_override);
    // Plan 2 v2 additions
    p.putString (kKeyBrokerJwtAudience,  cfg.jwt_audience);
    p.putULong  (kKeyBrokerJwtRefresh,   cfg.jwt_refresh_sec);
    p.putString (kKeyBrokerCaCertName,   cfg.ca_cert_name);
    // #63 additions: JWT identity claims
    p.putString (kKeyBrokerJwtOwner,     cfg.jwt_owner);
    p.putString (kKeyBrokerJwtEmail,     cfg.jwt_email);
    p.end();
    return true;
}

// ---------------------------------------------------------------------------
// populateDefaultBrokers — Plan 2 v2 Task 3 Step 4
// ---------------------------------------------------------------------------
// Seeds the owner's intended 6-slot broker registry, but ONLY into slots
// whose url is currently empty (cfg.url[0] == '\0'). User-set or
// previously-defaulted values are never overwritten -- so the new defaults
// only affect fresh-NVS devices, and the function is idempotent across boots.
//
//   slot 0  CoreScope Dayton   mqtt://mqtt.w8oof.net:1883   tcp / anon   ENABLED
//   slot 1  LetsMesh-US        wss ... :443 /mqtt           wss / jwt    disabled
//   slot 2  Eastme.sh          wss ... :443 /mqtt           wss / jwt    disabled
//   slot 3  LetsMesh-EU        wss ... :443 /mqtt           wss / jwt    disabled
//   slot 4  Eastmesh.au        wss ... :443 /mqtt           wss / jwt    disabled
//   slot 5  (MQTT Custom)      left empty for the owner to fill
//
// CoreScope is plaintext + anonymous, so it is the only slot enabled by
// default: no TLS cert, no JWT identity (validated live on HV3 -- #42/#48).
// The wss brokers stay disabled until the owner configures JWT identity AND
// the GTS WE1 cert lands (Item 2). They reference ca_cert "gts-we1", whose
// PEM is added + mapped in Item 2 after openssl-verifying each broker's
// actual chain; while disabled they never connect, so the forward-referenced
// cert name is harmless.
//
// Invoke once at WifiObserver::begin() before MqttBrokerPool::begin().

namespace {
struct DefaultBrokerSpec {
    bool             enabled;
    const char*      url;
    BrokerTransport  transport;
    uint16_t         port;
    BrokerAuthType   auth_type;
    const char*      jwt_audience;   // "" when not JWT
    const char*      ca_cert_name;   // "" when no TLS cert (tcp)
};

// Slots 0-4. Slot 5 (MQTT Custom) is intentionally absent so it stays empty.
constexpr DefaultBrokerSpec kDefaultBrokerSpecs[] = {
    {true,  "mqtt://mqtt.w8oof.net:1883",            BrokerTransport::Tcp, 1883, BrokerAuthType::None, "",                                ""},
    {false, "wss://mqtt-us-v1.letsmesh.net:443/mqtt", BrokerTransport::Wss, 443,  BrokerAuthType::Jwt,  "https://mqtt-us-v1.letsmesh.net", "gts-r4"},
    {false, "wss://mqtt.eastme.sh:443/mqtt",          BrokerTransport::Wss, 443,  BrokerAuthType::Jwt,  "https://mqtt.eastme.sh",          "letsencrypt"},
    {false, "wss://mqtt-eu-v1.letsmesh.net:443/mqtt", BrokerTransport::Wss, 443,  BrokerAuthType::Jwt,  "https://mqtt-eu-v1.letsmesh.net", "gts-r4"},
    {false, "wss://mqtt2.eastmesh.au:443/mqtt",       BrokerTransport::Wss, 443,  BrokerAuthType::Jwt,  "https://mqtt2.eastmesh.au",       "letsencrypt"},
};
constexpr uint8_t kNumDefaultBrokers =
    sizeof(kDefaultBrokerSpecs) / sizeof(kDefaultBrokerSpecs[0]);
}  // namespace

void populateDefaultBrokers() {
    // Seed the owner's IATA (HAO) if unset -- SWOH default to simplify setup
    // for most operators; non-SWOH operators override via the global mqtt iata.
    char iata[8] = {0};
    if (!readGlobalIata(iata, sizeof(iata)) || iata[0] == '\0') {
        writeGlobalIata("HAO");
    }

    for (uint8_t slot = 0;
         slot < kNumDefaultBrokers && slot < CROSSWIRE_MAX_BROKERS; ++slot) {
        BrokerConfig cur;
        readBrokerConfig(slot, cur);
        if (cur.url[0] != '\0') {
            // Slot already has a URL (user-set or previously-defaulted) -- skip.
            continue;
        }
        const DefaultBrokerSpec& spec = kDefaultBrokerSpecs[slot];
        BrokerConfig def;  // default-constructed sentinel values
        def.enabled   = spec.enabled;
        strncpy(def.url, spec.url, sizeof(def.url)); def.url[sizeof(def.url)-1] = '\0';
        def.transport = spec.transport;
        def.port      = spec.port;
        def.auth_type = spec.auth_type;
        strncpy(def.jwt_audience, spec.jwt_audience, sizeof(def.jwt_audience)); def.jwt_audience[sizeof(def.jwt_audience)-1] = '\0';
        def.jwt_refresh_sec = 3600;
        strncpy(def.ca_cert_name, spec.ca_cert_name, sizeof(def.ca_cert_name)); def.ca_cert_name[sizeof(def.ca_cert_name)-1] = '\0';
        strncpy(def.topic_prefix, kDefaultTopicPrefix, sizeof(def.topic_prefix)); def.topic_prefix[sizeof(def.topic_prefix)-1] = '\0';
        // username + password + jwt_token + jwt_owner + jwt_email +
        // iata_override stay empty -- owner fills in (jwt_owner/jwt_email
        // via `set mqtt.broker.<N>.jwt_owner|jwt_email`, #63)
        writeBrokerConfig(slot, def);
    }
}

#endif  // ARDUINO

// ---------------------------------------------------------------------------
// formatBrokerConfig — #98 (mqtt view <N>) + reusable by #96 (export)
// ---------------------------------------------------------------------------
// Pure renderer (no NVS/Arduino deps) so it compiles + host-tests on both the
// device and the bench harness. Lives outside the ARDUINO guard.
//
// Small enum->name helpers are file-local here; ObserverCli.cpp has its own
// transportStr/authStr for its status line (minor dup, noted for #97).
namespace {
const char* brokerTransportName(BrokerTransport t) {
    switch (t) {
        case BrokerTransport::Tcp: return "tcp";
        case BrokerTransport::Tls: return "tls";
        case BrokerTransport::Wss: return "wss";
        default:                   return "?";
    }
}
const char* brokerAuthName(BrokerAuthType a) {
    switch (a) {
        case BrokerAuthType::None:  return "none";
        case BrokerAuthType::Basic: return "basic";
        case BrokerAuthType::Jwt:   return "jwt";
        default:                    return "?";
    }
}
}  // namespace

// Bounded append: only writes if room remains; a field that would overflow is
// simply skipped (graceful truncation -- never overruns out_size).
#define CW_VIEW_APPEND(...) do {                                       \
    if (n >= 0 && (size_t)n < out_size) {                             \
        int added = snprintf(out + n, out_size - (size_t)n, __VA_ARGS__); \
        if (added > 0) n += added;                                    \
    }                                                                 \
} while (0)

size_t formatBrokerConfig(uint8_t slot, const BrokerConfig& cfg,
                          char* out, size_t out_size) {
    if (out == nullptr || out_size == 0) return 0;
    out[0] = '\0';
    int n = 0;

    CW_VIEW_APPEND("mqtt.broker.%u:\n", (unsigned)slot);
    CW_VIEW_APPEND("  enabled = %d\n", cfg.enabled ? 1 : 0);
    CW_VIEW_APPEND("  url = %s\n", cfg.url[0] ? cfg.url : "(unset)");
    CW_VIEW_APPEND("  port = %u\n", (unsigned)cfg.port);
    CW_VIEW_APPEND("  transport = %s\n", brokerTransportName(cfg.transport));
    CW_VIEW_APPEND("  auth_type = %s\n", brokerAuthName(cfg.auth_type));
    CW_VIEW_APPEND("  topic_prefix = %s\n",
                   cfg.topic_prefix[0] ? cfg.topic_prefix : "(unset)");
    CW_VIEW_APPEND("  iata_override = %s\n",
                   cfg.iata_override[0] ? cfg.iata_override : "(global)");
    CW_VIEW_APPEND("  ca_cert = %s\n",
                   cfg.ca_cert_name[0] ? cfg.ca_cert_name : "(none)");
    CW_VIEW_APPEND("  jwt_audience = %s\n",
                   cfg.jwt_audience[0] ? cfg.jwt_audience : "(unset)");
    CW_VIEW_APPEND("  jwt_refresh = %u\n", (unsigned)cfg.jwt_refresh_sec);
    CW_VIEW_APPEND("  jwt_owner = %s\n",
                   cfg.jwt_owner[0] ? cfg.jwt_owner : "(unset)");
    CW_VIEW_APPEND("  jwt_email = %s\n",
                   cfg.jwt_email[0] ? cfg.jwt_email : "(unset)");
    // username: for jwt brokers it auto-derives at connect (v1_<pubkey>), so
    // the stored field is normally empty -- say so rather than show blank.
    if (cfg.username[0]) {
        CW_VIEW_APPEND("  username = %s\n", cfg.username);
    } else if (cfg.auth_type == BrokerAuthType::Jwt) {
        CW_VIEW_APPEND("  username = (auto: v1_<pubkey> at connect)\n");
    } else {
        CW_VIEW_APPEND("  username = (unset)\n");
    }
    // SECRETS -- never emit the value, only a derived property (CLAUDE.md).
    if (cfg.password[0]) {
        CW_VIEW_APPEND("  password = (set, len=%u)\n",
                       (unsigned)strlen(cfg.password));
    } else {
        CW_VIEW_APPEND("  password = (unset)\n");
    }
    if (cfg.jwt_token[0]) {
        CW_VIEW_APPEND("  jwt_token = (cached, len=%u)\n",
                       (unsigned)strlen(cfg.jwt_token));
    } else {
        CW_VIEW_APPEND("  jwt_token = (none; minted live at connect)\n");
    }

    // snprintf returns the would-be length, so on truncation n can exceed
    // out_size; report the ACTUAL bytes written (== strlen(out)), capped.
    if (n < 0) return 0;
    return ((size_t)n >= out_size) ? (out_size - 1) : (size_t)n;
}

#undef CW_VIEW_APPEND

}  // namespace crosswire
