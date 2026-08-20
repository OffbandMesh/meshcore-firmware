#include "WifiMqttTransport.h"

#if defined(ENABLE_WIFI_TELEMETRY) && defined(ESP32)

// Local debug macro: independent of MESH_DEBUG so telemetry diagnostics
// remain available even when the repeater env compiles with MESH_DEBUG off
// for power. Override at build time with -D WIFI_TELEMETRY_DEBUG=0 once
// the module is stable.
#ifndef WIFI_TELEMETRY_DEBUG
#define WIFI_TELEMETRY_DEBUG 1
#endif

#if WIFI_TELEMETRY_DEBUG
  #define WIFI_TEL_DBG(fmt, ...) do { Serial.printf("[WTEL] " fmt "\n", ##__VA_ARGS__); } while (0)
#else
  #define WIFI_TEL_DBG(fmt, ...) do {} while (0)
#endif

// Singleton-instance pointer for the PubSubClient C-callback trampoline.
// Only one WifiMqttTransport exists per device; this is a safe assumption
// for the embedded use case and avoids the complexity of std::function or
// per-instance callback registration.
WifiMqttTransport* WifiMqttTransport::_instance = nullptr;

WifiMqttTransport::WifiMqttTransport(const char* ssid,
                                       const char* password,
                                       const char* mqtt_host,
                                       uint16_t mqtt_port,
                                       const char* mqtt_user,
                                       const char* mqtt_pass,
                                       const char* client_id)
    : _ssid(ssid),
      _password(password),
      _mqtt_host(mqtt_host),
      _mqtt_port(mqtt_port),
      _mqtt_user(mqtt_user),
      _mqtt_pass(mqtt_pass),
      _client_id(client_id),
      _mqtt(_wifi_client) {
    _mqtt.setBufferSize(WIFI_TELEMETRY_PAYLOAD_MAX + WIFI_TELEMETRY_TOPIC_MAX + 32);

    // Register singleton + PubSubClient callback trampoline (per issue #86).
    // If a second WifiMqttTransport is ever constructed, the most recent one
    // wins; not expected to happen but logged via debug if it does.
    if (_instance != nullptr) {
        WIFI_TEL_DBG("ctor: WARNING - _instance already set; replacing (multi-instance not supported)");
    }
    _instance = this;
    _mqtt.setCallback(&WifiMqttTransport::pubsubTrampoline);
}

// #913: advance the bring-up by ONE step and return. Never waits.
//
// This used to block the Arduino loop task for up to ~20 s (15 s WiFi + 5 s
// MQTT). `delay()` is `vTaskDelay()`, so it yielded rather than spinning -- but
// it was the LOOP TASK yielding, and while it slept in here `mesh::Mesh::loop()`
// did not run, so `Dispatcher::checkRecv()` did not run and the LoRa receiver
// was unserviced. Measured on ST-P: a 28.2 s hole in received traffic
// containing two WiFi cycles (#910), seen from a client as a command that never
// gets a reply (#893).
//
// The decision logic lives in LinkStateMachine.h: pure, Arduino-free, and
// host-tested including both timeout boundaries. This function is only the
// side effects.
bool WifiMqttTransport::begin() {
    offband::LinkInputs in;
    in.wifi_connected = (WiFi.status() == WL_CONNECTED);
    in.mqtt_connected = _mqtt.connected();
    in.phase_elapsed_ms = millis() - _phase_started_ms;
    in.stop_requested = false;

    const offband::LinkTick t = offband::linkTick(_link, in);
    if (t.phase_changed) _phase_started_ms = millis();
    _link = t.state;

    switch (t.action) {
        case offband::LinkAction::StartWifi:
            WIFI_TEL_DBG("begin: connecting WiFi...");
            startWifi();
            break;
        case offband::LinkAction::StartMqtt:
            WIFI_TEL_DBG("begin: WiFi connected, IP=%s RSSI=%d",
                         WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
            tryMqttConnect();
            break;
        case offband::LinkAction::ReportReady:
            WIFI_TEL_DBG("begin: MQTT connected to %s:%u", _mqtt_host, _mqtt_port);
            break;
        case offband::LinkAction::Teardown:
            // Reached only on a timeout. The state machine does NOT self-retry;
            // the 5-minute telemetry schedule owns the retry cadence.
            WIFI_TEL_DBG("begin: link FAILED (mqtt state=%d)", _last_mqtt_state);
            end();
            break;
        case offband::LinkAction::None:
            // Still connecting, inside budget. Do nothing -- and crucially, do
            // not wait. The caller returns to loop() and the mesh keeps running.
            if (_link == offband::LinkState::Connecting && in.wifi_connected
                && !in.mqtt_connected) {
                tryMqttConnect();   // retry within the MQTT budget
            }
            break;
    }
    return _link == offband::LinkState::Ready;
}

void WifiMqttTransport::end() {
    if (_mqtt.connected()) {
        // Let PubSubClient flush any pending writes before we tear down TCP.
        _mqtt.loop();
        _mqtt.disconnect();
    }
    WiFi.disconnect(true);  // also disable WiFi radio
    WiFi.mode(WIFI_OFF);
    WIFI_TEL_DBG("end: WiFi + MQTT torn down");
}

bool WifiMqttTransport::isReady() {
    return (WiFi.status() == WL_CONNECTED) && _mqtt.connected();
}

bool WifiMqttTransport::publish(const char* topic, const char* payload, bool retain) {
    if (!isReady()) {
        WIFI_TEL_DBG("publish: transport not ready (wifi=%d mqtt=%d)",
                     (int)(WiFi.status() == WL_CONNECTED),
                     (int)_mqtt.connected());
        return false;
    }
    bool ok = _mqtt.publish(topic, payload, retain);
    if (!ok) {
        WIFI_TEL_DBG("publish: FAILED topic=%s len=%u state=%d",
                     topic, (unsigned)strlen(payload), _mqtt.state());
    }
    return ok;
}

void WifiMqttTransport::loop() {
    if (_mqtt.connected()) {
        _mqtt.loop();
    }
}

// #913: kick WiFi off and RETURN. No wait.
//
// WiFi.begin() is genuinely asynchronous -- it hands the request to the ESP-IDF
// WiFi task and returns; progress is observed by polling WiFi.status(). The
// state machine does that polling across loop() passes, so the 15 s wait that
// used to sit on the loop task is now ZERO blocking.
void WifiMqttTransport::startWifi() {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);
}

// ---- Subscribe / message-callback API (issue #86) ----

bool WifiMqttTransport::subscribe(const char* topic, uint8_t qos) {
    if (!isReady()) {
        WIFI_TEL_DBG("subscribe: transport not ready, topic=%s", topic);
        return false;
    }
    bool ok = _mqtt.subscribe(topic, qos);
    if (!ok) {
        WIFI_TEL_DBG("subscribe: FAILED topic=%s qos=%u state=%d",
                     topic, (unsigned)qos, _mqtt.state());
    } else {
        WIFI_TEL_DBG("subscribe: ok topic=%s qos=%u", topic, (unsigned)qos);
    }
    return ok;
}

void WifiMqttTransport::setMessageCallback(MessageCallback cb, void* user_data) {
    _user_cb = cb;
    _user_cb_data = user_data;
}

// Static trampoline. PubSubClient delivers an incoming message via this C
// function pointer; we dispatch back through the singleton instance to the
// user-supplied callback with the original user_data context.
void WifiMqttTransport::pubsubTrampoline(char* topic, uint8_t* payload, unsigned int length) {
    if (_instance == nullptr) return;
    if (_instance->_user_cb == nullptr) {
        WIFI_TEL_DBG("trampoline: no user callback registered, dropping topic=%s len=%u",
                     topic, length);
        return;
    }
    _instance->_user_cb(topic,
                         (const uint8_t*)payload,
                         (size_t)length,
                         _instance->_user_cb_data);
}

// #913: ONE bounded attempt, then return. The retry loop is gone.
//
// HONEST LIMITATION, recorded rather than glossed: PubSubClient::connect()
// performs a BLOCKING TCP connect and exposes no asynchronous form, so unlike
// WiFi this cannot be reduced to zero blocking. What it can be is BOUNDED --
// setSocketTimeout() caps a single attempt, and the state machine retries
// across loop() passes within its 5 s budget instead of sitting in a
// `while (!ok) { ...; delay(500); }` loop.
//
// Net effect on the defect: the worst-case loop stall drops from ~20 s (15 s
// WiFi + 5 s MQTT, both fully on the loop task) to roughly one socket timeout.
// The remaining stall is the subject of the residual note in the design record.
bool WifiMqttTransport::tryMqttConnect() {
    if (_mqtt.connected()) {
        _last_mqtt_state = _mqtt.state();
        return true;
    }

    _mqtt.setServer(_mqtt_host, _mqtt_port);
    // Explicit, not left to the library default: the default is generous
    // (seconds) and is exactly the kind of unstated assumption that produced
    // this defect. Bounded here so one attempt cannot dominate a loop pass.
    _mqtt.setSocketTimeout(kMqttSocketTimeoutSec);

    const bool ok = (_mqtt_user && _mqtt_user[0])
                        ? _mqtt.connect(_client_id, _mqtt_user, _mqtt_pass)
                        : _mqtt.connect(_client_id);
    _last_mqtt_state = _mqtt.state();
    return ok;
}

#endif // ENABLE_WIFI_TELEMETRY && ESP32
