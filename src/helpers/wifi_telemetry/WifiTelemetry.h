/**
 * WifiTelemetry — Reusable telemetry publishing module for MeshCore.
 *
 * Publishes node status (battery, uptime, queue, noise, neighbors) to an
 * MQTT broker on a configurable interval, with offline queueing on transport
 * failure.
 *
 * Architecture:
 *   - TelemetryTransport is an abstract interface so future transports
 *     (LoRa-to-MQTT bridge, BLE-to-phone, RAK co-processor) can plug in
 *     without touching the telemetry collection code.
 *   - WifiMqttTransport is the v1 implementation for ESP32 boards with
 *     native WiFi (Heltec V3/V4, ESP32-WROOM DevKits, etc.).
 *   - HA Discovery payloads are generated on first successful publish so
 *     Home Assistant auto-creates entities.
 *   - Offline queue is an in-memory ring buffer sized for ~24h of samples
 *     at the default 15-minute interval.
 *
 * RAK / nRF52 board support is deferred — those boards lack native WiFi and
 * would require a separate transport implementation.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Compile-time tuning. Override via build_flags if you want different values.
// ---------------------------------------------------------------------------
#ifndef WIFI_TELEMETRY_INTERVAL_MS
#define WIFI_TELEMETRY_INTERVAL_MS (15UL * 60UL * 1000UL)
#endif

#ifndef WIFI_TELEMETRY_QUEUE_SIZE
#define WIFI_TELEMETRY_QUEUE_SIZE 100
#endif

#ifndef WIFI_TELEMETRY_TOPIC_MAX
#define WIFI_TELEMETRY_TOPIC_MAX 128
#endif

#ifndef WIFI_TELEMETRY_PAYLOAD_MAX
#define WIFI_TELEMETRY_PAYLOAD_MAX 384
#endif

// ---------------------------------------------------------------------------
// TelemetryData — snapshot of what we report each cycle.
// Fields chosen to match RepeaterStats from MeshCore's REQ_TYPE_GET_STATUS
// response, plus derived battery percentage and a timestamp.
// ---------------------------------------------------------------------------
struct TelemetryData {
    uint16_t battery_mv;        // raw battery voltage in millivolts
    uint8_t  battery_pct;       // derived 0-100 percentage (Li-ion curve)
    uint32_t uptime_seconds;    // since boot
    uint16_t tx_queue_len;      // outbound packet queue depth
    int16_t  noise_floor_dbm;   // measured noise floor on LoRa channel
    uint8_t  neighbor_count;    // adverts heard at zero hops
    uint32_t timestamp;         // epoch seconds (0 if no RTC sync)
};

// ---------------------------------------------------------------------------
// TelemetryTransport — abstract publish interface.
// Implementations: WifiMqttTransport (ESP32 native WiFi).
// Future: LoRaToMqttBridgeTransport, BleHostTransport, etc.
// ---------------------------------------------------------------------------
class TelemetryTransport {
public:
    virtual ~TelemetryTransport() = default;

    // Bring up the transport (connect WiFi, connect MQTT).
    // May be called multiple times; idempotent if already connected.
    // Returns true on success.
    virtual bool begin() = 0;

    // Tear down the transport (disconnect WiFi/MQTT, release resources).
    // Used to drop into low-power state between publish cycles.
    virtual void end() = 0;

    // Is the transport currently ready to accept publish() calls?
    virtual bool isReady() = 0;

    // Publish a message. Topic and payload are null-terminated strings.
    // retain=true for HA Discovery messages, false for state.
    // Returns true on confirmed publish, false on failure.
    virtual bool publish(const char* topic, const char* payload, bool retain) = 0;

    // Run the transport's internal loop (MQTT keepalive, etc.).
    // Safe to call frequently; cheap when idle.
    virtual void loop() = 0;
};

// ---------------------------------------------------------------------------
// TelemetryQueue — fixed-size in-memory ring buffer for offline samples.
// Drops oldest entry when full (FIFO with overwrite-on-full).
// ---------------------------------------------------------------------------
class TelemetryQueue {
public:
    TelemetryQueue();
    bool push(const TelemetryData& d);
    bool pop(TelemetryData& d);
    bool peek(TelemetryData& d) const;
    size_t size() const { return _count; }
    bool isEmpty() const { return _count == 0; }
    bool isFull() const { return _count == WIFI_TELEMETRY_QUEUE_SIZE; }

private:
    TelemetryData _buf[WIFI_TELEMETRY_QUEUE_SIZE];
    size_t _head;
    size_t _tail;
    size_t _count;
};

// ---------------------------------------------------------------------------
// WifiTelemetry — main manager class.
// Owns the queue, manages publish timing, generates HA Discovery payloads,
// drains queue on reconnect.
// ---------------------------------------------------------------------------
class WifiTelemetry {
public:
    WifiTelemetry();

    // Configure with transport, a node-id string (used in topics), and an
    // MQTT topic prefix. All strings must outlive this object (typically
    // PROGMEM or build-flag-defined literals).
    void begin(TelemetryTransport* transport,
               const char* node_id,
               const char* mqtt_prefix,
               const char* friendly_name);

    // Add a sample. If transport is ready, publishes immediately; otherwise
    // queues. Discovery is published on first successful state publish.
    // Returns true if accepted (published OR queued), false if queue full.
    bool sample(const TelemetryData& data);

    // Run loop: handles transport loop, retries on reconnect, drains queue.
    void loop();

    // Force publishing all queued samples now (bulk drain on reconnect).
    // Stops on first publish failure to preserve order.
    // Returns number of samples successfully drained.
    size_t drainQueue();

    // Convert raw battery voltage (mV) to estimated percentage using a
    // standard 1S Li-ion discharge curve. Exposed publicly so callers can
    // compute pct for their TelemetryData before sample().
    static uint8_t batteryPercent(uint16_t mv);

private:
    TelemetryTransport* _transport;
    const char* _node_id;
    const char* _mqtt_prefix;
    const char* _friendly_name;
    TelemetryQueue _queue;
    bool _discovery_sent;

    bool publishState(const TelemetryData& data);
    bool publishDiscovery();

    // Topic + payload builders. buf must be at least WIFI_TELEMETRY_*_MAX.
    void buildStateTopic(char* buf, size_t buflen);
    void buildDiscoveryTopic(char* buf, size_t buflen, const char* sensor_id);
    size_t buildStatePayload(char* buf, size_t buflen, const TelemetryData& d);
    size_t buildDiscoveryPayload(char* buf, size_t buflen,
                                  const char* sensor_id,
                                  const char* name,
                                  const char* unit,
                                  const char* device_class,
                                  const char* state_class,
                                  const char* value_template);
};
