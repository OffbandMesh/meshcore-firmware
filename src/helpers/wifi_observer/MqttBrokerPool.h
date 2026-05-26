// src/helpers/wifi_observer/MqttBrokerPool.h
//
// Plan 2 v2 Task 8: owns up to CROSSWIRE_MAX_BROKERS MqttBroker instances.
// Loads each from NVS via ConfigSchema. Schedules connect attempts
// (staggered by slot index). Fan-out publish + periodic /status.

#pragma once
#include "WifiObserverConfig.h"
#include "MqttBroker.h"

#ifdef ARDUINO
  #include <Identity.h>
#endif

namespace crosswire {

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

    // Reload one slot from NVS (after CLI config change). Tears down the
    // old broker for that slot, re-reads config, and re-begins if enabled.
    // Returns true if reload succeeded (or slot is now disabled).
#ifdef ARDUINO
    bool reloadSlot(uint8_t slot, const mesh::LocalIdentity& identity);
#endif

    // For status / CLI reporting.
    uint8_t configuredCount() const;
    uint8_t enabledCount()    const;
    uint8_t upCount()         const;
    const MqttBroker& broker(uint8_t slot) const { return brokers_[slot]; }

private:
#ifdef ARDUINO
    const mesh::LocalIdentity* identity_ = nullptr;
#endif
    MqttBroker brokers_[CROSSWIRE_MAX_BROKERS];

    // Cached strings for ctx fills (caller owns lifetime).
    const char* device_id_        = "";
    const char* node_name_        = "";
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

}  // namespace crosswire
