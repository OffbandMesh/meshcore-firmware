// src/helpers/wifi_observer/WifiObserver.cpp
//
// Plan 2 v2 Task 11: WifiObserver now drives MqttBrokerPool + ObserverPipeline
// (no more vendored MqttUplink placeholder). When WifiBootstrap reports
// STA up AND main.cpp has called wifiObserverSetMeshContext, the pool
// + pipeline are brought up exactly once.

#include "WifiObserver.h"
#include "WifiBootstrap.h"
#include <helpers/diagnostics/CrashLog.h>
#include "ObserverPipeline.h"

#ifdef ARDUINO
  #include <Arduino.h>
  #include <esp_system.h>   // esp_reset_reason()
  #include <time.h>         // #69: configTime() + time() for the SNTP wall clock
  #include <esp_sntp.h>     // #87: esp_sntp_stop() once GPS owns the clock
#endif

namespace offband {

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
// #87: one-shot guard -- stop SNTP once GPS has locked (GPS > NTP) so a later
// NTP poll cannot overwrite GPS-accurate time.
static bool        s_sntp_stopped_for_gps = false;
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
              OFFBAND_VERSION);
#else
    crashLogf("[WifiObserver] boot (host build) version=%s",
              OFFBAND_VERSION);
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

    // #69/#87 Task B: time-source arbiter. GPS > NTP > BLE for *accuracy*, but
    // we never block MQTT on GPS acquisition (#87). SNTP starts immediately on
    // STA-connect; the pool comes up as soon as SNTP is kicked off. GPS runs in
    // parallel (it writes the RTC directly via MicroNMEA on each fix) and, once
    // it locks, takes over the clock -- we then stop SNTP so a later NTP poll
    // cannot overwrite GPS-accurate time. wss/TLS brokers self-defer
    // (BrokerState::HeldNoClock) until wallClockSane(); tcp brokers publish now.

    // Phase 1 -- start SNTP immediately (no GPS pre-grace).
    if (!s_sntp_started && s_context_set && wifiBootstrap().isStaConnected()) {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
        s_sntp_started    = true;
        s_sntp_started_ms = now;
        crashLogf("[WifiObserver] SNTP started immediately (gps_en=%d gps_lock=%d pre-sync wall=%lu)",
                  (int)s_gps_time_enabled, (int)s_gps_time_locked,
                  (unsigned long)time(nullptr));
    }

    // #87: GPS > NTP after the fact -- once GPS owns the clock, stop SNTP so a
    // later poll cannot overwrite GPS-accurate time. One-shot.
    if (s_gps_time_locked && s_sntp_started && !s_sntp_stopped_for_gps) {
        esp_sntp_stop();
        s_sntp_stopped_for_gps = true;
        crashLogf("[WifiObserver] GPS locked -- SNTP stopped (GPS>NTP)");
    }

    // Phase 2 -- bring the pool up as soon as SNTP is running (or GPS locked).
    // No clock gate here (#87): tcp brokers publish immediately; wss/TLS brokers
    // self-defer to HeldNoClock until wallClockSane() and release the instant the
    // clock arrives. A clockless start no longer blocks the tcp feed.
    if (!s_pool_started && s_identity != nullptr &&
        wifiBootstrap().isStaConnected() &&
        (s_gps_time_locked || s_sntp_started)) {
        crashLogf("[WifiObserver] bringing up MqttBrokerPool (tcp now; wss held until clock; wall=%lu)",
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

}  // namespace offband
