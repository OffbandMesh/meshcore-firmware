#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <SafeBoot.h>

#include "MyMesh.h"

#ifdef PIN_STATUS_LED
  // Repeater heartbeat status LED (#9). Active-high default;
  // variants override LED_STATE_ON for active-low boards.
  #ifndef LED_STATE_ON
    #define LED_STATE_ON HIGH
  #endif
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

// ----------------------------------------------------------------------------
// WiFi telemetry module integration (issue #42)
// Active only when -D ENABLE_WIFI_TELEMETRY=1 is set in the build flags.
// All other env builds compile this block to nothing.
// ----------------------------------------------------------------------------
#ifdef ENABLE_WIFI_TELEMETRY
#include <WiFi.h>
#include <string.h>
#include "../../src/helpers/wifi_telemetry/WifiTelemetry.h"
#include "../../src/helpers/wifi_telemetry/WifiMqttTransport.h"
#include "../../src/helpers/wifi_telemetry/RemoteCommand.h"  // issue #86 - remote OTA trigger

#ifndef FIRMWARE_HW_MODEL
#define FIRMWARE_HW_MODEL "Heltec V4"
#endif

// Forward decl: the_mesh is defined further down in this translation unit;
// our telemetry helpers reference it before that definition is reached.
class MyMesh;
extern MyMesh the_mesh;

static WifiMqttTransport* g_tel_transport = nullptr;
static WifiTelemetry g_telemetry;
static uint32_t g_tel_next_publish_ms = 0;
// Captured once at setup so we don't re-query every publish cycle.
static const char* g_tel_reset_reason = "boot";
static char g_tel_repeater_pk_short[3] = {0};  // 2 hex chars + null

// Admin-CLI-controllable state. Kill switch for telemetry, plus running
// counters for diagnostic queries over LoRa admin.
static bool     g_tel_disabled = false;
static uint32_t g_tel_wifi_attempts = 0;
static uint32_t g_tel_wifi_fails = 0;
static uint32_t g_tel_publish_attempts = 0;
static uint32_t g_tel_publish_fails = 0;
static uint32_t g_tel_last_publish_ms = 0;     // millis() of last SUCCESSFUL publish, 0 if never
static int      g_tel_last_mqtt_state = 99;    // PubSubClient state after last attempt

// Persistent WiFi mode (D2 / issue #56). When non-zero, marks the millis()
// deadline at which to auto-revert to BURST mode. Zero = BURST mode (default).
// When persistent, wifi_telemetry_collect_and_publish() skips the final
// transport.end() so WiFi stays up between publish cycles, enabling features
// like OTA (D5) that need persistent connectivity.
static uint32_t g_tel_persistent_until_ms = 0;
// Hard cap on persistent-mode duration to prevent forgotten-on battery drain.
// Even if a CLI command tries to set a longer timeout, it gets clamped here.
#define WIFI_PERSISTENT_MAX_MS (60UL * 60UL * 1000UL)   // 60 minutes

// LoRa#216: standalone cmd-poll timing. Single timer drives both persistent
// and burst modes; interval picked from the right config var based on
// current mode (g_tel_persistent_until_ms). In burst mode, cmd-poll brings
// its own WiFi up-and-down (independent of telemetry publish cycle); in
// persistent mode, transport is already up so cmd-poll is essentially free.
static uint32_t g_cmd_poll_next_ms = 0;
// Runtime-tunable burst-mode interval (CLI: `cmd_poll interval N`).
// Initialized to default; range-checked by setter.
static uint32_t g_cmd_poll_burst_interval_ms = WIFI_CMD_POLL_BURST_INTERVAL_DEFAULT_MS;

// LoRa#216: WiFi-on-time instrumentation. Rolling-window 24h average of how
// much wall-clock time the device had WiFi associated (transport.isReady()
// true). Surfaced in the telemetry state payload as wifi_on_pct_24h so we
// can calibrate cadence-vs-power tradeoffs without external hardware.
// Strategy: on every loop iteration, if transport is ready, accumulate the
// delta since last check; at 24h elapsed, snapshot current accum as the
// "last-24h" value and start a new window. The published value always
// reflects the most recently COMPLETED 24h window (never a partial one),
// so it's a stable signal for power-budget reasoning.
#define WIFI_ON_WINDOW_MS (24UL * 60UL * 60UL * 1000UL)
static uint32_t g_wifi_on_window_start_ms = 0;
static uint32_t g_wifi_on_accum_ms = 0;
static uint32_t g_wifi_on_last_check_ms = 0;
static uint16_t g_wifi_on_pct_last_24h_x100 = 0;  // 0-10000 = 0.00-100.00%

// D6 / issue #60: OTA active-upload extension. While an OTA upload is in
// progress, refresh the persistent-mode deadline on each chunk so a slow
// upload doesn't get cut off when the user's original "wifi on N" window
// expires. A stall in Update.progress() lets the deadline elapse naturally —
// the timeout is a rolling activity window, not a per-session grace.
// AsyncElegantOTA uses ESP-IDF's global Update object (Update.h), so we can
// observe upload state from outside without monkeypatching the deprecated lib.
#include <Update.h>
#ifdef CMD_TRANSPORT_HTTP
  // Issue #188 / H3: HTTP cmd-relay transport. Replaces MQTT cmd channel.
  // Build with -D CMD_TRANSPORT_HTTP=1 to enable (default off until H6).
  #include <HTTPClient.h>
  #include <ArduinoJson.h>
  // Forward declarations - definitions live in the wifi_telemetry helper
  // block below. Needed because wifi_telemetry_collect_and_publish calls
  // wifi_telemetry_http_cmd_poll before the definition appears in source.
  static void wifi_telemetry_http_cmd_poll();
  static bool publishHttpResponseShim(const char*, const char*, void*);
#endif
#define OTA_EXTEND_MS (5UL * 60UL * 1000UL)   // 5 minutes per chunk
static size_t g_ota_last_progress = 0;
// E3 #67: track byte-count milestones so we log progress every ~256 KB.
// Stored as KB so an int comparison is cheap and the value is human-readable.
#define OTA_PROGRESS_KB_STRIDE 256
static size_t g_ota_last_logged_kb_mark = 0;
// E3 #67: track Update.isRunning() across loop iterations so we can detect
// the running->not-running transition that marks Update.end() completion.
static bool g_ota_was_running = false;

// Issue #86: remote command (MQTT-triggered OTA enable) state.
// Singletons - constructed in wifi_telemetry_setup().
class PatioRemoteCallbacks;  // forward decl; full def below extern "C" block
static PatioRemoteCallbacks* g_remote_callbacks = nullptr;
static RemoteCommandHandler* g_remote_cmd_handler = nullptr;
// Track subscription state so we re-subscribe after MQTT reconnect.
static bool g_remote_cmd_subscribed = false;
// Deferred-reboot state: 0 = no pending reboot, otherwise millis() deadline.
static uint32_t g_reboot_deadline_ms = 0;

// Message callback shim that the WifiMqttTransport invokes. Routes incoming
// messages to the handler if topic matches our cmd topic. Topic filter is a
// safety belt; PubSubClient should only deliver subscribed-topic messages.
static bool publishResponseShim(const char* response_topic,
                                 const char* response_payload,
                                 void* user_data);
static void remoteCommandMessageCallback(const char* topic,
                                          const uint8_t* payload,
                                          size_t length,
                                          void* user_data);

// Constructs the remote command handler + callbacks instance and registers
// the message-callback shim with the transport. Called once from
// wifi_telemetry_setup(); defined after PatioRemoteCallbacks below so the
// class type is complete at instantiation.
static void wifi_telemetry_remote_command_setup();

static void wifi_telemetry_setup() {
    g_tel_transport = new WifiMqttTransport(
        WIFI_TELEMETRY_WIFI_SSID,
        WIFI_TELEMETRY_WIFI_PASS,
        WIFI_TELEMETRY_MQTT_HOST,
        (uint16_t)WIFI_TELEMETRY_MQTT_PORT,
        WIFI_TELEMETRY_MQTT_USER,
        WIFI_TELEMETRY_MQTT_PASS,
        WIFI_TELEMETRY_NODE_ID
    );
    g_telemetry.begin(
        g_tel_transport,
        WIFI_TELEMETRY_NODE_ID,
        WIFI_TELEMETRY_MQTT_PREFIX,
        WIFI_TELEMETRY_FRIENDLY_NAME,
        FIRMWARE_VERSION,
        FIRMWARE_BUILD_DATE,
        FIRMWARE_HW_MODEL
    );

    g_tel_reset_reason = board.getResetReasonString(board.getResetReason());

    snprintf(g_tel_repeater_pk_short, sizeof(g_tel_repeater_pk_short),
             "%02x", the_mesh.self_id.pub_key[0]);

    // Issue #86: construct the remote command handler + callbacks, register
    // the message callback with the transport. Subscription to the cmd topic
    // happens later in the publish cycle when MQTT connect is verified.
    wifi_telemetry_remote_command_setup();

    g_tel_next_publish_ms = millis();
}

static void wifi_telemetry_fill_scalar(TelemetryData& d) {
    d.battery_mv = board.getBattMilliVolts();
    d.battery_pct = WifiTelemetry::batteryPercent(d.battery_mv);
    d.uptime_seconds = millis() / 1000UL;
    d.tx_queue_len = 0;
    d.noise_floor_dbm = (int16_t)radio_driver.getNoiseFloor();
#if MAX_NEIGHBOURS
    d.neighbor_count = (uint8_t)the_mesh.getNeighbourCount();
#else
    d.neighbor_count = 0;
#endif
    d.timestamp = (uint32_t)rtc_clock.getCurrentTime();

    d.mcu_temp_c = board.getMCUTemperature();
    d.last_rssi_dbm = (int16_t)radio_driver.getLastRSSI();
    d.last_snr_db = radio_driver.getLastSNR();
    d.packets_recv = radio_driver.getPacketsRecv();
    d.packets_sent = radio_driver.getPacketsSent();
    d.wifi_rssi_dbm = (WiFi.status() == WL_CONNECTED) ? (int16_t)WiFi.RSSI() : 0;
    d.free_heap_b = ESP.getFreeHeap();
    d.reset_reason = g_tel_reset_reason;
    d.wifi_on_pct_24h_x100 = g_wifi_on_pct_last_24h_x100;  // LoRa#216
}

#if MAX_NEIGHBOURS
static void wifi_telemetry_fill_neighbors(TelemetryNeighbors& n) {
    memset(&n, 0, sizeof(n));
    strncpy(n.repeater_pubkey_short, g_tel_repeater_pk_short,
            sizeof(n.repeater_pubkey_short) - 1);

    // Per issue #84: scalar neighbour count is no longer carried in
    // TelemetryNeighbors. It lives in the state topic (TelemetryData) only.
    // List below is the canonical neighbour information published on this
    // topic; consumers derive count from list length if needed.

    NeighbourInfo scratch[WIFI_TELEMETRY_MAX_NEIGHBORS];
    int filled = the_mesh.getNeighbours(scratch, WIFI_TELEMETRY_MAX_NEIGHBORS);
    if (filled < 0) filled = 0;
    if (filled > WIFI_TELEMETRY_MAX_NEIGHBORS) filled = WIFI_TELEMETRY_MAX_NEIGHBORS;
    n.entries_filled = (uint8_t)filled;

    uint32_t now_unix = rtc_clock.getCurrentTime();

    for (int i = 0; i < filled; i++) {
        NeighborEntry& e = n.entries[i];
        const NeighbourInfo& src = scratch[i];

        snprintf(e.pubkey_short, sizeof(e.pubkey_short),
                 "%02x", src.id.pub_key[0]);

        snprintf(e.pubkey_hash, sizeof(e.pubkey_hash),
                 "%02x%02x%02x%02x",
                 src.id.pub_key[PUB_KEY_SIZE - 4],
                 src.id.pub_key[PUB_KEY_SIZE - 3],
                 src.id.pub_key[PUB_KEY_SIZE - 2],
                 src.id.pub_key[PUB_KEY_SIZE - 1]);

        e.snr_db = (float)src.snr / 4.0f;
        e.age_seconds = (now_unix > src.heard_timestamp)
                          ? (now_unix - src.heard_timestamp)
                          : 0;
    }

    if (n.entries_filled == 0) {
        snprintf(n.summary, sizeof(n.summary), "none");
    } else {
        const int kMaxShow = 8;
        int show = (filled < kMaxShow) ? filled : kMaxShow;
        // Per issue #84: total_count removed; use entries_filled as the prefix
        // (the count of nodes actually listed below). Same value as total_count
        // had been in the common non-capped case, and consistent with the rest
        // of this payload (which now omits a duplicate scalar count field).
        int pos = snprintf(n.summary, sizeof(n.summary), "%u: ",
                           (unsigned)n.entries_filled);
        for (int i = 0; i < show; i++) {
            if (pos >= (int)sizeof(n.summary) - 5) break;
            int w = snprintf(n.summary + pos, sizeof(n.summary) - pos,
                             "%s%s",
                             i == 0 ? "" : ", ",
                             n.entries[i].pubkey_short);
            if (w < 0) break;
            pos += w;
        }
        if (filled > show && pos < (int)sizeof(n.summary) - 4) {
            snprintf(n.summary + pos, sizeof(n.summary) - pos, "...");
        }
    }
}
#endif // MAX_NEIGHBOURS

static void wifi_telemetry_collect_and_publish() {
    g_tel_wifi_attempts++;
    bool transport_up = g_tel_transport->begin();
    g_tel_last_mqtt_state = g_tel_transport->getMqttState();
    if (!transport_up) {
        g_tel_wifi_fails++;
        // Don't try to publish; ensure clean teardown for next cycle.
        g_tel_transport->end();
        return;
    }

    TelemetryData d;
    wifi_telemetry_fill_scalar(d);
    g_tel_publish_attempts++;
    bool published = g_telemetry.sample(d);
    if (published) {
        g_tel_last_publish_ms = millis();
    } else {
        g_tel_publish_fails++;
    }

#if MAX_NEIGHBOURS
    TelemetryNeighbors n;
    wifi_telemetry_fill_neighbors(n);
    g_telemetry.publishNeighbors(n);
#endif

    static bool first_cycle_completed = false;
    if (!first_cycle_completed && g_tel_transport->isReady()) {
        delay(3000);
        wifi_telemetry_fill_scalar(d);
        g_tel_publish_attempts++;
        if (g_telemetry.sample(d)) {
            g_tel_last_publish_ms = millis();
        } else {
            g_tel_publish_fails++;
        }
#if MAX_NEIGHBOURS
        wifi_telemetry_fill_neighbors(n);
        g_telemetry.publishNeighbors(n);
#endif
        first_cycle_completed = true;
    }

    // LoRa#216: HTTP cmd-poll was previously called HERE inside collect_and_publish
    // (H3 #195). It has moved to its own standalone check in wifi_telemetry_loop()
    // so cmd-poll cadence can be tuned independently of telemetry publish cadence.
    // Dispatched cmds that promote to persistent mode (ota_enable, wifi_keepalive)
    // still work the same way: dispatch sets g_tel_persistent_until_ms, then the
    // teardown check below sees the non-zero value and skips end().

    // Issue #86: MQTT cmd-topic subscribe (legacy path). Kept active during
    // the H3-to-H7 transition for compatibility. The burst-mode drain bug
    // (#187) means cmds delivered via this path during burst are lost; the
    // HTTP path above is the working replacement. H7 will remove this.
    if (g_remote_cmd_handler && !g_remote_cmd_subscribed && g_tel_transport->isReady()) {
        char cmd_topic[64];
        g_remote_cmd_handler->buildCmdTopic(cmd_topic, sizeof(cmd_topic));
        if (g_tel_transport->subscribe(cmd_topic, /* qos */ 1)) {
            g_remote_cmd_subscribed = true;
        }
    }

    // Persistent-mode hook (D2 / issue #56): when persistent timer is active,
    // skip the teardown so WiFi stays up between publish cycles. The
    // wifi_telemetry_loop()'s timeout check (below) handles auto-revert.
    if (g_tel_persistent_until_ms == 0) {
        g_tel_transport->end();
        // Issue #86: re-subscribe on next connect since the broker session ends
        // with the transport teardown.
        g_remote_cmd_subscribed = false;
    }
}

static void wifi_telemetry_loop() {
    g_telemetry.loop();
    // Drive incoming MQTT messages through PubSubClient's poll. Required for
    // the issue-#86 remote command callback to fire when persistent-mode WiFi
    // is up. No-op when transport not ready.
    if (g_tel_transport) g_tel_transport->loop();

    // Issue #86: deferred reboot check. Set by PatioRemoteCallbacks::rebootAfter
    // (called from REBOOT command dispatch). Honored regardless of telemetry
    // kill-switch state — a remote reboot command should always reboot.
    if (g_reboot_deadline_ms != 0 &&
        (int32_t)(millis() - g_reboot_deadline_ms) >= 0) {
        Serial.println("[#86] deferred reboot deadline reached, rebooting");
        board.reboot();
        // not reached
    }

    // Honor the admin kill-switch — skip everything when telemetry is disabled.
    if (g_tel_disabled) return;

    // D6 / issue #60: OTA active-upload extension.
    // Refresh the persistent-mode deadline whenever Update.progress() advances.
    // Runs BEFORE the expiry check so a chunk arriving close to the deadline
    // gets the extension applied before we'd otherwise tear the transport down.
    // No-op when no OTA is in progress (Update.isRunning() == false).
    bool ota_now_running = Update.isRunning();
    if (g_tel_persistent_until_ms != 0 && ota_now_running) {
        size_t now_progress = Update.progress();
        if (now_progress != g_ota_last_progress) {
            if (g_ota_last_progress == 0) {
                Serial.println("[OTA] upload started, auto-extending wifi window");
                // E3 #67: persistent forensic record of OTA start.
                board.appendSafetyEvent(mesh::EVT_OTA_START, "extend_5min");
            }
            g_ota_last_progress = now_progress;
            g_tel_persistent_until_ms = millis() + OTA_EXTEND_MS;
            // E3 #67: log every OTA_PROGRESS_KB_STRIDE KB so a stalled or
            // aborted upload leaves a trail showing how far it got.
            size_t now_kb = now_progress / 1024;
            while (now_kb >= g_ota_last_logged_kb_mark + OTA_PROGRESS_KB_STRIDE) {
                g_ota_last_logged_kb_mark += OTA_PROGRESS_KB_STRIDE;
                char d[20];
                snprintf(d, sizeof(d), "%uKB", (unsigned)g_ota_last_logged_kb_mark);
                board.appendSafetyEvent(mesh::EVT_OTA_PROGRESS, d);
            }
        }
    }
    // E3 #67: detect the running->not-running transition which marks
    // Update.end() completion. AsyncElegantOTA's restart() fires ~1s later;
    // this log line is the last persistent event before reboot.
    if (g_ota_was_running && !ota_now_running) {
        const char* d = Update.hasError() ? "Update.end_err" : "Update.end_ok";
        board.appendSafetyEvent(mesh::EVT_OTA_RESTART, d);
    }
    g_ota_was_running = ota_now_running;

    // D2 / issue #56: persistent-mode auto-revert.
    // If the persistent timer is active and has expired, tear down WiFi and
    // revert to BURST mode. The actual transport.end() happens here (the
    // collect_and_publish skips end() while persistent is non-zero).
    if (g_tel_persistent_until_ms != 0 &&
        (int32_t)(millis() - g_tel_persistent_until_ms) >= 0) {
        if (g_tel_transport) g_tel_transport->end();
        g_tel_persistent_until_ms = 0;
        g_ota_last_progress = 0;        // D6: reset for next OTA session
        g_ota_last_logged_kb_mark = 0;  // E3: reset progress milestone tracker
        // Issue #86: transport teardown means cmd-topic subscription is gone;
        // mark for re-subscribe on next connect.
        g_remote_cmd_subscribed = false;
        Serial.println("[WTEL] persistent timer expired, reverted to BURST");
    }

    // LoRa#216: WiFi-on-time accumulator. Runs every loop iteration; cheap.
    // Counts wall-clock time spent with transport.isReady() == true into the
    // current 24h window. On rollover, snapshots the completed-window % into
    // g_wifi_on_pct_last_24h_x100 (the value telemetry publishes).
    {
        uint32_t now = millis();
        if (g_wifi_on_window_start_ms == 0) {
            g_wifi_on_window_start_ms = now;
            g_wifi_on_last_check_ms = now;
        } else if (g_tel_transport && g_tel_transport->isReady()) {
            g_wifi_on_accum_ms += (now - g_wifi_on_last_check_ms);
        }
        g_wifi_on_last_check_ms = now;
        if ((uint32_t)(now - g_wifi_on_window_start_ms) >= WIFI_ON_WINDOW_MS) {
            // Snapshot completed window's % (x100 for 0.01% precision).
            g_wifi_on_pct_last_24h_x100 = (uint16_t)(
                ((uint64_t)g_wifi_on_accum_ms * 10000ULL) / WIFI_ON_WINDOW_MS);
            g_wifi_on_accum_ms = 0;
            g_wifi_on_window_start_ms = now;
        }
    }

#ifdef CMD_TRANSPORT_HTTP
    // LoRa#216: standalone cmd-poll, mode-aware.
    // Persistent mode (g_tel_persistent_until_ms != 0): transport is already
    //   up; we just call the poll function. Interval = PERSISTENT_INTERVAL_MS.
    // Burst mode (g_tel_persistent_until_ms == 0): we bring up transport,
    //   poll, and tear down (unless a dispatched cmd just promoted us to
    //   persistent — re-check g_tel_persistent_until_ms before teardown).
    //   Interval = g_cmd_poll_burst_interval_ms (CLI-tunable, default = telemetry interval).
    if ((int32_t)(millis() - g_cmd_poll_next_ms) >= 0 && g_tel_transport != nullptr) {
        bool was_persistent_before = (g_tel_persistent_until_ms != 0);
        bool was_ready_before      = g_tel_transport->isReady();
        // Bring transport up if it isn't (burst-mode cmd-poll cycle).
        if (!was_ready_before) {
            g_tel_wifi_attempts++;
            if (!g_tel_transport->begin()) {
                g_tel_wifi_fails++;
            }
        }
        if (g_tel_transport->isReady()) {
            wifi_telemetry_http_cmd_poll();
        }
        // Tear down only if (a) we brought it up, AND (b) we're STILL not in
        // persistent mode — a dispatched cmd may have just promoted us. Use
        // CURRENT value of g_tel_persistent_until_ms, not the pre-poll snapshot.
        if (!was_ready_before && g_tel_persistent_until_ms == 0) {
            g_tel_transport->end();
            // Issue #86: cmd-topic subscription is gone with the transport;
            // mark for re-subscribe on next connect.
            g_remote_cmd_subscribed = false;
        }
        // Schedule next poll using the mode we're in NOW (also captures any
        // mid-cycle promotion to persistent — next poll fires sooner).
        uint32_t interval = (g_tel_persistent_until_ms != 0)
            ? WIFI_CMD_POLL_PERSISTENT_INTERVAL_MS
            : g_cmd_poll_burst_interval_ms;
        g_cmd_poll_next_ms = millis() + interval;
        (void)was_persistent_before;  // captured for potential debug logging
    }
#endif

    if ((int32_t)(millis() - g_tel_next_publish_ms) >= 0) {
        wifi_telemetry_collect_and_publish();
        g_tel_next_publish_ms = millis() + WIFI_TELEMETRY_INTERVAL_MS;
    }
}

// ----------------------------------------------------------------------------
// Admin CLI bridge (called by CommonCLI from the LoRa-admin-reachable side).
// Exposed as C linkage so CommonCLI.cpp can extern them without including
// any C++ telemetry headers.
// ----------------------------------------------------------------------------
extern "C" {

void wifi_telemetry_set_disabled(int disabled) {
    g_tel_disabled = (disabled != 0);
}

int wifi_telemetry_is_disabled(void) {
    return g_tel_disabled ? 1 : 0;
}

void wifi_telemetry_force_now(void) {
    g_tel_next_publish_ms = millis();
}

#ifdef CMD_TRANSPORT_HTTP
// LoRa#216: CLI command "cmd_poll now" — fire cmd-poll on the next loop
// iteration regardless of cadence. Lets operators get cmd response without
// waiting for the timer in either mode.
void wifi_telemetry_cmd_poll_now(void) {
    g_cmd_poll_next_ms = millis();
}

// LoRa#216: CLI command "cmd_poll interval N" — set burst-mode cmd-poll
// cadence to N seconds. Range-checked against min (60s, prevents power
// runaway) and max (24h, sanity). Returns the value actually set in ms
// after clamping so the CLI can echo it. Setting in persistent mode is
// allowed but only takes effect when burst mode resumes.
uint32_t wifi_telemetry_set_cmd_poll_burst_interval(uint32_t seconds) {
    uint32_t ms = (uint32_t)seconds * 1000UL;
    if (ms < WIFI_CMD_POLL_BURST_INTERVAL_MIN_MS) {
        ms = WIFI_CMD_POLL_BURST_INTERVAL_MIN_MS;
    } else if (ms > WIFI_CMD_POLL_BURST_INTERVAL_MAX_MS) {
        ms = WIFI_CMD_POLL_BURST_INTERVAL_MAX_MS;
    }
    g_cmd_poll_burst_interval_ms = ms;
    return ms;
}

uint32_t wifi_telemetry_get_cmd_poll_burst_interval(void) {
    return g_cmd_poll_burst_interval_ms;
}
#endif

void wifi_telemetry_reset_state(void) {
    // Force clean transport state and a fresh publish on next loop pass.
    if (g_tel_transport) g_tel_transport->end();
    g_tel_wifi_attempts = 0;
    g_tel_wifi_fails = 0;
    g_tel_publish_attempts = 0;
    g_tel_publish_fails = 0;
    g_tel_last_publish_ms = 0;
    g_tel_last_mqtt_state = 99;
    g_tel_persistent_until_ms = 0;  // also clear persistent mode
    g_tel_next_publish_ms = millis();
}

// D2 / issue #56: persistent-mode admin API.
// Pass duration in milliseconds. 0 = revert to BURST immediately.
// Values above WIFI_PERSISTENT_MAX_MS get clamped to the hard cap.
void wifi_telemetry_set_persistent(uint32_t duration_ms) {
    g_ota_last_progress = 0;        // D6: reset OTA progress tracker for new session
    g_ota_last_logged_kb_mark = 0;  // E3: reset progress milestone tracker
    if (duration_ms == 0) {
        g_tel_persistent_until_ms = 0;
        // The next wifi_telemetry_loop() iteration will tear down the transport.
        return;
    }
    if (duration_ms > WIFI_PERSISTENT_MAX_MS) {
        duration_ms = WIFI_PERSISTENT_MAX_MS;
    }
    g_tel_persistent_until_ms = millis() + duration_ms;
    // Force an immediate publish cycle so WiFi comes up RIGHT NOW rather
    // than waiting up to 15 min for the next scheduled publish. The
    // collect_and_publish will skip transport.end() because persistent is set.
    g_tel_next_publish_ms = millis();
}

int wifi_telemetry_is_persistent(void) {
    return g_tel_persistent_until_ms != 0 ? 1 : 0;
}

uint32_t wifi_telemetry_persistent_remaining_ms(void) {
    if (g_tel_persistent_until_ms == 0) return 0;
    int32_t remaining = (int32_t)(g_tel_persistent_until_ms - millis());
    return remaining > 0 ? (uint32_t)remaining : 0;
}

int wifi_telemetry_get_status(char* buf, int buflen) {
    unsigned long since_last = (g_tel_last_publish_ms == 0)
                                  ? 0
                                  : (millis() - g_tel_last_publish_ms) / 1000UL;
    const char* state_str = "never";
    if (g_tel_last_publish_ms != 0) state_str = "ok";
    if (g_tel_wifi_fails > 0 && g_tel_publish_attempts == 0) state_str = "wifi_fail";
    if (g_tel_publish_fails > 0) state_str = "publish_fail";

    // Persistent-mode descriptor for the status line.
    char persist[40];
    if (g_tel_persistent_until_ms == 0) {
        snprintf(persist, sizeof(persist), "mode=burst");
    } else {
        uint32_t remain_s = wifi_telemetry_persistent_remaining_ms() / 1000UL;
        snprintf(persist, sizeof(persist), "mode=persistent(%lus left)",
                 (unsigned long)remain_s);
    }

    return snprintf(buf, buflen,
        "wifi a=%lu f=%lu | mqtt a=%lu f=%lu state=%d | last_ok=%lus ago | %s | %s%s",
        (unsigned long)g_tel_wifi_attempts,
        (unsigned long)g_tel_wifi_fails,
        (unsigned long)g_tel_publish_attempts,
        (unsigned long)g_tel_publish_fails,
        g_tel_last_mqtt_state,
        since_last,
        state_str,
        persist,
        g_tel_disabled ? " [DISABLED]" : "");
}

}  // extern "C"

// ----------------------------------------------------------------------------
// Issue #86 (LoRa-xci): MQTT remote-command integration.
//
// PatioRemoteCallbacks bridges RemoteCommandHandler's abstract action API to
// the existing wifi_telemetry helpers and board methods. The class lives
// here (in main.cpp's translation unit) rather than as a separate module so
// it can call the static wifi_telemetry_* helpers + reference globals like
// g_tel_persistent_until_ms.
// ----------------------------------------------------------------------------
class PatioRemoteCallbacks : public RemoteCommandCallbacks {
public:
    bool wifiOn(uint32_t seconds) override {
        // Map seconds to the existing persistent-mode helper which takes ms.
        // The helper internally clamps to WIFI_PERSISTENT_MAX_MS (60 min).
        wifi_telemetry_set_persistent(seconds * 1000UL);
        return true;
    }

    void wifiOff() override {
        wifi_telemetry_set_persistent(0);
    }

    bool otaStart(char* ota_url, size_t ota_url_buflen) override {
        if (!wifi_telemetry_is_persistent()) {
            // Caller should have called wifiOn() first via OTA_ENABLE dispatch
            // (RemoteCommand.cpp dispatch handles wifi+ota together for
            // OTA_ENABLE; if we got here for any other reason it's a logic error)
            snprintf(ota_url, ota_url_buflen, "ERR: WiFi persistent mode not active");
            return false;
        }
        char reply[160];
        bool ok = board.startOTAUpdateOverSTA(the_mesh.getNodePrefs()->node_name,
                                              the_mesh.getNodePrefs()->password,
                                              reply);
        if (!ok) {
            snprintf(ota_url, ota_url_buflen, "ERR: %s", reply);
            return false;
        }
        // startOTAUpdateOverSTA fills `reply` with a status string; for our
        // response we want the URL specifically. Extract via the OTA status
        // call which returns "http://<ip>/update" or similar.
        char status_buf[160];
        board.getOTAStatus(status_buf, sizeof(status_buf));
        snprintf(ota_url, ota_url_buflen, "%s", status_buf);
        return true;
    }

    void otaStop() override {
        board.stopOTAUpdate();
    }

    bool getOtaStatus(char* buf, size_t buflen) override {
        // board.getOTAStatus signature is (char*, size_t) and returns bool indicating running
        return board.getOTAStatus(buf, buflen);
    }

    void rebootAfter(uint32_t delay_ms) override {
        // Defer the reboot so the MQTT response publish has time to flush.
        // Checked in wifi_telemetry_loop() each iteration.
        uint32_t deadline = millis() + delay_ms;
        if (deadline == 0) deadline = 1;  // sentinel "no pending" is 0
        g_reboot_deadline_ms = deadline;
    }

    void getSafetyLog(char* buf, size_t buflen) override {
        board.getSafetyLog(buf, buflen);
    }

    void logSafetyEvent(uint8_t event_type, const char* detail) override {
        board.appendSafetyEvent(event_type, detail);
    }
};

// publishResponseShim: function-pointer adapter so RemoteCommandHandler can
// publish without holding a direct reference to the transport. Avoids any
// header-level coupling between the abstract handler and the concrete MQTT
// transport.
static bool publishResponseShim(const char* response_topic,
                                 const char* response_payload,
                                 void* /* user_data */)
{
    if (g_tel_transport == nullptr) return false;
    // Use retain=false for command responses: we don't want stale responses
    // sitting on the broker after a device reboot.
    return g_tel_transport->publish(response_topic, response_payload, false);
}

// remoteCommandMessageCallback: receives all messages from WifiMqttTransport.
// We filter for the cmd topic and route to the handler. Other topics are
// not expected (we only subscribe to cmd) but defensive filter included.
static void remoteCommandMessageCallback(const char* topic,
                                          const uint8_t* payload,
                                          size_t length,
                                          void* /* user_data */)
{
    if (g_remote_cmd_handler == nullptr) return;

    char expected_topic[64];
    g_remote_cmd_handler->buildCmdTopic(expected_topic, sizeof(expected_topic));
    if (strcmp(topic, expected_topic) != 0) {
        // Not our topic; ignore. PubSubClient delivers all subscribed
        // messages to a single callback, so future additional subscriptions
        // would need to fan out here.
        return;
    }

    g_remote_cmd_handler->onMessage(payload, length,
                                     &publishResponseShim,
                                     /* user_data */ nullptr);
}

#ifdef CMD_TRANSPORT_HTTP
// ----------------------------------------------------------------------------
// HTTP cmd-relay path (Strycher/LoRa#188 / H3).
//
// Replaces the MQTT cmd channel with HTTP polling. During each burst window
// (after publishing telemetry), the device GETs pending cmds from the relay
// server, dispatches each via the existing RemoteCommandHandler::onHttpCmd
// entry point, and POSTs responses back. Server URL injected at build time
// as CMDRELAY_URL (from platformio.local.ini [cmdrelay] url).
//
// Auth: bearer token in Authorization header, same value as
// OTA_TRIGGER_SECRET (server-side DEVICE_BEARER_TOKEN). HTTP-layer auth
// replaces the in-payload auth check the MQTT path uses.
//
// Sequence per burst:
//   GET  CMDRELAY_URL/devices/<node>/cmds              -> [array of cmds]
//   for each cmd: onHttpCmd(cmd_json) -> calls publishHttpResponseShim
//   POST CMDRELAY_URL/devices/<node>/responses         <- response payload
// ----------------------------------------------------------------------------

static bool publishHttpResponseShim(const char* /* response_topic_unused */,
                                      const char* response_payload,
                                      void* /* user_data */)
{
    // The response_payload is a complete JSON object built by
    // RemoteCommandHandler::dispatch (or publishReject). For HTTP it
    // already includes cmd_id thanks to the H3 refactor.
    HTTPClient http;
    String url = String(CMDRELAY_URL) + "/devices/" + String(WIFI_TELEMETRY_NODE_ID) + "/responses";
    if (!http.begin(url)) {
        Serial.println("[HTTP-CMD] response POST: begin() failed");
        return false;
    }
    http.addHeader("Authorization", String("Bearer ") + OTA_TRIGGER_SECRET);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(response_payload);
    http.end();
    if (code < 200 || code >= 300) {
        Serial.printf("[HTTP-CMD] response POST returned %d\n", code);
        return false;
    }
    return true;
}

static void wifi_telemetry_http_cmd_poll() {
    if (g_remote_cmd_handler == nullptr) return;
    if (g_tel_transport == nullptr || !g_tel_transport->isReady()) return;

    HTTPClient http;
    String url = String(CMDRELAY_URL) + "/devices/" + String(WIFI_TELEMETRY_NODE_ID) + "/cmds";
    if (!http.begin(url)) {
        Serial.println("[HTTP-CMD] cmd poll: begin() failed");
        return;
    }
    http.addHeader("Authorization", String("Bearer ") + OTA_TRIGGER_SECRET);
    http.setTimeout(5000);

    int code = http.GET();
    if (code != 200) {
        // 401 -> auth misconfig; >= 500 -> server problem; -1 -> network.
        // None of these are fatal -- we just skip the poll for this burst.
        if (code > 0) {
            Serial.printf("[HTTP-CMD] cmd poll returned %d\n", code);
        }
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    // Parse the array of pending cmds. Each cmd has shape
    // {cmd_id, ts_queued, action, params: {...}}.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[HTTP-CMD] cmd response JSON parse failed: %s\n", err.c_str());
        return;
    }
    if (!doc.is<JsonArray>()) {
        Serial.println("[HTTP-CMD] cmd response not a JSON array");
        return;
    }

    JsonArray cmds = doc.as<JsonArray>();
    int n = 0;
    for (JsonObject cmd : cmds) {
        // Dispatch via the existing handler. publishHttpResponseShim posts
        // the response back to the server (which closes the cmd in flight
        // -> completed). Any action that needs persistent WiFi (ota_enable,
        // wifi_keepalive) will call _callbacks.wifiOn(N) which sets
        // g_tel_persistent_until_ms; the existing main-loop persistent-mode
        // lifecycle (line 349-363) then keeps WiFi up beyond this burst.
        g_remote_cmd_handler->onHttpCmd(cmd, &publishHttpResponseShim, nullptr);
        n++;
    }
    if (n > 0) {
        Serial.printf("[HTTP-CMD] processed %d cmd(s) from poll\n", n);
    }
}
#endif // CMD_TRANSPORT_HTTP

// Constructs the remote command handler + callbacks, registers the
// transport-level message callback. Called once from wifi_telemetry_setup().
static void wifi_telemetry_remote_command_setup() {
    g_remote_callbacks = new PatioRemoteCallbacks();
    g_remote_cmd_handler = new RemoteCommandHandler(
        WIFI_TELEMETRY_NODE_ID,
        WIFI_TELEMETRY_MQTT_PREFIX,
        *g_remote_callbacks
    );
    if (g_tel_transport) {
        g_tel_transport->setMessageCallback(&remoteCommandMessageCallback,
                                             /* user_data */ nullptr);
    }
}

#endif // ENABLE_WIFI_TELEMETRY

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[160];

// For power saving
unsigned long lastActive = 0; // mark last active time
unsigned long nextSleepinSecs = 120; // next sleep in seconds. The first sleep (if enabled) is after 2 minutes from boot

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
static unsigned long userBtnDownAt = 0;
#define USER_BTN_HOLD_OFF_MILLIS 1500
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

  // SafeBoot: pre-init power guard. Must run before board.begin()
  // (which enables LoRa TCXO, display, sensors). On low battery the
  // MCU sleeps with exponential backoff and does not return.
  SafeBoot::checkAndMaybeSleep();

  board.begin();

#ifdef PIN_STATUS_LED
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, !LED_STATE_ON);   // start off
#endif

  // D9 / issue #63: app-level boot-rollback safety. Runs AFTER Serial.begin()
  // (so we can log) and AFTER board.begin() (so platform init is settled), but
  // BEFORE any radio/mesh/WiFi work (those are the things that can crash). If
  // the NVS boot counter has tripped, this call reboots into the other partition
  // and never returns. Otherwise it caches PENDING_VERIFY state for loop().
  board.beginBootSafety();

#if defined(MESH_DEBUG) && defined(NRF52_PLATFORM)
  // give some extra time for serial to settle so
  // boot debug messages can be seen on terminal
  delay(5000);
#endif

  // For power saving
  lastActive = millis(); // mark last active time since boot

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    MESH_DEBUG_PRINTLN("Radio init failed!");
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

#ifdef ENABLE_WIFI_TELEMETRY
  wifi_telemetry_setup();
#endif
}

void loop() {
#ifdef PIN_STATUS_LED
  // Heartbeat: brief blip = alive (#9). No unread-message
  // logic (repeaters carry none). Cadence follows the loop rate; under nRF52
  // powersave it blips on each wake.
  {
    static unsigned long s_led_next = 0;
    static bool s_led_on = false;
    unsigned long led_now = millis();
    if ((long)(led_now - s_led_next) >= 0) {
      s_led_on = !s_led_on;
      s_led_next = led_now + (s_led_on ? 40UL : 2960UL);   // ~40ms on / ~3s off
      digitalWrite(PIN_STATUS_LED, s_led_on ? LED_STATE_ON : !LED_STATE_ON);
    }
  }
#endif
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    Serial.print('\n');
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
  // Hold the user button to power off the SenseCAP Solar repeater.
  int btnState = digitalRead(PIN_USER_BTN);
  if (btnState == LOW) {
    if (userBtnDownAt == 0) {
      userBtnDownAt = millis();
    } else if ((unsigned long)(millis() - userBtnDownAt) >= USER_BTN_HOLD_OFF_MILLIS) {
      Serial.println("Powering off...");
      board.powerOff();  // does not return
    }
  } else {
    userBtnDownAt = 0;
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

#ifdef ENABLE_WIFI_TELEMETRY
  wifi_telemetry_loop();
#endif

  // D9 / issue #63: one-shot boot validation. After ROLLBACK_VALIDATION_MS of
  // healthy uptime (the_mesh.loop() and friends have been ticking), reset the
  // NVS boot counter and (if pending) tell the bootloader this image is good.
  // Runs ONCE per boot. Placed at end of loop() so the_mesh/sensors/wifi work
  // is part of the implicit self-check signal.
#ifndef ROLLBACK_VALIDATION_MS
#define ROLLBACK_VALIDATION_MS 60000UL   // 60s default; override per-env
#endif
  static bool s_boot_validated = false;
  if (!s_boot_validated && millis() > ROLLBACK_VALIDATION_MS) {
    board.markBootValid();
    s_boot_validated = true;
  }

  if (the_mesh.getNodePrefs()->powersaving_enabled && !the_mesh.hasPendingWork()) {
    #if defined(NRF52_PLATFORM)
    board.sleep(1800); // nrf ignores seconds param, sleeps whenever possible
    #else
    if (the_mesh.millisHasNowPassed(lastActive + nextSleepinSecs * 1000)) { // To check if it is time to sleep
      board.sleep(1800);             // To sleep. Wake up after 30 minutes or when receiving a LoRa packet
      lastActive = millis();
      nextSleepinSecs = 5;  // Default: To work for 5s and sleep again
    } else {
      nextSleepinSecs += 5; // When there is pending work, to work another 5s
    }
    #endif
  }
}
