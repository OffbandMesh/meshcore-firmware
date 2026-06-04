// src/helpers/wifi_observer/ObserverPipeline.h
//
// Plan 2 v2 Task 9: LoRa RX hook + bounded ring buffer of recent packets.
// Hooks MyMesh::logRxRaw (which fires for every raw RX with rssi+snr).
// Fans every received packet out to the broker pool via the /raw topic.
//
// MyMesh calibration (recorded during Task 9 Step 1 audit):
//   * Dispatcher.cpp:198 calls `logRxRaw(snr, rssi, raw, len)` for every
//     raw RX, BEFORE Packet construction. Already virtual on Dispatcher
//     (declared empty default) -- MyMesh overrides at MyMesh.cpp:282 to
//     forward to the serial interface.
//   * We extend MyMesh::logRxRaw to ALSO invoke crosswire's trampoline
//     when CROSSWIRE_OBSERVER is defined. Non-observer builds unaffected.
//   * Hook point gives us raw bytes + rssi + snr cleanly. Does NOT give
//     us a parsed mesh::Packet -- the Dispatcher constructs Packet AFTER
//     logRxRaw. So Plan 2 v2 publishes via /raw topic only (buildRawJson
//     semantics: minimal envelope, raw hex bytes). The /packets topic
//     with parsed-field JSON (route, payload_type, hash, path, etc.) is
//     deferred to a follow-up issue -- needs a different hook point or
//     RSSI/SNR side-channel.

#pragma once
#include "WifiObserverConfig.h"
#include "MqttBrokerPool.h"

namespace crosswire {

// Captured packet snapshot for the in-memory ring (web UI in Plan 3
// consumes this). Bounded to 256 raw bytes (MeshCore's max packet size).
struct ObservedPacket {
    uint32_t recv_at_ms;
    float    rssi;
    float    snr;
    uint8_t  raw_len;
    uint8_t  raw[256];
};

class ObserverPipeline {
public:
    // Wire the pipeline to its pool. Caller (WifiObserver) owns the pool;
    // pipeline keeps a borrowed pointer.
    void begin(MqttBrokerPool* pool);

    // Called via the trampoline below from MyMesh::logRxRaw.
    // Captures the packet in the ring + fans out via the pool's /raw publish.
    void onRawReceived(const uint8_t* raw, int len, float rssi, float snr);

    // Called via the parsed trampoline below from MyMesh::logRx (Strycher/
    // LoRa#335). Fans the PARSED packet out via the pool's /packets publish
    // (route/payload_type/dedupe-hash JSON -- the topic CoreScope ingests).
    // Does not touch the ring (that mirrors the raw RX stream).
    void onParsedReceived(const mesh::Packet& packet, int rssi, float snr,
                          int score, int duration);

    // Ring accessors (Plan 3 web UI will read these).
    uint8_t recentCount() const { return count_; }
    const ObservedPacket& recent(uint8_t idx) const;

private:
    MqttBrokerPool* pool_  = nullptr;
    ObservedPacket  ring_[CROSSWIRE_MAX_RECENT_PACKETS];
    uint8_t         head_  = 0;
    uint8_t         count_ = 0;
};

// Process-wide singleton accessor. ObserverPipeline is intentionally a
// single instance (matches the single LoRa radio + single pool).
ObserverPipeline& observerPipeline();

// C-style trampoline that MyMesh::logRxRaw calls under
// #ifdef CROSSWIRE_OBSERVER. Delegates to observerPipeline().onRawReceived.
// Signature deliberately matches the logRxRaw shape so the wiring at the
// call site is a one-liner.
void observerLogRxTrampoline(float snr, float rssi, const uint8_t* raw, int len);

// C-style trampoline that MyMesh::logRx calls under #ifdef CROSSWIRE_OBSERVER
// (Strycher/LoRa#335). Delegates to observerPipeline().onParsedReceived for
// the /packets publish path.
void observerLogRxParsedTrampoline(const mesh::Packet& packet, int rssi,
                                   float snr, int score, int duration);

}  // namespace crosswire
