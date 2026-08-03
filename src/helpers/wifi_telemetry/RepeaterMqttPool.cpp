// src/helpers/wifi_telemetry/RepeaterMqttPool.cpp — see header (#536 / #302).

#include "RepeaterMqttPool.h"

#if defined(OFFBAND_MQTT_POOL)

// Impl-only include of the shared engine so the observer surface stays out of
// the telemetry public header.
#include "helpers/wifi_observer/MqttBrokerPool.h"

namespace offband {
namespace {
// One long-lived pool instance, mirroring the observer's static pool. Held here
// (not as a class member) so the observer type never appears in the header.
MqttBrokerPool& thePool() {
    static MqttBrokerPool pool;
    return pool;
}
}  // namespace

void RepeaterMqttPool::begin(const mesh::LocalIdentity& identity,
                             const char* device_id,
                             const char* node_name,
                             const char* firmware_version,
                             const char* model) {
#ifdef ARDUINO
    // The repeater carries a single firmware string; the engine's begin() splits
    // client_version vs firmware_version (an observer distinction) — pass the
    // one repeater FW string for both.
    thePool().begin(identity, device_id, node_name,
                    firmware_version, firmware_version, model);
    started_ = true;
#else
    (void)identity; (void)device_id; (void)node_name;
    (void)firmware_version; (void)model;
#endif
}

void RepeaterMqttPool::loop(uint32_t now_ms) {
    if (started_) thePool().loop(now_ms);
}

uint8_t RepeaterMqttPool::publish(const uint8_t* payload, size_t len) {
    return started_ ? thePool().publishPacket(payload, len) : 0;
}

void RepeaterMqttPool::shutdown() {
    if (started_) {
        thePool().shutdown();
        started_ = false;
    }
}

}  // namespace offband

#ifdef ARDUINO
// #536 link-proof: force the linker to resolve the full engine symbol set in the
// repeater image, catching any observer-pipeline dependency the engine still
// carries. `used` keeps this past --gc-sections; it is NEVER called, so the pool
// never actually starts here (that wiring is #537). If the engine had an
// unresolved observer-only symbol, THIS is where the repeater link would fail.
#include "MeshCore.h"  // mesh::LocalIdentity full type for the dummy instance
__attribute__((used)) static void _repeaterMqttPoolLinkProof() {
    static offband::RepeaterMqttPool pool;
    static mesh::LocalIdentity identity;
    pool.begin(identity, "", "", "", "");
    pool.loop(0);
    (void)pool.publish(nullptr, 0);
    pool.shutdown();
}
#endif  // ARDUINO

#endif  // OFFBAND_MQTT_POOL
