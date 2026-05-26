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
            owner_[0] ? owner_ : nullptr,
            email_[0] ? email_ : nullptr);
        if (!ok) return false;
        last_mint_ms_ = now_ms;
    }

    // JWT bearer goes in the password field (broker side parses "v1_<device_id>" + token).
#if ESP_IDF_VERSION_MAJOR >= 5
    cfg.credentials.username = nullptr;
    cfg.credentials.authentication.password = token_;
#else
    cfg.username = nullptr;
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
            return new MqttAuthJwt(identity, cfg.jwt_audience, cfg.jwt_refresh_sec,
                                   cfg.username[0] ? cfg.username : nullptr,
                                   nullptr);
#else
            return nullptr;
#endif
        default:
            return nullptr;
    }
}

}  // namespace crosswire
