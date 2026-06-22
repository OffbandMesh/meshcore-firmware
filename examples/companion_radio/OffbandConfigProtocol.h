// examples/companion_radio/OffbandConfigProtocol.h
//
// Offband config command -- companion-API wire CONTRACT (Epic F / F1, #160).
//
// Design-of-record: OffbandMesh/meshcore-firmware#143 (Option 2 -- locked).
// Epic F: #159.  Client mirror: OffbandMesh/meshcore-client #64 (agent DarkBasin).
//
// ONE Offband-owned companion-API command (CMD_OFFBAND_CONFIG) lets the Offband
// client read/write observer + companion settings as key/value, plus a paginated
// read of the 10-slot MQTT broker pool. It is a THIN ADAPTER over
// dispatchObserverCli() -- the exact backend the `_sys` channel uses -- so it
// inherits ConfigSchema validation + secret redaction with zero new logic.
//
// This header is the contract BOTH repos implement; keep it byte-for-byte in sync
// with the client codec (#64).
//
// CODE OWNERSHIP --------------------------------------------------------------
// Offband-owned, deliberately high to stay clear of upstream MeshCore growth:
//   upstream CMD codes today: 1..65 (max 65); 44..49 reserved (WiFi); 40/41 are
//   custom-vars.  upstream RESP codes today: 0..28.
// We never reuse an upstream code and never define inside 44..49. Because the
// command is ours, we are NOT bound by the 140-byte GET_CUSTOM_VARS sub-cap --
// frames use the full MAX_FRAME_SIZE (176).

#pragma once
#include <stdint.h>

// ===========================================================================
// HARDENING GUARD (F7 #168) -- the config command must NOT ship on the
// UNAUTHENTICATED TCP companion.
// ===========================================================================
// main.cpp selects SerialWifiInterface (a WiFiServer on TCP port 5000 with NO
// connection auth) whenever WIFI_SSID is defined -- in preference to PIN-paired
// BLE. CMD_OFFBAND_CONFIG can rewrite WiFi/MQTT-broker credentials and repoint
// the MQTT uplink, so exposing it on that socket = an unauthenticated control
// channel reachable by any host on the device's LAN. Observer builds use runtime
// WiFi (WifiBootstrap) + BLE companion and deliberately omit WIFI_SSID; this
// guard makes that invariant load-bearing instead of a convention. Authenticating
// the TCP companion is the real fix -- tracked in #167 (P1).
#if defined(OFFBAND_OBSERVER) && defined(WIFI_SSID)
#error "Offband observer config command would be exposed on the unauthenticated TCP companion (WIFI_SSID selects SerialWifiInterface :5000, no auth). Build the observer with BLE_PIN_CODE + runtime WiFi (WifiBootstrap), not WIFI_SSID. See #167 / #168."
#endif

namespace offband {

// ---------------------------------------------------------------------------
// Command + response codes (companion-API frame byte [0])
// ---------------------------------------------------------------------------
// Offband extension space starts at 0xC0 -- far above upstream's current max
// (65); leaves 66..191 for upstream growth, 0xC0.. for Offband.
constexpr uint8_t CMD_OFFBAND_CONFIG       = 0xC0;  // request:  cmd_frame[0]
constexpr uint8_t RESP_CODE_OFFBAND_CONFIG = 0xC0;  // response: out_frame[0]

// Capability negotiation: the command exists when the device reports
// FIRMWARE_VER_CODE >= this value (bumped 13 -> 14 in F4). Stock / lower-version
// clients never send CMD_OFFBAND_CONFIG, so they are unaffected (purely additive).
constexpr uint8_t OFFBAND_CONFIG_MIN_VER = 14;

// ---------------------------------------------------------------------------
// Request sub-type -- cmd_frame[1]
// ---------------------------------------------------------------------------
enum OffbandConfigOp : uint8_t {
    OCFG_SET     = 0x00,  // cmd[2..] = "<key> <value>"  (see WIRE ENCODING RULES)
    OCFG_GET     = 0x01,  // cmd[2..] = "<key>"          -> one OCFG_R_VALUE frame
    OCFG_VIEW    = 0x02,  // cmd[2..] = read-only selector ("mqtt status", "mqtt view 0")
    OCFG_BROKERS = 0x03,  // (no payload)                -> paginated broker-pool dump
};

// ---------------------------------------------------------------------------
// Response sub-type -- out_frame[1]
// ---------------------------------------------------------------------------
enum OffbandConfigResp : uint8_t {
    // scalar replies (single frame; out[2..] = NUL-terminated text)
    OCFG_R_ACK   = 0x00,  // SET ok; out[2..] = optional confirmation text
    OCFG_R_ERR   = 0x01,  // any op failed; out[2..] = error text
    OCFG_R_VALUE = 0x02,  // GET ok;  out[2..] = value text (one field, fits one frame)

    // broker-pool dump -- modeled on the contacts START -> ITEM -> END stream.
    // FIELD-granular because one populated slot's non-secret fields total ~513 B,
    // far over MAX_FRAME_SIZE (176): a slot cannot fit in one frame.
    // Empty pool: START(count=0) is sent, immediately followed by END.
    OCFG_R_BROKERS_START = 0x10,  // out[2] = count of populated slots to follow
    OCFG_R_BROKER_KV     = 0x11,  // out[2] = slot index (0..9), out[3..] = "key=value"
    OCFG_R_BROKERS_END   = 0x12,

    // VIEW text dump -- chunked, because `mqtt view`/`status` text can exceed one frame.
    OCFG_R_VIEW_START = 0x20,
    OCFG_R_VIEW_CHUNK = 0x21,  // out[2..] = NUL-terminated text chunk
    OCFG_R_VIEW_END   = 0x22,
};

// ---------------------------------------------------------------------------
// Broker enum values (shared so neither side hard-codes magic numbers)
// ---------------------------------------------------------------------------
// NVS-internal ordinals (the stored enum). On the WIRE, transport/auth_type are
// transmitted as the STRING NAME the firmware CLI parses + returns -- "tcp"/"tls"/
// "wss" and "none"/"basic"/"jwt" -- NOT these numbers.
enum MqttTransport : uint8_t { MQTT_TCP = 0, MQTT_TLS = 1, MQTT_WSS = 2 };
enum MqttAuthType  : uint8_t { MQTT_AUTH_NONE = 0, MQTT_AUTH_BASIC = 1, MQTT_AUTH_JWT = 2 };

// ---------------------------------------------------------------------------
// Device capability bit -- appended to RESP_CODE_DEVICE_INFO (CMD_DEVICE_QUERY)
// ---------------------------------------------------------------------------
// F4 appends ONE `offband_caps` byte to the device-info reply, after the existing
// path_hash_mode byte (additive -- pre-v14 clients read a shorter frame and ignore
// it). The ENTIRE config command is observer-only: its backend (ConfigSchema +
// dispatchObserverCli) compiles only under OFFBAND_OBSERVER, so on a non-observer
// node type there is no config at all -- display config included (the #141/#148
// display prefs are persisted via ConfigSchema, itself observer-only). Version code
// alone cannot tell an observer-out build from an observer-in one, so the client
// gates the WHOLE command -- every key, display included -- on WIFI_OBSERVER_SUPPORT
// (in addition to FIRMWARE_VER_CODE >= 14). There is NO display-without-observer path.
constexpr uint8_t OFFBAND_CAP_WIFI_OBSERVER = 0x01;  // bit 0: config backend (wifi_observer) compiled in

// ---------------------------------------------------------------------------
// Key schema -- the firmware<->client contract surface
// ---------------------------------------------------------------------------
// Flat keys (OCFG_SET / OCFG_GET):
//   wifi.ssid   wifi.pwd(WO)   wifi.enabled
//   mqtt.iata   mqtt.status_interval
//   display.always_on   display.rotation (0|180)
// Broker pool (OCFG_SET per-field; read via OCFG_BROKERS):
//   mqtt.broker.<0-9>.{ enabled, url, port, transport(tcp|tls|wss),
//     auth_type(none|basic|jwt), username, password(WO), topic_prefix,
//     iata_override, jwt_audience, jwt_refresh, jwt_owner, jwt_email, ca_cert }
//   (jwt_token is NOT a config key -- it is live-minted at connect, never set/read.)
//   ACTIONS (F6 #166, OCFG_SET only -- routed to activation/wipe handlers, not stored
//   as plain fields):
//     mqtt.broker.<N>.enabled <0|1> -- activate/deactivate the slot (write LAST; see
//       the recovery handshake below). Read back via OCFG_BROKERS + OCFG_GET.
//     mqtt.broker.<N>.clear <any>   -- wipe the slot (clearBrokerConfig). Value is
//       ignored; write-only (no GET).
// The secret `password` is WRITE-ONLY: never returned by GET/BROKERS; rendered as
// "(set)" / "(unset)" only (existing ConfigSchema redaction).

// ---------------------------------------------------------------------------
// WIRE ENCODING RULES (resolve every parsing ambiguity -- both sides MUST follow)
// ---------------------------------------------------------------------------
// 1. ALL values are ASCII TEXT. Numerics (port, jwt_refresh, enabled,
//    status_interval, rotation) are sent and returned as DECIMAL ASCII strings
//    ("8883", not a binary u16). transport/auth_type are the STRING enum NAMES
//    (tcp|tls|wss ; none|basic|jwt), matching the firmware CLI. No binary anywhere.
// 2. OCFG_SET payload = "<key> <value>" split on the FIRST space ONLY. <value>
//    is the verbatim remainder and MAY contain spaces (e.g. wifi.ssid "My Net");
//    it is NOT quoted and MUST NOT be re-tokenized past the first space.
// 3. OCFG_R_BROKER_KV payload = "<key>=<value>" split on the FIRST '=' ONLY.
//    <value> MAY contain '=' (e.g. a URL query string) -- never split again.
// 4. NUL-TERMINATION: every string payload (OCFG_R_VALUE, OCFG_R_BROKER_KV,
//    OCFG_R_VIEW_CHUNK) is NUL-terminated; payload bytes incl. the NUL never
//    exceed the frame's remaining space (MAX_FRAME_SIZE minus the [0]/[1] header
//    bytes, and minus the [2] slot byte for OCFG_R_BROKER_KV).
// 5. SINGLE-FRAME SCALARS: every flat OCFG_GET value (wifi.ssid<=32, mqtt.iata<=8,
//    status_interval, display.*) and every broker OCFG_R_BROKER_KV "key=value"
//    (largest field url<=128 -> "url=" + 128 + NUL < 176) fits ONE frame by
//    construction. Multi-frame applies ONLY to OCFG_BROKERS and OCFG_VIEW.

// ---------------------------------------------------------------------------
// BROKER-SET RECOVERY HANDSHAKE (decided on #143; field-at-a-time, no
// transactional setBroker -- stays upstream-consistent)
// ---------------------------------------------------------------------------
//   * `enabled` is written LAST = the activation guard. A broker only goes live
//     when enabled=true, so a partial SET leaves the slot DISABLED (never
//     live-corrupt).
//       - Configure: write all fields, then enabled=true only after every field ACKs.
//       - Edit a live broker: enabled=false first -> update fields -> enabled=true last.
//   * Each field SET returns OCFG_R_ACK / OCFG_R_ERR for THAT field, so the client
//     knows exactly which field failed.
//   * On any ERR/timeout the client re-reads true state (OCFG_BROKERS), shows a
//     "partial save" error, and offers retry or discard via the
//     `mqtt.broker.<N>.clear` action (F6 #166 -> clearBrokerConfig) definitive wipe.

}  // namespace offband
