// src/helpers/wifi_observer/MqttBroker.cpp
//
// Plan 2 v2 Task 7. esp_mqtt_client wrapper with pluggable auth.

#include "MqttBroker.h"
#include <cstring>
#include <cstdio>
#include <ctime>          // #69: std::time() for the wall-clock sanity gate

#ifdef ARDUINO
  #include <Arduino.h>
  #include "MqttCaCerts.h"  // embedded CA PEMs (Plan 1 vendored, retained)
#endif

namespace crosswire {

#if defined(ARDUINO) && defined(ESP_PLATFORM)
namespace {
// RAII guard for the per-broker recursive client mutex. Take on
// construction, give on destruction -- covers all early-return paths
// (Strycher/Crosswire#53).
struct ClientLockGuard {
    SemaphoreHandle_t m_;
    explicit ClientLockGuard(SemaphoreHandle_t m) : m_(m) {
        if (m_ != nullptr) xSemaphoreTakeRecursive(m_, portMAX_DELAY);
    }
    ~ClientLockGuard() { if (m_ != nullptr) xSemaphoreGiveRecursive(m_); }
    ClientLockGuard(const ClientLockGuard&) = delete;
    ClientLockGuard& operator=(const ClientLockGuard&) = delete;
};
}  // namespace
#endif

// ---------------------------------------------------------------------------
// Backoff schedule -- shared free function.
// ---------------------------------------------------------------------------
uint32_t brokerBackoffMs(uint32_t retry_count) {
    static const uint32_t kSchedule[] = {5000, 15000, 30000, 60000, 120000};
    const size_t n = sizeof(kSchedule) / sizeof(kSchedule[0]);
    return retry_count < n ? kSchedule[retry_count] : kSchedule[n - 1];
}

// #69: the wall clock is "sane" (SNTP or BLE has set real time) once it is past
// this epoch. Below it -- e.g. the build-time placeholder of May 2024 -- TLS
// server certs read "not yet valid" and JWT iat/exp are garbage, so a wss/TLS
// connect only burns backoff cycles. Threshold = 2025-01-01 UTC.
static bool wallClockSane() {
    return std::time(nullptr) > 1735689600;
}

// ---------------------------------------------------------------------------
// CA cert lookup table.
// ---------------------------------------------------------------------------
// MqttCaCerts.h embeds the ISRG Root X1 cert under the name
// kEastmeshIsrgRootX1Pem. EastMesh + LetsMesh both use Let's Encrypt
// (ISRG Root X1) so one cert covers all three vendored brokers.
//
// Naming convention exposed to operators: "letsencrypt", "eastmesh"
// (alias), or "isrg-x1" (alias) all map to ISRG Root X1 (Let's Encrypt RSA;
// covers EastMesh). "isrg-x2" = Let's Encrypt ECDSA root; "gts-r4" = Google
// Trust Services Root R4 (covers LetsMesh). New brokers add a name here +
// a PEM in MqttCaCerts.h. #48 Item 2.
const char* lookupCaCertPem(const char* name) {
    if (name == nullptr || name[0] == '\0') return nullptr;
#ifdef ARDUINO
    if (strcmp(name, "letsencrypt") == 0) return mqtt_ca_certs::kEastmeshIsrgRootX1Pem;
    if (strcmp(name, "eastmesh")    == 0) return mqtt_ca_certs::kEastmeshIsrgRootX1Pem;
    if (strcmp(name, "isrg-x1")     == 0) return mqtt_ca_certs::kEastmeshIsrgRootX1Pem;
    if (strcmp(name, "isrg_root_x1")== 0) return mqtt_ca_certs::kEastmeshIsrgRootX1Pem;
    if (strcmp(name, "isrg-x2")     == 0) return mqtt_ca_certs::kIsrgRootX2Pem;
    if (strcmp(name, "gts-r4")      == 0) return mqtt_ca_certs::kGtsRootR4Pem;
#else
    (void)name;
#endif
    return nullptr;  // unknown name -- caller treats as missing cert
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
MqttBroker::~MqttBroker() {
    shutdown();
}

void MqttBroker::initLock() {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    if (client_lock_ == nullptr) {
        client_lock_ = xSemaphoreCreateRecursiveMutex();
    }
#endif
}

void MqttBroker::shutdown() {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    ClientLockGuard _g(client_lock_);
    if (client_ != nullptr) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }
    started_ = false;  // LoRa#327: client gone -> next start() is a fresh first start
#endif
    if (auth_ != nullptr) {
        delete auth_;
        auth_ = nullptr;
    }
    rt_ = BrokerRuntimeState{};
}

#ifdef ARDUINO
bool MqttBroker::begin(uint8_t slot, const BrokerConfig& cfg,
                       const mesh::LocalIdentity& identity) {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    ClientLockGuard _g(client_lock_);
#endif
    shutdown();  // idempotent re-init
    if (cfg.url[0] == '\0') {
        return false;  // disabled-by-config
    }
    slot_ = slot;
    cfg_  = cfg;
    // #53: a DISABLED slot is "configured" (status reflects cfg_) but holds
    // NO live client/auth -- the esp_mqtt client exists only while enabled.
    // Enabling later routes through reloadSlot()->begin() which creates it.
    if (!cfg.enabled) {
        rt_ = BrokerRuntimeState{};
        return true;
    }
    auth_ = makeAuth(cfg, identity);
    if (auth_ == nullptr) {
        return false;  // bad auth config (e.g., Jwt without audience)
    }

#if defined(ESP_PLATFORM)
    esp_mqtt_client_config_t mqcfg = {};
#if ESP_IDF_VERSION_MAJOR >= 5
    mqcfg.broker.address.uri = cfg_.url;
    if (cfg_.transport == BrokerTransport::Tls ||
        cfg_.transport == BrokerTransport::Wss) {
        const char* pem = lookupCaCertPem(cfg_.ca_cert_name);
        if (pem != nullptr) mqcfg.broker.verification.certificate = pem;
    }
#else
    mqcfg.uri = cfg_.url;
    if (cfg_.transport == BrokerTransport::Tls ||
        cfg_.transport == BrokerTransport::Wss) {
        const char* pem = lookupCaCertPem(cfg_.ca_cert_name);
        if (pem != nullptr) mqcfg.cert_pem = pem;
    }
#endif

    // Apply auth strategy (sets username + password/token fields).
    if (!auth_->apply(mqcfg, /*now_ms=*/0)) {
        // Auth setup failed (e.g., JWT mint error). Leave broker Down.
        rt_.state = BrokerState::Down;
        rt_.last_error_class = BrokerErrorClass::Auth;
        return false;
    }

    client_ = esp_mqtt_client_init(&mqcfg);
    if (client_ == nullptr) {
        rt_.state = BrokerState::Down;
        rt_.last_error_class = BrokerErrorClass::Other;
        return false;
    }
    // ESP_EVENT_ANY_ID is `-1` (int); cast to esp_mqtt_event_id_t to
    // satisfy esp_mqtt_client_register_event's stricter signature.
    esp_mqtt_client_register_event(client_,
                                   (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   &MqttBroker::eventHandler, this);
#endif  // ESP_PLATFORM

    rt_.state = BrokerState::Down;  // Down until tryConnect
    return true;
}
#else
bool MqttBroker::begin(uint8_t slot, const BrokerConfig& cfg) {
    shutdown();
    if (cfg.url[0] == '\0') return false;
    slot_ = slot;
    cfg_  = cfg;
    // Host build: no esp_mqtt, no identity -> auth set to None for shape.
    auth_ = new MqttAuthNone();
    rt_.state = BrokerState::Down;
    return true;
}
#endif

// ---------------------------------------------------------------------------
// tryConnect: state machine entry point. Initiates a connection attempt
// if the broker is eligible (enabled + Down + backoff window expired).
// ---------------------------------------------------------------------------
bool MqttBroker::tryConnect(uint32_t now_ms) {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    ClientLockGuard _g(client_lock_);
#endif
    if (!isConfigured() || !cfg_.enabled) {
        rt_.state = BrokerState::Down;
        return false;
    }
    if (rt_.state == BrokerState::Connecting || rt_.state == BrokerState::Up) {
        return true;
    }
    if (rt_.state == BrokerState::Backoff) {
        uint32_t elapsed = now_ms - rt_.last_attempt_ms;
        if (elapsed < brokerBackoffMs(rt_.retry_count)) {
            return false;
        }
    }

    // #69: wss/TLS brokers can't complete the TLS handshake until the wall clock
    // is valid (server certs read "not yet valid"; JWT iat/exp would be wrong
    // too). Hold off -- without burning a backoff cycle -- until SNTP sets a sane
    // clock; we retry on the next drive tick. tcp brokers are unaffected.
    if ((cfg_.transport == BrokerTransport::Tls ||
         cfg_.transport == BrokerTransport::Wss) && !wallClockSane()) {
        return false;
    }

    rt_.last_attempt_ms = now_ms;
    rt_.state = BrokerState::Connecting;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    if (client_ == nullptr) {
        rt_.state = BrokerState::Down;
        return false;
    }
    // For JWT, re-apply if needsRefresh -- the apply may have been at init
    // time and the token may have aged out before first connect attempt.
    if (auth_ != nullptr && auth_->needsRefresh(now_ms)) {
        esp_mqtt_client_config_t mqcfg = {};
#if ESP_IDF_VERSION_MAJOR >= 5
        mqcfg.broker.address.uri = cfg_.url;
        if (cfg_.transport == BrokerTransport::Tls ||
            cfg_.transport == BrokerTransport::Wss) {
            const char* pem = lookupCaCertPem(cfg_.ca_cert_name);
            if (pem != nullptr) mqcfg.broker.verification.certificate = pem;
        }
#else
        mqcfg.uri = cfg_.url;
        if (cfg_.transport == BrokerTransport::Tls ||
            cfg_.transport == BrokerTransport::Wss) {
            const char* pem = lookupCaCertPem(cfg_.ca_cert_name);
            if (pem != nullptr) mqcfg.cert_pem = pem;
        }
#endif
        if (!auth_->apply(mqcfg, now_ms)) {
            rt_.state = BrokerState::Backoff;
            rt_.retry_count++;
            rt_.last_error_class = BrokerErrorClass::Auth;
            return false;
        }
        esp_mqtt_set_config(client_, &mqcfg);
    }
    // LoRa#327: pair start with a preceding stop on retries. esp-mqtt expects a
    // single start(); re-calling esp_mqtt_client_start() on an already-started
    // client (after a dropped/failed connection that left it in Backoff) leaks
    // the transport + mbedTLS allocations -- ~68KB bled to exhaustion in the
    // field, which then starves WiFi/lwIP and takes the whole device down.
    // Stop here in the pool-task context, NOT in onDisconnected: that runs in
    // the mqtt event-handler/task context, where stop() self-joins the calling
    // task and deadlocks. Each attempt is now a clean stop->start cycle; the
    // JWT was already re-applied above so the fresh start uses a current token.
    if (started_) {
        esp_mqtt_client_stop(client_);
        started_ = false;
        // LoRa#327 (Gemini review): re-assert Connecting AFTER stop(). stop()
        // joins the old mqtt task, so by here a late onDisconnected from the
        // dying task can no longer fire and clobber state to Backoff while we
        // are actually reconnecting (which would cause reconnect stutter).
        rt_.state = BrokerState::Connecting;
    }
    esp_err_t err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
        rt_.state = BrokerState::Backoff;
        rt_.retry_count++;
        rt_.last_error_class = BrokerErrorClass::Other;
        return false;
    }
    started_ = true;
#endif
    return true;
}

// ---------------------------------------------------------------------------
// loop: drive auth refresh + housekeeping. Connection lifecycle is event-
// driven via eventHandler, so this is mostly a token-refresh poke point.
// ---------------------------------------------------------------------------
void MqttBroker::loop(uint32_t now_ms) {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    ClientLockGuard _g(client_lock_);
#endif
    if (!isConfigured() || auth_ == nullptr) return;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    // While Up, periodically check whether the JWT token has aged out.
    // If so, re-apply (which re-mints) and push the new credential into
    // the client. esp_mqtt will use it on the next reconnect cycle.
    if (rt_.state == BrokerState::Up && client_ != nullptr) {
        if (auth_->needsRefresh(now_ms)) {
            esp_mqtt_client_config_t mqcfg = {};
#if ESP_IDF_VERSION_MAJOR >= 5
            mqcfg.broker.address.uri = cfg_.url;
            if (cfg_.transport == BrokerTransport::Tls ||
                cfg_.transport == BrokerTransport::Wss) {
                const char* pem = lookupCaCertPem(cfg_.ca_cert_name);
                if (pem != nullptr) mqcfg.broker.verification.certificate = pem;
            }
#else
            mqcfg.uri = cfg_.url;
            if (cfg_.transport == BrokerTransport::Tls ||
                cfg_.transport == BrokerTransport::Wss) {
                const char* pem = lookupCaCertPem(cfg_.ca_cert_name);
                if (pem != nullptr) mqcfg.cert_pem = pem;
            }
#endif
            if (auth_->apply(mqcfg, now_ms)) {
                esp_mqtt_set_config(client_, &mqcfg);
            }
        }
    }
#else
    (void)now_ms;
#endif
}

// ---------------------------------------------------------------------------
// publish
// ---------------------------------------------------------------------------
bool MqttBroker::publish(const char* topic, const uint8_t* payload, size_t len,
                         bool retain) {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    ClientLockGuard _g(client_lock_);
#endif
    if (rt_.state != BrokerState::Up) return false;
    if (topic == nullptr || payload == nullptr) return false;

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    if (client_ == nullptr) return false;
    int rc = esp_mqtt_client_enqueue(client_, topic, (const char*)payload, (int)len,
                                     /*qos=*/1, retain ? 1 : 0, /*store=*/true);
    if (rc < 0) {
        return false;
    }
    rt_.last_publish_ms = millis();
    return true;
#else
    (void)topic; (void)payload; (void)len; (void)retain;
    return false;  // host stub
#endif
}

// ---------------------------------------------------------------------------
// fillPayloadCtx
// ---------------------------------------------------------------------------
void MqttBroker::fillPayloadCtx(MqttPayloadCtx& ctx,
                                const char* global_iata,
                                const char* device_id,
                                const char* node_name,
                                const char* client_version,
                                const char* firmware_version,
                                const char* model) const {
    ctx.iata = (cfg_.iata_override[0] != '\0') ? cfg_.iata_override
                                               : global_iata;
    ctx.device_id        = device_id;
    ctx.node_name        = node_name;
    ctx.topic_prefix     = (cfg_.topic_prefix[0] != '\0') ? cfg_.topic_prefix
                                                          : kDefaultTopicPrefix;
    ctx.client_version   = client_version;
    ctx.firmware_version = firmware_version;
    ctx.model            = model;
}

// ---------------------------------------------------------------------------
// esp_mqtt event handler (ARDUINO + ESP_PLATFORM only)
// ---------------------------------------------------------------------------
#if defined(ARDUINO) && defined(ESP_PLATFORM)

void MqttBroker::eventHandler(void* handler_args,
                              esp_event_base_t /*base*/,
                              int32_t event_id,
                              void* event_data) {
    MqttBroker* self = static_cast<MqttBroker*>(handler_args);
    if (self == nullptr) return;
    uint32_t now_ms = millis();
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            self->onConnected(now_ms);
            break;
        case MQTT_EVENT_DISCONNECTED:
            self->onDisconnected(now_ms, BrokerErrorClass::Tcp);
            break;
        case MQTT_EVENT_ERROR: {
            BrokerErrorClass err = BrokerErrorClass::Other;
            if (event != nullptr && event->error_handle != nullptr) {
                // Classify by error_type. Connection refused is auth-y;
                // TLS errors get their own class.
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                    err = BrokerErrorClass::Auth;
                } else if (event->error_handle->esp_tls_last_esp_err != 0 ||
                           event->error_handle->esp_tls_stack_err != 0) {
                    err = BrokerErrorClass::Tls;
                } else if (event->error_handle->esp_transport_sock_errno != 0) {
                    err = BrokerErrorClass::Tcp;
                }
            }
            self->onError(now_ms, err);
            break;
        }
        default:
            // PUBLISHED, SUBSCRIBED, etc. -- no state change needed.
            break;
    }
}

void MqttBroker::onConnected(uint32_t /*now_ms*/) {
    rt_.state = BrokerState::Up;
    rt_.retry_count = 0;
    rt_.last_error_class = BrokerErrorClass::None;
}

void MqttBroker::onDisconnected(uint32_t now_ms, BrokerErrorClass err) {
    // Move to Backoff (esp_mqtt will not auto-retry; pool's tryConnect
    // honors backoff schedule and re-initiates).
    rt_.state = BrokerState::Backoff;
    rt_.last_error_ms = now_ms;
    rt_.retry_count++;
    rt_.last_error_class = err;
}

void MqttBroker::onError(uint32_t now_ms, BrokerErrorClass err) {
    // Errors typically precede a DISCONNECTED; record the class so the
    // subsequent disconnect transition records a meaningful reason.
    rt_.last_error_ms = now_ms;
    rt_.last_error_class = err;
}

#endif  // ARDUINO && ESP_PLATFORM

}  // namespace crosswire
