// src/helpers/wifi_observer/MqttBrokerPool.cpp
//
// Plan 2 v2 Task 8.

#include "MqttBrokerPool.h"
#include "MqttPayload.h"
#include <helpers/diagnostics/CrashLog.h>   // #181: crashLogf() -- worker has no user ACK channel
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
    // Append the Offband tell (radar emoji U+1F4E1 = UTF-8 F0 9F 93 A1) to the
    // observer's display name in every MQTT payload's `origin`, so feeds/maps show
    // "<name> [radar]" for Offband observers. Falls back to device_id when there is
    // no node_name, so it is never a bare emoji. node_name_ is the only OWNED cached
    // string (the rest borrow the caller's lifetime). The TOPIC uses device_id, not
    // this, so the emoji never reaches a topic.
    {
        const char* nm_base = (node_name != nullptr && node_name[0] != '\0') ? node_name
                            : (device_id != nullptr ? device_id : "");
        // Bound the base name with %.*s so the trailing " " + 4-byte emoji + NUL
        // (6 bytes) always fit whole -- the emoji is never split mid-UTF-8 even on a
        // pathologically long name (>58 chars). escapeJsonString passes the bytes >=0x20.
        snprintf(node_name_, sizeof(node_name_), "%.*s \xF0\x9F\x93\xA1",
                 (int)(sizeof(node_name_) - 6), nm_base);
    }
    client_version_   = client_version   != nullptr ? client_version   : "";
    firmware_version_ = firmware_version != nullptr ? firmware_version : "";
    model_            = model            != nullptr ? model            : "";

    // Seed defaults (idempotent -- no-op if slots already populated).
    populateDefaultBrokers();
    // #182: one-time reclaim of an upgraded observer's NVS to the per-key/no-blank
    // format, BEFORE any slot is read or a worker runs -- so the space is freed at
    // boot and the user never sees the interactive first-write error. Gated to run
    // once; idempotent on already-migrated/fresh devices.
    migrateBrokerStorage();

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
    // on MQTT for any reason (#53). Created once; lives for
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
    // #175: append ONCE. Brokers pull from their own cursor in drainBroker(),
    // so a broker that is down (rotated out, backoff, HeldNoHeap) resumes where
    // it left off instead of losing the traffic.
    if (ring_.append(payload, payload_len) == 0) return 0;  // oversize/rejected
    uint8_t drained = 0;
    for (uint8_t slot = 0; slot < OFFBAND_MAX_BROKERS; ++slot) {
        drained += drainBroker(slot) ? 1 : 0;
    }
    return drained;
}

// #175: publish this broker's unread backlog. Bounded per call so one lagging
// broker cannot monopolise a loop pass. Only commits the cursor on a SUCCESSFUL
// publish, so a refused enqueue is retried next pass rather than dropped.
uint8_t MqttBrokerPool::drainBroker(uint8_t slot) {
    if (slot >= OFFBAND_MAX_BROKERS) return 0;
    if (reconciling_[slot]) return 0;              // #53: mid-reconcile
    MqttBroker& b = brokers_[slot];
    if (!b.isConfigured() || b.runtime().state != BrokerState::Up) return 0;

    MqttPayloadCtx ctx;
    b.fillPayloadCtx(ctx, global_iata_, device_id_, node_name_,
                     client_version_, firmware_version_, model_);
    // Per HARD RULE: silent skip on missing IATA (no garbage topics).
    if (ctx.iata == nullptr || ctx.iata[0] == '\0') return 0;
    char topic[160];
    formatTopic(topic, sizeof(topic), "packets", ctx);
    if (topic[0] == '\0') return 0;

    uint8_t sent = 0;
    uint8_t buf[MQTT_RING_MSG_MAX];
    size_t  len = 0;
    uint32_t seq = 0;
    // Bound the burst: at most MQTT_RING_SLOTS messages per pass.
    for (uint8_t i = 0; i < MQTT_RING_SLOTS; ++i) {
        if (!ring_.peek(slot, buf, sizeof(buf), len, seq)) break;
        if (!b.publish(topic, buf, len, /*retain=*/false)) break;  // retry next pass
        ring_.commit(slot);
        sent++;
    }
    return sent;
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
    // #181: the worker runs off any user channel, so a reconcile that fails to
    // bring up a configured + ENABLED slot must surface in the persistent crash
    // log (SAFELANE 6) -- otherwise the slot silently stays Down with only
    // rt_.state as evidence. begin() returns false on bad auth / client-init OOM;
    // a DISABLED slot returns true with no client (expected, not a failure).
    // URL is omitted from the log -- a broker URL may carry inline credentials.
    if (!brokers_[slot].begin(slot, cfg, *identity_) && cfg.enabled) {
        crashLogf("[pool] reconcileSlot %u begin FAILED state=%d err_class=%d",
                  (unsigned)slot,
                  (int)brokers_[slot].runtime().state,
                  (int)brokers_[slot].runtime().last_error_class);
    }
}

namespace {
// #171: a broker holds a ~60KB mbedTLS context only for TLS/wss transports.
inline bool isTlsTransport(const BrokerConfig& c) {
    return c.transport == BrokerTransport::Tls ||
           c.transport == BrokerTransport::Wss;
}
}  // namespace

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
        // #171: count TLS contexts already live (Up/Connecting). Each holds a
        // ~60KB mbedTLS context; HV3's heap fits ~2 before a 3rd handshake OOM-
        // reboots. We refuse to START a TLS bring-up past OFFBAND_MAX_LIVE_TLS
        // (the broker self-defers to HeldNoHeap). Plaintext brokers are exempt.
        uint8_t tls_live = 0;
        for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
            const MqttBroker& b = brokers_[s];
            if (!b.isConfigured() || !isTlsTransport(b.config())) continue;
            BrokerState st = b.runtime().state;
            if (st == BrokerState::Up || st == BrokerState::Connecting) tls_live++;
        }
        for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
            if (reconciling_[s]) continue;
            MqttBroker& b = brokers_[s];
            if (!b.isConfigured()) continue;
            b.loop(now);
            // #175: a broker that just came Up (or was rotated back in) drains
            // its backlog here, not only when a new packet arrives.
            if (b.runtime().state == BrokerState::Up) (void)drainBroker(s);
            BrokerState st = b.runtime().state;
            // Re-drive idle/held slots. HeldNoClock releases when the clock is
            // sane (#87); HeldNoHeap releases when a TLS slot frees (#171).
            if (st == BrokerState::Down || st == BrokerState::Backoff ||
                st == BrokerState::HeldNoClock || st == BrokerState::HeldNoHeap) {
                uint32_t biased = now + static_cast<uint32_t>(s) * 1000U;
                // TLS budget: non-TLS always OK; TLS only if a live slot is free.
                bool budget_ok = !isTlsTransport(b.config()) ||
                                 (tls_live < OFFBAND_MAX_LIVE_TLS);
                (void)b.tryConnect(biased, budget_ok);
                // Consume a budget slot if this TLS broker just began bringing up.
                if (isTlsTransport(b.config()) &&
                    b.runtime().state == BrokerState::Connecting) {
                    tls_live++;
                }
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

// #173: UPPERCASE hex of the device's own pubkey -- the connect-time default for a
// broker's jwt_owner (#95). Renders "" when the identity is unset or on host builds
// (no identity_), so the caller simply omits jwt_owner_resolved in that case.
void MqttBrokerPool::deviceOwnerHex(char* out, size_t out_len) const {
    if (out == nullptr || out_len == 0) return;
    out[0] = '\0';
#if defined(ARDUINO)
    if (identity_ == nullptr || out_len < 2 * PUB_KEY_SIZE + 1) return;
    for (size_t i = 0; i < PUB_KEY_SIZE; ++i) {
        snprintf(&out[i * 2], 3, "%02X", identity_->pub_key[i]);
    }
    out[2 * PUB_KEY_SIZE] = '\0';
#endif
}

}  // namespace offband
