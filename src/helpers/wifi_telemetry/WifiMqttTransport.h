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

private:
    const char* _ssid;
    const char* _password;
    const char* _mqtt_host;
    uint16_t _mqtt_port;
    const char* _mqtt_user;
    const char* _mqtt_pass;
    const char* _client_id;

    WiFiClient _wifi_client;
    PubSubClient _mqtt;

    bool connectWifi(uint32_t timeout_ms = 15000);
    bool connectMqtt(uint32_t timeout_ms = 5000);
};

#endif // ENABLE_WIFI_TELEMETRY && ESP32
