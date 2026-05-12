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
}

bool WifiMqttTransport::begin() {
    if (isReady()) return true;
    WIFI_TEL_DBG("begin: connecting WiFi...");
    if (!connectWifi()) {
        WIFI_TEL_DBG("begin: WiFi connect FAILED");
        return false;
    }
    WIFI_TEL_DBG("begin: WiFi connected, IP=%s RSSI=%d", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    if (!connectMqtt()) {
        WIFI_TEL_DBG("begin: MQTT connect FAILED (state=%d)", _mqtt.state());
        return false;
    }
    WIFI_TEL_DBG("begin: MQTT connected to %s:%u", _mqtt_host, _mqtt_port);
    return true;
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

bool WifiMqttTransport::connectWifi(uint32_t timeout_ms) {
    if (WiFi.status() == WL_CONNECTED) return true;

    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if ((millis() - start) > timeout_ms) {
            WIFI_TEL_DBG("connectWifi: timeout after %lums (status=%d)", (unsigned long)(millis() - start), (int)WiFi.status());
            return false;
        }
        delay(100);
    }
    return true;
}

bool WifiMqttTransport::connectMqtt(uint32_t timeout_ms) {
    if (_mqtt.connected()) {
        _last_mqtt_state = _mqtt.state();
        return true;
    }

    _mqtt.setServer(_mqtt_host, _mqtt_port);

    uint32_t start = millis();
    bool ok = false;
    while (!ok) {
        if (_mqtt_user && _mqtt_user[0]) {
            ok = _mqtt.connect(_client_id, _mqtt_user, _mqtt_pass);
        } else {
            ok = _mqtt.connect(_client_id);
        }
        _last_mqtt_state = _mqtt.state();
        if (ok) break;
        if ((millis() - start) > timeout_ms) {
            WIFI_TEL_DBG("connectMqtt: timeout (last state=%d)", _last_mqtt_state);
            return false;
        }
        delay(500);
    }
    return ok;
}

#endif // ENABLE_WIFI_TELEMETRY && ESP32
