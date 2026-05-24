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

#ifdef ARDUINO
  #include <Arduino.h>
#endif

namespace crosswire {

static bool s_mqtt_started = false;

void wifiObserverBegin() {
#ifdef ARDUINO
    Serial.println("[WifiObserver] subsystem starting; version "
                   CROSSWIRE_OBSERVER_VERSION);
#endif
    wifiBootstrap().begin();
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
#ifdef ARDUINO
        Serial.println("[WifiObserver] STA up; ready for MQTT (Plan 2 wires uplink).");
#endif
        s_mqtt_started = true;
    }
}

}  // namespace crosswire
