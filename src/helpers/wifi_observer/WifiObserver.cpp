// src/helpers/wifi_observer/WifiObserver.cpp
//
// Plan 2 v2 Task 11: WifiObserver now drives MqttBrokerPool + ObserverPipeline
// (no more vendored MqttUplink placeholder). When WifiBootstrap reports
// STA up AND main.cpp has called wifiObserverSetMeshContext, the pool
// + pipeline are brought up exactly once.

#include "WifiObserver.h"
#include "WifiBootstrap.h"
#include "CrashLog.h"
#include "ObserverPipeline.h"

#ifdef ARDUINO
  #include <Arduino.h>
  #include <esp_system.h>   // esp_reset_reason()
  #include <time.h>         // #69: configTime() + time() for the SNTP wall clock
#endif

namespace crosswire {

// Singleton pool. ObserverCli + main.cpp's status snapshot updater
// access via wifiObserverPool().
static MqttBrokerPool s_pool;

// Mesh context cache (set by wifiObserverSetMeshContext).
#ifdef ARDUINO
static const mesh::LocalIdentity* s_identity = nullptr;
#endif
static const char* s_device_id        = "";
static const char* s_node_name        = "";
static const char* s_client_version   = "";
static const char* s_firmware_version = "";
static const char* s_model            = "";
static bool        s_context_set      = false;
static bool        s_pool_started     = false;
// #69 Task A: GPS time-state pushed by main.cpp each loop tick (~1 Hz).
// Read by the SNTP arbiter (next task). No ARDUINO guard needed: the
// setter is only called in the ARDUINO build (guarded at call site).
static bool        s_gps_time_enabled = false;
static bool        s_gps_time_locked  = false;
#ifdef ARDUINO
// #69: SNTP bring-up tracking. Wall clock is "sane" (SNTP or BLE set real time)
// once past 2025-01-01 UTC; below that TLS certs read "not yet valid" and JWT
// iat/exp are garbage, so the pool waits for it before enabling brokers.
static bool        s_sntp_started     = false;
static uint32_t    s_sntp_started_ms  = 0;
// #69 Task B: STA-up timestamp used by the GPS grace-window calculation.
// Set once on the first loop iteration where STA becomes connected.
static uint32_t    s_sta_up_ms        = 0;
static bool wallClockSane() { return time(nullptr) > 1735689600; }
#endif

// ---------------------------------------------------------------------------
// wifiObserverBegin -- early setup. Same as Plan 1; pool comes up later.
// ---------------------------------------------------------------------------
void wifiObserverBegin() {
    // CrashLog FIRST: initializes RTC_NOINIT ring buffer; if the previous
    // boot's buffer survived (= soft reset, BOR, panic, watchdog), dumps
    // that buffer to Serial so we can see the last log lines before the
    // reset. On fresh power-on, the dump is skipped + buffer initializes
    // empty.
    crashLogBegin();

    // Stage A: reset-reason print.
#ifdef ARDUINO
    crashLogf("[WifiObserver] boot; reset_reason=%d (%s) version=%s",
              (int)esp_reset_reason(),
              resetReasonString((int)esp_reset_reason()),
              CROSSWIRE_VERSION);
#else
    crashLogf("[WifiObserver] boot (host build) version=%s",
              CROSSWIRE_VERSION);
#endif

    crashLogf("[WifiObserver] subsystem starting");
    wifiBootstrap().begin();
    crashLogf("[WifiObserver] wifiBootstrap.begin() returned; state=%d",
              (int)wifiBootstrap().state());

    // Plan 2 v2: pool + pipeline init deferred to first loop() tick where
    // STA is up AND wifiObserverSetMeshContext has been called by main.cpp
    // (after the_mesh.begin() so identity is populated).
}

// ---------------------------------------------------------------------------
// wifiObserverSetMeshContext -- main.cpp wires identity + cached strings
// ---------------------------------------------------------------------------
#ifdef ARDUINO
void wifiObserverSetMeshContext(
    const mesh::LocalIdentity& identity,
    const char* device_id,
    const char* node_name,
    const char* client_version,
    const char* firmware_version,
    const char* model) {
    s_identity         = &identity;
    s_device_id        = (device_id        != nullptr) ? device_id        : "";
    s_node_name        = (node_name        != nullptr) ? node_name        : "";
    s_client_version   = (client_version   != nullptr) ? client_version   : "";
    s_firmware_version = (firmware_version != nullptr) ? firmware_version : "";
    s_model            = (model            != nullptr) ? model            : "";
    s_context_set      = true;
    crashLogf("[WifiObserver] mesh context set; node=%s", s_node_name);
}
#endif

// ---------------------------------------------------------------------------
// wifiObserverLoop -- per-iteration driver
// ---------------------------------------------------------------------------
void wifiObserverLoop() {
    wifiBootstrap().loop();

#ifdef ARDUINO
    uint32_t now = millis();

    // Bring pool + pipeline up on the STA-connected transition (with
    // mesh context). One-shot guarded by s_pool_started.

    // #69 Task B: Phase 1 -- time-source arbiter. Record STA-up timestamp once
    // (for the GPS grace window), then start SNTP only when GPS will NOT serve
    // the clock.
    //
    // Decision tree (D1/D3 -- GPS > NTP > BLE):
    //   - GPS locked:         GPS owns the clock; do NOT start SNTP.
    //   - GPS enabled+unlocked, within cold-fix budget: wait for GPS first.
    //   - GPS enabled+unlocked, grace expired: fall back to NTP.
    //   - GPS disabled:       start NTP immediately (existing behavior).
    //
    // GPS sets the RTC directly via MicroNMEA on each fix, so wallClockSane()
    // is satisfied by either source -- Phase 2 remains source-agnostic.
    constexpr uint32_t kGpsGraceMs = 60000;  // GPS cold-fix budget before NTP fallback
    if (wifiBootstrap().isStaConnected() && s_sta_up_ms == 0) {
        s_sta_up_ms = now;  // record once; used for grace-window math below
    }
    {
        bool gps_will_serve = s_gps_time_enabled &&
                              (s_gps_time_locked ||
                               (s_sta_up_ms != 0 && (now - s_sta_up_ms) < kGpsGraceMs));
        if (!s_sntp_started && s_context_set &&
            wifiBootstrap().isStaConnected() && !gps_will_serve) {
            configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
            s_sntp_started    = true;
            s_sntp_started_ms = now;
            crashLogf("[WifiObserver] SNTP started (gps_en=%d gps_lock=%d pre-sync wall=%lu)",
                      (int)s_gps_time_enabled, (int)s_gps_time_locked,
                      (unsigned long)time(nullptr));
        }
    }

    // #69: Phase 2 -- bring up the MQTT pool once we have a real wall clock, so
    // wss/jwt brokers connect on their first attempt instead of failing TLS into
    // backoff. Source-agnostic: wallClockSane() is true whether GPS or NTP set
    // the clock. Bounded wait (NTP path only): if NTP is blocked and GPS has
    // not locked, the tcp brokers still come up rather than MQTT never starting.
    constexpr uint32_t kClockWaitMs = 30000;
    bool clock_wait_expired = s_sntp_started &&
                              (now - s_sntp_started_ms >= kClockWaitMs);
    if (!s_pool_started && s_identity != nullptr &&
        (s_gps_time_locked || s_sntp_started) &&
        (wallClockSane() || clock_wait_expired)) {
        if (!wallClockSane()) {
            crashLogf("[WifiObserver] clock not synced after %us -- pool up anyway "
                      "(wss brokers will wait)", (unsigned)(kClockWaitMs / 1000));
        }
        crashLogf("[WifiObserver] clock ready -- bringing up MqttBrokerPool (wall=%lu)",
                  (unsigned long)time(nullptr));
        s_pool.begin(*s_identity, s_device_id, s_node_name,
                     s_client_version, s_firmware_version, s_model);
        observerPipeline().begin(&s_pool);
        s_pool_started = true;
        crashLogf("[WifiObserver] pool configured=%u enabled=%u",
                  s_pool.configuredCount(), s_pool.enabledCount());
    }

    // Drive pool every iteration once it's started.
    if (s_pool_started) {
        s_pool.loop(now);
    }

    // #69: one-shot log the moment the wall clock becomes sane (GPS fix or SNTP
    // sync), so serial captures show exactly when TLS brokers became eligible.
    static bool s_clock_synced_logged = false;
    if (!s_clock_synced_logged && wallClockSane()) {
        s_clock_synced_logged = true;
        crashLogf("[WifiObserver] wall clock sane (gps_lock=%d sntp=%d): %lu",
                  (int)s_gps_time_locked, (int)s_sntp_started,
                  (unsigned long)time(nullptr));
    }

    // Periodic heap/stack snapshot every 5 seconds.
    static uint32_t s_last_stats_ms = 0;
    if (now - s_last_stats_ms > 5000) {
        s_last_stats_ms = now;
        crashLogHeapStats("loop");
    }
#endif
}

// ---------------------------------------------------------------------------
// wifiObserverSetStatusSnapshot -- pool consumes for next publish window
// ---------------------------------------------------------------------------
void wifiObserverSetStatusSnapshot(const MqttStatusSnapshot& snap) {
    s_pool.setStatusSnapshot(snap);
}

// ---------------------------------------------------------------------------
// wifiObserverSetGpsTimeState -- #69 Task A: GPS time-state push from main.cpp
// ---------------------------------------------------------------------------
void wifiObserverSetGpsTimeState(bool enabled, bool locked) {
    s_gps_time_enabled = enabled;
    s_gps_time_locked  = locked;
}

// ---------------------------------------------------------------------------
// Pool singleton accessor
// ---------------------------------------------------------------------------
MqttBrokerPool& wifiObserverPool() {
    return s_pool;
}

// ---------------------------------------------------------------------------
// Borrowed pubkey accessor (Plan 3 Task 10, Strycher/LoRa#272).
// Returns nullptr until wifiObserverSetMeshContext has been called.
// ---------------------------------------------------------------------------
const uint8_t* wifiObserverPubKey() {
#ifdef ARDUINO
    return (s_identity != nullptr) ? s_identity->pub_key : nullptr;
#else
    return nullptr;
#endif
}

}  // namespace crosswire
