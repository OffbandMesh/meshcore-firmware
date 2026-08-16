// src/helpers/wifi_observer/MqttPayload.h
//
// Shared payload + topic builders for the Offband observer broker pool.
// All brokers (vendored + custom) call these same functions; per-broker
// variation is via the MqttPayloadCtx argument, not via class state.
//
// Output is byte-identical to the original vendored MqttUplink builders
// (locked by scripts/test_observer_status_payload.py golden-JSON test in
// Plan 2 Task 5).
//
// Plan 2 v2 Task 4.

#pragma once
#include "WifiObserverConfig.h"
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Forward-declare MeshCore types to avoid pulling Mesh.h into this header.
// MqttPayload.cpp includes the real headers under #ifdef ARDUINO.
namespace mesh {
    class Packet;
}

namespace offband {

// Re-export the same status snapshot shape the vendored MqttUplink used,
// to keep MeshCore main.cpp's snapshot-build code unchanged when Task 11
// retires MqttUplink.
struct MqttStatusSnapshot {
    int      battery_mv;
    uint32_t uptime_secs;
    uint16_t error_flags;
    uint16_t queue_len;
    int      noise_floor;
    uint32_t tx_air_secs;
    uint32_t rx_air_secs;
    uint32_t recv_errors;
    float    radio_freq;
    float    radio_bw;
    uint8_t  radio_sf;
    uint8_t  radio_cr;
    bool     repeat_enabled;
    // #31 Task D: observer position, selected in main.cpp by advert_loc_policy
    // EXACTLY as the companion advert path selects its location (MyMesh::advert()
    // / createSelfAdvert), so the MQTT /status position always agrees with the
    // advert. Decimal degrees (same double units createSelfAdvert/AdvertDataBuilder
    // consume before their internal *1E6 micro-degree scaling).
    double   node_lat;   // decimal degrees; valid only when loc_valid
    double   node_lon;   // decimal degrees; valid only when loc_valid
    bool     loc_valid;  // false => omit lat/lon from JSON
};

// Per-broker payload context. Filled by MqttBrokerPool when fanning out.
// All pointer fields must be valid for the duration of the build*Json call.
//
// Extension vs. the Plan doc's ctx: added `firmware_version` and `model`
// so the builders don't bake the FIRMWARE_VERSION macro or
// board.getManufacturerName() global into their bodies. The caller fills
// these from their own source; host tests can pass stubs. Documented in
// the Task 4 commit message.
struct MqttPayloadCtx {
    const char* iata;             // effective IATA (per-broker override OR global)
    const char* device_id;        // pubkey-derived identifier (64-char hex from PUB_KEY_SIZE bytes)
    const char* node_name;        // human-readable; builders fall back to device_id if empty
    const char* topic_prefix;     // usually "meshcore"; per-broker overridable
    const char* client_version;   // CLIENT_VERSION macro value (set by caller)
    const char* firmware_version; // FIRMWARE_VERSION macro value (set by caller)
    const char* model;            // raw board.getManufacturerName() (sanitized inside)
};

// Format the canonical topic: "<topic_prefix>/<iata>/<device_id>/<leaf>".
// leaf must be one of {"status", "packets", "raw"}. Writes up to
// dst_size-1 bytes + NUL. If ctx.iata is null/empty, writes empty string.
void formatTopic(char* dst, size_t dst_size, const char* leaf,
                 const MqttPayloadCtx& ctx);

// Status JSON. Returns bytes written (excluding NUL terminator); -1 if
// buf_size is 0 or snprintf fails. Caller-supplied buf must be >= ~600
// bytes to fit the realistic payload.
int buildStatusJson(char* buf, size_t buf_size,
                    const MqttStatusSnapshot& snapshot,
                    const MqttPayloadCtx& ctx, bool online);

// Packet JSON. score == -1 means "omit score/duration fields" (matches
// the vendored two-variant behavior).
// ---------------------------------------------------------------------------
// PacketRecord (#727)
// ---------------------------------------------------------------------------
// What the publish ring stores, INSTEAD of a rendered JSON body.
//
// The ring used to hold the finished /packets JSON. That body embeds
// "raw":"<whole packet in hex>", and hex DOUBLES the packet before ~316 B of
// fields are layered on -- so a 255 B packet became an ~826 B body and every
// ring slot had to be sized for that. This record is the same information
// before rendering: ~296 B instead of ~826 B, so slots shrink ~3x.
//
// rx_time IS PART OF THE RECORD ON PURPOSE. Rendering moved to drain time, and
// drain can happen a full rotation dwell after the packet arrived. Calling
// time(nullptr) during the render would stamp a backlogged broker's messages
// with the DRAIN time -- silently wrong timestamps in exactly the rotated-out
// case the ring exists to serve. Capture at receive, carry it through.
//
// POD by design: it is memcpy'd into the ring as opaque bytes.
// MeshCore's MAX_HASH_SIZE, restated rather than #included: this header only
// forward-declares mesh::Packet on purpose, to stay light. A static_assert in
// MqttPayload.cpp pins this to the real value, so the two cannot drift.
static constexpr size_t kPacketHashSize = 8;

struct PacketRecord {
    uint32_t rx_time;                  // unix seconds, captured AT RECEIVE
    int32_t  rssi;
    float    snr;
    int32_t  score;
    int32_t  duration;
    uint8_t  hash[kPacketHashSize];      // precomputed: the packet is not kept
    uint8_t  payload_type;
    uint8_t  payload_len;
    uint8_t  route_direct;             // 1 = direct route, 0 = flood
    uint8_t  path_hash_count;
    uint8_t  path_hash_size;
    uint8_t  path_byte_len;
    uint16_t raw_len;
    uint8_t  raw[256];
};

// Capture everything the /packets body needs from a live packet, so the packet
// itself does not have to survive until publish time.
void fillPacketRecord(PacketRecord& rec, const mesh::Packet& packet,
                      int rssi, float snr, int score, int duration,
                      uint32_t rx_time);

// Render the /packets body from a stored record. Byte-identical output to
// buildPacketJson() for the same packet -- consumers see no change (#727).
int buildPacketJsonFromRecord(char* buf, size_t buf_size,
                              const PacketRecord& rec, bool is_tx,
                              const MqttPayloadCtx& ctx);


// Raw packet JSON (hex-encoded bytes only, minimal envelope).
int buildRawJson(char* buf, size_t buf_size,
                 const mesh::Packet& packet, bool is_tx,
                 int rssi, float snr,
                 const MqttPayloadCtx& ctx);

// ---------------------------------------------------------------------------
// Helpers exposed for testing + reuse
// ---------------------------------------------------------------------------

// Hex-encode `src` into `dst` as uppercase pairs ("DE", "AD", ...). Always
// NUL-terminates dst. Stops if dst would overflow.
void bytesToHexUpper(const uint8_t* src, size_t len,
                     char* dst, size_t dst_size);

// ISO-8601 timestamp ("YYYY-MM-DDTHH:MM:SS.000000") in LOCAL time.
// Matches the vendored behavior exactly (note: vendored uses localtime_r,
// not gmtime_r, despite the name "ISO timestamp").
void formatIsoTimestamp(time_t ts, char* dst, size_t dst_size);

// JSON-string escape. Replaces ", \, \n, \r, \t with their escape sequences.
// Replaces any control char < 0x20 with '_'. Always NUL-terminates output.
void escapeJsonString(const char* input, char* output, size_t output_size);

// Token-safe sanitizer: replaces every non-[A-Za-z0-9_-] char with '_'.
// Used for the "model" field in status JSON.
void makeSafeToken(const char* input, char* output, size_t output_size);

}  // namespace offband
