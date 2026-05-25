// src/helpers/wifi_observer/WifiObserver.cpp
//
// Top-level lifecycle coordinator for the Crosswire observer subsystem.
//
// MqttUplink wiring decision (Plan 1):
//   MqttUplink requires constructor injection of mesh::RTCClock& and
//   mesh::LocalIdentity&, plus a FILESYSTEM* at begin() time, plus a
//   populated MqttStatusSnapshot on every loop() call (battery_mv,
//   noise_floor, radio_freq/bw/sf/cr, etc.). Those radio telemetry
//   fields require integrating with the companion_radio mesh object,
//   which is outside Plan 1's "compile + WiFi up" scope.
//
//   DEFERRED to Plan 2: Plan 2 wires the configurable broker pool and
//   the full MeshCore dependency injection (RTCClock&, LocalIdentity&,
//   FILESYSTEM*, MqttStatusSnapshot assembly). Until then, Plan 1 brings
//   WiFi STA up and logs readiness; observed packets do NOT yet publish.

#include "WifiObserver.h"
#include "WifiBootstrap.h"
#include "CrashLog.h"

#ifdef ARDUINO
  #include <Arduino.h>
  #include <esp_system.h>   // esp_reset_reason()
#endif

namespace crosswire {

static bool s_mqtt_started = false;

void wifiObserverBegin() {
    // CrashLog FIRST: initializes RTC_NOINIT ring buffer; if the previous
    // boot's buffer survived (= soft reset, BOR, panic, watchdog), dumps
    // that buffer to Serial so we can see the last log lines before the
    // reset. On fresh power-on, the dump is skipped + buffer initializes
    // empty.
    crashLogBegin();

    // Stage A: reset-reason print. Tells us WHY the previous boot ended.
    // POWERON = first boot since plug-in. PANIC = exception triggered.
    // BROWNOUT = power supply dropped below ~2.8V. INT_WDT / TASK_WDT =
    // watchdog timeout. DEEPSLEEP = woke from deep sleep.
#ifdef ARDUINO
    crashLogf("[WifiObserver] boot; reset_reason=%d (%s) version=%s",
              (int)esp_reset_reason(),
              resetReasonString((int)esp_reset_reason()),
              CROSSWIRE_OBSERVER_VERSION);
#else
    crashLogf("[WifiObserver] boot (host build) version=%s",
              CROSSWIRE_OBSERVER_VERSION);
#endif

    crashLogf("[WifiObserver] subsystem starting");
    wifiBootstrap().begin();
    crashLogf("[WifiObserver] wifiBootstrap.begin() returned; state=%d",
              (int)wifiBootstrap().state());

    // TODO(Plan 2): Instantiate MqttUplink with the MeshCore-provided
    // RTCClock + LocalIdentity references. In companion_radio these are
    // available as the global `rtc_clock` (from target.h) and
    // `the_mesh.self_id` (public LocalIdentity on Mesh). begin() also
    // needs a FILESYSTEM* (SPIFFS on ESP32). The loop() call requires a
    // MqttStatusSnapshot populated with live radio telemetry, which
    // requires deeper integration with companion_radio's mesh object.
    // Plan 2 wires all of this alongside the configurable broker pool.
}

void wifiObserverLoop() {
    wifiBootstrap().loop();
    if (wifiBootstrap().isStaConnected() && !s_mqtt_started) {
        crashLogf("[WifiObserver] STA up; ready for MQTT (Plan 2 wires uplink)");
        s_mqtt_started = true;
    }
}

}  // namespace crosswire
