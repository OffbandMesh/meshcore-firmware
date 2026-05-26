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
    p.end();
    return true;
}

// ---------------------------------------------------------------------------
// populateDefaultBrokers — Plan 2 v2 Task 3 Step 4
// ---------------------------------------------------------------------------
// Seeds slots 0-2 with EastMesh + LetsMesh-EU + LetsMesh-US defaults ONLY
// if those slots are currently empty (cfg.url is "\0"). User-set values are
// never overwritten. All three defaults ship with enabled=false; the user
// explicitly enables after configuring their owner identity for the JWT
// claim. Idempotent across boots.
//
// Pre-Plan-2-execution callers: invoke this once at WifiObserver::begin()
// before MqttBrokerPool::begin().
//
// Cert name convention: "letsencrypt" matches MqttCaCerts.h's ISRG Root X1
// (Let's Encrypt root) which currently signs all three vendored brokers'
// certificates. Task 7 (MqttBroker) establishes the canonical cert-name
// lookup; this function uses the documented EastMesh-isrg-x1 reference.

namespace {
struct DefaultBrokerSpec {
    const char* url;
    const char* audience;
    const char* ca_cert_name;
};

constexpr DefaultBrokerSpec kDefaultBrokerSpecs[3] = {
    {"wss://mqtt2.eastmesh.au:443/mqtt",       "https://mqtt2.eastmesh.au",       "letsencrypt"},
    {"wss://mqtt-eu-v1.letsmesh.net:443/mqtt", "https://mqtt-eu-v1.letsmesh.net", "letsencrypt"},
    {"wss://mqtt-us-v1.letsmesh.net:443/mqtt", "https://mqtt-us-v1.letsmesh.net", "letsencrypt"},
};
}  // namespace

void populateDefaultBrokers() {
    for (uint8_t slot = 0; slot < 3 && slot < CROSSWIRE_MAX_BROKERS; ++slot) {
        BrokerConfig cur;
        readBrokerConfig(slot, cur);
        if (cur.url[0] != '\0') {
            // Slot already has a URL (user-set or previously-defaulted) -- skip.
            continue;
        }
        const DefaultBrokerSpec& spec = kDefaultBrokerSpecs[slot];
        BrokerConfig def;  // default-constructed sentinel values
        def.enabled       = false;  // user explicitly enables after configuring identity
        strncpy(def.url,           spec.url,           sizeof(def.url));           def.url[sizeof(def.url)-1] = '\0';
        def.transport     = BrokerTransport::Wss;
        def.port          = 443;
        def.auth_type     = BrokerAuthType::Jwt;
        strncpy(def.jwt_audience,  spec.audience,      sizeof(def.jwt_audience));  def.jwt_audience[sizeof(def.jwt_audience)-1] = '\0';
        def.jwt_refresh_sec = 3600;
        strncpy(def.ca_cert_name,  spec.ca_cert_name,  sizeof(def.ca_cert_name));  def.ca_cert_name[sizeof(def.ca_cert_name)-1] = '\0';
        strncpy(def.topic_prefix,  kDefaultTopicPrefix, sizeof(def.topic_prefix));  def.topic_prefix[sizeof(def.topic_prefix)-1] = '\0';
        // username + password + jwt_token + iata_override stay empty -- user fills in
        writeBrokerConfig(slot, def);
    }
}

#endif  // ARDUINO

}  // namespace crosswire
