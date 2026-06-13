// src/helpers/wifi_observer/MqttBrokerPool.cpp
//
// Plan 2 v2 Task 8.

#include "MqttBrokerPool.h"
#include "MqttPayload.h"
#include <cstring>
#include <cstdio>

#ifdef ARDUINO
  #include <Arduino.h>
#endif

namespace offband {

// ---------------------------------------------------------------------------
// begin
// ---------------------------------------------------------------------------
#ifdef ARDUINO
void MqttBrokerPool::begin(const mesh::LocalIdentity& identity,
                           const char* device_id,
                           const char* node_name,
                           const char* client_version,
                           const char* firmware_version,
                           const char* model) {
    identity_         = &identity;
    device_id_        = device_id        != nullptr ? device_id        : "";
    node_name_        = node_name        != nullptr ? node_name        : "";
    client_version_   = client_version   != nullptr ? client_version   : "";
    firmware_version_ = firmware_version != nullptr ? firmware_version : "";
    model_            = model            != nullptr ? model            : "";

    // Seed defaults (idempotent -- no-op if slots already populated).
    populateDefaultBrokers();

    // Read global iata + status interval from NVS.
    if (!readGlobalIata(global_iata_, sizeof(global_iata_))) {
        global_iata_[0] = '\0';
    }
    status_interval_sec_ = readStatusIntervalSec();
    status_interval_check_ms_ = 30000;  // re-read NVS every 30s in case CLI changed it

    // Create per-broker client locks + clear reconcile flags BEFORE the
    // worker task starts -- runs single-threaded on loopTask here (#53).
    for (uint8_t slot = 0; slot < OFFBAND_MAX_BROKERS; ++slot) {
        brokers_[slot].initLock();
        reconciling_[slot] = false;
    }

    // Initialize each configured slot. begin() stores config always and
    // creates a live client ONLY if the slot is enabled (#53); disabled or
    // url-empty slots stay client-less.
    for (uint8_t slot = 0; slot < OFFBAND_MAX_BROKERS; ++slot) {
        BrokerConfig cfg;
        if (!readBrokerConfig(slot, cfg)) continue;
        if (cfg.url[0] == '\0') continue;  // skip unused slots
        brokers_[slot].begin(slot, cfg, identity);
    }

#if defined(ARDUINO) && defined(ESP_PLATFORM)
    // Start the lifecycle worker. It owns ALL blocking esp_mqtt ops --
    // create/destroy AND the connect/retry loop -- so loopTask never stalls
    // on MQTT for any reason (Strycher/Crosswire#53). Created once; lives for
    // the device's life (pool.begin() is one-shot, guarded upstream).
    worker_run_  = true;
    reconcile_q_ = xQueueCreate(OFFBAND_MAX_BROKERS * 2, sizeof(uint8_t));
    if (reconcile_q_ != nullptr) {
        xTaskCreatePinnedToCore(&MqttBrokerPool::workerTrampoline, "mqtt_worker",
                                6144, this, 5, &worker_task_, tskNO_AFFINITY);
    }
#endif
}
#endif

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------
void MqttBrokerPool::shutdown() {
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    // Stop the worker first so it is not mid-reconcile during teardown.
    // (Defensive: pool.begin() is one-shot and nothing calls pool.shutdown()
    // at runtime, but keep it correct.)
    worker_run_ = false;
    if (reconcile_q_ != nullptr) {
        uint8_t sentinel = 0xFF;
        xQueueSend(reconcile_q_, &sentinel, 0);  // wake worker so it can exit
    }
#endif
    for (uint8_t slot = 0; slot < OFFBAND_MAX_BROKERS; ++slot) {
        brokers_[slot].shutdown();
    }
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------
void MqttBrokerPool::loop(uint32_t now_ms) {
    // #53: the per-slot connect/backoff drive + auth refresh moved to the
    // lifecycle worker task (workerLoop) so a blocking esp_mqtt stop/start can
    // never stall loopTask / the BLE command channel. loopTask keeps only the
    // status publish here -- non-blocking: it publishes only to Up brokers
    // (never a slot the worker is connect-blocking on) and skips slots that are
    // mid-reconcile, with the per-broker lock as the in-flight backstop.
    //
    // On host builds (no worker) nothing drives connects; the host stubs never
    // connect anyway, so this is correct for tests.
    publishStatusIfDue(now_ms);
}

// ---------------------------------------------------------------------------
// setStatusSnapshot
// ---------------------------------------------------------------------------
void MqttBrokerPool::setStatusSnapshot(const MqttStatusSnapshot& snap) {
    last_status_snap_ = snap;
    have_snapshot_    = true;
}

// ---------------------------------------------------------------------------
// publishPacket -- fan-out to every enabled+Up broker
// ---------------------------------------------------------------------------
uint8_t MqttBrokerPool::publishPacket(const uint8_t* payload, size_t payload_len) {
    if (payload == nullptr || payload_len == 0) return 0;
    uint8_t accepted = 0;
    for (uint8_t slot = 0; slot < OFFBAND_MAX_BROKERS; ++slot) {
        if (reconciling_[slot]) continue;  // #53: skip slots mid-reconcile
        MqttBroker& b = brokers_[slot];
        if (!b.isConfigured() || b.runtime().state != BrokerState::Up) continue;

        MqttPayloadCtx ctx;
        b.fillPayloadCtx(ctx, global_iata_, device_id_, node_name_,
                         client_version_, firmware_version_, model_);
        if (ctx.iata == nullptr || ctx.iata[0] == '\0') {
            // Per HARD RULE: silent skip on missing IATA (no garbage topics).
            continue;
        }
        char topic[160];
        formatTopic(topic, sizeof(topic), "packets", ctx);
        if (topic[0] == '\0') continue;
        if (b.publish(topic, payload, payload_len, /*retain=*/false)) {
            accepted++;
        }
    }
    return accepted;
}

// ---------------------------------------------------------------------------
// publishRawFromBytes -- /raw topic publish (Plan 2 v2 path)
// ---------------------------------------------------------------------------
// Builds the minimal /raw JSON (matches buildRawJson format, inlined here
// because we receive raw bytes not a mesh::Packet). Hex-encodes the raw
// bytes, fills origin + origin_id + timestamp + type=RAW + data, then
// fans out to every enabled+Up broker with per-broker topic.
uint8_t MqttBrokerPool::publishRawFromBytes(const uint8_t* raw, size_t raw_len,
                                            float rssi, float snr) {
    (void)rssi; (void)snr;  // not in /raw JSON envelope (consumed by ring only)
    if (raw == nullptr || raw_len == 0) return 0;
    if (configuredCount() == 0) return 0;
    if (raw_len > 256) raw_len = 256;  // truncate at MeshCore packet ceiling

    // Hex-encode bytes (520 bytes accommodates 256 input * 2 + NUL).
    char raw_hex[520];
    bytesToHexUpper(raw, raw_len, raw_hex, sizeof(raw_hex));

    // Build body once -- uses shared ctx strings; iata unused for body.
    char ts[32];
    formatIsoTimestamp(time(nullptr), ts, sizeof(ts));

    char origin[80];
    const char* node_name = (node_name_ != nullptr && node_name_[0] != 0)
                            ? node_name_
                            : (device_id_ != nullptr ? device_id_ : "");
    escapeJsonString(node_name, origin, sizeof(origin));

    const char* dev_id = device_id_ != nullptr ? device_id_ : "";

    char json[1024];
    int n = snprintf(json, sizeof(json),
        "{\"origin\":\"%s\",\"origin_id\":\"%s\",\"timestamp\":\"%s\",\"type\":\"RAW\",\"data\":\"%s\"}",
        origin, dev_id, ts, raw_hex);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(json)) return 0;

    // Fan-out per broker with their own iata + topic_prefix.
    uint8_t accepted = 0;
    for (uint8_t slot = 0; slot < OFFBAND_MAX_BROKERS; ++slot) {
        if (reconciling_[slot]) continue;  // #53: skip slots mid-reconcile
        MqttBroker& b = brokers_[slot];
        if (!b.isConfigured() || b.runtime().state != BrokerState::Up) continue;

        MqttPayloadCtx topic_ctx;
        b.fillPayloadCtx(topic_ctx, global_iata_, device_id_, node_name_,
                         client_version_, firmware_version_, model_);
        if (topic_ctx.iata == nullptr || topic_ctx.iata[0] == '\0') {
            continue;  // silent skip per HARD RULE
        }
        char topic[160];
        formatTopic(topic, sizeof(topic), "raw", topic_ctx);
        if (topic[0] == '\0') continue;
        if (b.publish(topic, reinterpret_cast<const uint8_t*>(json),
                      static_cast<size_t>(n), /*retain=*/false)) {
            accepted++;
        }
    }
    return accepted;
}

// ---------------------------------------------------------------------------
// publishParsedPacket -- /packets topic publish (Strycher/LoRa#335)
// ---------------------------------------------------------------------------
// Builds the parsed-field /packets JSON via buildPacketJson (which computes
// the SHA256 dedupe hash + route/path/payload_type from the parsed packet),
// then fans out to every enabled+Up broker via publishPacket. The body is
// broker-independent (origin/origin_id come from the cached identity;
// iata/topic_prefix only affect the per-broker TOPIC, set inside
// publishPacket), so it is built once.
uint8_t MqttBrokerPool::publishParsedPacket(const mesh::Packet& packet,
                                            int rssi, float snr,
                                            int score, int duration) {
    if (configuredCount() == 0) return 0;

    MqttPayloadCtx ctx;
    ctx.iata             = global_iata_;
    ctx.device_id        = device_id_;
    ctx.node_name        = node_name_;
    ctx.topic_prefix     = "meshcore";   // body ignores this; topic set per-broker
    ctx.client_version   = client_version_;
    ctx.firmware_version = firmware_version_;
    ctx.model            = model_;

    char json[1024];
    int n = buildPacketJson(json, sizeof(json), packet, /*is_tx=*/false,
                            rssi, snr, score, duration, ctx);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(json)) return 0;

    return publishPacket(reinterpret_cast<const uint8_t*>(json),
                         static_cast<size_t>(n));
}

// ---------------------------------------------------------------------------
// publishStatusIfDue -- scheduled in loop()
// ---------------------------------------------------------------------------
void MqttBrokerPool::publishStatusIfDue(uint32_t now_ms) {
    if (!have_snapshot_) return;

    // Periodically re-read status interval from NVS (CLI may have changed it).
    static uint32_t s_last_interval_recheck_ms = 0;
    if (now_ms - s_last_interval_recheck_ms > status_interval_check_ms_) {
        s_last_interval_recheck_ms = now_ms;
#ifdef ARDUINO
        uint16_t fresh = readStatusIntervalSec();
        if (fresh != status_interval_sec_) {
            status_interval_sec_ = fresh;
        }
#endif
    }

    uint32_t interval_ms = static_cast<uint32_t>(status_interval_sec_) * 1000U;
    if (last_status_ms_ != 0 && (now_ms - last_status_ms_) < interval_ms) {
        return;  // not yet due
    }

    char status_buf[1024];
    for (uint8_t slot = 0; slot < OFFBAND_MAX_BROKERS; ++slot) {
        if (reconciling_[slot]) continue;  // #53: skip slots mid-reconcile
        MqttBroker& b = brokers_[slot];
        if (!b.isConfigured() || b.runtime().state != BrokerState::Up) continue;

        MqttPayloadCtx ctx;
        b.fillPayloadCtx(ctx, global_iata_, device_id_, node_name_,
                         client_version_, firmware_version_, model_);
        if (ctx.iata == nullptr || ctx.iata[0] == '\0') continue;

        int n = buildStatusJson(status_buf, sizeof(status_buf),
                                last_status_snap_, ctx, /*online=*/true);
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(status_buf)) continue;

        char topic[160];
        formatTopic(topic, sizeof(topic), "status", ctx);
        if (topic[0] == '\0') continue;

        (void)b.publish(topic, reinterpret_cast<const uint8_t*>(status_buf),
                        static_cast<size_t>(n), /*retain=*/true);
    }
    last_status_ms_ = now_ms;
}

// ---------------------------------------------------------------------------
// reloadSlot -- uses cached identity_ from begin()
// ---------------------------------------------------------------------------
bool MqttBrokerPool::reloadSlot(uint8_t slot) {
    if (slot >= OFFBAND_MAX_BROKERS) return false;
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    // #53: NON-BLOCKING. Mark the slot reconciling (loopTask skips it from
    // here on) and hand it to the worker, which does the actual begin/shutdown
    // (potentially multi-second esp_mqtt teardown) OFF loopTask.
    if (identity_ == nullptr || reconcile_q_ == nullptr) return false;
    reconciling_[slot] = true;
    uint8_t s = slot;
    if (xQueueSend(reconcile_q_, &s, 0) != pdTRUE) {
        reconciling_[slot] = false;  // couldn't queue; release the guard
        return false;
    }
    return true;
#else
    (void)slot;
    return false;  // host stub
#endif
}

// ---------------------------------------------------------------------------
// Lifecycle worker (#53): owns ALL blocking esp_mqtt ops off loopTask.
// ---------------------------------------------------------------------------
#if defined(ARDUINO) && defined(ESP_PLATFORM)
void MqttBrokerPool::workerTrampoline(void* arg) {
    static_cast<MqttBrokerPool*>(arg)->workerLoop();
}

// Reconcile one slot's live client to its NVS desired-state. Blocking
// (begin() may stop+destroy+recreate the esp_mqtt client); runs ONLY on the
// worker task. begin() creates a client only if the slot is enabled.
void MqttBrokerPool::reconcileSlot(uint8_t slot) {
    if (slot >= OFFBAND_MAX_BROKERS || identity_ == nullptr) return;
    BrokerConfig cfg;
    if (!readBrokerConfig(slot, cfg) || cfg.url[0] == '\0') {
        brokers_[slot].shutdown();   // no/empty config -> ensure no client
        return;
    }
    brokers_[slot].begin(slot, cfg, *identity_);
}

void MqttBrokerPool::workerLoop() {
    for (;;) {
        // Block for a reconcile request, but wake every 500ms to drive the
        // connect/backoff state machine + JWT refresh (relocated from loopTask
        // -- #53). 500ms is fine: backoff windows are seconds.
        uint8_t slot = 0xFF;
        bool got = (xQueueReceive(reconcile_q_, &slot, pdMS_TO_TICKS(500)) == pdTRUE);
        if (!worker_run_) break;

        if (got && slot < OFFBAND_MAX_BROKERS) {
            reconcileSlot(slot);
            reconciling_[slot] = false;  // hand the slot back to loopTask
        }

        // Periodic drive for all configured, non-reconciling slots. tryConnect
        // honors backoff internally; the slot*1000ms bias staggers reconnect
        // storms. These calls take the per-broker lock; a loopTask publish on
        // the same slot serializes safely (and loopTask only publishes to Up
        // slots, never one the worker is connect-blocking on).
        uint32_t now = millis();
        for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
            if (reconciling_[s]) continue;
            MqttBroker& b = brokers_[s];
            if (!b.isConfigured()) continue;
            b.loop(now);
            if (b.runtime().state == BrokerState::Down ||
                b.runtime().state == BrokerState::Backoff ||
                b.runtime().state == BrokerState::HeldNoClock) {  // #87: re-check the clock gate each tick so held wss slots release the moment the clock is sane
                uint32_t biased = now + static_cast<uint32_t>(s) * 1000U;
                (void)b.tryConnect(biased);
            }
        }
    }
    worker_task_ = nullptr;
    vTaskDelete(nullptr);
}
#endif  // ARDUINO && ESP_PLATFORM

// ---------------------------------------------------------------------------
// Counters
// ---------------------------------------------------------------------------
uint8_t MqttBrokerPool::configuredCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < OFFBAND_MAX_BROKERS; ++i) {
        if (brokers_[i].isConfigured()) n++;
    }
    return n;
}

uint8_t MqttBrokerPool::enabledCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < OFFBAND_MAX_BROKERS; ++i) {
        if (brokers_[i].isConfigured() && brokers_[i].config().enabled) n++;
    }
    return n;
}

uint8_t MqttBrokerPool::upCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < OFFBAND_MAX_BROKERS; ++i) {
        if (brokers_[i].runtime().state == BrokerState::Up) n++;
    }
    return n;
}

}  // namespace offband
