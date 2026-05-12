#include "WifiMqttTransport.h"

#if defined(ENABLE_WIFI_TELEMETRY) && defined(ESP32)

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
    if (!connectWifi()) return false;
    if (!connectMqtt()) return false;
    return true;
}

void WifiMqttTransport::end() {
    if (_mqtt.connected()) {
        _mqtt.disconnect();
    }
    WiFi.disconnect(true);  // also disable WiFi radio
    WiFi.mode(WIFI_OFF);
}

bool WifiMqttTransport::isReady() {
    return (WiFi.status() == WL_CONNECTED) && _mqtt.connected();
}

bool WifiMqttTransport::publish(const char* topic, const char* payload, bool retain) {
    if (!isReady()) return false;
    return _mqtt.publish(topic, payload, retain);
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
            return false;
        }
        delay(100);
    }
    return true;
}

bool WifiMqttTransport::connectMqtt(uint32_t timeout_ms) {
    if (_mqtt.connected()) return true;

    _mqtt.setServer(_mqtt_host, _mqtt_port);

    uint32_t start = millis();
    bool ok = false;
    while (!ok) {
        if (_mqtt_user && _mqtt_user[0]) {
            ok = _mqtt.connect(_client_id, _mqtt_user, _mqtt_pass);
        } else {
            ok = _mqtt.connect(_client_id);
        }
        if (ok) break;
        if ((millis() - start) > timeout_ms) return false;
        delay(500);
    }
    return ok;
}

#endif // ENABLE_WIFI_TELEMETRY && ESP32
