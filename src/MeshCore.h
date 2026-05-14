#pragma once

#include <stdint.h>
#include <math.h>

#define MAX_HASH_SIZE        8
#define PUB_KEY_SIZE        32
#define PRV_KEY_SIZE        64
#define SEED_SIZE           32
#define SIGNATURE_SIZE      64
#define MAX_ADVERT_DATA_SIZE  32
#define CIPHER_KEY_SIZE     16
#define CIPHER_BLOCK_SIZE   16

// V1
#define CIPHER_MAC_SIZE      2
#define PATH_HASH_SIZE       1

#define MAX_PACKET_PAYLOAD  184
#define MAX_GROUP_DATA_LENGTH  (MAX_PACKET_PAYLOAD - CIPHER_BLOCK_SIZE - 3)
#define MAX_PATH_SIZE        64
#define MAX_TRANS_UNIT      255

#if MESH_DEBUG && ARDUINO
  #include <Arduino.h>
  #define MESH_DEBUG_PRINT(F, ...) Serial.printf("DEBUG: " F, ##__VA_ARGS__)
  #define MESH_DEBUG_PRINTLN(F, ...) Serial.printf("DEBUG: " F "\n", ##__VA_ARGS__)
#else
  #define MESH_DEBUG_PRINT(...) {}
  #define MESH_DEBUG_PRINTLN(...) {}
#endif

#if BRIDGE_DEBUG && ARDUINO
#define BRIDGE_DEBUG_PRINTLN(F, ...) Serial.printf("%s BRIDGE: " F, getLogDateTime(), ##__VA_ARGS__)
#else
#define BRIDGE_DEBUG_PRINTLN(...) {}
#endif

namespace mesh {

#define  BD_STARTUP_NORMAL     0  // getStartupReason() codes
#define  BD_STARTUP_RX_PACKET  1

// Epic E (#64) / E1 #65: SafetyEventType codes for the persistent
// on-device safety/diagnostic event log. Boards that implement the
// persistent log (currently ESP32Board) accept these codes via
// MainBoard::appendSafetyEvent(). Cross-board API: values are stable.
enum SafetyEventType : uint8_t {
  EVT_NONE             = 0,  // empty slot marker; never appended directly
  EVT_BOOT_INC         = 1,  // boot counter incremented at beginBootSafety
  EVT_BOOT_THRESHOLD   = 2,  // counter exceeded threshold (rollback failed/rejected)
  EVT_BOOT_ROLLBACK    = 3,  // app-level rollback succeeded, about to restart
  EVT_BOOT_PENDING     = 4,  // running partition is PENDING_VERIFY (post-OTA)
  EVT_BOOT_VALID       = 5,  // markBootValid succeeded
  EVT_BOOT_VALID_FAIL  = 6,  // markBootValid failed (esp_ota_mark error)
  EVT_OTA_START        = 7,  // OTA upload first chunk observed
  EVT_OTA_PROGRESS     = 8,  // OTA upload progress milestone
  EVT_OTA_RESTART      = 9,  // OTA-triggered reset about to fire (or just fired)
  EVT_NVS_FAIL         = 10, // nvs_open or commit failed in safety codepath
};

class MainBoard {
public:
  virtual uint16_t getBattMilliVolts() = 0;
  virtual float getMCUTemperature() { return NAN; }
  virtual bool setAdcMultiplier(float multiplier) { return false; };
  virtual float getAdcMultiplier() const { return 0.0f; }
  virtual const char* getManufacturerName() const = 0;
  virtual void onBeforeTransmit() { }
  virtual void onAfterTransmit() { }
  virtual void reboot() = 0;
  virtual void powerOff() { /* no op */ }
  virtual void sleep(uint32_t secs)  { /* no op */ }
  virtual uint32_t getGpio() { return 0; }
  virtual void setGpio(uint32_t values) {}
  virtual uint8_t getStartupReason() const = 0;
  virtual bool getBootloaderVersion(char* version, size_t max_len) { return false; }
  virtual bool startOTAUpdate(const char* id, char reply[]) { return false; }   // not supported (AP-mode OTA)

  // STA-mode OTA: uses existing connected WiFi instead of starting an AP.
  // Requires caller to have brought WiFi up (e.g., via persistent-mode admin command).
  // password is used for HTTP Basic Auth on the /update endpoint.
  virtual bool startOTAUpdateOverSTA(const char* id, const char* password, char reply[]) { return false; }
  // Stop any running OTA server (AP-mode or STA-mode). No-op if not running.
  virtual void stopOTAUpdate() { /* no-op */ }
  // Query OTA running state; fills buf with human-readable status. Returns true if running.
  virtual bool getOTAStatus(char* buf, size_t buflen) {
    if (buf && buflen > 0) buf[0] = 0;
    return false;
  }

  // Power management interface (boards with power management override these)
  virtual bool isExternalPowered() { return false; }
  virtual uint16_t getBootVoltage() { return 0; }
  virtual uint32_t getResetReason() const { return 0; }
  virtual const char* getResetReasonString(uint32_t reason) { return "Not available"; }
  virtual uint8_t getShutdownReason() const { return 0; }
  virtual const char* getShutdownReasonString(uint8_t reason) { return "Not available"; }

  // External LoRa FEM LNA control (boards with a controllable FEM override these).
  // Default: not supported (boards without an external FEM, or without a controllable LNA path).
  virtual bool setLoRaFemLnaEnabled(bool enable) { return false; }
  virtual bool canControlLoRaFemLna() const { return false; }
  virtual bool isLoRaFemLnaEnabled() const { return false; }

  // D9 / issue #63: app-level boot rollback safety. Boards with ESP-IDF
  // rollback APIs override these. Default no-op for non-ESP32 boards.
  // beginBootSafety()    - call EARLY in setup(); increments NVS boot counter
  //                        and rolls back to the other partition if threshold
  //                        is exceeded. Also detects PENDING_VERIFY state so
  //                        the app knows whether the bootloader is waiting on
  //                        a markBootValid() call.
  // markBootValid()      - call from loop() after a healthy uptime window;
  //                        resets the counter and (if pending) confirms the
  //                        partition so the bootloader stops considering rollback.
  // isBootValidationPending() - true if the bootloader has us in PENDING_VERIFY.
  virtual void beginBootSafety() { /* no-op */ }
  virtual void markBootValid() { /* no-op */ }
  virtual bool isBootValidationPending() const { return false; }

  // Epic E (#64) / E1 #65: persistent on-device safety/diagnostic logging.
  // Fixes SAFELANE Error Visibility violation discovered during D7 (#61) test:
  // D9 SAFETY and OTA events were emitted only via Serial.println, lost if no
  // monitor attached or if monitor dropped on USB re-enumeration. These getters
  // expose the NVS-backed event ring buffer + current snapshot. Default no-op
  // on boards without persistent storage; ESP32 overrides format real data.
  // Each getter writes a NUL-terminated human-readable string into buf, truncating
  // (with "..." marker) if buflen is exceeded.
  virtual void getSafetyLog(char* buf, size_t buflen) {
    if (buf && buflen > 0) buf[0] = 0;
  }
  virtual void getSafetyState(char* buf, size_t buflen) {
    if (buf && buflen > 0) buf[0] = 0;
  }

  // Epic E / E3 #67: append an event to the persistent log. Type is a
  // SafetyEventType value; detail is a free-form NUL-terminated string
  // (truncated to ~27 chars by the implementation). Best-effort: callers
  // do not need to check return because logging failure must never bring
  // down the calling path. Default no-op for boards without persistent
  // storage.
  virtual void appendSafetyEvent(uint8_t type, const char* detail) {
    (void)type; (void)detail;
  }

  // Epic E / E8 #72: dump per-partition state for OTA diagnostic visibility.
  // Driver: E7 (#71) needed full otadata visibility (state of BOTH OTA
  // partitions, not just the running one) to resolve why a post-OTA boot
  // didn't show PENDING_VERIFY despite both compile units having rollback
  // support. Default no-op for boards without partition support.
  virtual void getPartitionsInfo(char* buf, size_t buflen) {
    if (buf && buflen > 0) buf[0] = 0;
  }

  // Epic E / E10 #74: dump the N newest safety events, newest-first.
  // Pairs with getSafetyLog (oldest-first). Used when the buffer-size budget
  // forces us to choose: the OLDEST events are forensic history (already
  // observed); the NEWEST events are the diagnostic for the current boot.
  // Default no-op for boards without persistent storage.
  virtual void getSafetyLogTail(char* buf, size_t buflen, uint8_t max_events) {
    if (buf && buflen > 0) buf[0] = 0;
    (void)max_events;
  }
};

/**
 * An abstraction of the device's Realtime Clock.
*/
class RTCClock {
  uint32_t last_unique;
protected:
  RTCClock() { last_unique = 0; }

public:
  /**
   * \returns  the current time. in UNIX epoch seconds.
  */
  virtual uint32_t getCurrentTime() = 0;

  /**
   * \param time  current time in UNIX epoch seconds.
  */
  virtual void setCurrentTime(uint32_t time) = 0;

  /**
   * override in classes that need to periodically update internal state
   */
  virtual void tick() { /* no op */}

  uint32_t getCurrentTimeUnique() {
    uint32_t t = getCurrentTime();
    if (t <= last_unique) {
      return ++last_unique;
    }
    return last_unique = t;
  }
};

}
