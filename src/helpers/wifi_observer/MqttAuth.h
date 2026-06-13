// src/helpers/wifi_observer/MqttAuth.h
//
// Plan 2 v2 Task 6: pluggable auth strategy interface.
// One strategy per BrokerConfig::auth_type. Strategy is applied to
// esp_mqtt_client_config_t at connect time; JWT can re-mint via
// needsRefresh() between iterations.

#pragma once
#include "ConfigSchema.h"

#ifdef ARDUINO
  #include <Arduino.h>
  #include <Identity.h>          // mesh::LocalIdentity
#endif

#if defined(ARDUINO) && defined(ESP_PLATFORM)
  #include <mqtt_client.h>       // esp_mqtt_client_config_t
#endif

namespace crosswire {

// Abstract auth strategy. Two methods:
//   apply() -- mutate the esp_mqtt config to install credentials.
//              Returns false if setup failed (e.g., JWT mint error).
//   needsRefresh() -- called between pool iterations. If returns true,
//                     the broker re-calls apply() before next connect
//                     attempt. Default: never refreshes (None / Basic).
class MqttAuth {
public:
    virtual ~MqttAuth() = default;
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    virtual bool apply(esp_mqtt_client_config_t& cfg, uint32_t now_ms) = 0;
#endif
    virtual bool needsRefresh(uint32_t now_ms) { (void)now_ms; return false; }
};

// ---------------------------------------------------------------------------
// MqttAuthNone -- anonymous publish. Used by Mosquitto LAN, W8OOF CoreScope.
// ---------------------------------------------------------------------------
class MqttAuthNone : public MqttAuth {
public:
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    bool apply(esp_mqtt_client_config_t& cfg, uint32_t now_ms) override;
#endif
};

// ---------------------------------------------------------------------------
// MqttAuthBasic -- username + password. Used for brokers that demand
// flat credentials (likely future Mosquitto-with-auth deployments).
// ---------------------------------------------------------------------------
class MqttAuthBasic : public MqttAuth {
public:
    MqttAuthBasic(const char* username, const char* password);
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    bool apply(esp_mqtt_client_config_t& cfg, uint32_t now_ms) override;
#endif
private:
    char username_[64];
    char password_[128];
};

// ---------------------------------------------------------------------------
// MqttAuthJwt -- JWT token, auto-refreshed every refresh_sec_.
// Used by vendored brokers (EastMesh, LetsMesh-EU, LetsMesh-US).
// Token is minted via the Plan-1-vendored JwtHelper::createAuthToken.
// ---------------------------------------------------------------------------
class MqttAuthJwt : public MqttAuth {
public:
#ifdef ARDUINO
    MqttAuthJwt(const mesh::LocalIdentity& identity,
                const char* audience, uint32_t refresh_sec,
                const char* owner = nullptr, const char* email = nullptr);
#endif

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    bool apply(esp_mqtt_client_config_t& cfg, uint32_t now_ms) override;
#endif
    bool needsRefresh(uint32_t now_ms) override;

private:
#ifdef ARDUINO
    const mesh::LocalIdentity* identity_ = nullptr;
#endif
    char     audience_[96] = {0};
    char     owner_[65]    = {0};  // JWT "owner"; #95: defaults to device pubkey at apply() if unset
    char     email_[96]    = {0};
    uint32_t refresh_sec_  = 3600;
    uint32_t last_mint_ms_ = 0;
    char     token_[768]   = {0};
    // #68: MQTT CONNECT username = "v1_" + UPPERCASE hex(pubkey). eastme.sh /
    // LetsMesh gate auth on this; a null username is rejected (CONNACK rc=5)
    // even with a valid token (proven live). Built once on first apply().
    char     username_[72] = {0};
};

// ---------------------------------------------------------------------------
// Factory: build the right strategy for a slot's BrokerConfig.
// Returns nullptr if cfg has an invalid auth_type or missing required
// fields (e.g., Jwt without an audience). Caller owns the returned object.
// ---------------------------------------------------------------------------
MqttAuth* makeAuth(const BrokerConfig& cfg
#ifdef ARDUINO
                   , const mesh::LocalIdentity& identity
#endif
                  );

}  // namespace crosswire
