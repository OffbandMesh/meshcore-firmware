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
    p.end();
    return true;
}

#endif  // ARDUINO

}  // namespace crosswire
