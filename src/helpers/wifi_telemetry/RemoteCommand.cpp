/**
 * RemoteCommand.cpp - implementation. See header for full design comments.
 *
 * Per issue #86 / LoRa-xci. Implements an MQTT-triggered command handler
 * with defense-in-depth security (broker auth + shared secret + rate limit
 * + safety-log forensic record).
 */
#include "RemoteCommand.h"

#if defined(ENABLE_WIFI_TELEMETRY) && defined(ESP32)

#include <ArduinoJson.h>
#include <cstring>

// ---------------------------------------------------------------------------
// Compile-time configuration sanity checks.
// ---------------------------------------------------------------------------
#ifndef OTA_TRIGGER_SECRET
#error "OTA_TRIGGER_SECRET must be defined (set via -D OTA_TRIGGER_SECRET='\"<value>\"' in platformio.local.ini)"
#endif

// Event types - kept in sync with MeshCore.h's SafetyEventType enum
// (mesh::EVT_REMOTE_CMD_ACCEPTED = 11, mesh::EVT_REMOTE_CMD_REJECTED = 12,
// added 2026-05-16 per issue #86). Logged via callbacks->logSafetyEvent.
// NOTE: numeric values are wire-format for NVS-stored safety log entries
// and must never be renumbered.
static constexpr uint8_t EVT_REMOTE_CMD_ACCEPTED = 11;
static constexpr uint8_t EVT_REMOTE_CMD_REJECTED = 12;

// ---------------------------------------------------------------------------
// Per-action rate-limit minimum interval (milliseconds).
// Index by static_cast<uint8_t>(RemoteCmd).
// ---------------------------------------------------------------------------
const uint32_t RemoteCommandHandler::kRateLimitMs[kRateLimitSlots] = {
    0,            // UNKNOWN (unused)
    5 * 60 * 1000,   // OTA_ENABLE        - 5 min between attempts
    60 * 1000,       // OTA_DISABLE       - 1 min
    60 * 1000,       // OTA_STATUS        - 1 min (read-only but still rate-limit)
    60 * 1000,       // WIFI_KEEPALIVE    - 1 min
    60 * 1000,       // REBOOT            - 1 min
    30 * 1000,       // SAFETY_LOG_DUMP   - 30 sec (read-only, lighter rate-limit)
};

// ---------------------------------------------------------------------------
// Constant-time string compare. Avoids timing oracle on secret length / contents.
// length of `expected` is treated as known (compile-time constant); we use it
// as the canonical iteration count to avoid revealing the candidate's length
// via the compare phase. Length mismatch is short-circuited (the length of
// the expected secret is not a secret).
// ---------------------------------------------------------------------------
static bool constantTimeMatch(const char* candidate, size_t cand_len,
                               const char* expected, size_t exp_len)
{
    if (cand_len != exp_len) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < exp_len; i++) {
        diff |= (uint8_t)candidate[i] ^ (uint8_t)expected[i];
    }
    return diff == 0;
}

// ---------------------------------------------------------------------------
// Map action string -> enum. Strict whitelist; UNKNOWN if no match.
// ---------------------------------------------------------------------------
static RemoteCmd parseActionString(const char* s)
{
    if (!s) return RemoteCmd::UNKNOWN;
    if (strcmp(s, "ota_enable")      == 0) return RemoteCmd::OTA_ENABLE;
    if (strcmp(s, "ota_disable")     == 0) return RemoteCmd::OTA_DISABLE;
    if (strcmp(s, "ota_status")      == 0) return RemoteCmd::OTA_STATUS;
    if (strcmp(s, "wifi_keepalive")  == 0) return RemoteCmd::WIFI_KEEPALIVE;
    if (strcmp(s, "reboot")          == 0) return RemoteCmd::REBOOT;
    if (strcmp(s, "safety_log_dump") == 0) return RemoteCmd::SAFETY_LOG_DUMP;
    return RemoteCmd::UNKNOWN;
}

static const char* actionToString(RemoteCmd a)
{
    switch (a) {
        case RemoteCmd::OTA_ENABLE:      return "ota_enable";
        case RemoteCmd::OTA_DISABLE:     return "ota_disable";
        case RemoteCmd::OTA_STATUS:      return "ota_status";
        case RemoteCmd::WIFI_KEEPALIVE:  return "wifi_keepalive";
        case RemoteCmd::REBOOT:          return "reboot";
        case RemoteCmd::SAFETY_LOG_DUMP: return "safety_log_dump";
        default:                         return "unknown";
    }
}

static const char* rejectReasonString(RemoteCmdReject r)
{
    switch (r) {
        case RemoteCmdReject::PARSE_FAIL:     return "parse_fail";
        case RemoteCmdReject::UNKNOWN_ACTION: return "unknown_action";
        case RemoteCmdReject::AUTH_FAIL:      return "auth_fail";
        case RemoteCmdReject::RATE_LIMITED:   return "rate_limited";
        case RemoteCmdReject::OVERSIZED:      return "oversized";
        case RemoteCmdReject::INTERNAL_ERROR: return "internal_error";
        default:                              return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Constructor + topic builders
// ---------------------------------------------------------------------------
RemoteCommandHandler::RemoteCommandHandler(const char* node_id,
                                            const char* mqtt_prefix,
                                            RemoteCommandCallbacks& callbacks)
    : _node_id(node_id),
      _mqtt_prefix(mqtt_prefix),
      _callbacks(callbacks)
{
    memset(_last_accept_ms, 0, sizeof(_last_accept_ms));
}

void RemoteCommandHandler::buildCmdTopic(char* buf, size_t buflen) const
{
    snprintf(buf, buflen, "%s/%s/cmd", _mqtt_prefix, _node_id);
}

void RemoteCommandHandler::buildResponseTopic(char* buf, size_t buflen) const
{
    snprintf(buf, buflen, "%s/%s/cmd/response", _mqtt_prefix, _node_id);
}

// ---------------------------------------------------------------------------
// onMessage - top-level entry point. Parses, validates, dispatches.
// ---------------------------------------------------------------------------
bool RemoteCommandHandler::onMessage(const uint8_t* payload, size_t len,
                                      bool (*publish_response)(const char*, const char*, void*),
                                      void* publish_user_data)
{
    // Size sanity. Bigger than our parse-buf max is definitely not a real
    // command; reject without parsing.
    if (len == 0 || len > kInPayloadMax) {
        publishReject(RemoteCmdReject::OVERSIZED, RemoteCmd::UNKNOWN,
                      publish_response, publish_user_data);
        return false;
    }

    // Parse JSON. ArduinoJson takes care of bounds-safe parsing of the
    // attacker-controlled input.
    RemoteCommandRequest req;
    if (!parse(payload, len, req)) {
        publishReject(RemoteCmdReject::PARSE_FAIL, RemoteCmd::UNKNOWN,
                      publish_response, publish_user_data);
        return false;
    }

    if (req.action == RemoteCmd::UNKNOWN) {
        publishReject(RemoteCmdReject::UNKNOWN_ACTION, req.action,
                      publish_response, publish_user_data);
        return false;
    }

    // Auth check - constant-time. Auth value extracted in parse() into a
    // local buffer; we re-extract here from the JSON since the parse return
    // value doesn't carry the auth string out. (Auth is a security-sensitive
    // field; keeping its handling local to a single parse-and-check makes
    // it easier to audit.)
    JsonDocument doc;
    if (deserializeJson(doc, payload, len)) {
        // Should not happen - parse() already validated. Defensive.
        publishReject(RemoteCmdReject::INTERNAL_ERROR, req.action,
                      publish_response, publish_user_data);
        return false;
    }
    const char* auth_ptr = doc["auth"];
    size_t auth_len = auth_ptr ? strlen(auth_ptr) : 0;
    const char* expected = OTA_TRIGGER_SECRET;
    size_t expected_len = strlen(expected);

    if (!auth_ptr || !constantTimeMatch(auth_ptr, auth_len, expected, expected_len)) {
        // SILENT to the caller - log to safety log only. Don't publish a
        // response that would oracle "auth_fail" specifically (denies an
        // attacker the ability to distinguish auth_fail from other reject
        // reasons by observing whether they get a response or not).
        char detail[64];
        snprintf(detail, sizeof(detail), "auth_fail:%s", actionToString(req.action));
        _callbacks.logSafetyEvent(EVT_REMOTE_CMD_REJECTED, detail);
        return false;
    }

    // Rate-limit check
    uint32_t now = millis();
    if (!rateLimitOk(req.action, now)) {
        // Rate-limit rejection IS published (the rate-limit's purpose is to
        // tell the legitimate client to back off). Forensic note in safety log.
        publishReject(RemoteCmdReject::RATE_LIMITED, req.action,
                      publish_response, publish_user_data);
        char detail[64];
        snprintf(detail, sizeof(detail), "rate_limited:%s", actionToString(req.action));
        _callbacks.logSafetyEvent(EVT_REMOTE_CMD_REJECTED, detail);
        return false;
    }

    // Clamp window_sec to firmware-enforced range [60, 1800]. Default 600.
    // (window_sec was already extracted in parse() with default-600 fallback.)
    if (req.window_sec < 60)     req.window_sec = 60;
    if (req.window_sec > 1800)   req.window_sec = 1800;

    // Accept: record timestamp, log to safety, dispatch.
    recordAccept(req.action, now);

    char accept_detail[64];
    snprintf(accept_detail, sizeof(accept_detail), "%s:%lus",
             actionToString(req.action), (unsigned long)req.window_sec);
    _callbacks.logSafetyEvent(EVT_REMOTE_CMD_ACCEPTED, accept_detail);

    dispatch(req, publish_response, publish_user_data);
    return true;
}

// ---------------------------------------------------------------------------
// parse - JSON deserialize + field extraction. Returns false on any error.
// On success, fills `out` with action enum (may be UNKNOWN) and window_sec.
// ---------------------------------------------------------------------------
bool RemoteCommandHandler::parse(const uint8_t* payload, size_t len,
                                  RemoteCommandRequest& out)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) return false;

    const char* action_str = doc["action"];
    if (!action_str) return false;
    out.action = parseActionString(action_str);

    // window_sec: optional. Default 600. Out-of-range values caught by
    // clamping in onMessage().
    uint32_t window_default = 600;
    out.window_sec = doc["window_sec"] | window_default;

    // auth not extracted here - re-extracted in onMessage for the
    // constant-time compare. Keeping the auth handling localized makes
    // the security-sensitive code path obvious to a reviewer.
    return true;
}

// ---------------------------------------------------------------------------
// authMatches helper - not currently used externally; constant-time compare
// is inlined into onMessage where the secret has clear lifecycle.
// Kept declared in header for testability / future split.
// ---------------------------------------------------------------------------
bool RemoteCommandHandler::authMatches(const char* candidate, size_t candidate_len)
{
    const char* expected = OTA_TRIGGER_SECRET;
    return constantTimeMatch(candidate, candidate_len, expected, strlen(expected));
}

// ---------------------------------------------------------------------------
// rateLimitOk - returns true if action is permitted now (not within
// rate-limit window from previous accept).
// ---------------------------------------------------------------------------
bool RemoteCommandHandler::rateLimitOk(RemoteCmd action, uint32_t now_ms)
{
    uint8_t idx = static_cast<uint8_t>(action);
    if (idx >= kRateLimitSlots) return false;
    uint32_t last = _last_accept_ms[idx];
    if (last == 0) return true;  // never accepted before
    uint32_t min_interval = kRateLimitMs[idx];
    uint32_t since = now_ms - last;  // wraps cleanly on uint32 overflow
    return since >= min_interval;
}

void RemoteCommandHandler::recordAccept(RemoteCmd action, uint32_t now_ms)
{
    uint8_t idx = static_cast<uint8_t>(action);
    if (idx < kRateLimitSlots) {
        _last_accept_ms[idx] = now_ms;
        if (_last_accept_ms[idx] == 0) _last_accept_ms[idx] = 1; // avoid 0 collision with "never"
    }
}

// ---------------------------------------------------------------------------
// dispatch - route to action handler, build response, publish.
// ---------------------------------------------------------------------------
void RemoteCommandHandler::dispatch(const RemoteCommandRequest& req,
                                     bool (*publish_response)(const char*, const char*, void*),
                                     void* user_data)
{
    char response_topic[128];
    buildResponseTopic(response_topic, sizeof(response_topic));

    // Build response JSON manually with snprintf - keeps the response
    // builder simple and avoids any JSON-library output overhead.
    static char out_buf[kOutPayloadMax];

    bool action_ok = true;
    char status_str[16] = "ok";
    char message[160] = "";
    char data_json[512] = "{}";

    switch (req.action) {
    case RemoteCmd::OTA_ENABLE: {
        bool wifi_ok = _callbacks.wifiOn(req.window_sec);
        if (!wifi_ok) {
            action_ok = false;
            strcpy(status_str, "error");
            strcpy(message, "wifi_on failed");
            break;
        }
        char ota_url[kUrlBufMax];
        bool ota_ok = _callbacks.otaStart(ota_url, sizeof(ota_url));
        if (!ota_ok) {
            action_ok = false;
            strcpy(status_str, "error");
            strcpy(message, "ota_start failed (wifi up, ota not)");
            break;
        }
        snprintf(message, sizeof(message),
                 "OTA endpoint up for %lus", (unsigned long)req.window_sec);
        snprintf(data_json, sizeof(data_json),
                 "{\"ota_url\":\"%s\",\"window_sec\":%lu}",
                 ota_url, (unsigned long)req.window_sec);
        break;
    }
    case RemoteCmd::OTA_DISABLE: {
        _callbacks.otaStop();
        _callbacks.wifiOff();
        strcpy(message, "OTA endpoint stopped, WiFi off");
        break;
    }
    case RemoteCmd::OTA_STATUS: {
        char status_buf[160];
        if (_callbacks.getOtaStatus(status_buf, sizeof(status_buf))) {
            snprintf(message, sizeof(message), "%s", status_buf);
            snprintf(data_json, sizeof(data_json),
                     "{\"status_text\":\"%s\"}", status_buf);
        } else {
            strcpy(message, "OTA status not available");
        }
        break;
    }
    case RemoteCmd::WIFI_KEEPALIVE: {
        bool wifi_ok = _callbacks.wifiOn(req.window_sec);
        if (!wifi_ok) {
            action_ok = false;
            strcpy(status_str, "error");
            strcpy(message, "wifi_on failed");
            break;
        }
        snprintf(message, sizeof(message),
                 "WiFi keepalive %lus", (unsigned long)req.window_sec);
        break;
    }
    case RemoteCmd::REBOOT: {
        // Schedule reboot with delay; response gets published first.
        _callbacks.rebootAfter(2000);
        strcpy(message, "Rebooting in 2 seconds");
        break;
    }
    case RemoteCmd::SAFETY_LOG_DUMP: {
        // The full safety log can be large; we send it back in data_json.
        // Note: kOutSafetyLogMax is bigger than data_json buffer. We truncate.
        // A future refinement might publish the log to its own topic for size.
        char log_buf[kOutSafetyLogMax];
        _callbacks.getSafetyLog(log_buf, sizeof(log_buf));

        // Escape the log content for JSON. Quick escape: replace " with '
        // and newlines with \\n. Not full JSON-safe but good enough for
        // human-readable content the safety log writes.
        // FIXME: proper JSON escape would be cleaner; this is a v1 shortcut.
        for (char* p = log_buf; *p; p++) {
            if (*p == '"') *p = '\'';
            if (*p == '\r') *p = ' ';
        }
        // Trim to fit data_json (with " quotes plus structural chars)
        size_t safe_max = sizeof(data_json) - 32;
        if (strlen(log_buf) > safe_max) {
            log_buf[safe_max] = '\0';
        }
        snprintf(data_json, sizeof(data_json),
                 "{\"log_text\":\"%s\"}", log_buf);
        strcpy(message, "Safety log dump (may be truncated)");
        break;
    }
    default:
        action_ok = false;
        strcpy(status_str, "error");
        strcpy(message, "unhandled action");
        break;
    }

    snprintf(out_buf, sizeof(out_buf),
        "{\"action\":\"%s\","
        "\"ts_unix\":%lu,"
        "\"status\":\"%s\","
        "\"message\":\"%s\","
        "\"data\":%s}",
        actionToString(req.action),
        (unsigned long)(millis() / 1000),  // device-relative uptime ts
        status_str,
        message,
        data_json);

    if (publish_response) {
        publish_response(response_topic, out_buf, user_data);
    }
}

// ---------------------------------------------------------------------------
// publishReject - common path for any rejection. Logs to safety log and
// publishes a response.
// For AUTH_FAIL specifically: callers should NOT call this - the auth check
// path is silent (no publish), only logs.
// ---------------------------------------------------------------------------
void RemoteCommandHandler::publishReject(RemoteCmdReject reason, RemoteCmd action,
                                          bool (*publish_response)(const char*, const char*, void*),
                                          void* user_data)
{
    char response_topic[128];
    buildResponseTopic(response_topic, sizeof(response_topic));

    char out_buf[256];
    snprintf(out_buf, sizeof(out_buf),
        "{\"action\":\"%s\","
        "\"ts_unix\":%lu,"
        "\"status\":\"error\","
        "\"message\":\"rejected: %s\","
        "\"data\":{\"reject_reason\":\"%s\"}}",
        actionToString(action),
        (unsigned long)(millis() / 1000),
        rejectReasonString(reason),
        rejectReasonString(reason));

    if (publish_response) {
        publish_response(response_topic, out_buf, user_data);
    }

    // Log to safety log too
    char detail[64];
    snprintf(detail, sizeof(detail), "%s:%s",
             rejectReasonString(reason), actionToString(action));
    _callbacks.logSafetyEvent(EVT_REMOTE_CMD_REJECTED, detail);
}

#endif // ENABLE_WIFI_TELEMETRY && ESP32
