/**
 * RemoteCommand — MQTT-triggered remote command handler for patio Repeater.
 *
 * Subscribes to meshcore/<node>/cmd over the existing wifi_telemetry MQTT
 * connection. Validates incoming commands against a shared secret + rate
 * limit, dispatches to firmware-side action handlers, publishes responses
 * to meshcore/<node>/cmd/response.
 *
 * Per issue #86 / LoRa-xci. Defense-in-depth security model:
 *   - MQTT broker auth (handled by transport)
 *   - Shared secret in every command payload (compile-time, constant-time compare)
 *   - Per-action rate limiting (DoS prevention)
 *   - Safety-log forensic record of every accept and every reject
 *
 * Build dependencies:
 *   - ArduinoJson (added for this feature, ^7.x)
 *   - Existing wifi_telemetry MQTT transport with subscribe API
 *
 * Compile-time configuration (set via build flags in env):
 *   - ENABLE_WIFI_TELEMETRY (required; module is fully #if'd out otherwise)
 *   - OTA_TRIGGER_SECRET (required when ENABLE_WIFI_TELEMETRY is set; the
 *     shared secret string. Compile fails if undefined.)
 *
 * Build flag value example, set in platformio.local.ini (NOT git-committed):
 *   build_flags = -D OTA_TRIGGER_SECRET='"<random-32-char-string>"'
 */
#pragma once

#include "WifiTelemetry.h"

#if defined(ENABLE_WIFI_TELEMETRY) && defined(ESP32)

#include <ArduinoJson.h>

#include <Arduino.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Command enumeration. Matches the public "action" string set the firmware
// accepts in incoming MQTT command payloads.
// ---------------------------------------------------------------------------
enum class RemoteCmd : uint8_t {
    UNKNOWN          = 0,
    OTA_ENABLE       = 1,    // wifi_on + ota_start; window_sec required
    OTA_DISABLE      = 2,    // ota_stop + wifi_off
    OTA_STATUS       = 3,    // read-only; returns getOtaStatus() text
    WIFI_KEEPALIVE   = 4,    // wifi_on (no ota_start); window_sec required
    REBOOT           = 5,    // calls reboot after a brief response-publish delay
    SAFETY_LOG_DUMP  = 6,    // read-only; returns full safety log text
    CLI              = 7,    // #538: run a CLI line (params.cmd) via handleCommand;
                             // post-auth like every action. Used for repeater
                             // broker config (`set mqtt.broker.N.*`, `mqtt enable`).
};

// ---------------------------------------------------------------------------
// Parsed command result. Fields populated only on successful parse + auth.
// Pointers reference internal copies (no aliasing of MQTT-callback bufs).
// ---------------------------------------------------------------------------
struct RemoteCommandRequest {
    RemoteCmd action;
    uint32_t  window_sec;             // clamped [60, 1800] before dispatch
                                       // ignored for actions that don't take a window
    uint32_t  cmd_id;                  // HTTP path: server-assigned cmd_id for response
                                       // correlation. MQTT path: 0 (no correlation needed).
                                       // Included in response payload when nonzero.
    char      cli_cmd[192];            // #538: CLI line for RemoteCmd::CLI (from
                                       // params.cmd); empty for every other action.
};

// ---------------------------------------------------------------------------
// Rejection reasons - logged to safety log for forensic record.
// ---------------------------------------------------------------------------
enum class RemoteCmdReject : uint8_t {
    PARSE_FAIL       = 1,    // malformed JSON / missing required field
    UNKNOWN_ACTION   = 2,    // action not in whitelist
    AUTH_FAIL        = 3,    // shared secret mismatch
    RATE_LIMITED     = 4,    // command came in inside per-action rate-limit window
    OVERSIZED        = 5,    // payload too big for the receive buffer
    INTERNAL_ERROR   = 6,    // dispatch handler returned failure
};

// ---------------------------------------------------------------------------
// Callbacks - the firmware-side action implementations.
// User code (typically main.cpp) provides a concrete instance that maps each
// virtual method to existing helpers/board calls. This module never calls
// hardware-specific code directly.
// ---------------------------------------------------------------------------
class RemoteCommandCallbacks {
public:
    virtual ~RemoteCommandCallbacks() = default;

    // Bring up WiFi for `seconds` (firmware enforces internal clamps if needed).
    // Returns true if the operation was accepted (not necessarily completed).
    virtual bool wifiOn(uint32_t seconds) = 0;

    // Tear down WiFi.
    virtual void wifiOff() = 0;

    // Start OTA HTTP endpoint. Assumes WiFi is up. Returns true on success.
    // On success, ota_url is filled with a NUL-terminated URL string up to
    // ota_url_buflen-1 chars.
    virtual bool otaStart(char* ota_url, size_t ota_url_buflen) = 0;

    // Stop OTA endpoint. WiFi state untouched.
    virtual void otaStop() = 0;

    // Read OTA status text into buf. Returns true if buffer was filled.
    virtual bool getOtaStatus(char* buf, size_t buflen) = 0;

    // Schedule a reboot, allowing approximately `delay_ms` for the caller
    // to publish a response message before the device resets.
    virtual void rebootAfter(uint32_t delay_ms) = 0;

    // Read full safety log text into buf. Truncates if log exceeds buflen.
    virtual void getSafetyLog(char* buf, size_t buflen) = 0;

    // #538: run a CLI line (post-auth) and write its human reply into `reply`.
    // Returns true if executed. Default no-op (false) so non-repeater callback
    // implementations don't have to provide it. The repeater routes this to
    // MyMesh::handleCommand, which reaches the broker-config CLI.
    virtual bool runCli(const char* cmd, char* reply, size_t reply_size) {
        (void)cmd;
        if (reply != nullptr && reply_size > 0) reply[0] = '\0';
        return false;
    }

    // Log an event to the persistent safety log. Used by RemoteCommandHandler
    // for forensic record of every accept and reject. The detail string is
    // copied internally up to a board-determined max length.
    // event_type semantically EVT_REMOTE_CMD or EVT_REMOTE_CMD_REJECTED;
    // pass raw uint8_t so this header doesn't depend on MyMesh.h.
    virtual void logSafetyEvent(uint8_t event_type, const char* detail) = 0;
};

// ---------------------------------------------------------------------------
// RemoteCommandHandler - the module's public class.
//
// Lifecycle:
//   1. Construct (typically singleton, owned by main.cpp or wifi_telemetry)
//   2. setTransport() when MQTT transport is available
//   3. subscribe() once MQTT is connected (idempotent across reconnects)
//   4. dispatch from the MQTT message callback via onMessage()
// ---------------------------------------------------------------------------
class RemoteCommandHandler {
public:
    // Pointers/refs must outlive this object.
    // node_id is used to build the cmd topic: "<mqtt_prefix>/<node_id>/cmd"
    // mqtt_prefix follows the same convention as wifi_telemetry state topic
    RemoteCommandHandler(const char* node_id,
                         const char* mqtt_prefix,
                         RemoteCommandCallbacks& callbacks);

    // Build the canonical cmd-receive topic into buf.
    // E.g., "meshcore/wsmj898-ltb/cmd"
    void buildCmdTopic(char* buf, size_t buflen) const;

    // Build the response topic into buf.
    // E.g., "meshcore/wsmj898-ltb/cmd/response"
    void buildResponseTopic(char* buf, size_t buflen) const;

    // Called by transport / dispatch infrastructure when a message arrives
    // on the cmd topic. payload may not be NUL-terminated.
    // The publish_response callback is invoked to publish the response;
    // RemoteCommandHandler does not hold the transport ref directly so this
    // module remains testable without an actual MQTT client.
    //
    // Returns true if the message was accepted (regardless of dispatch
    // outcome); false if rejected entirely (no response published).
    bool onMessage(const uint8_t* payload, size_t len,
                   bool (*publish_response)(const char* response_topic,
                                            const char* response_payload,
                                            void* user_data),
                   void* publish_user_data);

    // HTTP cmd-relay path entry point (Strycher/LoRa#188). The HTTP transport
    // authenticates devices at the bearer-header layer before delivering the
    // cmd, so this entry skips the in-payload auth check that onMessage uses
    // for MQTT. Extracts cmd_id from the JSON for response correlation; the
    // publish_response callback should POST to the server's /responses
    // endpoint rather than publishing to MQTT.
    //
    // cmd_obj is a single cmd element from the server's GET /cmds response
    // array, shape: {cmd_id, ts_queued, action, params: {...}}.
    bool onHttpCmd(JsonObject cmd_obj,
                   bool (*publish_response)(const char* response_topic_unused,
                                            const char* response_payload,
                                            void* user_data),
                   void* publish_user_data);

private:
    bool parse(const uint8_t* payload, size_t len, RemoteCommandRequest& out);
    bool parseFromJson(JsonObject cmd_obj, RemoteCommandRequest& out);
    bool authMatches(const char* candidate, size_t candidate_len);
    bool rateLimitOk(RemoteCmd action, uint32_t now_ms);
    void recordAccept(RemoteCmd action, uint32_t now_ms);

    // Post-auth dispatch. Both onMessage (after MQTT-layer auth) and
    // onHttpCmd (which has no in-payload auth; HTTP-layer auth done already)
    // converge here. Handles rate-limit, window clamp, accept-log, dispatch.
    bool executeAuthenticated(RemoteCommandRequest& req,
                              bool (*publish_response)(const char*, const char*, void*),
                              void* user_data);

    void dispatch(const RemoteCommandRequest& req,
                  bool (*publish_response)(const char*, const char*, void*),
                  void* user_data);
    void publishReject(RemoteCmdReject reason, RemoteCmd action, uint32_t cmd_id,
                       bool (*publish_response)(const char*, const char*, void*),
                       void* user_data);

    const char* _node_id;
    const char* _mqtt_prefix;
    RemoteCommandCallbacks& _callbacks;

    // Per-action last-accept timestamps for rate limiting.
    // Index by static_cast<uint8_t>(RemoteCmd). Slot 0 (UNKNOWN) unused.
    static constexpr size_t kRateLimitSlots = 8;   // #538: +1 for RemoteCmd::CLI
    uint32_t _last_accept_ms[kRateLimitSlots];

    // Per-action minimum interval between accepts (milliseconds).
    static const uint32_t kRateLimitMs[kRateLimitSlots];

    // Buffers reused across messages. Sized for max expected payload.
    // 256 bytes is comfortably above the worst-case command JSON
    // ({"action":"...","window_sec":99999,"auth":"<32 char secret>"})
    static constexpr size_t kInPayloadMax = 256;
    static constexpr size_t kOutPayloadMax = 768;
    static constexpr size_t kOutSafetyLogMax = 4096;  // for safety_log_dump action
    static constexpr size_t kUrlBufMax = 64;          // for ota_enable response URL field
};

#endif // ENABLE_WIFI_TELEMETRY && ESP32
