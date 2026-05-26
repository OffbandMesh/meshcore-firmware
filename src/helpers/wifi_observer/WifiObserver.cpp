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
    if (!s_pool_started && s_context_set && wifiBootstrap().isStaConnected()
        && s_identity != nullptr) {
        crashLogf("[WifiObserver] STA up + context set -- bringing up MqttBrokerPool");
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
// Pool singleton accessor
// ---------------------------------------------------------------------------
MqttBrokerPool& wifiObserverPool() {
    return s_pool;
}

}  // namespace crosswire
