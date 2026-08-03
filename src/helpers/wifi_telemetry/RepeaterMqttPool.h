// src/helpers/wifi_telemetry/RepeaterMqttPool.h
//
// #536 (Epic #302) — role-neutral reuse surface for the merged multi-broker
// pool + #175 round-robin TLS scheduler engine on the REPEATER telemetry path.
//
// The engine (MqttBrokerPool + MqttBroker + MqttAuth + MqttRingLog, all under
// helpers/wifi_observer/) is already role-neutral in its API — it takes a mesh
// identity + device strings and fans a pre-built payload out to every live
// broker, rotating TLS slots within a heap-derived budget. This adapter is the
// thin owner the repeater drives from its main loop, exactly as WifiObserver
// does on the observer side, WITHOUT pulling the observer pipeline
// (WifiObserver/ObserverCli/ObserverPipeline) into the repeater build.
//
// It deliberately keeps the observer engine headers out of this PUBLIC header
// (impl-only include in the .cpp) so telemetry translation units don't inherit
// the whole wifi_observer surface.
//
// Gated behind OFFBAND_MQTT_POOL (set on the repeater telemetry env). Actual
// routing of repeater telemetry through the pool is #537; this task only proves
// the engine compiles + links + is callable from here.

#pragma once

#if defined(OFFBAND_MQTT_POOL)

#include <stddef.h>
#include <stdint.h>

namespace mesh { class LocalIdentity; }

namespace offband {

class RepeaterMqttPool {
public:
    // Bring the pool up: seeds default broker slots (idempotent), reads each
    // slot's config, and connects enabled brokers within the #175 heap budget.
    // The string args must outlive the pool (typical: static/global storage).
    void begin(const mesh::LocalIdentity& identity,
               const char* device_id,
               const char* node_name,
               const char* firmware_version,
               const char* model);

    // Drive the pool once per main-loop iteration (schedules connects, dwell
    // rotation, periodic status). No-op until begin() has run.
    void loop(uint32_t now_ms);

    // Fan a pre-built payload out to every enabled + live broker. Returns the
    // number of brokers published to (0 if not started).
    uint8_t publish(const uint8_t* payload, size_t len);

    // Tear the pool down. Idempotent.
    void shutdown();

    bool started() const { return started_; }

private:
    bool started_ = false;
};

}  // namespace offband

#endif  // OFFBAND_MQTT_POOL
