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

namespace offband {

enum class BrokerState : uint8_t {
    Down        = 0,  // disabled OR not yet attempted
    Connecting  = 1,  // tcp connect / TLS handshake in progress
    Up          = 2,  // healthy
    Backoff     = 3,  // failed; waiting for backoff window
    HeldNoClock = 4,  // #87: wss/TLS deferred pending a sane wall clock (NTP/GPS).
                      // NOT a failure -- no retry burned; released automatically on
                      // the next drive tick once wallClockSane() becomes true.
    HeldNoHeap  = 5,  // #171: wss/TLS bring-up deferred -- the pool's TLS budget is
                      // full (>= OFFBAND_MAX_LIVE_TLS live) or free heap is below
                      // the floor. NOT a failure -- no retry burned; released on a
                      // later drive tick once a TLS slot frees or heap recovers
                      // (mirrors HeldNoClock).
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
    uint32_t         went_up_ms        = 0;  // #175: set when state -> Up; picks the oldest live TLS slot to rotate out
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
    // #98: ALSO deconfigures the slot (slot_ = 0xFF, cfg_ cleared) so a cleared
    // slot drops out of `mqtt status` immediately. That makes shutdown() the
    // wrong verb for a TEMPORARY demotion -- see releaseClient().
    void shutdown();

    // #175: release the live connection WITHOUT deconfiguring the slot.
    // Destroys the esp_mqtt client + auth (reclaiming the ~60KB mbedTLS context,
    // same as shutdown -- verified ~56KB on hardware) but RETAINS slot_ and cfg_,
    // so isConfigured() stays true. This is what TLS rotation needs: shutdown()'s
    // #98 deconfigure makes the victim invisible to every pool loop (all of which
    // are gated on isConfigured()), stranding it permanently -- one-way eviction
    // instead of round-robin. With the slot still configured, the pool's normal
    // cooldown -> budget -> reconcileSlot -> tryConnect path can promote it back.
    void releaseClient();

    // Create the per-broker client mutex. Call ONCE from the pool on
    // loopTask, before the lifecycle worker task starts (#53),
    // so worker create/destroy and loopTask publish/connect serialize on the
    // same handle. Idempotent; no-op on host builds.
    void initLock();

    // Initiate connection if not already up/connecting. Honors backoff.
    // Returns true if a connect attempt is in progress after this call,
    // false if disabled or backoff window unexpired.
    // tls_budget_ok: pool-supplied, REQUIRED (no default -- so no caller can
    // silently bypass the concurrency guard #171). false means OFFBAND_MAX_LIVE_TLS
    // TLS contexts are already live, so a wss/TLS bring-up self-defers to
    // HeldNoHeap. Pass true for tcp brokers (the guard ignores them).
    bool tryConnect(uint32_t now_ms, bool tls_budget_ok);

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

    // #175: true if the esp_mqtt client handle currently exists. shutdown()
    // destroys it (client_ = nullptr) to free the ~60KB TLS context without
    // leaking (#327); tryConnect can only START an existing client, never create
    // one, so the pool must recreate (reconcileSlot -> begin) a rotated-out
    // broker before it can be brought back up. Host build: always true (no real
    // client, nothing to recreate).
    bool hasClient() const;
    bool                       isConfigured() const { return slot_ != 0xFF; }

private:
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    // #532: fill the transport-level fields of an esp_mqtt config -- uri,
    // keepalive, and CA cert. The config is rebuilt in THREE places (begin() and
    // the two JWT-refresh paths in tryConnect()/loop()), and the refresh paths
    // used to set only uri+cert. esp_mqtt_set_config() would then leave keepalive
    // at esp-mqtt's default 120, so a JWT broker behind a max_keepalive cap
    // silently reverted after a token refresh and drew the misleading CONNACK
    // 0x02 diagnosed in #506. Centralizing it makes every build site identical.
    // Auth is applied by the caller AFTER this (auth_->apply()).
    void populateBaseConfig(esp_mqtt_client_config_t& mqcfg) const;
#endif

    uint8_t              slot_  = 0xFF;
    BrokerConfig         cfg_;
    BrokerRuntimeState   rt_;
    MqttAuth*            auth_  = nullptr;  // owned

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    esp_mqtt_client_handle_t client_ = nullptr;
    // Serializes ALL client_ ops (publish/start/stop/destroy) so the lifecycle
    // worker's blocking stop/destroy never races a loopTask publish/connect on
    // the same handle (#53). Recursive: begin() -> shutdown().
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

}  // namespace offband
