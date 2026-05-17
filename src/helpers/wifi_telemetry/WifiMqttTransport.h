/**
 * WifiMqttTransport — ESP32 WiFi + MQTT publish transport.
 *
 * Use case: duty-cycled publish from a solar-powered Repeater.
 * Recommended pattern:
 *   - Call begin() just before publishing
 *   - Call publish() once (or drain a small queue)
 *   - Call end() to drop WiFi + MQTT and save power
 *
 * Target: average added current ~5 mA at 15-minute publish interval.
 *
 * Build dependencies:
 *   - Arduino-ESP32 framework (WiFi.h)
 *   - knolleary/PubSubClient (MQTT client)
 */
#pragma once

#include "WifiTelemetry.h"

#if defined(ENABLE_WIFI_TELEMETRY) && defined(ESP32)

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

class WifiMqttTransport : public TelemetryTransport {
public:
    // ssid/password/mqtt_* must outlive this object (typically build-flag literals).
    WifiMqttTransport(const char* ssid,
                      const char* password,
                      const char* mqtt_host,
                      uint16_t mqtt_port,
                      const char* mqtt_user,
                      const char* mqtt_pass,
                      const char* client_id);

    bool begin() override;
    void end() override;
    bool isReady() override;
    bool publish(const char* topic, const char* payload, bool retain) override;
    void loop() override;

    // PubSubClient state code from last attempt, for diagnostic readout via CLI.
    // -4: timeout, -3: connection lost, -2: connect failed, -1: disconnected,
    // 0: connected, 1-5: protocol/auth errors. Returns 99 if no MQTT attempt yet.
    int getMqttState() { return _last_mqtt_state; }

    // ---- Subscribe / message-callback API (added per issue #86) ----
    // Subscribe to an MQTT topic. Must be called after begin() (MQTT must be
    // connected). Re-subscription needed after reconnect; the caller is
    // responsible for re-subscribing if the connection drops.
    // Returns false if MQTT is not connected.
    bool subscribe(const char* topic, uint8_t qos = 0);

    // C-style callback signature delivered to the registered user callback.
    // topic is NUL-terminated. payload is NOT NUL-terminated (binary-safe).
    typedef void (*MessageCallback)(const char* topic,
                                    const uint8_t* payload,
                                    size_t length,
                                    void* user_data);

    // Register a single callback to receive MQTT messages for all subscribed
    // topics. Internally bridges PubSubClient's C callback to user_data-aware
    // dispatch. Pass nullptr to clear.
    void setMessageCallback(MessageCallback cb, void* user_data);

private:
    // PubSubClient takes a C function pointer for incoming messages. We use a
    // static trampoline that dispatches via _instance back to the registered
    // user callback. Single-instance assumption (only one WifiMqttTransport
    // exists per device); see _instance comment.
    static void pubsubTrampoline(char* topic, uint8_t* payload, unsigned int length);
    static WifiMqttTransport* _instance;

    MessageCallback _user_cb = nullptr;
    void* _user_cb_data = nullptr;

    const char* _ssid;
    const char* _password;
    const char* _mqtt_host;
    uint16_t _mqtt_port;
    const char* _mqtt_user;
    const char* _mqtt_pass;
    const char* _client_id;

    WiFiClient _wifi_client;
    PubSubClient _mqtt;
    int _last_mqtt_state = 99;  // last PubSubClient state code, 99 = never attempted

    bool connectWifi(uint32_t timeout_ms = 15000);
    bool connectMqtt(uint32_t timeout_ms = 5000);
};

#endif // ENABLE_WIFI_TELEMETRY && ESP32
