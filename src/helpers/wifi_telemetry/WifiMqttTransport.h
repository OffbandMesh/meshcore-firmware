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

// #913: bound on a single MQTT connect attempt, in SECONDS (PubSubClient's unit).
//
// `[verified: PubSubClient.h:34-36]` the library default is MQTT_SOCKET_TIMEOUT
// = 15, and this code never overrode it. So one `_mqtt.connect()` could block
// for ~15 s -- the old outer loop's nominal 5 s budget could not bound it,
// because the elapsed check ran only AFTER an attempt returned. Worst case was
// therefore ~15 s WiFi + ~15 s MQTT, which matches the 28.2 s stall measured on
// ST-P (#910) better than the 20 s originally estimated.
//
// 2 s is ample for a LAN broker and caps what a single attempt can cost the
// loop; the state machine retries across ticks within its own budget.
static constexpr uint16_t kMqttSocketTimeoutSec = 2;

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

    // #913: NON-BLOCKING. Starts or advances the bring-up by one step and
    // returns true only when the link is Ready NOW. It no longer waits, so a
    // caller must either loop until isReady() or consult linkState() to tell
    // "still connecting" from "failed".
    bool begin() override;
    void end() override;
    bool isReady() override;
    offband::LinkState linkState() override { return _link; }
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

    // #913: link bring-up state, advanced one step per begin() call. The
    // decision logic is in LinkStateMachine.h -- pure, Arduino-free and
    // host-tested; this class keeps only the calls that need a radio.
    offband::LinkState _link = offband::LinkState::Idle;
    uint32_t _phase_started_ms = 0;      // when the CURRENT phase began
    uint32_t _last_mqtt_attempt_ms = 0;  // rate-limits MQTT retries (#913)

    // Non-blocking: kicks WiFi off and returns immediately. WiFi.begin() is
    // genuinely asynchronous -- progress is observed via WiFi.status().
    void startWifi();

    // ONE bounded MQTT connect attempt. PubSubClient::connect() performs a
    // blocking TCP connect and cannot be made asynchronous, so it is BOUNDED
    // via setSocketTimeout() and retried across ticks instead. This is the one
    // place the loop can still stall, and only for that bound.
    bool tryMqttConnect();
};

#endif // ENABLE_WIFI_TELEMETRY && ESP32
