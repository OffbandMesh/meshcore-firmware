// src/helpers/wifi_observer/MqttPayload.cpp
//
// Plan 2 v2 Task 4: free-function port of MqttUplink's payload + topic
// builders. Implementations preserve the vendored format strings byte-
// for-byte; Task 5's golden-JSON test enforces the contract.
//
// The vendored MqttUplink.cpp still has its own copies of these functions
// until Task 11 retires it. Both copies producing identical bytes is the
// invariant Task 5 locks.

#include "MqttPayload.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>  // std::min

#ifdef ARDUINO
  #include <Arduino.h>
  #include <Mesh.h>          // mesh::Packet, MAX_HASH_SIZE, getPayloadType, etc.
  #include <target.h>        // `board` global (getManufacturerName())
#endif

namespace offband {

// #727: MqttPayload.h restates the hash size to avoid including MeshCore.h.
// Pin it to the real definition here, where MeshCore.h IS in scope.
static_assert(kPacketHashSize == MAX_HASH_SIZE,
              "kPacketHashSize must track MeshCore's MAX_HASH_SIZE");

// ---------------------------------------------------------------------------
// Helpers — ported verbatim from MqttUplink.cpp:371-456
// ---------------------------------------------------------------------------

void makeSafeToken(const char* input, char* output, size_t output_size) {
    if (output_size == 0) {
        return;
    }
    size_t oi = 0;
    for (size_t i = 0; input != nullptr && input[i] != 0 && oi + 1 < output_size; ++i) {
        char c = input[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            output[oi++] = c;
        } else {
            output[oi++] = '_';
        }
    }
    output[oi] = 0;
}

void bytesToHexUpper(const uint8_t* src, size_t len, char* dst, size_t dst_size) {
    if (dst_size == 0) {
        return;
    }
    size_t di = 0;
    for (size_t i = 0; i < len && di + 2 < dst_size; ++i) {
        snprintf(&dst[di], dst_size - di, "%02X", src[i]);
        di += 2;
    }
    dst[std::min(di, dst_size - 1)] = 0;
}

void formatIsoTimestamp(time_t ts, char* dst, size_t dst_size) {
    if (dst_size == 0) {
        return;
    }
    struct tm tm_local;
#ifdef _MSC_VER
    localtime_s(&tm_local, &ts);
#else
    localtime_r(&ts, &tm_local);
#endif
    strftime(dst, dst_size, "%Y-%m-%dT%H:%M:%S", &tm_local);
    size_t len = strlen(dst);
    if (len + 8 < dst_size) {
        memcpy(&dst[len], ".000000", 8);
    }
}

void escapeJsonString(const char* input, char* output, size_t output_size) {
    if (output_size == 0) {
        return;
    }

    size_t oi = 0;
    for (size_t i = 0; input != nullptr && input[i] != 0 && oi + 1 < output_size; ++i) {
        char c = input[i];
        const char* escape = nullptr;
        switch (c) {
            case '\"':  escape = "\\\""; break;
            case '\\':  escape = "\\\\"; break;
            case '\n':  escape = "\\n";  break;
            case '\r':  escape = "\\r";  break;
            case '\t':  escape = "\\t";  break;
            default:    break;
        }

        if (escape != nullptr) {
            for (size_t j = 0; escape[j] != 0 && oi + 1 < output_size; ++j) {
                output[oi++] = escape[j];
            }
            continue;
        }

        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20) {
            output[oi++] = '_';
            continue;
        }
        output[oi++] = c;
    }
    output[oi] = 0;
}

// ---------------------------------------------------------------------------
// formatTopic — ported from MqttUplink.cpp:339-344
// ---------------------------------------------------------------------------
// Vendored: "meshcore/%s/%s/%s" with _prefs.iata + _device_id + leaf.
// Ported: topic prefix now ctx.topic_prefix (with "meshcore" fallback),
// iata from ctx.iata. Skips entirely if ctx.iata is null/empty.

void formatTopic(char* dst, size_t dst_size, const char* leaf,
                 const MqttPayloadCtx& ctx) {
    if (dst == nullptr || dst_size == 0) {
        return;
    }
    if (ctx.iata == nullptr || ctx.iata[0] == '\0') {
        dst[0] = '\0';
        return;
    }
    const char* prefix = (ctx.topic_prefix != nullptr && ctx.topic_prefix[0] != '\0')
                         ? ctx.topic_prefix : "meshcore";
    const char* dev_id = (ctx.device_id != nullptr) ? ctx.device_id : "";
    snprintf(dst, dst_size, "%s/%s/%s/%s", prefix, ctx.iata, dev_id, leaf);
}

// ---------------------------------------------------------------------------
// buildStatusJson — ported from MqttUplink.cpp:585-609
// ---------------------------------------------------------------------------
// All snprintf format strings IDENTICAL to vendored. Field-source mappings:
//   _prefs.iata          -> ctx.iata          (via formatTopic, not used in body)
//   _device_id           -> ctx.device_id
//   _node_name           -> ctx.node_name (falls back to device_id when empty)
//   _last_status         -> snapshot
//   board.getManufacturerName() -> ctx.model  (sanitized via makeSafeToken)
//   FIRMWARE_VERSION     -> ctx.firmware_version
//   CLIENT_VERSION       -> ctx.client_version

int buildStatusJson(char* buf, size_t buf_size,
                    const MqttStatusSnapshot& snapshot,
                    const MqttPayloadCtx& ctx, bool online) {
    if (buf == nullptr || buf_size == 0) return -1;

    char ts[32];
    formatIsoTimestamp(time(nullptr), ts, sizeof(ts));

    char model[48];
    makeSafeToken(ctx.model != nullptr ? ctx.model : "", model, sizeof(model));

    char origin[80];
    const char* node_name = (ctx.node_name != nullptr && ctx.node_name[0] != 0)
                            ? ctx.node_name
                            : (ctx.device_id != nullptr ? ctx.device_id : "");
    escapeJsonString(node_name, origin, sizeof(origin));

    char client_version[96];
    snprintf(client_version, sizeof(client_version), "%s",
             ctx.client_version != nullptr ? ctx.client_version : "");

    char radio_info[48];
    snprintf(radio_info, sizeof(radio_info), "%.6f,%.1f,%u,%u",
             static_cast<double>(snapshot.radio_freq),
             static_cast<double>(snapshot.radio_bw),
             snapshot.radio_sf, snapshot.radio_cr);

    const char* firmware_version = ctx.firmware_version != nullptr ? ctx.firmware_version : "";
    const char* device_id        = ctx.device_id        != nullptr ? ctx.device_id        : "";

    // #31 Task D: optional top-level lat/lon (siblings of "stats"), emitted only
    // when snapshot.loc_valid. The fragment is spliced in via a single "%s" placed
    // immediately before the outer closing brace, so every existing field stays
    // byte-identical and the offline (loc_valid==false => empty fragment) payload
    // is unchanged. Decimal degrees, matching the advert (see MqttPayload.h).
    char pos[48];
    if (snapshot.loc_valid) {
        snprintf(pos, sizeof(pos), ",\"lat\":%.6f,\"lon\":%.6f",
                 snapshot.node_lat, snapshot.node_lon);
    } else {
        pos[0] = '\0';
    }

    return snprintf(buf, buf_size,
                    "{\"status\":\"%s\",\"timestamp\":\"%s\",\"origin\":\"%s\",\"origin_id\":\"%s\","
                    "\"model\":\"%s\",\"firmware_version\":\"%s\",\"radio\":\"%s\",\"client_version\":\"%s\","
                    "\"repeat\":\"%s\",\"stats\":{\"battery_mv\":%d,\"uptime_secs\":%lu,\"errors\":%u,\"queue_len\":%u,"
                    "\"noise_floor\":%d,\"tx_air_secs\":%lu,\"rx_air_secs\":%lu,\"recv_errors\":%lu}%s}",
                    online ? "online" : "offline", ts, origin, device_id, model,
                    firmware_version, radio_info, client_version,
                    snapshot.repeat_enabled ? "on" : "off",
                    snapshot.battery_mv,
                    static_cast<unsigned long>(snapshot.uptime_secs),
                    snapshot.error_flags, snapshot.queue_len,
                    snapshot.noise_floor,
                    static_cast<unsigned long>(snapshot.tx_air_secs),
                    static_cast<unsigned long>(snapshot.rx_air_secs),
                    static_cast<unsigned long>(snapshot.recv_errors),
                    pos);
}

#ifdef ARDUINO
// ---------------------------------------------------------------------------
// Packet builders — ported from MqttUplink.cpp:611-715
// ---------------------------------------------------------------------------
// Only compiled in ARDUINO build because they reference mesh::Packet methods
// (writeTo, calculatePacketHash, isRouteDirect, etc.) and Mesh.h's constants
// (MAX_HASH_SIZE). The host test harness in Task 5 skips packet tests; if
// needed later, a stub Packet in the harness can re-enable host-side tests.

// #727: capture what the body needs while the packet is still alive.
void fillPacketRecord(PacketRecord& rec, const mesh::Packet& packet,
                      int rssi, float snr, int score, int duration,
                      uint32_t rx_time) {
    memset(&rec, 0, sizeof(rec));
    rec.rx_time  = rx_time;      // AT RECEIVE -- see the header comment
    rec.rssi     = rssi;
    rec.snr      = snr;
    rec.score    = score;
    rec.duration = duration;
    int n = packet.writeTo(rec.raw);
    rec.raw_len  = (n > 0 && (size_t)n <= sizeof(rec.raw)) ? (uint16_t)n : 0;
    packet.calculatePacketHash(rec.hash);
    rec.payload_type    = packet.getPayloadType();
    rec.payload_len     = packet.payload_len;
    rec.route_direct    = packet.isRouteDirect() ? 1 : 0;
    rec.path_hash_count = (uint8_t)packet.getPathHashCount();
    rec.path_hash_size  = (uint8_t)packet.getPathHashSize();
    rec.path_byte_len   = (uint8_t)packet.getPathByteLen();
}

int buildPacketJsonFromRecord(char* buf, size_t buf_size,
                              const PacketRecord& rec, bool is_tx,
                              const MqttPayloadCtx& ctx) {
    if (buf == nullptr || buf_size == 0) return -1;

    const int   raw_len = (int)rec.raw_len;
    const int   rssi    = (int)rec.rssi;
    const float snr     = rec.snr;
    const int   score   = (int)rec.score;
    const int   duration = (int)rec.duration;

    // Stack buffer instead of heap alloc (vendored used allocScratchBuffer(520);
    // 520 bytes is well within ESP32 task stack budget).
    char raw_hex[520];
    bytesToHexUpper(rec.raw, (size_t)raw_len, raw_hex, sizeof(raw_hex));

    char hash_hex[(MAX_HASH_SIZE * 2) + 1];
    bytesToHexUpper(rec.hash, MAX_HASH_SIZE, hash_hex, sizeof(hash_hex));

    // #727: the RECEIVE time, not time(nullptr). Drain can be a dwell later.
    time_t now = (time_t)rec.rx_time;
    char ts[32];
    formatIsoTimestamp(now, ts, sizeof(ts));
    struct tm tm_utc;
#ifdef _MSC_VER
    gmtime_s(&tm_utc, &now);
#else
    gmtime_r(&now, &tm_utc);
#endif
    char time_only[16];
    char date_only[16];
    strftime(time_only, sizeof(time_only), "%H:%M:%S", &tm_utc);
    strftime(date_only, sizeof(date_only), "%d/%m/%Y", &tm_utc);

    char origin[80];
    const char* node_name = (ctx.node_name != nullptr && ctx.node_name[0] != 0)
                            ? ctx.node_name
                            : (ctx.device_id != nullptr ? ctx.device_id : "");
    escapeJsonString(node_name, origin, sizeof(origin));

    const char* device_id = ctx.device_id != nullptr ? ctx.device_id : "";

    if (rec.route_direct && rec.path_byte_len > 0) {
        char path_info[128];
        snprintf(path_info, sizeof(path_info), "path_%dx%d_%db",
                 (int)rec.path_hash_count,
                 (int)rec.path_hash_size,
                 (int)rec.path_byte_len);
        if (score >= 0) {
            return snprintf(buf, buf_size,
                "{\"origin\":\"%s\",\"origin_id\":\"%s\",\"timestamp\":\"%s\",\"type\":\"PACKET\","
                "\"direction\":\"%s\",\"time\":\"%s\",\"date\":\"%s\",\"len\":\"%d\",\"packet_type\":\"%u\","
                "\"route\":\"D\",\"payload_len\":\"%u\",\"raw\":\"%s\",\"SNR\":\"%.1f\",\"RSSI\":\"%d\","
                "\"score\":\"%d\",\"duration\":\"%d\",\"hash\":\"%s\",\"path\":\"%s\"}",
                origin, device_id, ts, is_tx ? "tx" : "rx", time_only, date_only, raw_len,
                rec.payload_type, rec.payload_len, raw_hex, snr, rssi, score, duration, hash_hex,
                path_info);
        }
        return snprintf(buf, buf_size,
            "{\"origin\":\"%s\",\"origin_id\":\"%s\",\"timestamp\":\"%s\",\"type\":\"PACKET\","
            "\"direction\":\"%s\",\"time\":\"%s\",\"date\":\"%s\",\"len\":\"%d\",\"packet_type\":\"%u\","
            "\"route\":\"D\",\"payload_len\":\"%u\",\"raw\":\"%s\",\"SNR\":\"%.1f\",\"RSSI\":\"%d\","
            "\"hash\":\"%s\",\"path\":\"%s\"}",
            origin, device_id, ts, is_tx ? "tx" : "rx", time_only, date_only, raw_len,
            rec.payload_type, rec.payload_len, raw_hex, snr, rssi, hash_hex, path_info);
    }

    if (score >= 0) {
        return snprintf(buf, buf_size,
            "{\"origin\":\"%s\",\"origin_id\":\"%s\",\"timestamp\":\"%s\",\"type\":\"PACKET\","
            "\"direction\":\"%s\",\"time\":\"%s\",\"date\":\"%s\",\"len\":\"%d\",\"packet_type\":\"%u\","
            "\"route\":\"F\",\"payload_len\":\"%u\",\"raw\":\"%s\",\"SNR\":\"%.1f\",\"RSSI\":\"%d\","
            "\"score\":\"%d\",\"duration\":\"%d\",\"hash\":\"%s\"}",
            origin, device_id, ts, is_tx ? "tx" : "rx", time_only, date_only, raw_len,
            rec.payload_type, rec.payload_len, raw_hex, snr, rssi, score, duration, hash_hex);
    }
    return snprintf(buf, buf_size,
        "{\"origin\":\"%s\",\"origin_id\":\"%s\",\"timestamp\":\"%s\",\"type\":\"PACKET\","
        "\"direction\":\"%s\",\"time\":\"%s\",\"date\":\"%s\",\"len\":\"%d\",\"packet_type\":\"%u\","
        "\"route\":\"F\",\"payload_len\":\"%u\",\"raw\":\"%s\",\"SNR\":\"%.1f\",\"RSSI\":\"%d\","
        "\"hash\":\"%s\"}",
        origin, device_id, ts, is_tx ? "tx" : "rx", time_only, date_only, raw_len,
        rec.payload_type, rec.payload_len, raw_hex, snr, rssi, hash_hex);
}

int buildRawJson(char* buf, size_t buf_size,
                 const mesh::Packet& packet, bool is_tx,
                 int rssi, float snr,
                 const MqttPayloadCtx& ctx) {
    (void)is_tx;
    (void)rssi;
    (void)snr;
    if (buf == nullptr || buf_size == 0) return -1;

    uint8_t raw[256];
    int raw_len = packet.writeTo(raw);
    char raw_hex[520];
    bytesToHexUpper(raw, raw_len, raw_hex, sizeof(raw_hex));

    char ts[32];
    formatIsoTimestamp(time(nullptr), ts, sizeof(ts));

    char origin[80];
    const char* node_name = (ctx.node_name != nullptr && ctx.node_name[0] != 0)
                            ? ctx.node_name
                            : (ctx.device_id != nullptr ? ctx.device_id : "");
    escapeJsonString(node_name, origin, sizeof(origin));

    const char* device_id = ctx.device_id != nullptr ? ctx.device_id : "";

    return snprintf(buf, buf_size,
        "{\"origin\":\"%s\",\"origin_id\":\"%s\",\"timestamp\":\"%s\",\"type\":\"RAW\",\"data\":\"%s\"}",
        origin, device_id, ts, raw_hex);
}

#else  // !ARDUINO -- host build: provide weak stubs so callers compile without Mesh.h.
       // The host test harness in Task 5 provides a stub mesh::Packet definition;
       // the linker resolves these stubs OR the harness's own packet builders.

void fillPacketRecord(PacketRecord& rec, const mesh::Packet& /*packet*/,
                      int rssi, float snr, int score, int duration,
                      uint32_t rx_time) {
    memset(&rec, 0, sizeof(rec));
    rec.rx_time = rx_time; rec.rssi = rssi; rec.snr = snr;
    rec.score = score; rec.duration = duration;
}

int buildPacketJsonFromRecord(char* buf, size_t buf_size,
                              const PacketRecord& /*rec*/, bool /*is_tx*/,
                              const MqttPayloadCtx& /*ctx*/) {
    if (buf == nullptr || buf_size == 0) return -1;
    return snprintf(buf, buf_size, "{\"type\":\"PACKET-host-stub\"}");
}

int buildRawJson(char* buf, size_t buf_size,
                 const mesh::Packet& /*packet*/, bool /*is_tx*/,
                 int /*rssi*/, float /*snr*/,
                 const MqttPayloadCtx& /*ctx*/) {
    if (buf == nullptr || buf_size == 0) return -1;
    return snprintf(buf, buf_size, "{\"type\":\"RAW-host-stub\"}");
}

#endif  // ARDUINO

}  // namespace offband
