// src/helpers/wifi_observer/MqttBrokerPool.cpp
//
// Plan 2 v2 Task 8.

#include "MqttBrokerPool.h"
#include "BrokerRotationSelect.h"   // #708: fair TLS promotion
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
    //
    // #534: TLS/wss slots additionally pass the pool's ALLOCATION admission.
    // The metric is clients already ALLOCATED (tlsClientsAllocated()), not
    // connections live -- at boot nothing is Up/Connecting yet, so a live-based
    // check would admit every slot and allocate a client for each, which is the
    // deadlock this fixes. Surplus TLS slots stay configured-but-client-less and
    // are promoted later by the re-drive's reconcileSlot() path.
    for (uint8_t slot = 0; slot < OFFBAND_MAX_BROKERS; ++slot) {
        BrokerConfig cfg;
        if (!readBrokerConfig(slot, cfg)) continue;
        if (cfg.url[0] == '\0') continue;  // skip unused slots
        brokers_[slot].begin(slot, cfg, identity,
                             tlsClientsAllocated() < OFFBAND_MAX_LIVE_TLS);
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
    // #175: drain each Up broker's ring backlog here, on loopTask. This keeps ALL
    // ring_ access single-threaded -- append (publishPacket), drain (here +
    // publishPacket), and lag/lapped (broker dump) all run on loopTask and never
    // preempt each other, so the ring needs no lock. Draining here (every main-loop
    // pass) also flushes a rotated-back-in broker even when no new packet arrives
    // to trigger publishPacket. The worker task does NOT touch the ring.
    for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
        if (brokers_[s].runtime().state == BrokerState::Up) (void)drainBroker(s);
    }
#if defined(ARDUINO)
    // #710: publish-ring overrun report (one line / 60s -- rule-10 safe).
    //
    // Emitted HERE on loopTask and deliberately NOT beside the [rot] line in
    // workerLoop(): "The worker task does NOT touch the ring" (above) is the
    // invariant that lets the ring run lock-free, and reading dropped_ from the
    // worker would break exactly what the #175 concurrency review established.
    //
    // Covers EVERY configured slot, not just TLS ones -- [rot] filters to
    // isTlsTransport() because it is the rotation diagnostic, which makes it
    // structurally blind to the plaintext always-on primary (#707). That broker is
    // the one that must never drop, so it is the one this line most needs to show.
    //
    // Unconditional: prints zeros when healthy. An edge-triggered line would make
    // silence ambiguous ("no drops" vs "reporting broken"), which is the same class
    // of silent failure this counter exists to remove.
    {
        static uint32_t s_ring_log_ms = 0;
        if (now_ms - s_ring_log_ms >= 60000U) {
            s_ring_log_ms = now_ms;
            char L[160];
            int o = snprintf(L, sizeof(L), "[ring] head=%u drops", (unsigned)ring_.head());
            for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS && o < (int)sizeof(L) - 16; ++s) {
                if (!brokers_[s].isConfigured()) continue;
                o += snprintf(L + o, (size_t)(sizeof(L) - o), " s%u=%u",
                              (unsigned)s, (unsigned)ring_.droppedCount(s));
            }
            Serial.println(L);
        }
    }
#endif
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
    // #534: allocation admission. This slot's own client (if any) is destroyed by
    // begin()'s shutdown() before reallocation, so exclude it from the count --
    // otherwise a plain reload of an already-allocated slot would count itself
    // and refuse to re-allocate.
    const bool may_alloc =
        (tlsClientsAllocated() - (brokers_[slot].hasClient() ? 1u : 0u))
            < OFFBAND_MAX_LIVE_TLS;
    if (!brokers_[slot].begin(slot, cfg, *identity_, may_alloc) && cfg.enabled) {
        crashLogf("[pool] reconcileSlot %u begin FAILED state=%d err_class=%d",
                  (unsigned)slot,
                  (int)brokers_[slot].runtime().state,
                  (int)brokers_[slot].runtime().last_error_class);
    }
}

// #534: count TLS/wss brokers holding an allocated esp_mqtt client. See the
// header for why allocation admission counts CLIENTS, not live connections.
uint8_t MqttBrokerPool::tlsClientsAllocated() const {
    uint8_t n = 0;
    for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
        const MqttBroker& b = brokers_[s];
        if (!b.isConfigured()) continue;
        const BrokerConfig& c = b.config();
        if ((c.transport == BrokerTransport::Tls ||
             c.transport == BrokerTransport::Wss) && b.hasClient()) {
            n++;
        }
    }
    return n;
}

namespace {
// #171: a broker holds a ~60KB mbedTLS context only for TLS/wss transports.
inline bool isTlsTransport(const BrokerConfig& c) {
    return c.transport == BrokerTransport::Tls ||
           c.transport == BrokerTransport::Wss;
}
}  // namespace

#ifndef MQTT_ROTATE_DWELL_MS
  #define MQTT_ROTATE_DWELL_MS 60000U   // #175: how long a TLS slot holds the budget
#endif

// #175: rotate the live TLS set on a dwell timer. Demotes the OLDEST live TLS
// broker so a parked (HeldNoHeap) one can take the freed budget slot. WORKER-ONLY:
// called from workerLoop, the sole thread permitted to block on esp_mqtt teardown
// (#53). We shut the victim down DIRECTLY (a full shutdown() == client_destroy;
// skipping the destroy leaks ~68KB, #327) rather than via reloadSlot(): reloadSlot
// re-runs begin() and would bring the victim straight back UP, defeating the
// rotation. A per-slot cooldown then blocks the victim from reclaiming the budget
// before the parked broker can, otherwise slot order alone decides the winner and
// the victim usually wins instantly (reconnect storm, no rotation).
//
// The demoted broker keeps its RING CURSOR, so it resumes from where it left off
// on its next window rather than losing the traffic it missed.
void MqttBrokerPool::rotateTlsIfDue(uint32_t now_ms) {
    if (now_ms - last_rotate_ms_ < MQTT_ROTATE_DWELL_MS) return;

    // Count enabled TLS brokers + find whether one is parked waiting for budget.
    uint8_t tls_enabled = 0;
    bool    waiting     = false;
    for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
        const MqttBroker& b = brokers_[s];
        if (!b.isConfigured() || !isTlsTransport(b.config())) continue;
        tls_enabled++;
        // #715: HeldBudget and HeldNoHeap are both 'deferred, wants the budget'.
        if (b.runtime().state == BrokerState::HeldNoHeap ||
            b.runtime().state == BrokerState::HeldBudget) waiting = true;
    }
    // Nothing to share (budget covers all enabled TLS -- PSRAM/large-heap case),
    // or nobody is actually waiting for the budget: no rotation.
    if (tls_enabled <= OFFBAND_MAX_LIVE_TLS || !waiting) return;

    // Pick the oldest live TLS slot (largest elapsed since went_up; wrap-safe).
    uint8_t  victim   = 0xFF;
    uint32_t best_age = 0;
    for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
        if (reconciling_[s]) continue;          // #53: already mid-lifecycle
        const MqttBroker& b = brokers_[s];
        if (!b.isConfigured() || !isTlsTransport(b.config())) continue;
        if (b.runtime().state != BrokerState::Up) continue;
        uint32_t age = now_ms - b.runtime().went_up_ms;
        if (victim == 0xFF || age > best_age) { best_age = age; victim = s; }
    }
    if (victim == 0xFF) return;

    // Do not thrash: only rotate a slot that has actually held the budget for a
    // full dwell, so a freshly-promoted broker is not evicted almost immediately.
    if (best_age < MQTT_ROTATE_DWELL_MS) return;

    last_rotate_ms_ = now_ms;
#if defined(ARDUINO)
    Serial.printf("[rot] ROTATE-OUT s%u age=%us heapBefore=%u -> cooldown %us\n",
                  (unsigned)victim, (unsigned)(best_age / 1000U),
                  (unsigned)ESP.getFreeHeap(), (unsigned)(MQTT_ROTATE_DWELL_MS / 1000U));
#endif
    // Guard the slot during teardown so loopTask's drainBroker skips it while the
    // (possibly multi-second) client_destroy runs. The per-broker client_lock_ is
    // the true publish-vs-shutdown backstop (publish() re-checks state + client_
    // under that lock); this just avoids loopTask spinning on a slot mid-destroy.
    reconciling_[victim] = true;
    // #175: releaseClient(), NOT shutdown(). Both destroy the client (the ~60KB
    // mbedTLS reclaim, #327), but shutdown() ALSO deconfigures the slot (#98:
    // slot_ = 0xFF, cfg_ cleared) -- and every pool loop, including the re-drive
    // that would promote this broker back, is gated on isConfigured(). Using
    // shutdown() here made rotation a ONE-WAY EVICTION: the victim vanished from
    // the scheduler permanently (verified on hardware: en 5->4, victim absent
    // from [rot] for the rest of the run). releaseClient() keeps slot_/cfg_ so
    // the cooldown -> budget -> reconcileSlot -> tryConnect path can bring it back.
    brokers_[victim].releaseClient();
    reconciling_[victim] = false;
#if defined(ARDUINO)
    Serial.printf("[rot] post-shutdown s%u heapAfter=%u\n",
                  (unsigned)victim, (unsigned)ESP.getFreeHeap());
#endif
    // Block the victim from reclaiming the freed budget for one dwell, giving the
    // parked broker first claim on the next re-drive pass.
    rotated_out_until_ms_[victim] = now_ms + MQTT_ROTATE_DWELL_MS;
    if (rotated_out_until_ms_[victim] == 0) rotated_out_until_ms_[victim] = 1;  // 0 means "not cooling"
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
        // #175: rotate the live TLS set BEFORE counting the budget, so a victim
        // shut down here frees its context for the parked broker this same pass.
        rotateTlsIfDue(now);
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
#if defined(ARDUINO)
        // #175 diag: low-rate scheduler-state line (one line / 10s -- rule-10 safe)
        // so rotation decisions are visible on serial: why a held TLS broker is or
        // is not being promoted. Fields: heap, enabled-TLS, live/budget, dwell-left,
        // then per-TLS-slot state[/age-if-up][/cooldown-remaining].
        {
            static uint32_t s_rot_log_ms = 0;
            if (now - s_rot_log_ms >= 10000U) {
                s_rot_log_ms = now;
                // #554: tls_cfg = configured TLS/WSS brokers the rotation
                // scheduler tracks (rotation-eligible). This is NOT the enabled
                // count -- it ignores cfg.enabled entirely. Do NOT confuse the
                // "tls=" field with `mqtt status`'s "enabled=" (enabledCount()):
                // a slot can be configured-TLS here yet disabled there. The field
                // was previously mislabeled "en=", which read as "enabled".
                char L[220]; int o = 0; uint8_t tls_cfg = 0;
                for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
                    const MqttBroker& b = brokers_[s];
                    if (b.isConfigured() && isTlsTransport(b.config())) tls_cfg++;
                }
                uint32_t dwleft = (now - last_rotate_ms_ < MQTT_ROTATE_DWELL_MS)
                                  ? (MQTT_ROTATE_DWELL_MS - (now - last_rotate_ms_)) / 1000U : 0U;
                o += snprintf(L + o, (size_t)(sizeof(L) - o),
                              "[rot] heap=%u tls=%u live=%u/%u dwleft=%us |",
                              (unsigned)ESP.getFreeHeap(), (unsigned)tls_cfg, (unsigned)tls_live,
                              (unsigned)OFFBAND_MAX_LIVE_TLS, (unsigned)dwleft);
                static const char* AB[] = {"DN","CO","UP","BK","HC","HH","HB"};  // #715: HB=held(budget)
                for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS && o < (int)sizeof(L) - 24; ++s) {
                    const MqttBroker& b = brokers_[s];
                    if (!b.isConfigured() || !isTlsTransport(b.config())) continue;
                    uint8_t stv = (uint8_t)b.runtime().state;
                    o += snprintf(L + o, (size_t)(sizeof(L) - o), " s%u:%s", (unsigned)s,
                                  stv < 7 ? AB[stv] : "?");
                    if (b.runtime().state == BrokerState::Up)
                        o += snprintf(L + o, (size_t)(sizeof(L) - o), "/%us",
                                      (unsigned)((now - b.runtime().went_up_ms) / 1000U));
                    uint32_t cd = (rotated_out_until_ms_[s] != 0 &&
                                   (int32_t)(rotated_out_until_ms_[s] - now) > 0)
                                  ? (rotated_out_until_ms_[s] - now) / 1000U : 0U;
                    if (cd) o += snprintf(L + o, (size_t)(sizeof(L) - o), "/cd%us", (unsigned)cd);
                }
                Serial.println(L);
            }
        }
#endif
        // #708: build the TLS candidate set while driving per-broker lifecycle.
        // Promotion order used to be "first eligible by slot index", whose only
        // fairness mechanism was #175's per-victim cooldown -- which uniquely
        // determines a winner at exactly TWO candidates and does nothing at three
        // or more. Measured on hv3-bench with three enabled TLS brokers: 36/35/0
        // promotions over 481 samples; slot 3 never ran. Selection now lives in
        // selectTlsPromotion() (BrokerRotationSelect.h) so it is unit-testable --
        // the defect was invisible at N=2, which is the arity it was validated at.
        TlsCandidate cand[OFFBAND_MAX_BROKERS];
        for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
            if (reconciling_[s]) continue;
            MqttBroker& b = brokers_[s];
            if (!b.isConfigured()) continue;
            b.loop(now);
            BrokerState st = b.runtime().state;
            // Re-drive idle/held slots. HeldNoClock releases when the clock is
            // sane (#87); HeldNoHeap releases when a TLS slot frees (#171).
            if (st == BrokerState::Down || st == BrokerState::Backoff ||
                st == BrokerState::HeldNoClock || st == BrokerState::HeldNoHeap ||
                st == BrokerState::HeldBudget) {   // #715
                // #175: skip a slot still in its rotation cooldown, so the parked
                // broker claims the freed budget instead of the just-evicted victim
                // reclaiming it. Wrap-safe; 0 means "not cooling".
                if (rotated_out_until_ms_[s] != 0 &&
                    (int32_t)(rotated_out_until_ms_[s] - now) > 0) {
                    continue;
                }
                // #708: TLS slots compete for the budget and are decided below by
                // waiting time. Non-TLS hold no mbedTLS context, never contend, and
                // are driven immediately exactly as before.
                if (isTlsTransport(b.config())) {
                    cand[s].eligible          = true;
                    cand[s].cooling           = false;   // cooldown handled above
                    cand[s].last_served_epoch = last_served_epoch_[s];
                    continue;
                }
                uint32_t biased = now + static_cast<uint32_t>(s) * 1000U;
                const bool budget_ok = true;   // non-TLS: never budget-limited
                // #175 fix: rotation demotes a TLS broker via shutdown(), which
                // DESTROYS its esp_mqtt client (client_ = nullptr) to free the
                // ~60KB context without leaking (#327). tryConnect can only START
                // an existing client -- it cannot recreate one -- so a rotated-out
                // broker would sit Down forever and the freed budget would never
                // refill (verified on HV3: rotation empties the budget and strands
                // it). When we're actually about to bring this TLS broker up
                // (budget available), recreate its client first via reconcileSlot
                // (-> begin) so tryConnect has a client to start. Only fires for a
                // genuinely client-less broker (the rotation victim): a failed
                // connect goes to Backoff and KEEPS its client, so this is not a
                // reconnect storm. We are on the worker task -- the only caller
                // allowed to run reconcileSlot's blocking begin().
                (void)b.tryConnect(biased, budget_ok);
            }
        }

        // #708: award the free TLS budget to whoever has waited longest, one slot
        // at a time until the budget is full. Loops rather than promoting once so
        // it stays correct if OFFBAND_MAX_LIVE_TLS is ever raised above 1.
        for (;;) {
            uint8_t pick = selectTlsPromotion(cand, OFFBAND_MAX_BROKERS,
                                              tls_live, OFFBAND_MAX_LIVE_TLS);
            if (pick == kNoSlot) break;
            cand[pick].eligible = false;          // decided this pass either way
            MqttBroker& b = brokers_[pick];
            // #175 fix: rotation demotes a TLS broker via releaseClient(), which
            // DESTROYS its esp_mqtt client to free the ~60KB context without
            // leaking (#327). tryConnect can only START an existing client -- it
            // cannot recreate one -- so a rotated-out broker would sit Down forever
            // and the freed budget would never refill (verified on HV3). Recreate
            // the client first via reconcileSlot (-> begin). Only fires for a
            // genuinely client-less broker: a failed connect goes to Backoff and
            // KEEPS its client, so this is not a reconnect storm. We are on the
            // worker task -- the only caller allowed to run the blocking begin().
            if (!b.hasClient()) reconcileSlot(pick);
            uint32_t biased = now + static_cast<uint32_t>(pick) * 1000U;
            (void)b.tryConnect(biased, /*budget_ok=*/true);
            if (b.runtime().state == BrokerState::Connecting) {
                tls_live++;
                // Stamp service only on an actual bring-up, so a broker that fails
                // to start does not lose its place in the queue.
                last_served_epoch_[pick] = ++service_epoch_;
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
