// src/helpers/wifi_observer/MqttBrokerPool.h
//
// Plan 2 v2 Task 8: owns up to OFFBAND_MAX_BROKERS MqttBroker instances.
// Loads each from NVS via ConfigSchema. Schedules connect attempts
// (staggered by slot index). Fan-out publish + periodic /status.

#pragma once
#include "WifiObserverConfig.h"
#include "MqttBroker.h"
#include "MqttRingLog.h"

// #175: every broker slot is a ring reader, so the ring must have at least as
// many cursors as there are broker slots. Caught at compile time rather than
// silently dropping reads for the tail slots if OFFBAND_MAX_BROKERS ever grows.
static_assert(MQTT_RING_MAX_READERS >= OFFBAND_MAX_BROKERS,
              "MQTT_RING_MAX_READERS must be >= OFFBAND_MAX_BROKERS (#175)");

#ifdef ARDUINO
  #include <Identity.h>
#endif
#if defined(ARDUINO) && defined(ESP_PLATFORM)
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/queue.h"
#endif

// Forward-declare so the /packets publish entry point can take a parsed
// packet by reference without pulling Mesh.h into this header (mirrors
// MqttPayload.h). The .cpp only passes the reference through to
// buildPacketJson, so the forward declaration is sufficient here.
namespace mesh { class Packet; }

namespace offband {

class MqttBrokerPool {
public:
    MqttBrokerPool() = default;
    ~MqttBrokerPool() = default;

    // Disable copy + move (owns brokers + esp_mqtt clients).
    MqttBrokerPool(const MqttBrokerPool&) = delete;
    MqttBrokerPool& operator=(const MqttBrokerPool&) = delete;

    // Initialize the pool. Calls populateDefaultBrokers() (idempotent;
    // seeds slots 0-2 with EastMesh/LetsMesh defaults if empty), then
    // reads each slot's config and brings up brokers where enabled.
    //
    // device_id / node_name / version strings are stored for later
    // status payload + ctx fills. Pool keeps pointers; caller must
    // ensure strings outlive the pool (typical: static / global storage).
#ifdef ARDUINO
    void begin(const mesh::LocalIdentity& identity,
               const char* device_id,
               const char* node_name,
               const char* client_version,
               const char* firmware_version,
               const char* model);
#endif

    // Tear down all brokers. Idempotent.
    void shutdown();

    // Drive each broker's loop + schedule connects + periodic status.
    // Call once per main loop iteration.
    void loop(uint32_t now_ms);

    // Caller supplies a populated MqttStatusSnapshot. Pool tracks
    // status_interval_sec internally and publishes when due.
    void setStatusSnapshot(const MqttStatusSnapshot& snap);

    // Fan a pre-built JSON packet payload out to every enabled+Up broker.
    // Each broker uses its own iata_override + topic_prefix to format
    // its individual topic; payload bytes are identical across brokers.
    // Returns number of brokers that accepted the enqueue.
    uint8_t publishPacket(const uint8_t* payload, size_t payload_len);

    // Pool-side /raw publish: caller hands in raw RX bytes + rssi/snr.
    // Pool builds the minimal /raw JSON ONCE using its own cached
    // strings, then formats per-broker topic + publishes to each
    // enabled+Up broker. Returns number of brokers that accepted enqueue.
    //
    // This is the preferred entry point for ObserverPipeline -- matches
    // MyMesh::logRxRaw's signature (raw bytes only, no parsed Packet).
    // The /packets topic with parsed-field JSON (route, payload_type,
    // hash, path, etc.) is deferred to a follow-up issue; requires a
    // different hook point or RSSI/SNR side-channel through onRecvPacket.
    uint8_t publishRawFromBytes(const uint8_t* raw, size_t raw_len,
                                float rssi, float snr);

    // Pool-side /packets publish (Strycher/LoRa#335): caller hands in a
    // PARSED mesh::Packet + rssi/snr/score/duration. Pool builds the
    // parsed-field JSON ONCE via buildPacketJson (route, payload_type,
    // dedupe hash, path, ...) then fans out per-broker via publishPacket.
    // This is the CoreScope/EastMesh-ingested topic. Fed by MyMesh::logRx
    // (the post-parse hook) -- unlike publishRawFromBytes which is fed by
    // the pre-parse logRxRaw hook and can only emit /raw.
    uint8_t publishParsedPacket(const mesh::Packet& packet, int rssi,
                                float snr, int score, int duration);

    // Request an ASYNC reload of one slot after a CLI config change.
    // NON-BLOCKING: marks the slot "reconciling" + posts it to the lifecycle
    // worker task, which performs the (possibly multi-second) esp_mqtt client
    // create/destroy OFF loopTask so the BLE command channel never stalls
    // (#53). Returns false if the pool/worker isn't up or
    // the request couldn't be queued; true if the reload was queued.
    bool reloadSlot(uint8_t slot);

    // For status / CLI reporting.
    uint8_t configuredCount() const;
    uint8_t enabledCount()    const;
    uint8_t upCount()         const;
    const MqttBroker& broker(uint8_t slot) const { return brokers_[slot]; }
    // #173: the device's own pubkey as UPPERCASE hex (the connect-time default for a
    // broker's jwt_owner when none is set, #95) -- so the OCFG_BROKERS dump can show
    // the resolved owner as a placeholder. Writes "" if identity unset / host build.
    void deviceOwnerHex(char* out, size_t out_len) const;

private:
#ifdef ARDUINO
    const mesh::LocalIdentity* identity_ = nullptr;
#endif
    MqttBroker brokers_[OFFBAND_MAX_BROKERS];

    // #175: one retained copy of each published payload; each broker reads at
    // its own cursor. Decouples "message published" from "broker currently live",
    // which is what makes TLS rotation lossless within the ring depth.
    MqttRingLog ring_;

    // Drain one broker's backlog to its transport. Returns messages published.
    uint8_t drainBroker(uint8_t slot);

    // #175: TLS rotation. When more TLS brokers are enabled than the heap-derived
    // budget allows, the live set is rotated on a dwell timer so every feed gets
    // serviced. Degenerates to a no-op when the budget covers every enabled TLS
    // broker (PSRAM boards), via the same code path. Runs on the worker task
    // (the only thread allowed to block on esp_mqtt teardown, #53).
    uint32_t last_rotate_ms_ = 0;
    // Per-slot cooldown: a rotated-out broker is ineligible for reconnect until
    // this deadline, so the parked (HeldNoHeap) broker gets first claim on the
    // freed budget instead of the victim immediately reclaiming it. 0 = not
    // cooling. Compared wrap-safe via (int32_t)(deadline - now).
    uint32_t rotated_out_until_ms_[OFFBAND_MAX_BROKERS] = {0};
    void rotateTlsIfDue(uint32_t now_ms);

    // Per-slot guard: true while the lifecycle worker is creating/destroying
    // that slot's client. loopTask publish/status paths SKIP reconciling slots
    // so they never wait on the worker's blocking teardown (#53).
    volatile bool reconciling_[OFFBAND_MAX_BROKERS] = {false};

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    // Lifecycle worker: owns ALL blocking esp_mqtt ops so loopTask never
    // stalls on MQTT (#53). Created once in begin(); lives for the device's life.
    TaskHandle_t  worker_task_ = nullptr;
    QueueHandle_t reconcile_q_ = nullptr;
    volatile bool worker_run_  = false;
    static void   workerTrampoline(void* arg);
    void          workerLoop();
    void          reconcileSlot(uint8_t slot);  // worker-only; blocking begin/shutdown
#endif

    // Cached strings for ctx fills (caller owns lifetime).
    const char* device_id_        = "";
    char        node_name_[64]    = "";  // OWNED (built in begin()): "<name> radar-emoji" Offband tell
    const char* client_version_   = "";
    const char* firmware_version_ = "";
    const char* model_            = "";

    // Global iata + status interval reloaded from NVS at begin().
    // (CLI can re-write; pool re-reads on next status publish.)
    char     global_iata_[8] = {0};

    // Status scheduler state.
    MqttStatusSnapshot last_status_snap_   = {};
    bool               have_snapshot_      = false;
    uint16_t           status_interval_sec_ = 30;
    uint32_t           last_status_ms_      = 0;
    uint16_t           status_interval_check_ms_ = 0;  // re-read interval every N ms

    // Helpers.
    void publishStatusIfDue(uint32_t now_ms);
};

}  // namespace offband
