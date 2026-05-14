#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <SafeBoot.h>

#include "MyMesh.h"

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

// D6 / issue #60: OTA active-upload extension. While an OTA upload is in
// progress, refresh the persistent-mode deadline on each chunk so a slow
// upload doesn't get cut off when the user's original "wifi on N" window
// expires. A stall in Update.progress() lets the deadline elapse naturally —
// the timeout is a rolling activity window, not a per-session grace.
// AsyncElegantOTA uses ESP-IDF's global Update object (Update.h), so we can
// observe upload state from outside without monkeypatching the deprecated lib.
#include <Update.h>
#define OTA_EXTEND_MS (5UL * 60UL * 1000UL)   // 5 minutes per chunk
static size_t g_ota_last_progress = 0;
// E3 #67: track byte-count milestones so we log progress every ~256 KB.
// Stored as KB so an int comparison is cheap and the value is human-readable.
#define OTA_PROGRESS_KB_STRIDE 256
static size_t g_ota_last_logged_kb_mark = 0;
// E3 #67: track Update.isRunning() across loop iterations so we can detect
// the running->not-running transition that marks Update.end() completion.
static bool g_ota_was_running = false;

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
}

#if MAX_NEIGHBOURS
static void wifi_telemetry_fill_neighbors(TelemetryNeighbors& n) {
    memset(&n, 0, sizeof(n));
    strncpy(n.repeater_pubkey_short, g_tel_repeater_pk_short,
            sizeof(n.repeater_pubkey_short) - 1);

    n.total_count = (uint8_t)the_mesh.getNeighbourCount();

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
        int pos = snprintf(n.summary, sizeof(n.summary), "%u: ",
                           (unsigned)n.total_count);
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

    // Persistent-mode hook (D2 / issue #56): when persistent timer is active,
    // skip the teardown so WiFi stays up between publish cycles. The
    // wifi_telemetry_loop()'s timeout check (below) handles auto-revert.
    if (g_tel_persistent_until_ms == 0) {
        g_tel_transport->end();
    }
}

static void wifi_telemetry_loop() {
    g_telemetry.loop();
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
        Serial.println("[WTEL] persistent timer expired, reverted to BURST");
    }

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
