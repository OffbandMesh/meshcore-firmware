#ifdef ESP_PLATFORM

#include "ESP32Board.h"
#include <esp_ota_ops.h>
#include <nvs.h>
#include <nvs_flash.h>

#if defined(ADMIN_PASSWORD) && !defined(DISABLE_WIFI_OTA)   // Repeater or Room Server only
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#include <SPIFFS.h>

// Static state for both AP-mode and STA-mode OTA. AsyncWebServer survives
// the function returning (lives on heap), and we need a handle to it for
// stopOTAUpdate() and getOTAStatus().
static AsyncWebServer* g_ota_server = nullptr;
static char g_ota_url[80] = {0};       // last known OTA URL ("http://x.x.x.x/update")
static bool g_ota_is_sta_mode = false; // true if STA-mode, false if AP-mode (or not running)

bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  // Existing AP-mode OTA path. Spins up a softAP "MeshCore-OTA" — used as
  // fallback when STA WiFi is unavailable (credential rotation, AP failure).
  inhibit_sleep = true;   // prevent sleep during OTA
  WiFi.softAP("MeshCore-OTA", NULL);

  sprintf(reply, "Started: http://%s/update", WiFi.softAPIP().toString().c_str());
  snprintf(g_ota_url, sizeof(g_ota_url), "http://%s/update",
           WiFi.softAPIP().toString().c_str());
  g_ota_is_sta_mode = false;
  MESH_DEBUG_PRINTLN("startOTAUpdate: %s", reply);

  static char id_buf[60];
  sprintf(id_buf, "%s (%s)", id, getManufacturerName());
  static char home_buf[90];
  sprintf(home_buf, "<H2>Hi! I am a MeshCore Repeater. ID: %s</H2>", id);

  // Free any prior server before allocating a new one.
  if (g_ota_server) { delete g_ota_server; g_ota_server = nullptr; }
  g_ota_server = new AsyncWebServer(80);

  g_ota_server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", home_buf);
  });
  g_ota_server->on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/packet_log", "text/plain");
  });

  AsyncElegantOTA.setID(id_buf);
  AsyncElegantOTA.begin(g_ota_server);    // Start ElegantOTA (no auth in AP mode — fallback path)
  g_ota_server->begin();

  return true;
}

// D4 / issue #58: STA-mode OTA. Caller MUST ensure WiFi is up in STA mode
// before invoking — typically by the user issuing `wifi on N` over LoRa
// admin CLI first.
bool ESP32Board::startOTAUpdateOverSTA(const char* id, const char* password, char reply[]) {
  // Verify STA WiFi is actually up. If not, refuse cleanly.
  if (WiFi.status() != WL_CONNECTED) {
    strcpy(reply, "ERR: STA WiFi not connected");
    return false;
  }

  // If an OTA server is already running, refuse — caller should stopOTAUpdate first.
  if (g_ota_server != nullptr) {
    snprintf(reply, 80, "ERR: OTA already running at %s",
             g_ota_url[0] ? g_ota_url : "(unknown)");
    return false;
  }

  inhibit_sleep = true;   // prevent sleep during OTA

  IPAddress ip = WiFi.localIP();
  snprintf(g_ota_url, sizeof(g_ota_url), "http://%s/update", ip.toString().c_str());
  snprintf(reply, 80, "Started: %s", g_ota_url);
  g_ota_is_sta_mode = true;
  MESH_DEBUG_PRINTLN("startOTAUpdateOverSTA: %s", reply);

  static char id_buf[60];
  snprintf(id_buf, sizeof(id_buf), "%s (%s)", id, getManufacturerName());
  static char home_buf[120];
  snprintf(home_buf, sizeof(home_buf),
           "<H2>MeshCore Repeater %s</H2><p><a href=\"/update\">Update firmware</a></p>", id);

  g_ota_server = new AsyncWebServer(80);

  g_ota_server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", home_buf);
  });
  g_ota_server->on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/packet_log", "text/plain");
  });

  AsyncElegantOTA.setID(id_buf);
  // HTTP Basic Auth — username fixed "admin", password from caller (admin password).
  // Note: AsyncElegantOTA.begin() supports (server, user, pass) overload.
  AsyncElegantOTA.begin(g_ota_server, "admin", password);
  g_ota_server->begin();

  return true;
}

void ESP32Board::stopOTAUpdate() {
  if (g_ota_server) {
    g_ota_server->end();
    delete g_ota_server;
    g_ota_server = nullptr;
  }
  g_ota_url[0] = 0;
  g_ota_is_sta_mode = false;
  inhibit_sleep = false;  // allow sleep again

  // If we were in AP mode, also tear down the softAP.
  // (STA-mode leaves WiFi up because the caller's `wifi on N` controls that.)
  if (WiFi.getMode() & WIFI_AP) {
    WiFi.softAPdisconnect(true);
  }
}

bool ESP32Board::getOTAStatus(char* buf, size_t buflen) {
  if (g_ota_server == nullptr) {
    snprintf(buf, buflen, "OTA not running");
    return false;
  }
  snprintf(buf, buflen, "OTA running (%s) at %s",
           g_ota_is_sta_mode ? "STA" : "AP", g_ota_url);
  return true;
}

#else
// Builds without ADMIN_PASSWORD: OTA functions return failure.
bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  return false; // not supported
}
bool ESP32Board::startOTAUpdateOverSTA(const char* id, const char* password, char reply[]) {
  strcpy(reply, "ERR: OTA disabled in this build");
  return false;
}
void ESP32Board::stopOTAUpdate() {
  // no-op
}
bool ESP32Board::getOTAStatus(char* buf, size_t buflen) {
  snprintf(buf, buflen, "OTA disabled in this build");
  return false;
}
#endif

// ----------------------------------------------------------------------------
// D9 / issue #63: app-level boot-rollback safety.
//
// Layered defense:
//   1. Bootloader-level: stock arduino-esp32 v3.20017 ESP32-S3 bootloader has
//      CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1 and CONFIG_BOOTLOADER_WDT_ENABLE=1
//      (9s timeout) baked in. After an OTA, AsyncElegantOTA's Update.h calls
//      esp_ota_set_boot_partition() which marks the new partition PENDING_VERIFY.
//      If the app doesn't call esp_ota_mark_app_valid_cancel_rollback() before
//      the next reboot, bootloader auto-reverts to the previous partition.
//      Handles: catastrophic crashes, panics, hangs, WDT timeouts.
//
//   2. App-level (this code): NVS counter for the "alive but silently broken"
//      failure mode — app runs but radio doesn't init, mesh never registers,
//      etc. Bootloader can't catch this because no crash happens. We increment
//      a counter on every boot, reset it after ROLLBACK_VALIDATION_MS of uptime
//      via markBootValid(). If counter > ROLLBACK_THRESHOLD on boot, we manually
//      swap boot partition and reboot.
//
// Tunables (override per-env in platformio.ini build_flags):
//   ROLLBACK_THRESHOLD       — consecutive unvalidated boots before app-level
//                              rollback fires (default 3, so the 4th unvalidated
//                              boot triggers the manual rollback)
//   ROLLBACK_VALIDATION_MS   — uptime window after which the app is considered
//                              healthy enough to confirm the new image (the
//                              call site in main.cpp uses this; this file only
//                              defines the API)
// ----------------------------------------------------------------------------

#ifndef ROLLBACK_THRESHOLD
#define ROLLBACK_THRESHOLD 3
#endif

static const char* k_nvs_safety_ns = "ota_safety";
static const char* k_nvs_boot_count = "boot_count";
static bool s_validation_pending = false;

void ESP32Board::beginBootSafety() {
  s_validation_pending = false;

  // NVS is initialized by arduino-esp32's pre-setup() framework startup, so
  // we can just open a namespace here. If NVS itself is hosed, we bail rather
  // than risk an unsafe rollback decision.
  nvs_handle_t h;
  esp_err_t err = nvs_open(k_nvs_safety_ns, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    Serial.printf("[SAFETY] nvs_open failed: %s (no rollback this boot)\n",
                  esp_err_to_name(err));
    return;
  }

  uint8_t boot_count = 0;
  // ESP_ERR_NVS_NOT_FOUND is expected on first boot ever — boot_count stays 0.
  nvs_get_u8(h, k_nvs_boot_count, &boot_count);
  boot_count++;
  nvs_set_u8(h, k_nvs_boot_count, boot_count);
  nvs_commit(h);
  Serial.printf("[SAFETY] boot count: %u (threshold %u)\n",
                (unsigned)boot_count, (unsigned)ROLLBACK_THRESHOLD);

  if (boot_count > ROLLBACK_THRESHOLD) {
    // Too many consecutive boots without successful validation. Reset the
    // counter (so the OTHER partition gets a fresh attempt budget) and swap.
    nvs_set_u8(h, k_nvs_boot_count, 0);
    nvs_commit(h);
    nvs_close(h);

    const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
    if (other == nullptr) {
      // Single-partition layout (factory only). Nothing to roll back to.
      Serial.println("[SAFETY] threshold exceeded but no other partition exists");
      return;
    }
    esp_err_t set_err = esp_ota_set_boot_partition(other);
    if (set_err != ESP_OK) {
      Serial.printf("[SAFETY] esp_ota_set_boot_partition failed: %s\n",
                    esp_err_to_name(set_err));
      return;
    }
    Serial.printf("[SAFETY] threshold exceeded, rolling back to partition '%s'\n",
                  other->label);
    Serial.flush();
    delay(500);   // let serial drain before reboot
    esp_restart();
    return;       // unreachable
  }
  nvs_close(h);

  // Check the running partition's OTA state. If we just booted a freshly-OTA'd
  // image, the bootloader will have transitioned it from ESP_OTA_IMG_NEW to
  // ESP_OTA_IMG_PENDING_VERIFY. We must call cancel_rollback() before the next
  // reboot or bootloader will revert us. Cache the flag so loop() can do it.
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running != nullptr) {
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK
        && state == ESP_OTA_IMG_PENDING_VERIFY) {
      s_validation_pending = true;
      Serial.printf("[SAFETY] running '%s' is PENDING_VERIFY; bootloader rollback armed\n",
                    running->label);
    }
  }
}

void ESP32Board::markBootValid() {
  // Reset the app-level counter — this boot is healthy.
  nvs_handle_t h;
  if (nvs_open(k_nvs_safety_ns, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u8(h, k_nvs_boot_count, 0);
    nvs_commit(h);
    nvs_close(h);
  }

  // If the bootloader is waiting on us to confirm a PENDING_VERIFY partition,
  // do it now. After this, the bootloader's per-partition rollback machine
  // disarms for this boot.
  if (s_validation_pending) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
      Serial.println("[SAFETY] partition marked valid (bootloader rollback canceled)");
      s_validation_pending = false;
    } else {
      Serial.printf("[SAFETY] mark_valid failed: %s\n", esp_err_to_name(err));
    }
  } else {
    Serial.println("[SAFETY] boot validated (no bootloader rollback was pending)");
  }
}

bool ESP32Board::isBootValidationPending() const {
  return s_validation_pending;
}

#endif
