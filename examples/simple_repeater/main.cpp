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

    g_tel_transport->end();
}

static void wifi_telemetry_loop() {
    g_telemetry.loop();
    // Honor the admin kill-switch — skip everything when telemetry is disabled.
    if (g_tel_disabled) return;
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
    g_tel_next_publish_ms = millis();
}

int wifi_telemetry_get_status(char* buf, int buflen) {
    unsigned long since_last = (g_tel_last_publish_ms == 0)
                                  ? 0
                                  : (millis() - g_tel_last_publish_ms) / 1000UL;
    const char* state_str = "never";
    if (g_tel_last_publish_ms != 0) state_str = "ok";
    if (g_tel_wifi_fails > 0 && g_tel_publish_attempts == 0) state_str = "wifi_fail";
    if (g_tel_publish_fails > 0) state_str = "publish_fail";
    return snprintf(buf, buflen,
        "wifi a=%lu f=%lu | mqtt a=%lu f=%lu state=%d | last_ok=%lus ago | %s%s",
        (unsigned long)g_tel_wifi_attempts,
        (unsigned long)g_tel_wifi_fails,
        (unsigned long)g_tel_publish_attempts,
        (unsigned long)g_tel_publish_fails,
        g_tel_last_mqtt_state,
        since_last,
        state_str,
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
