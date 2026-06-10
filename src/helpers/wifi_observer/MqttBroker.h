// src/helpers/wifi_observer/MqttBroker.h
//
// Plan 2 v2 Task 7: per-slot concrete broker. Wraps esp_mqtt_client.
// Owns one MqttAuth strategy. Transport (TCP/TLS/WSS) is implicit from
// the URI scheme in cfg.url. State machine: Down/Connecting/Up/Backoff.

#pragma once
#include "ConfigSchema.h"
#include "MqttAuth.h"
#include "MqttPayload.h"

#ifdef ARDUINO
  #include <Identity.h>
#endif
#if defined(ARDUINO) && defined(ESP_PLATFORM)
  #include <mqtt_client.h>
  #include "freertos/FreeRTOS.h"
  #include "freertos/semphr.h"
#endif

namespace crosswire {

enum class BrokerState : uint8_t {
    Down       = 0,  // disabled OR not yet attempted
    Connecting = 1,  // tcp connect / TLS handshake in progress
    Up         = 2,  // healthy
    Backoff    = 3,  // failed; waiting for backoff window
};

enum class BrokerErrorClass : uint8_t {
    None  = 0,
    Tcp   = 1,  // connection refused / timeout
    Auth  = 2,  // bad credentials / token rejected
    Tls   = 3,  // TLS handshake / cert verify
    Other = 4,  // unclassified
};

struct BrokerRuntimeState {
    BrokerState      state             = BrokerState::Down;
    uint32_t         last_publish_ms   = 0;
    uint32_t         last_attempt_ms   = 0;
    uint32_t         last_error_ms     = 0;
    uint32_t         retry_count       = 0;
    BrokerErrorClass last_error_class  = BrokerErrorClass::None;
};

class MqttBroker {
public:
    MqttBroker() = default;
    ~MqttBroker();

    // Disable copy + move (owns raw pointers).
    MqttBroker(const MqttBroker&) = delete;
    MqttBroker& operator=(const MqttBroker&) = delete;

    // Bind slot to config. Allocates esp_mqtt_client + MqttAuth.
    // Does NOT start the connection; pool decides via tryConnect().
    // Returns false if cfg is invalid (empty URL, unknown auth, etc.).
#ifdef ARDUINO
    bool begin(uint8_t slot, const BrokerConfig& cfg,
               const mesh::LocalIdentity& identity);
#else
    bool begin(uint8_t slot, const BrokerConfig& cfg);  // host stub (no identity needed)
#endif

    // Tear down: stop client, destroy client, free auth. Idempotent.
    void shutdown();

    // Create the per-broker client mutex. Call ONCE from the pool on
    // loopTask, before the lifecycle worker task starts (Strycher/Crosswire#53),
    // so worker create/destroy and loopTask publish/connect serialize on the
    // same handle. Idempotent; no-op on host builds.
    void initLock();

    // Initiate connection if not already up/connecting. Honors backoff.
    // Returns true if a connect attempt is in progress after this call,
    // false if disabled or backoff window unexpired.
    bool tryConnect(uint32_t now_ms);

    // Drive auth refresh + any housekeeping. Called every pool tick.
    void loop(uint32_t now_ms);

    // Publish raw bytes to a fully-formatted topic. Returns false if
    // state != Up or enqueue fails.
    bool publish(const char* topic, const uint8_t* payload, size_t len,
                 bool retain);

    // Build a payload context for fan-out callers.
    void fillPayloadCtx(MqttPayloadCtx& ctx,
                        const char* global_iata,
                        const char* device_id,
                        const char* node_name,
                        const char* client_version,
                        const char* firmware_version,
                        const char* model) const;

    uint8_t                    slot()    const { return slot_; }
    const BrokerConfig&        config()  const { return cfg_; }
    const BrokerRuntimeState&  runtime() const { return rt_; }
    bool                       isConfigured() const { return slot_ != 0xFF; }

private:
    uint8_t              slot_  = 0xFF;
    BrokerConfig         cfg_;
    BrokerRuntimeState   rt_;
    MqttAuth*            auth_  = nullptr;  // owned

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    esp_mqtt_client_handle_t client_ = nullptr;
    // Serializes ALL client_ ops (publish/start/stop/destroy) so the lifecycle
    // worker's blocking stop/destroy never races a loopTask publish/connect on
    // the same handle (Strycher/Crosswire#53). Recursive: begin() -> shutdown().
    SemaphoreHandle_t        client_lock_ = nullptr;
    // LoRa#327: tracks whether esp_mqtt_client_start() has been called since the
    // last stop/destroy, so tryConnect() can pair a stop() before each re-start.
    // esp-mqtt expects start() once; re-calling it on an already-started client
    // (after a dropped/failed connection) leaks transport + mbedTLS allocations.
    bool started_ = false;

    // C-style event dispatch -- esp_mqtt requires a static function.
    // The handler_args is the MqttBroker* registered via
    // esp_mqtt_client_register_event.
    static void eventHandler(void* handler_args,
                             esp_event_base_t base,
                             int32_t event_id,
                             void* event_data);
    void onConnected(uint32_t now_ms);
    void onDisconnected(uint32_t now_ms, BrokerErrorClass err);
    void onError(uint32_t now_ms, BrokerErrorClass err);
#endif
};

// Backoff schedule (ms): 5s, 15s, 30s, 60s, 120s, then 120s capped.
// Plan 2 v2 keeps the same schedule as the original plan. The pool
// staggers initiating by slot index (slot * 1000ms) to avoid thundering
// herd on simultaneous WiFi recovery.
uint32_t brokerBackoffMs(uint32_t retry_count);

// CA cert lookup: name -> PEM string (or nullptr if name unknown).
// Lookup table is defined in MqttBroker.cpp and references the embedded
// PEM strings in MqttCaCerts.h. Empty/null name means no cert (caller
// uses system trust store or skips TLS verify).
const char* lookupCaCertPem(const char* name);

}  // namespace crosswire
