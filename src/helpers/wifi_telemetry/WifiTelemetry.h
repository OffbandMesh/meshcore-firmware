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

// Bumped from 384 in v1 to 2048 to accommodate the neighbours JSON list
// (up to WIFI_TELEMETRY_MAX_NEIGHBORS entries at ~60 bytes each plus wrapper).
#ifndef WIFI_TELEMETRY_PAYLOAD_MAX
#define WIFI_TELEMETRY_PAYLOAD_MAX 2048
#endif

// Max neighbours to include in a single publish. The Repeater may track up
// to MAX_NEIGHBOURS (50 by default) but we cap what's serialized to keep
// the payload bounded.
#ifndef WIFI_TELEMETRY_MAX_NEIGHBORS
#define WIFI_TELEMETRY_MAX_NEIGHBORS 20
#endif

// ---------------------------------------------------------------------------
// TelemetryData — snapshot of what we report each cycle.
// ---------------------------------------------------------------------------
struct TelemetryData {
    // Live measurements
    uint16_t battery_mv;        // raw battery voltage in millivolts
    uint8_t  battery_pct;       // derived 0-100 percentage (Li-ion curve)
    uint32_t uptime_seconds;    // since boot
    uint16_t tx_queue_len;      // outbound packet queue depth (stubbed at 0 — needs MyMesh accessor)
    int16_t  noise_floor_dbm;   // measured noise floor on LoRa channel
    uint8_t  neighbor_count;    // adverts heard at zero hops (denormalized — also in TelemetryNeighbors)
    uint32_t timestamp;         // epoch seconds (0 if no RTC sync)

    // System / network diagnostics
    float    mcu_temp_c;        // ESP32 MCU internal temp (NaN if unsupported)
    int16_t  last_rssi_dbm;     // RSSI of most recent received LoRa packet
    float    last_snr_db;       // SNR of most recent received LoRa packet
    uint32_t packets_recv;      // total LoRa packets received since boot
    uint32_t packets_sent;      // total LoRa packets transmitted since boot
    int16_t  wifi_rssi_dbm;     // WiFi signal strength to the AP (valid only when WiFi up)
    uint32_t free_heap_b;       // ESP32 free heap in bytes
    const char* reset_reason;   // human-readable reset reason from last boot (static string, never free)
};

// ---------------------------------------------------------------------------
// NeighborEntry — one zero-hop neighbour as we serialize it.
// pubkey_short = first 2 hex chars of public key (MeshCore community
// registry convention used as a short repeater ID).
// pubkey_hash  = last 8 hex chars of public key (Meshtastic-style hash for
// disambiguation when multiple neighbours share a 2-char short prefix).
// ---------------------------------------------------------------------------
struct NeighborEntry {
    char     pubkey_short[3];   // 2 hex chars + null
    char     pubkey_hash[9];    // 8 hex chars + null
    float    snr_db;            // SNR in dB
    uint32_t age_seconds;       // seconds since heard_timestamp
};

struct TelemetryNeighbors {
    uint8_t  total_count;                                   // total neighbours known
    uint8_t  entries_filled;                                // valid entries in entries[] (<= total)
    char     repeater_pubkey_short[3];                      // THIS repeater's own first-2-hex registry short ID
    char     summary[96];                                   // compact text for at-a-glance display, e.g. "3: a6, b8, 0a"
    NeighborEntry entries[WIFI_TELEMETRY_MAX_NEIGHBORS];
};

// ---------------------------------------------------------------------------
// TelemetryTransport — abstract publish interface.
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
    // retain=true for HA Discovery messages and slow telemetry (sticky last value).
    // Returns true on confirmed publish, false on failure.
    virtual bool publish(const char* topic, const char* payload, bool retain) = 0;

    // Run the transport's internal loop (MQTT keepalive, etc.).
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
// ---------------------------------------------------------------------------
class WifiTelemetry {
public:
    WifiTelemetry();

    // Configure with transport, a node-id string (used in topics), MQTT topic
    // prefix, friendly name, and firmware version metadata.
    // All strings must outlive this object (typically PROGMEM or build-flag
    // -defined literals).
    void begin(TelemetryTransport* transport,
               const char* node_id,
               const char* mqtt_prefix,
               const char* friendly_name,
               const char* firmware_version,
               const char* firmware_build_date,
               const char* hardware_model);

    // Add a scalar sample. If transport is ready, brings it up + publishes
    // immediately; otherwise queues. Discovery is published on first
    // successful state publish.
    // Returns true if accepted (published OR queued), false if queue full.
    bool sample(const TelemetryData& data);

    // Publish the neighbours snapshot. Assumes transport is already up
    // (typically called immediately after sample()). NOT queued on failure
    // — neighbours are a current-state view, not a time-series.
    // Returns true on confirmed publish, false on failure / transport down.
    bool publishNeighbors(const TelemetryNeighbors& n);

    // Run loop: handles transport loop, retries on reconnect, drains queue.
    void loop();

    // Force publishing all queued samples now (bulk drain on reconnect).
    // Stops on first publish failure to preserve order.
    // Returns number of samples successfully drained.
    size_t drainQueue();

    // Convert raw battery voltage (mV) to estimated percentage using a
    // standard 1S Li-ion discharge curve.
    static uint8_t batteryPercent(uint16_t mv);

private:
    TelemetryTransport* _transport;
    const char* _node_id;
    const char* _mqtt_prefix;
    const char* _friendly_name;
    const char* _fw_version;
    const char* _fw_build_date;
    const char* _hw_model;
    TelemetryQueue _queue;
    bool _discovery_sent;

    bool publishState(const TelemetryData& data);
    bool publishDiscovery();

    // Topic + payload builders.
    void buildStateTopic(char* buf, size_t buflen);
    void buildNeighborsTopic(char* buf, size_t buflen);
    void buildDiscoveryTopic(char* buf, size_t buflen, const char* sensor_id);
    size_t buildStatePayload(char* buf, size_t buflen, const TelemetryData& d);
    size_t buildNeighborsPayload(char* buf, size_t buflen, const TelemetryNeighbors& n);
    size_t buildScalarDiscoveryPayload(char* buf, size_t buflen,
                                       const char* sensor_id,
                                       const char* name,
                                       const char* unit,
                                       const char* device_class,
                                       const char* state_class,
                                       const char* value_template,
                                       const char* entity_category);
    size_t buildNeighborsDiscoveryPayload(char* buf, size_t buflen);
    size_t buildNeighborsSummaryDiscoveryPayload(char* buf, size_t buflen);
};
