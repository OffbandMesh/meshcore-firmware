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

namespace crosswire {

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

    // Initialize each slot. begin() returns false if cfg.url is empty,
    // which is OK -- means that slot is intentionally unused.
    for (uint8_t slot = 0; slot < CROSSWIRE_MAX_BROKERS; ++slot) {
        BrokerConfig cfg;
        if (!readBrokerConfig(slot, cfg)) continue;
        if (cfg.url[0] == '\0') continue;  // skip unused slots
        brokers_[slot].begin(slot, cfg, identity);
    }
}
#endif

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------
void MqttBrokerPool::shutdown() {
    for (uint8_t slot = 0; slot < CROSSWIRE_MAX_BROKERS; ++slot) {
        brokers_[slot].shutdown();
    }
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------
void MqttBrokerPool::loop(uint32_t now_ms) {
    for (uint8_t slot = 0; slot < CROSSWIRE_MAX_BROKERS; ++slot) {
        MqttBroker& b = brokers_[slot];
        if (!b.isConfigured()) continue;

        // Drive auth refresh + housekeeping.
        b.loop(now_ms);

        // Initiate a connect if Down/Backoff and eligible. Stagger by
        // slot index to prevent all-brokers-thunder on simultaneous
        // WiFi recovery (slot 0 immediate; slot 5 after 5s slot bias).
        if (b.runtime().state == BrokerState::Down ||
            b.runtime().state == BrokerState::Backoff) {
            // tryConnect itself checks backoff window; the slot * 1000ms
            // bias on the staleness sample shifts the effective deadline.
            uint32_t biased_now = now_ms + static_cast<uint32_t>(slot) * 1000U;
            (void)b.tryConnect(biased_now);
        }
    }

    // Status publish on schedule.
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
    for (uint8_t slot = 0; slot < CROSSWIRE_MAX_BROKERS; ++slot) {
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
    for (uint8_t slot = 0; slot < CROSSWIRE_MAX_BROKERS; ++slot) {
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
    for (uint8_t slot = 0; slot < CROSSWIRE_MAX_BROKERS; ++slot) {
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
// reloadSlot
// ---------------------------------------------------------------------------
#ifdef ARDUINO
bool MqttBrokerPool::reloadSlot(uint8_t slot, const mesh::LocalIdentity& identity) {
    if (slot >= CROSSWIRE_MAX_BROKERS) return false;
    BrokerConfig cfg;
    if (!readBrokerConfig(slot, cfg)) return false;
    // shutdown is implicit in begin's first call. If the new config has
    // an empty URL or is disabled, just shutdown and leave Down.
    if (cfg.url[0] == '\0') {
        brokers_[slot].shutdown();
        return true;
    }
    return brokers_[slot].begin(slot, cfg, identity);
}
#endif

// ---------------------------------------------------------------------------
// Counters
// ---------------------------------------------------------------------------
uint8_t MqttBrokerPool::configuredCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < CROSSWIRE_MAX_BROKERS; ++i) {
        if (brokers_[i].isConfigured()) n++;
    }
    return n;
}

uint8_t MqttBrokerPool::enabledCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < CROSSWIRE_MAX_BROKERS; ++i) {
        if (brokers_[i].isConfigured() && brokers_[i].config().enabled) n++;
    }
    return n;
}

uint8_t MqttBrokerPool::upCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < CROSSWIRE_MAX_BROKERS; ++i) {
        if (brokers_[i].runtime().state == BrokerState::Up) n++;
    }
    return n;
}

}  // namespace crosswire
