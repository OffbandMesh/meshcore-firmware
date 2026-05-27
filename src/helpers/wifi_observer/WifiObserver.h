// src/helpers/wifi_observer/WifiObserver.h
//
// Top-level entry point for the Crosswire observer subsystem. Owns
// the WifiBootstrap state machine and (Plan 2 v2) the MqttBrokerPool +
// ObserverPipeline that publish observed LoRa traffic to up to
// CROSSWIRE_MAX_BROKERS configured brokers.
//
// Lifecycle on companion_radio/main.cpp:
//   1. wifiObserverBegin()           -- from setup(), after Serial.begin().
//                                       Brings WiFi STA up; pool dormant.
//   2. wifiObserverSetMeshContext()  -- from setup(), AFTER the_mesh.begin()
//                                       so identity is initialized.
//   3. wifiObserverLoop()            -- from loop(), every iteration.
//                                       Starts pool + pipeline when STA up
//                                       and context available; drives pool.
//   4. wifiObserverSetStatusSnapshot() -- from loop() (or its periodic
//                                       updater); pool uses it for the next
//                                       status publish window.

#pragma once
#include "WifiObserverConfig.h"
#include "MqttBrokerPool.h"     // MqttBrokerPool + transitively MqttStatusSnapshot

#ifdef ARDUINO
  #include <Identity.h>          // mesh::LocalIdentity
#endif

namespace crosswire {

// Call from setup(), after Serial.begin() but before any user-facing
// operation. Does NOT block.
void wifiObserverBegin();

// Call from setup(), AFTER the_mesh.begin() so identity (self_id) is
// populated. Caches pointers; pool will be brought up on next loop tick
// where STA is connected. All string args must outlive the observer
// (typical: global / static storage).
#ifdef ARDUINO
void wifiObserverSetMeshContext(
    const mesh::LocalIdentity& identity,
    const char* device_id,
    const char* node_name,
    const char* client_version,
    const char* firmware_version,
    const char* model);
#endif

// Per-iteration: drives WifiBootstrap + pool + status scheduler.
void wifiObserverLoop();

// Update the current MqttStatusSnapshot (battery_mv, uptime_secs,
// radio_freq, etc.). Pool re-reads on each status publish window.
void wifiObserverSetStatusSnapshot(const MqttStatusSnapshot& snap);

// Singleton accessors -- exposed for ObserverCli + future Plan 3 web UI.
MqttBrokerPool& wifiObserverPool();

// Borrowed pointer to the cached 32-byte identity public key,
// or nullptr if mesh context has not yet been set (i.e.
// wifiObserverSetMeshContext has not been called this boot).
// Used by WifiBootstrap to derive the mDNS hostname posted on
// STA-connect. Cheaper than threading the pubkey through every
// caller and consistent with how WebAuth borrows the same bytes.
const uint8_t* wifiObserverPubKey();

}  // namespace crosswire
