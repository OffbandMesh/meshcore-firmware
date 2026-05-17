#include "WifiTelemetry.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

// ---------------------------------------------------------------------------
// TelemetryQueue
// ---------------------------------------------------------------------------
TelemetryQueue::TelemetryQueue() : _head(0), _tail(0), _count(0) {}

bool TelemetryQueue::push(const TelemetryData& d) {
    if (_count == WIFI_TELEMETRY_QUEUE_SIZE) {
        // Full — drop oldest to make room.
        _tail = (_tail + 1) % WIFI_TELEMETRY_QUEUE_SIZE;
        _count--;
    }
    _buf[_head] = d;
    _head = (_head + 1) % WIFI_TELEMETRY_QUEUE_SIZE;
    _count++;
    return true;
}

bool TelemetryQueue::pop(TelemetryData& d) {
    if (_count == 0) return false;
    d = _buf[_tail];
    _tail = (_tail + 1) % WIFI_TELEMETRY_QUEUE_SIZE;
    _count--;
    return true;
}

bool TelemetryQueue::peek(TelemetryData& d) const {
    if (_count == 0) return false;
    d = _buf[_tail];
    return true;
}

// ---------------------------------------------------------------------------
// WifiTelemetry
// ---------------------------------------------------------------------------
WifiTelemetry::WifiTelemetry()
    : _transport(nullptr),
      _node_id(nullptr),
      _mqtt_prefix(nullptr),
      _friendly_name(nullptr),
      _fw_version(nullptr),
      _fw_build_date(nullptr),
      _hw_model(nullptr),
      _discovery_sent(false) {}

void WifiTelemetry::begin(TelemetryTransport* transport,
                          const char* node_id,
                          const char* mqtt_prefix,
                          const char* friendly_name,
                          const char* firmware_version,
                          const char* firmware_build_date,
                          const char* hardware_model) {
    _transport = transport;
    _node_id = node_id;
    _mqtt_prefix = mqtt_prefix;
    _friendly_name = friendly_name;
    _fw_version = firmware_version;
    _fw_build_date = firmware_build_date;
    _hw_model = hardware_model;
    _discovery_sent = false;
}

bool WifiTelemetry::sample(const TelemetryData& data) {
    if (!_transport) return false;

    // Try bringing transport up if not ready.
    if (!_transport->isReady()) {
        _transport->begin();
    }

    if (_transport->isReady()) {
        // Drain any queued samples first to preserve order.
        if (!_queue.isEmpty()) {
            drainQueue();
        }
        // Send discovery once, before first state message.
        if (!_discovery_sent) {
            if (publishDiscovery()) {
                _discovery_sent = true;
            }
        }
        // Publish current sample.
        if (publishState(data)) {
            return true;
        }
        // Publish failed; fall through to queue.
    }

    // Transport not ready or publish failed — queue it.
    return _queue.push(data);
}

bool WifiTelemetry::publishNeighbors(const TelemetryNeighbors& n) {
    if (!_transport || !_transport->isReady()) return false;

    char topic[WIFI_TELEMETRY_TOPIC_MAX];
    // Use a static buffer for the larger neighbours payload — 2 KB on the
    // stack each call would be wasteful when this is called once per publish
    // cycle.
    static char payload[WIFI_TELEMETRY_PAYLOAD_MAX];

    buildNeighborsTopic(topic, sizeof(topic));
    size_t len = buildNeighborsPayload(payload, sizeof(payload), n);
    if (len == 0) return false;

    return _transport->publish(topic, payload, true /* retain */);
}

void WifiTelemetry::loop() {
    if (_transport) {
        _transport->loop();
    }
}

size_t WifiTelemetry::drainQueue() {
    if (!_transport || !_transport->isReady()) return 0;

    size_t drained = 0;
    TelemetryData d;
    while (_queue.peek(d)) {
        if (publishState(d)) {
            _queue.pop(d);
            drained++;
        } else {
            // Stop on first failure to preserve order.
            break;
        }
    }
    return drained;
}

bool WifiTelemetry::publishState(const TelemetryData& data) {
    if (!_transport || !_transport->isReady()) return false;

    char topic[WIFI_TELEMETRY_TOPIC_MAX];
    char payload[WIFI_TELEMETRY_PAYLOAD_MAX];

    buildStateTopic(topic, sizeof(topic));
    size_t n = buildStatePayload(payload, sizeof(payload), data);
    if (n == 0) return false;

    // retain=true: HA shows last-known value after reconnect or after HA
    // restart. Correct choice for slow periodic telemetry — high-frequency
    // event streams would use retain=false.
    return _transport->publish(topic, payload, true);
}

bool WifiTelemetry::publishDiscovery() {
    if (!_transport || !_transport->isReady()) return false;

    // Sensor definitions: id, friendly name, unit, device_class, state_class,
    // value_template, entity_category (nullptr = no category, "diagnostic" = diagnostic).
    struct SensorDef {
        const char* id;
        const char* name;
        const char* unit;
        const char* device_class;
        const char* state_class;
        const char* value_template;
        const char* entity_category;
    };

    static const SensorDef sensors[] = {
        // Primary live measurements
        { "battery_mv",     "Battery Voltage",   "mV",  "voltage",          "measurement",      "{{ value_json.battery_mv }}",         nullptr },
        { "battery_pct",    "Battery Level",     "%",   "battery",          "measurement",      "{{ value_json.battery_pct }}",        nullptr },
        { "uptime",         "Uptime",            "s",   "duration",         "total_increasing", "{{ value_json.uptime_seconds }}",     "diagnostic" },
        { "tx_queue",       "TX Queue Length",   "",    nullptr,            "measurement",      "{{ value_json.tx_queue_len }}",       "diagnostic" },
        { "noise_floor",    "Noise Floor",       "dBm", "signal_strength",  "measurement",      "{{ value_json.noise_floor_dbm }}",    "diagnostic" },
        { "neighbors_simple", "Neighbor Count",  "",    nullptr,            "measurement",      "{{ value_json.neighbor_count }}",     nullptr },
        // System / network diagnostics
        { "mcu_temp",       "MCU Temperature",   "°C",  "temperature",      "measurement",      "{{ value_json.mcu_temp_c }}",         "diagnostic" },
        { "last_rssi",      "Last RSSI",         "dBm", "signal_strength",  "measurement",      "{{ value_json.last_rssi_dbm }}",      "diagnostic" },
        { "last_snr",       "Last SNR",          "dB",  "signal_strength",  "measurement",      "{{ value_json.last_snr_db }}",        "diagnostic" },
        { "packets_recv",   "Packets Received",  "",    nullptr,            "total_increasing", "{{ value_json.packets_recv }}",       "diagnostic" },
        { "packets_sent",   "Packets Sent",      "",    nullptr,            "total_increasing", "{{ value_json.packets_sent }}",       "diagnostic" },
        { "wifi_rssi",      "WiFi RSSI",         "dBm", "signal_strength",  "measurement",      "{{ value_json.wifi_rssi_dbm }}",      "diagnostic" },
        { "free_heap",      "Free Heap",         "B",   "data_size",        "measurement",      "{{ value_json.free_heap_b }}",        "diagnostic" },
        { "reset_reason",   "Last Reset Reason", nullptr, nullptr,          nullptr,            "{{ value_json.reset_reason }}",       "diagnostic" },
    };

    char topic[WIFI_TELEMETRY_TOPIC_MAX];
    static char payload[WIFI_TELEMETRY_PAYLOAD_MAX];

    // 1) Publish discovery for each scalar sensor.
    for (size_t i = 0; i < sizeof(sensors) / sizeof(sensors[0]); i++) {
        const SensorDef& s = sensors[i];
        buildDiscoveryTopic(topic, sizeof(topic), s.id);
        size_t n = buildScalarDiscoveryPayload(payload, sizeof(payload),
                                                s.id, s.name, s.unit,
                                                s.device_class, s.state_class,
                                                s.value_template,
                                                s.entity_category);
        if (n == 0) return false;
        if (!_transport->publish(topic, payload, true /* retain */)) {
            return false;
        }
    }

    // 2) Publish discovery for the neighbours summary sensor (text on the
    //    neighbours topic, reading value_json.summary). No state_class because
    //    the value is a text string — graphing wouldn't make sense.
    //
    //    Per issue #84: the numeric-count sensor that previously bound to
    //    value_json.count was removed when the duplicate count field was
    //    dropped from the neighbours-topic payload. State topic's
    //    neighbor_count is the canonical scalar going forward.
    buildDiscoveryTopic(topic, sizeof(topic), "neighbors_summary");
    size_t nss = buildNeighborsSummaryDiscoveryPayload(payload, sizeof(payload));
    if (nss == 0) return false;
    if (!_transport->publish(topic, payload, true /* retain */)) {
        return false;
    }

    return true;
}

void WifiTelemetry::buildStateTopic(char* buf, size_t buflen) {
    snprintf(buf, buflen, "%s/%s/state", _mqtt_prefix, _node_id);
}

void WifiTelemetry::buildNeighborsTopic(char* buf, size_t buflen) {
    snprintf(buf, buflen, "%s/%s/neighbors", _mqtt_prefix, _node_id);
}

void WifiTelemetry::buildDiscoveryTopic(char* buf, size_t buflen, const char* sensor_id) {
    snprintf(buf, buflen, "homeassistant/sensor/%s/%s/config", _node_id, sensor_id);
}

size_t WifiTelemetry::buildStatePayload(char* buf, size_t buflen, const TelemetryData& d) {
    // Sanitize float for JSON: NaN must not appear as "nan" (HA will reject).
    // We emit "null" instead. snprintf with %g handles finite floats.
    char mcu_temp_str[16];
    if (isnan(d.mcu_temp_c)) {
        snprintf(mcu_temp_str, sizeof(mcu_temp_str), "null");
    } else {
        snprintf(mcu_temp_str, sizeof(mcu_temp_str), "%.2f", d.mcu_temp_c);
    }

    char last_snr_str[16];
    if (isnan(d.last_snr_db)) {
        snprintf(last_snr_str, sizeof(last_snr_str), "null");
    } else {
        snprintf(last_snr_str, sizeof(last_snr_str), "%.2f", d.last_snr_db);
    }

    int n = snprintf(buf, buflen,
        "{\"battery_mv\":%u,"
        "\"battery_pct\":%u,"
        "\"uptime_seconds\":%lu,"
        "\"tx_queue_len\":%u,"
        "\"noise_floor_dbm\":%d,"
        "\"neighbor_count\":%u,"
        "\"mcu_temp_c\":%s,"
        "\"last_rssi_dbm\":%d,"
        "\"last_snr_db\":%s,"
        "\"packets_recv\":%lu,"
        "\"packets_sent\":%lu,"
        "\"wifi_rssi_dbm\":%d,"
        "\"free_heap_b\":%lu,"
        "\"reset_reason\":\"%s\","
        "\"timestamp\":%lu}",
        (unsigned)d.battery_mv,
        (unsigned)d.battery_pct,
        (unsigned long)d.uptime_seconds,
        (unsigned)d.tx_queue_len,
        (int)d.noise_floor_dbm,
        (unsigned)d.neighbor_count,
        mcu_temp_str,
        (int)d.last_rssi_dbm,
        last_snr_str,
        (unsigned long)d.packets_recv,
        (unsigned long)d.packets_sent,
        (int)d.wifi_rssi_dbm,
        (unsigned long)d.free_heap_b,
        d.reset_reason ? d.reset_reason : "unknown",
        (unsigned long)d.timestamp);

    if (n < 0 || (size_t)n >= buflen) return 0;
    return (size_t)n;
}

size_t WifiTelemetry::buildNeighborsPayload(char* buf, size_t buflen, const TelemetryNeighbors& n) {
    size_t pos = 0;
    int w;

    // Per issue #84: 'count' was removed from this payload. State topic's
    // neighbor_count is the canonical scalar count. Consumers of this topic
    // derive count from list length if needed, or subscribe to the state topic.
    w = snprintf(buf + pos, buflen - pos,
                 "{\"repeater_pk\":\"%s\",\"summary\":\"%s\",\"list\":[",
                 n.repeater_pubkey_short,
                 n.summary);
    if (w < 0 || (size_t)w >= buflen - pos) return 0;
    pos += w;

    for (uint8_t i = 0; i < n.entries_filled; i++) {
        const NeighborEntry& e = n.entries[i];
        char snr_str[16];
        if (isnan(e.snr_db)) {
            snprintf(snr_str, sizeof(snr_str), "null");
        } else {
            snprintf(snr_str, sizeof(snr_str), "%.2f", e.snr_db);
        }

        w = snprintf(buf + pos, buflen - pos,
                     "%s{\"pk\":\"%s\",\"hash\":\"%s\",\"snr\":%s,\"age\":%lu}",
                     i == 0 ? "" : ",",
                     e.pubkey_short,
                     e.pubkey_hash,
                     snr_str,
                     (unsigned long)e.age_seconds);
        if (w < 0 || (size_t)w >= buflen - pos) return 0;
        pos += w;
    }

    w = snprintf(buf + pos, buflen - pos, "]}");
    if (w < 0 || (size_t)w >= buflen - pos) return 0;
    pos += w;

    return pos;
}

size_t WifiTelemetry::buildScalarDiscoveryPayload(char* buf, size_t buflen,
                                                  const char* sensor_id,
                                                  const char* name,
                                                  const char* unit,
                                                  const char* device_class,
                                                  const char* state_class,
                                                  const char* value_template,
                                                  const char* entity_category) {
    char state_topic[WIFI_TELEMETRY_TOPIC_MAX];
    buildStateTopic(state_topic, sizeof(state_topic));

    // Compose sw_version string: "v1.15.0 (12 May 2026)" if both supplied, else
    // just the version.
    char sw_version[64];
    if (_fw_version && _fw_build_date) {
        snprintf(sw_version, sizeof(sw_version), "%s (%s)", _fw_version, _fw_build_date);
    } else if (_fw_version) {
        snprintf(sw_version, sizeof(sw_version), "%s", _fw_version);
    } else {
        snprintf(sw_version, sizeof(sw_version), "unknown");
    }

    // Entity name is JUST the sensor's short name (e.g. "Battery Voltage").
    // HA's MQTT integration auto-prepends the device name in global contexts
    // (activity feed, alerts, etc.) so including it here would duplicate it.
    int n = snprintf(buf, buflen,
        "{\"name\":\"%s\","
        "\"uniq_id\":\"%s_%s\","
        "\"stat_t\":\"%s\","
        "\"val_tpl\":\"%s\","
        "%s%s%s"
        "%s%s%s"
        "%s%s%s"
        "%s%s%s"
        "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"MeshCore\",\"mdl\":\"Repeater\",\"sw_version\":\"%s\",\"hw_version\":\"%s\"}}",
        name,
        _node_id, sensor_id,
        state_topic,
        value_template,
        unit ? "\"unit_of_meas\":\"" : "",
        unit ? unit : "",
        unit ? "\"," : "",
        device_class ? "\"dev_cla\":\"" : "",
        device_class ? device_class : "",
        device_class ? "\"," : "",
        state_class ? "\"stat_cla\":\"" : "",
        state_class ? state_class : "",
        state_class ? "\"," : "",
        entity_category ? "\"ent_cat\":\"" : "",
        entity_category ? entity_category : "",
        entity_category ? "\"," : "",
        _node_id,
        _friendly_name,
        sw_version,
        _hw_model ? _hw_model : "Heltec V4");

    if (n < 0 || (size_t)n >= buflen) return 0;
    return (size_t)n;
}

// buildNeighborsDiscoveryPayload removed per issue #84 / LoRa-6fw:
//   - It bound a HA sensor's state to value_json.count from the neighbours
//     MQTT topic; with the duplicate 'count' field gone from that topic, the
//     binding has no valid source.
//   - The numeric neighbor count is now exclusively in the state topic
//     (TelemetryData::neighbor_count), reachable via the neighbors_simple /
//     "Neighbor Count" Discovery config in publishDiscovery().
//   - The companion text summary remains via buildNeighborsSummaryDiscoveryPayload
//     below; that sensor reads value_json.summary from the neighbours topic
//     which is still populated.
// If a future need arises to expose the neighbour list as HA entity attributes
// (e.g., state_attr('sensor.X_neighbors', 'list')), reintroduce a Discovery
// config whose val_tpl computes its state from value_json.list (e.g., list
// length) rather than from a parallel scalar field that can desync.

size_t WifiTelemetry::buildNeighborsSummaryDiscoveryPayload(char* buf, size_t buflen) {
    char state_topic[WIFI_TELEMETRY_TOPIC_MAX];
    buildNeighborsTopic(state_topic, sizeof(state_topic));

    char sw_version[64];
    if (_fw_version && _fw_build_date) {
        snprintf(sw_version, sizeof(sw_version), "%s (%s)", _fw_version, _fw_build_date);
    } else if (_fw_version) {
        snprintf(sw_version, sizeof(sw_version), "%s", _fw_version);
    } else {
        snprintf(sw_version, sizeof(sw_version), "unknown");
    }

    // Text-state sensor, no stat_cla (so no graph). Default entity category
    // (not diagnostic) so it shows in the primary Sensors block on the
    // device card — that's the "glanceable at-a-glance" placement.
    int n = snprintf(buf, buflen,
        "{\"name\":\"Neighbors Summary\","
        "\"uniq_id\":\"%s_neighbors_summary\","
        "\"stat_t\":\"%s\","
        "\"val_tpl\":\"{{ value_json.summary }}\","
        "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"MeshCore\",\"mdl\":\"Repeater\",\"sw_version\":\"%s\",\"hw_version\":\"%s\"}}",
        _node_id,
        state_topic,
        _node_id,
        _friendly_name,
        sw_version,
        _hw_model ? _hw_model : "Heltec V4");

    if (n < 0 || (size_t)n >= buflen) return 0;
    return (size_t)n;
}

// ---------------------------------------------------------------------------
// Battery percentage calculation
// ---------------------------------------------------------------------------
//
// Standard 1S Li-ion discharge curve, piecewise linear approximation.
//
// Reference points:
//   4.20V = 100%, 4.00V = 80%, 3.85V = 60%, 3.70V = 40%, 3.55V = 20%,
//   3.40V = 5%, 3.30V = 0% (cutoff)
//
uint8_t WifiTelemetry::batteryPercent(uint16_t mv) {
    if (mv >= 4200) return 100;
    if (mv >= 4000) return 80 + (uint8_t)(((uint32_t)(mv - 4000) * 20) / 200);
    if (mv >= 3850) return 60 + (uint8_t)(((uint32_t)(mv - 3850) * 20) / 150);
    if (mv >= 3700) return 40 + (uint8_t)(((uint32_t)(mv - 3700) * 20) / 150);
    if (mv >= 3550) return 20 + (uint8_t)(((uint32_t)(mv - 3550) * 20) / 150);
    if (mv >= 3400) return 5  + (uint8_t)(((uint32_t)(mv - 3400) * 15) / 150);
    if (mv >= 3300) return (uint8_t)(((uint32_t)(mv - 3300) * 5) / 100);
    return 0;
}
