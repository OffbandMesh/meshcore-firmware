// src/helpers/wifi_observer/ObserverPipeline.cpp
//
// Plan 2 v2 Task 9.

#include "ObserverPipeline.h"
#include <cstring>

#ifdef ARDUINO
  #include <Arduino.h>
#endif

namespace crosswire {

void ObserverPipeline::begin(MqttBrokerPool* pool) {
    pool_  = pool;
    head_  = 0;
    count_ = 0;
}

void ObserverPipeline::onRawReceived(const uint8_t* raw, int len, float rssi, float snr) {
    if (raw == nullptr || len <= 0) return;
    if (pool_ == nullptr) return;

#ifdef ARDUINO
    // 1. Capture into ring at head_, advance + saturate count.
    ObservedPacket& slot = ring_[head_];
    slot.recv_at_ms = millis();
    slot.rssi       = rssi;
    slot.snr        = snr;
    int copy_len = len;
    if (copy_len > (int)sizeof(slot.raw)) copy_len = sizeof(slot.raw);
    memcpy(slot.raw, raw, copy_len);
    slot.raw_len = (uint8_t)copy_len;
    head_ = (head_ + 1) % CROSSWIRE_MAX_RECENT_PACKETS;
    if (count_ < CROSSWIRE_MAX_RECENT_PACKETS) count_++;

    // 2. Fan-out via pool (pool handles cached strings + per-broker ctx).
    pool_->publishRawFromBytes(raw, (size_t)len, rssi, snr);
#else
    (void)raw; (void)len; (void)rssi; (void)snr;
#endif
}

void ObserverPipeline::onParsedReceived(const mesh::Packet& packet, int rssi,
                                        float snr, int score, int duration) {
    if (pool_ == nullptr) return;
#ifdef ARDUINO
    // Fan-out via pool's /packets path (parsed-field JSON + dedupe hash).
    pool_->publishParsedPacket(packet, rssi, snr, score, duration);
#else
    (void)packet; (void)rssi; (void)snr; (void)score; (void)duration;
#endif
}

const ObservedPacket& ObserverPipeline::recent(uint8_t idx) const {
    uint8_t base = (head_ + CROSSWIRE_MAX_RECENT_PACKETS - count_) % CROSSWIRE_MAX_RECENT_PACKETS;
    uint8_t real = (base + idx) % CROSSWIRE_MAX_RECENT_PACKETS;
    return ring_[real];
}

// ---------------------------------------------------------------------------
// Singleton + trampoline
// ---------------------------------------------------------------------------
ObserverPipeline& observerPipeline() {
    static ObserverPipeline inst;
    return inst;
}

void observerLogRxTrampoline(float snr, float rssi, const uint8_t* raw, int len) {
    observerPipeline().onRawReceived(raw, len, rssi, snr);
}

void observerLogRxParsedTrampoline(const mesh::Packet& packet, int rssi,
                                   float snr, int score, int duration) {
    observerPipeline().onParsedReceived(packet, rssi, snr, score, duration);
}

}  // namespace crosswire
