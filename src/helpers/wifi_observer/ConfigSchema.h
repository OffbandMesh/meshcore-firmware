// src/helpers/wifi_observer/ConfigSchema.h
//
// Single source of truth for Offband observer NVS keys. All
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

namespace offband {

// ---------------------------------------------------------------------------
// Namespaces
// ---------------------------------------------------------------------------
constexpr const char* kNvsWifi      = "wifi";      // Plan 1
constexpr const char* kNvsMqtt      = "mqtt";      // global mqtt keys (iata, status_interval)
constexpr const char* kNvsObserver  = "observer";  // ring buffer size etc.
constexpr const char* kNvsOffbandUi = "offband_ui"; // #141: fork UI prefs (display.always_on). Fork-branded so a future upstream NVS namespace can never clash.

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
// Offband UI keys (in "offband_ui" namespace)
// ---------------------------------------------------------------------------
constexpr const char* kKeyDisplayAlwaysOn = "always_on";  // bool; default false (#141)
constexpr const char* kKeyDisplayRotation = "rotation";   // uint8 0/180; default 0 (#148)

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
constexpr const char* kKeyBrokerJwtOwner     = "jwt_owner";    // #63: JWT "owner" claim (owner pubkey, 64 hex)
constexpr const char* kKeyBrokerJwtEmail     = "jwt_email";    // #63: JWT "email" claim
constexpr const char* kKeyBrokerCaCertName   = "ca_cert";      // Plan 2 v2: ref into MqttCaCerts.h; "" = system store / no verify
constexpr const char* kKeyBrokerTopicPrefix  = "topic_prefix";
constexpr const char* kKeyBrokerIataOverride = "iata_override";

// #182: per-key / no-blank broker-storage schema version, stored in the "observer"
// namespace. migrateBrokerStorage() runs the one-time reclaim when the stored
// version is older than this, then stamps it so the migration runs exactly once.
constexpr const char* kKeyCfgSchema    = "cfg_schema";
constexpr uint8_t     kCfgSchemaVersion = 1;

enum class BrokerTransport : uint8_t { Tcp = 0, Tls = 1, Wss = 2 };
enum class BrokerAuthType  : uint8_t { None = 0, Basic = 1, Jwt = 2 };

constexpr uint16_t kDefaultTcpPort = 1883;
constexpr uint16_t kDefaultTlsPort = 8883;
constexpr uint16_t kDefaultWssPort = 9001;

constexpr const char* kDefaultTopicPrefix = "meshcore";

// ---------------------------------------------------------------------------
// Typed accessors (in ConfigSchema.cpp)
// ---------------------------------------------------------------------------
// Returns false if NVS error / missing. Defaults applied at call site.
bool readGlobalIata(char* out, size_t out_len);
// #181: writers return false on NVS failure (begin/put), and self-log the cause
// (+ free-entry stats) before returning -- never silent (SAFELANE 6). Callers
// must surface the failure rather than ACK success on an unverified write.
bool writeGlobalIata(const char* iata);

uint16_t readStatusIntervalSec();
bool     writeStatusIntervalSec(uint16_t seconds);

// #141: display always-on toggle. When true, UITask never auto-blanks the
// screen. Stored in the fork-branded "offband_ui" NVS namespace.
bool getDisplayAlwaysOn();
bool setDisplayAlwaysOn(bool on);   // #181: false on NVS failure (logged)

// #148: display rotation in degrees (0 or 180). Stored in "offband_ui".
uint8_t getDisplayRotation();
bool    setDisplayRotation(uint8_t deg);   // #181: false on NVS failure (logged)

// Broker-slot accessors. Slot range [0, OFFBAND_MAX_BROKERS).
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
    // ----- #63 additions: per-broker JWT identity claims
    char             jwt_owner[65] = {0};      // "owner" claim: owner pubkey, exactly 64 hex chars
    char             jwt_email[96] = {0};      // "email" claim (matches MqttAuthJwt::email_[96])
};

bool readBrokerConfig(uint8_t slot, BrokerConfig& out);
bool writeBrokerConfig(uint8_t slot, const BrokerConfig& cfg);
// #98: wipe slot N's NVS namespace entirely (definitive clear for
// `mqtt clear <N>` -- putString("") does NOT reliably clear an ESP32 NVS key).
bool clearBrokerConfig(uint8_t slot);

// Phase 2 (#48; layout revised in #95): seed the public-broker registry into
// empty slots only (cfg.url[0] == '\0'); never overwrites user-set values;
// idempotent. Slot 0 = CoreScope (tcp/anon) ships ENABLED; slots 1-5 (wss/jwt:
// LetsMesh-US, Eastme.sh, MeshMapper, LetsMesh-EU, Eastmesh.au) ship disabled
// pending operator opt-in + JWT identity claims; slots 6-9 are left empty for
// operator-custom brokers.
void populateDefaultBrokers();

// #182: one-time boot migration of an upgraded observer to the per-key / no-blank
// storage format. Reclaims NVS space behind the scenes so the user never sees the
// interactive "first write errors, then works". Gated + idempotent; call once at
// observer startup, AFTER populateDefaultBrokers(). See ConfigSchema.cpp.
void migrateBrokerStorage();

// #98: render a broker slot's stored config to a human-readable, multi-line
// text block (one "  key = value" per line) for `mqtt view <N>`. SECRETS ARE
// REDACTED -- password and jwt_token are never emitted; only derived
// properties ("(set, len=N)" / "(unset)" / "(cached, len=N)") appear, per the
// CLAUDE.md "never echo a secret" rule. Pure: no NVS/Arduino deps -- the caller
// passes a cfg already read via readBrokerConfig, so this is host-testable and
// reusable by #96 (config export). Writes at most out_size-1 bytes + NUL,
// truncating gracefully; returns bytes written (excluding NUL).
size_t formatBrokerConfig(uint8_t slot, const BrokerConfig& cfg,
                          char* out, size_t out_size);

}  // namespace offband
