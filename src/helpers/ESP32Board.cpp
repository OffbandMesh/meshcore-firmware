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

// =============================================================================
// Epic E (#64) / E1 #65: Persistent safety/diagnostic event ring buffer.
//
// SAFELANE Error Visibility violation fix discovered during D7 (#61) testing:
// D9 SAFETY logs and OTA events were emitted only via Serial.println, lost if
// no monitor attached or if monitor dropped on USB re-enumeration. This block
// adds an NVS-backed ring buffer that persists events across reboots, giving
// the safety machinery a forensic record of its own operation that is
// retrievable later via the admin CLI (E4 #68: safety log / safety state).
//
// NVS layout (same namespace 'ota_safety' as the existing D9 boot_count):
//   key 'evt_buf' : single blob, SAFETY_LOG_SLOTS * sizeof(SafetyEvent) bytes
//   key 'evt_idx' : uint16_t write index (next slot to write, mod SLOTS)
//   key 'evt_seq' : uint32_t monotonic event sequence number across boots
//
// Append semantics (safety_log_append): writes new event to slot evt_idx,
// bumps evt_seq and evt_idx, commits. Best-effort: silently drops if NVS
// is unavailable - safety machinery must not crash because logging failed.
//
// Read semantics (getSafetyLog): starts at evt_idx (oldest, soon-to-be-
// overwritten slot), iterates SLOTS times mod SLOTS, skipping EVT_NONE
// slots. Output is oldest-first chronological order.
// =============================================================================

#define SAFETY_LOG_SLOTS 16
#define SAFETY_LOG_DETAIL_LEN 28

// SafetyEventType enum moved to src/MeshCore.h (E3 #67) so main.cpp can use
// it via the appendSafetyEvent virtual without duplicating the codes.
using mesh::EVT_NONE;
using mesh::EVT_BOOT_INC;
using mesh::EVT_BOOT_THRESHOLD;
using mesh::EVT_BOOT_ROLLBACK;
using mesh::EVT_BOOT_PENDING;
using mesh::EVT_BOOT_VALID;
using mesh::EVT_BOOT_VALID_FAIL;
using mesh::EVT_OTA_START;
using mesh::EVT_OTA_PROGRESS;
using mesh::EVT_OTA_RESTART;
using mesh::EVT_NVS_FAIL;

struct __attribute__((packed)) SafetyEvent {
  uint32_t seq;       // monotonic across boots (from evt_seq NVS counter)
  uint32_t ts_ms;     // millis() at time of event; resets per boot
  uint8_t  type;      // SafetyEventType enum value
  uint8_t  _pad;      // reserved for alignment / future flags
  char     detail[SAFETY_LOG_DETAIL_LEN];  // free-form, NUL-terminated when len permits
};
// sizeof(SafetyEvent) = 4 + 4 + 1 + 1 + 28 = 38 bytes
// Total blob = 16 * 38 = 608 bytes (NVS handles this trivially)

static const char* k_nvs_safety_evt_buf = "evt_buf";
static const char* k_nvs_safety_evt_idx = "evt_idx";
static const char* k_nvs_safety_evt_seq = "evt_seq";

// safety_log_append: append a single event to the ring buffer.
// Best-effort - returns silently on NVS failure. Called from D9 SAFETY paths
// (E2) and from OTA upload path (E3); both are file-internal callers.
// Synchronous: blocks until NVS commit returns (~tens of ms for 608-byte blob).
static void safety_log_append(uint8_t type, const char* detail) {
  nvs_handle_t h;
  if (nvs_open(k_nvs_safety_ns, NVS_READWRITE, &h) != ESP_OK) return;

  SafetyEvent events[SAFETY_LOG_SLOTS] = {0};
  size_t size = sizeof(events);
  // NOT_FOUND is fine on first call; events stays zeroed.
  nvs_get_blob(h, k_nvs_safety_evt_buf, events, &size);

  uint16_t idx = 0;
  uint32_t seq = 0;
  nvs_get_u16(h, k_nvs_safety_evt_idx, &idx);
  nvs_get_u32(h, k_nvs_safety_evt_seq, &seq);
  if (idx >= SAFETY_LOG_SLOTS) idx = 0;  // defensive against corruption

  events[idx].seq = ++seq;
  events[idx].ts_ms = millis();
  events[idx].type = type;
  events[idx]._pad = 0;
  if (detail) {
    strncpy(events[idx].detail, detail, SAFETY_LOG_DETAIL_LEN - 1);
    events[idx].detail[SAFETY_LOG_DETAIL_LEN - 1] = '\0';
  } else {
    events[idx].detail[0] = '\0';
  }

  idx = (idx + 1) % SAFETY_LOG_SLOTS;

  nvs_set_blob(h, k_nvs_safety_evt_buf, events, sizeof(events));
  nvs_set_u16(h, k_nvs_safety_evt_idx, idx);
  nvs_set_u32(h, k_nvs_safety_evt_seq, seq);
  nvs_commit(h);
  nvs_close(h);
}

// Map event type code to short human-readable string for log dump output.
static const char* safety_event_type_str(uint8_t t) {
  switch (t) {
    case EVT_BOOT_INC:        return "boot_inc";
    case EVT_BOOT_THRESHOLD:  return "threshold";
    case EVT_BOOT_ROLLBACK:   return "rollback";
    case EVT_BOOT_PENDING:    return "pending";
    case EVT_BOOT_VALID:      return "valid";
    case EVT_BOOT_VALID_FAIL: return "valid_fail";
    case EVT_OTA_START:       return "ota_start";
    case EVT_OTA_PROGRESS:    return "ota_prog";
    case EVT_OTA_RESTART:     return "ota_restart";
    case EVT_NVS_FAIL:        return "nvs_fail";
    default:                  return "?";
  }
}

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
    // Can't log to persistent buffer if NVS itself is dead. Serial is our only
    // record of this edge case.
    return;
  }

  uint8_t boot_count = 0;
  // ESP_ERR_NVS_NOT_FOUND is expected on first boot ever - boot_count stays 0.
  nvs_get_u8(h, k_nvs_boot_count, &boot_count);
  boot_count++;
  nvs_set_u8(h, k_nvs_boot_count, boot_count);
  nvs_commit(h);
  Serial.printf("[SAFETY] boot count: %u (threshold %u)\n",
                (unsigned)boot_count, (unsigned)ROLLBACK_THRESHOLD);
  {
    char d[16];
    snprintf(d, sizeof(d), "%u/%u", (unsigned)boot_count, (unsigned)ROLLBACK_THRESHOLD);
    safety_log_append(EVT_BOOT_INC, d);
  }

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
      safety_log_append(EVT_BOOT_THRESHOLD, "no_other_partition");
      return;
    }
    esp_err_t set_err = esp_ota_set_boot_partition(other);
    if (set_err != ESP_OK) {
      Serial.printf("[SAFETY] esp_ota_set_boot_partition failed: %s\n",
                    esp_err_to_name(set_err));
      {
        char d[28];
        snprintf(d, sizeof(d), "set_part_fail:%s", esp_err_to_name(set_err));
        safety_log_append(EVT_BOOT_THRESHOLD, d);
      }
      return;
    }
    Serial.printf("[SAFETY] threshold exceeded, rolling back to partition '%s'\n",
                  other->label);
    {
      char d[28];
      snprintf(d, sizeof(d), "->%s", other->label ? other->label : "?");
      safety_log_append(EVT_BOOT_ROLLBACK, d);
    }
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
      safety_log_append(EVT_BOOT_PENDING, running->label ? running->label : "?");
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
      safety_log_append(EVT_BOOT_VALID, "mark_valid_ok");
      s_validation_pending = false;
    } else {
      Serial.printf("[SAFETY] mark_valid failed: %s\n", esp_err_to_name(err));
      safety_log_append(EVT_BOOT_VALID_FAIL, esp_err_to_name(err));
    }
  } else {
    Serial.println("[SAFETY] boot validated (no bootloader rollback was pending)");
    safety_log_append(EVT_BOOT_VALID, "no_rollback");
  }
}

bool ESP32Board::isBootValidationPending() const {
  return s_validation_pending;
}

// -----------------------------------------------------------------------------
// Epic E (#64) / E1 #65: getter implementations exposing the persistent log.
// Read-only on NVS; safe to call from CLI handler context.
// -----------------------------------------------------------------------------

void ESP32Board::getSafetyLog(char* buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  buf[0] = 0;

  nvs_handle_t h;
  if (nvs_open(k_nvs_safety_ns, NVS_READONLY, &h) != ESP_OK) {
    snprintf(buf, buflen, "(no log: nvs unavailable)");
    return;
  }

  SafetyEvent events[SAFETY_LOG_SLOTS] = {0};
  size_t size = sizeof(events);
  esp_err_t err = nvs_get_blob(h, k_nvs_safety_evt_buf, events, &size);
  uint16_t idx = 0;
  nvs_get_u16(h, k_nvs_safety_evt_idx, &idx);
  nvs_close(h);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    snprintf(buf, buflen, "(no events yet)");
    return;
  }
  if (err != ESP_OK) {
    snprintf(buf, buflen, "(log read err: %s)", esp_err_to_name(err));
    return;
  }

  // Iterate oldest-first: start at evt_idx (oldest slot, soon to be overwritten).
  size_t pos = 0;
  size_t emitted = 0;
  for (size_t i = 0; i < SAFETY_LOG_SLOTS; i++) {
    size_t slot = (idx + i) % SAFETY_LOG_SLOTS;
    const SafetyEvent& e = events[slot];
    if (e.type == EVT_NONE) continue;

    int n = snprintf(buf + pos, buflen - pos,
                     "#%lu +%lums %s:%s\n",
                     (unsigned long)e.seq,
                     (unsigned long)e.ts_ms,
                     safety_event_type_str(e.type),
                     e.detail);
    if (n < 0 || (size_t)n >= buflen - pos) {
      // Output would overflow the caller's buffer; truncate with marker.
      if (buflen >= 5) {
        snprintf(buf + buflen - 5, 5, "...\n");
      }
      return;
    }
    pos += n;
    emitted++;
  }

  if (emitted == 0) {
    snprintf(buf, buflen, "(no events yet)");
  }
}

void ESP32Board::appendSafetyEvent(uint8_t type, const char* detail) {
  // External entry: delegates to the file-static helper. Keeps the NVS
  // marshaling localized while letting non-board code log events through
  // the polymorphic MainBoard interface.
  safety_log_append(type, detail);
}

void ESP32Board::getSafetyState(char* buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  buf[0] = 0;

  uint32_t uptime_s = millis() / 1000UL;

  uint8_t boot_count = 0;
  nvs_handle_t h;
  if (nvs_open(k_nvs_safety_ns, NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u8(h, k_nvs_boot_count, &boot_count);
    nvs_close(h);
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  const char* part_label = running ? running->label : "?";
  const char* state_str = "unknown";
  if (running) {
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) == ESP_OK) {
      switch (st) {
        case ESP_OTA_IMG_NEW:            state_str = "new"; break;
        case ESP_OTA_IMG_PENDING_VERIFY: state_str = "pending_verify"; break;
        case ESP_OTA_IMG_VALID:          state_str = "valid"; break;
        case ESP_OTA_IMG_INVALID:        state_str = "invalid"; break;
        case ESP_OTA_IMG_ABORTED:        state_str = "aborted"; break;
        case ESP_OTA_IMG_UNDEFINED:      state_str = "undefined"; break;
      }
    }
  }

  snprintf(buf, buflen,
           "boot_count=%u pending=%s partition=%s state=%s uptime=%lus",
           (unsigned)boot_count,
           s_validation_pending ? "yes" : "no",
           part_label,
           state_str,
           (unsigned long)uptime_s);
}

// -----------------------------------------------------------------------------
// E8 #72: enumerate app partitions and dump their states. Resolves the visibility
// gap in getSafetyState() (which only reports the running partition's state) and
// gives direct evidence for diagnostic questions about OTA partition transitions.
// -----------------------------------------------------------------------------

// Map esp_ota_img_states_t to a short string suitable for the compact reply
// buffer. Mirrors the mapping in getSafetyState() but kept short for inline use.
static const char* ota_state_short(esp_ota_img_states_t st) {
  switch (st) {
    case ESP_OTA_IMG_NEW:            return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending";
    case ESP_OTA_IMG_VALID:          return "valid";
    case ESP_OTA_IMG_INVALID:        return "invalid";
    case ESP_OTA_IMG_ABORTED:        return "aborted";
    case ESP_OTA_IMG_UNDEFINED:      return "undef";
    default:                         return "?";
  }
}

void ESP32Board::getPartitionsInfo(char* buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  buf[0] = 0;

  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);

  // Lead with the run/next summary so the most diagnostic info is visible
  // even if the per-partition list gets truncated.
  int pos = snprintf(buf, buflen, "run=%s next=%s [",
                     running ? running->label : "?",
                     next ? next->label : "?");
  if (pos < 0 || (size_t)pos >= buflen) return;

  // Iterate every app-type partition (factory + ota_0..ota_15) and report
  // each one's stored OTA state. The ESP-IDF partition_next() API automatically
  // releases each previous iterator; the loop terminates when next() returns
  // NULL, at which point no release is needed.
  bool first = true;
  esp_partition_iterator_t it = esp_partition_find(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (it != nullptr) {
    const esp_partition_t* p = esp_partition_get(it);
    if (p != nullptr) {
      esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
      // Factory partition has no otadata entry; esp_ota_get_state_partition
      // returns ESP_ERR_NOT_SUPPORTED in that case. Leave st at UNDEFINED.
      esp_ota_get_state_partition(p, &st);
      int n = snprintf(buf + pos, buflen - pos, "%s%s:%s",
                       first ? "" : " ",
                       p->label,
                       ota_state_short(st));
      if (n < 0 || (size_t)n >= buflen - pos) {
        // Truncation; release iterator and bail (closing bracket lost).
        esp_partition_iterator_release(it);
        return;
      }
      pos += n;
      first = false;
    }
    it = esp_partition_next(it);
  }

  // Close the bracket if we have room.
  if ((size_t)pos + 1 < buflen) {
    buf[pos++] = ']';
    buf[pos] = 0;
  }
}

#endif
