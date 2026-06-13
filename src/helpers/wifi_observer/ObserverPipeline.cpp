// src/helpers/wifi_observer/ObserverPipeline.cpp
//
// Plan 2 v2 Task 9.

#include "ObserverPipeline.h"
#include <cstring>

#ifdef ARDUINO
  #include <Arduino.h>
#endif

namespace offband {

void ObserverPipeline::begin(MqttBrokerPool* pool) {
    pool_  = pool;
}

void ObserverPipeline::onRawReceived(const uint8_t* raw, int len, float rssi, float snr) {
    if (raw == nullptr || len <= 0) return;
    if (pool_ == nullptr) return;

#ifdef ARDUINO
    // Fan-out via pool (pool handles cached strings + per-broker ctx).
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

}  // namespace offband
