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
constexpr const char* kKeyBrokerJwtToken     = "jwt_token";    // sensitive (cached minted token; live-minted at connect)
constexpr const char* kKeyBrokerJwtAudience  = "jwt_aud";      // Plan 2 v2: JWT "aud" claim per broker
constexpr const char* kKeyBrokerJwtRefresh   = "jwt_refresh";  // Plan 2 v2: re-mint interval (seconds)
constexpr const char* kKeyBrokerCaCertName   = "ca_cert";      // Plan 2 v2: ref into MqttCaCerts.h; "" = system store / no verify
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
//
// Plan 2 v2 additions: jwt_audience, jwt_refresh_sec, ca_cert_name model
// the per-broker JWT claim audience + refresh cadence and the TLS CA cert
// reference (lookup by name into MqttCaCerts.h's embedded bundle).
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
    // ----- Plan 2 v2 additions (used only when auth_type=Jwt / transport=Tls|Wss)
    char             jwt_audience[96] = {0};   // e.g. "https://mqtt2.eastmesh.au"
    uint32_t         jwt_refresh_sec = 3600;   // re-mint token every N seconds
    char             ca_cert_name[24] = {0};   // ref into MqttCaCerts.h; "" = system store / no verify
};

bool readBrokerConfig(uint8_t slot, BrokerConfig& out);
bool writeBrokerConfig(uint8_t slot, const BrokerConfig& cfg);

// Plan 2 v2: populate slots 0-2 with EastMesh + LetsMesh-EU + LetsMesh-US
// defaults ONLY if those slots are completely empty (cfg.url[0] == '\0').
// Never overwrites user-set values. Idempotent across boots.
// All three default-enabled=false; user explicitly enables after configuring
// their identity (the JWT claim needs owner+email per JwtHelper).
void populateDefaultBrokers();

}  // namespace crosswire
