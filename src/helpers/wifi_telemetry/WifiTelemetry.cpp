#include "WifiTelemetry.h"
#include <string.h>
#include <stdio.h>

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
      _discovery_sent(false) {}

void WifiTelemetry::begin(TelemetryTransport* transport,
                          const char* node_id,
                          const char* mqtt_prefix,
                          const char* friendly_name) {
    _transport = transport;
    _node_id = node_id;
    _mqtt_prefix = mqtt_prefix;
    _friendly_name = friendly_name;
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

    return _transport->publish(topic, payload, false);
}

bool WifiTelemetry::publishDiscovery() {
    if (!_transport || !_transport->isReady()) return false;

    // Sensor definitions: id, friendly name, unit, device_class, state_class, value_template
    struct SensorDef {
        const char* id;
        const char* name;
        const char* unit;
        const char* device_class;
        const char* state_class;
        const char* value_template;
    };

    static const SensorDef sensors[] = {
        { "battery_mv", "Battery Voltage", "mV", "voltage", "measurement", "{{ value_json.battery_mv }}" },
        { "battery_pct", "Battery Level", "%", "battery", "measurement", "{{ value_json.battery_pct }}" },
        { "uptime", "Uptime", "s", "duration", "total_increasing", "{{ value_json.uptime_seconds }}" },
        { "tx_queue", "TX Queue Length", "", nullptr, "measurement", "{{ value_json.tx_queue_len }}" },
        { "noise_floor", "Noise Floor", "dBm", "signal_strength", "measurement", "{{ value_json.noise_floor_dbm }}" },
        { "neighbors", "Neighbor Count", "", nullptr, "measurement", "{{ value_json.neighbor_count }}" },
    };

    char topic[WIFI_TELEMETRY_TOPIC_MAX];
    char payload[WIFI_TELEMETRY_PAYLOAD_MAX];

    for (size_t i = 0; i < sizeof(sensors) / sizeof(sensors[0]); i++) {
        const SensorDef& s = sensors[i];
        buildDiscoveryTopic(topic, sizeof(topic), s.id);
        size_t n = buildDiscoveryPayload(payload, sizeof(payload),
                                          s.id, s.name, s.unit,
                                          s.device_class, s.state_class,
                                          s.value_template);
        if (n == 0) return false;
        if (!_transport->publish(topic, payload, true /* retain */)) {
            return false;
        }
    }
    return true;
}

void WifiTelemetry::buildStateTopic(char* buf, size_t buflen) {
    snprintf(buf, buflen, "%s/%s/state", _mqtt_prefix, _node_id);
}

void WifiTelemetry::buildDiscoveryTopic(char* buf, size_t buflen, const char* sensor_id) {
    snprintf(buf, buflen, "homeassistant/sensor/%s/%s/config", _node_id, sensor_id);
}

size_t WifiTelemetry::buildStatePayload(char* buf, size_t buflen, const TelemetryData& d) {
    int n = snprintf(buf, buflen,
        "{\"battery_mv\":%u,"
        "\"battery_pct\":%u,"
        "\"uptime_seconds\":%lu,"
        "\"tx_queue_len\":%u,"
        "\"noise_floor_dbm\":%d,"
        "\"neighbor_count\":%u,"
        "\"timestamp\":%lu}",
        (unsigned)d.battery_mv,
        (unsigned)d.battery_pct,
        (unsigned long)d.uptime_seconds,
        (unsigned)d.tx_queue_len,
        (int)d.noise_floor_dbm,
        (unsigned)d.neighbor_count,
        (unsigned long)d.timestamp);
    if (n < 0 || (size_t)n >= buflen) return 0;
    return (size_t)n;
}

size_t WifiTelemetry::buildDiscoveryPayload(char* buf, size_t buflen,
                                             const char* sensor_id,
                                             const char* name,
                                             const char* unit,
                                             const char* device_class,
                                             const char* state_class,
                                             const char* value_template) {
    // Build state topic for the value_template to reference.
    char state_topic[WIFI_TELEMETRY_TOPIC_MAX];
    buildStateTopic(state_topic, sizeof(state_topic));

    // HA Discovery JSON. Skip device_class field if null.
    int n = snprintf(buf, buflen,
        "{\"name\":\"%s %s\","
        "\"uniq_id\":\"%s_%s\","
        "\"stat_t\":\"%s\","
        "\"val_tpl\":\"%s\","
        "\"unit_of_meas\":\"%s\","
        "%s%s%s"
        "\"stat_cla\":\"%s\","
        "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"MeshCore\",\"mdl\":\"Repeater\"}}",
        _friendly_name, name,
        _node_id, sensor_id,
        state_topic,
        value_template,
        unit ? unit : "",
        device_class ? "\"dev_cla\":\"" : "",
        device_class ? device_class : "",
        device_class ? "\"," : "",
        state_class,
        _node_id,
        _friendly_name);

    if (n < 0 || (size_t)n >= buflen) return 0;
    return (size_t)n;
}

// ---------------------------------------------------------------------------
// Battery percentage calculation
// ---------------------------------------------------------------------------
//
// Standard 1S Li-ion discharge curve, piecewise linear approximation.
// Curve picked to match typical 18650 / LiPo behavior under modest load.
// Tunable later via build flags or a runtime curve.
//
// Reference points:
//   4.20V = 100%
//   4.00V =  80%
//   3.85V =  60%
//   3.70V =  40%
//   3.55V =  20%
//   3.40V =   5%
//   3.30V =   0% (cutoff)
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
