// src/helpers/wifi_observer/MqttAuth.cpp
//
// Plan 2 v2 Task 6: auth strategy implementations.

#include "MqttAuth.h"
#include <cstring>

#ifdef ARDUINO
  #include "JwtHelper.h"
  #include <ctime>
#endif

namespace crosswire {

// ---------------------------------------------------------------------------
// MqttAuthNone -- no credentials, no refresh.
// ---------------------------------------------------------------------------
#if defined(ARDUINO) && defined(ESP_PLATFORM)
bool MqttAuthNone::apply(esp_mqtt_client_config_t& cfg, uint32_t /*now_ms*/) {
#if ESP_IDF_VERSION_MAJOR >= 5
    cfg.credentials.username = nullptr;
    cfg.credentials.authentication.password = nullptr;
#else
    cfg.username = nullptr;
    cfg.password = nullptr;
#endif
    return true;
}
#endif

// ---------------------------------------------------------------------------
// MqttAuthBasic -- static username+password copy at construction.
// ---------------------------------------------------------------------------
MqttAuthBasic::MqttAuthBasic(const char* username, const char* password) {
    username_[0] = '\0';
    password_[0] = '\0';
    if (username != nullptr) {
        strncpy(username_, username, sizeof(username_) - 1);
        username_[sizeof(username_) - 1] = '\0';
    }
    if (password != nullptr) {
        strncpy(password_, password, sizeof(password_) - 1);
        password_[sizeof(password_) - 1] = '\0';
    }
}

#if defined(ARDUINO) && defined(ESP_PLATFORM)
bool MqttAuthBasic::apply(esp_mqtt_client_config_t& cfg, uint32_t /*now_ms*/) {
#if ESP_IDF_VERSION_MAJOR >= 5
    cfg.credentials.username = username_[0] ? username_ : nullptr;
    cfg.credentials.authentication.password = password_[0] ? password_ : nullptr;
#else
    cfg.username = username_[0] ? username_ : nullptr;
    cfg.password = password_[0] ? password_ : nullptr;
#endif
    return true;
}
#endif

// ---------------------------------------------------------------------------
// MqttAuthJwt -- mints via JwtHelper, re-mints every refresh_sec_.
// ---------------------------------------------------------------------------
#ifdef ARDUINO
MqttAuthJwt::MqttAuthJwt(const mesh::LocalIdentity& identity,
                         const char* audience, uint32_t refresh_sec,
                         const char* owner, const char* email)
    : identity_(&identity), refresh_sec_(refresh_sec) {
    if (audience != nullptr) {
        strncpy(audience_, audience, sizeof(audience_) - 1);
        audience_[sizeof(audience_) - 1] = '\0';
    }
    if (owner != nullptr) {
        strncpy(owner_, owner, sizeof(owner_) - 1);
        owner_[sizeof(owner_) - 1] = '\0';
    }
    if (email != nullptr) {
        strncpy(email_, email, sizeof(email_) - 1);
        email_[sizeof(email_) - 1] = '\0';
    }
    // Defensive minimum: a 0-second refresh would flap aggressively.
    if (refresh_sec_ < 60) refresh_sec_ = 60;
}
#endif

#if defined(ARDUINO) && defined(ESP_PLATFORM)
bool MqttAuthJwt::apply(esp_mqtt_client_config_t& cfg, uint32_t now_ms) {
    if (identity_ == nullptr || audience_[0] == '\0') return false;

    // #95: default the JWT "owner" claim to THIS device's own pubkey (UPPERCASE
    // hex -- the same form as the token's publicKey claim) when no jwt_owner was
    // configured. owner==device-pubkey is the verified-working convention across
    // every target broker (eastme.sh / LetsMesh), so this makes wss zero-touch:
    // no per-slot jwt_owner entry needed. An explicit jwt_owner still overrides.
    // Built once (pubkey is constant); owner_[65] holds exactly 64 hex + NUL.
    if (owner_[0] == '\0') {
        for (size_t i = 0; i < PUB_KEY_SIZE; ++i) {
            snprintf(&owner_[i * 2], 3, "%02X", identity_->pub_key[i]);
        }
        owner_[2 * PUB_KEY_SIZE] = '\0';
    }

    // Mint if never minted OR refresh window elapsed.
    bool need_mint = (last_mint_ms_ == 0) || needsRefresh(now_ms);
    if (need_mint) {
        time_t issued = time(nullptr);
        time_t expires = issued + static_cast<time_t>(refresh_sec_) + 60;  // small lead time
        bool ok = JwtHelper::createAuthToken(
            *identity_,
            audience_,
            issued, expires,
            token_, sizeof(token_),
            owner_,  // #95: always set -- configured jwt_owner, else device pubkey (above)
            email_[0] ? email_ : nullptr);
        if (!ok) return false;
        last_mint_ms_ = now_ms;
    }

    // #68: build the MQTT CONNECT username once -- "v1_" + UPPERCASE hex of the
    // device pubkey. The broker strips "v1_" and verifies the token's publicKey
    // claim against it; a null username is rejected (CONNACK rc=5) even with a
    // valid token -- proven live against mqtt.eastme.sh. Same convention for
    // LetsMesh. The pubkey is constant, so build it only on first apply().
    if (username_[0] == '\0' && identity_ != nullptr) {
        char hex[2 * PUB_KEY_SIZE + 1];
        for (size_t i = 0; i < PUB_KEY_SIZE; ++i) {
            snprintf(&hex[i * 2], 3, "%02X", identity_->pub_key[i]);
        }
        hex[2 * PUB_KEY_SIZE] = '\0';
        snprintf(username_, sizeof(username_), "v1_%s", hex);
    }

    // JWT bearer goes in the password field; username = v1_<pubkey> (#68).
#if ESP_IDF_VERSION_MAJOR >= 5
    cfg.credentials.username = username_;
    cfg.credentials.authentication.password = token_;
#else
    cfg.username = username_;
    cfg.password = token_;
#endif
    return true;
}
#endif

bool MqttAuthJwt::needsRefresh(uint32_t now_ms) {
    if (last_mint_ms_ == 0) return true;  // never minted -> refresh
    uint32_t elapsed_ms = now_ms - last_mint_ms_;
    return elapsed_ms >= (refresh_sec_ * 1000U);
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
MqttAuth* makeAuth(const BrokerConfig& cfg
#ifdef ARDUINO
                   , const mesh::LocalIdentity& identity
#endif
                  ) {
    switch (cfg.auth_type) {
        case BrokerAuthType::None:
            return new MqttAuthNone();
        case BrokerAuthType::Basic:
            return new MqttAuthBasic(cfg.username, cfg.password);
        case BrokerAuthType::Jwt:
#ifdef ARDUINO
            if (cfg.jwt_audience[0] == '\0') return nullptr;
            // #95/#63: the "owner" claim is the dedicated jwt_owner field when
            // set; otherwise MqttAuthJwt::apply() defaults it to this device's
            // own pubkey (owner==device, the verified-working convention). The
            // old username fallback is dropped -- username[64] can't hold a
            // 64-hex key and the device-pubkey default supersedes it.
            return new MqttAuthJwt(identity, cfg.jwt_audience, cfg.jwt_refresh_sec,
                                   cfg.jwt_owner[0] ? cfg.jwt_owner : nullptr,
                                   cfg.jwt_email[0] ? cfg.jwt_email : nullptr);
#else
            return nullptr;
#endif
        default:
            return nullptr;
    }
}

}  // namespace crosswire
