// src/helpers/wifi_observer/ConfigSchema.h
//
// Single source of truth for Crosswire observer NVS keys. All
// observer-subsystem reads/writes flow through the typed accessors
// below; raw Preferences calls are forbidden in observer code so
// typos in key names cannot drift across files.
//
// ESP32 NVS namespace limit is 15 chars. "mqtt" and "wifi" already
// claimed by Plan 1; this header adds "observer" for everything else.

#pragma once
#include "WifiObserverConfig.h"
#include <stdint.h>
#include <stddef.h>

namespace crosswire {

// ---------------------------------------------------------------------------
// Namespaces
// ---------------------------------------------------------------------------
constexpr const char* kNvsWifi      = "wifi";      // Plan 1
constexpr const char* kNvsMqtt      = "mqtt";      // global mqtt keys (iata, status_interval)
constexpr const char* kNvsObserver  = "observer";  // ring buffer size etc.

// Per-broker namespace is generated at runtime: "mqtt_b0".."mqtt_b5".
// 15-char ceiling tolerates "mqtt_b<digit>" comfortably.
void mqttBrokerNamespace(uint8_t broker_index, char* out, size_t out_len);

// ---------------------------------------------------------------------------
// Global mqtt.* keys (in "mqtt" namespace)
// ---------------------------------------------------------------------------
constexpr const char* kKeyMqttIata           = "iata";
constexpr const char* kKeyMqttStatusInterval = "status_int";  // <15 chars

// Defaults
constexpr uint16_t kDefaultStatusIntervalSec = 30;
constexpr uint16_t kMinStatusIntervalSec     = 10;
constexpr uint16_t kMaxStatusIntervalSec     = 3600;

// ---------------------------------------------------------------------------
// Per-broker keys (in per-broker "mqtt_bN" namespace)
// ---------------------------------------------------------------------------
constexpr const char* kKeyBrokerEnabled      = "enabled";
constexpr const char* kKeyBrokerUrl          = "url";
constexpr const char* kKeyBrokerPort         = "port";
constexpr const char* kKeyBrokerTransport    = "transport";    // 0=tcp,1=tls,2=wss
constexpr const char* kKeyBrokerAuthType     = "auth_type";    // 0=none,1=basic,2=jwt
constexpr const char* kKeyBrokerUsername     = "username";
constexpr const char* kKeyBrokerPassword     = "password";     // sensitive
constexpr const char* kKeyBrokerJwtToken     = "jwt_token";    // sensitive
constexpr const char* kKeyBrokerTopicPrefix  = "topic_prefix";
constexpr const char* kKeyBrokerIataOverride = "iata_override";

enum class BrokerTransport : uint8_t { Tcp = 0, Tls = 1, Wss = 2 };
enum class BrokerAuthType  : uint8_t { None = 0, Basic = 1, Jwt = 2 };

constexpr uint16_t kDefaultTcpPort = 1883;
constexpr uint16_t kDefaultTlsPort = 8883;
constexpr uint16_t kDefaultWssPort = 9001;

constexpr const char* kDefaultTopicPrefix = "meshcore";

// ---------------------------------------------------------------------------
// observer.* keys (in "observer" namespace)
// ---------------------------------------------------------------------------
constexpr const char* kKeyRecentPacketsBuf = "ring_size";
constexpr uint8_t kDefaultRecentPacketsBuf = CROSSWIRE_MAX_RECENT_PACKETS;

// ---------------------------------------------------------------------------
// Typed accessors (in ConfigSchema.cpp)
// ---------------------------------------------------------------------------
// Returns false if NVS error / missing. Defaults applied at call site.
bool readGlobalIata(char* out, size_t out_len);
void writeGlobalIata(const char* iata);

uint16_t readStatusIntervalSec();
void     writeStatusIntervalSec(uint16_t seconds);

// Broker-slot accessors. Slot range [0, CROSSWIRE_MAX_BROKERS).
// Returns sensible defaults on read-miss (e.g., empty url, enabled=false,
// transport=Tcp, port=1883).
struct BrokerConfig {
    bool             enabled = false;
    char             url[128] = {0};
    uint16_t         port = kDefaultTcpPort;
    BrokerTransport  transport = BrokerTransport::Tcp;
    BrokerAuthType   auth_type = BrokerAuthType::None;
    char             username[64] = {0};
    char             password[128] = {0};
    char             jwt_token[512] = {0};
    char             topic_prefix[32] = {0};  // defaults to kDefaultTopicPrefix at read
    char             iata_override[8] = {0};
};

bool readBrokerConfig(uint8_t slot, BrokerConfig& out);
bool writeBrokerConfig(uint8_t slot, const BrokerConfig& cfg);

}  // namespace crosswire
