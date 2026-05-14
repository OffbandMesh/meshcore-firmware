#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#if defined(ESP_PLATFORM)

#include <rom/rtc.h>
#include <sys/time.h>
#include <Wire.h>
#include "driver/rtc_io.h"

class ESP32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  bool inhibit_sleep = false;

public:
  void begin() {
    // for future use, sub-classes SHOULD call this from their begin()
    startup_reason = BD_STARTUP_NORMAL;

  #ifdef ESP32_CPU_FREQ
    setCpuFrequencyMhz(ESP32_CPU_FREQ);
  #endif

  #ifdef PIN_VBAT_READ
    // battery read support
    pinMode(PIN_VBAT_READ, INPUT);
    adcAttachPin(PIN_VBAT_READ);
  #endif

  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif

  #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
   #if PIN_BOARD_SDA >= 0 && PIN_BOARD_SCL >= 0
    Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL);
   #endif
  #else
    Wire.begin();
  #endif
  }

  // Temperature from ESP32 MCU
  float getMCUTemperature() override {
    uint32_t raw = 0;

    // To get and average the temperature so it is more accurate, especially in low temperature
    for (int i = 0; i < 4; i++) {
      raw += temperatureRead();
    }

    return raw / 4;
  }

  void enterLightSleep(uint32_t secs) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(P_LORA_DIO_1) // Supported ESP32 variants
    if (rtc_gpio_is_valid_gpio((gpio_num_t)P_LORA_DIO_1)) { // Only enter sleep mode if P_LORA_DIO_1 is RTC pin
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
      esp_sleep_enable_ext1_wakeup((1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH); // To wake up when receiving a LoRa packet

      if (secs > 0) {
        esp_sleep_enable_timer_wakeup(secs * 1000000); // To wake up every hour to do periodically jobs
      }

      esp_light_sleep_start(); // CPU enters light sleep
    }
#endif
  }

  void sleep(uint32_t secs) override {
    if (!inhibit_sleep) {
      enterLightSleep(secs);      // To wake up after "secs" seconds or when receiving a LoRa packet
    }
  }

  uint8_t getStartupReason() const override { return startup_reason; }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#elif defined(P_LORA_TX_NEOPIXEL_LED)
  #define NEOPIXEL_BRIGHTNESS    64  // white brightness (max 255)

  void onBeforeTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS);   // turn TX neopixel on (White)
  }
  void onAfterTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, 0, 0, 0);   // turn TX neopixel off
  }
#endif

  uint16_t getBattMilliVolts() override {
  #ifdef PIN_VBAT_READ
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) {
      raw += analogReadMilliVolts(PIN_VBAT_READ);
    }
    raw = raw / 4;

    return (2 * raw);
  #else
    return 0;  // not supported
  #endif
  }

  const char* getManufacturerName() const override {
    return "Generic ESP32";
  }

  void reboot() override {
    esp_restart();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;

  // D4 / issue #58: STA-mode OTA. Uses the already-connected STA WiFi
  // (caller's responsibility to ensure WiFi is up) and binds an
  // AsyncElegantOTA server to the device's STA IP. Returns false if WiFi
  // is not in a usable STA state.
  // password is used for HTTP Basic Auth on the /update endpoint.
  bool startOTAUpdateOverSTA(const char* id, const char* password, char reply[]) override;

  // D5/D6: stop the OTA server (if running) and release resources.
  // No-op if not currently running.
  void stopOTAUpdate() override;

  // D5: query whether the OTA server is currently running and where.
  // buf is filled with a human-readable status string. Returns true if running.
  bool getOTAStatus(char* buf, size_t buflen) override;

  // D9 / issue #63: app-level rollback safety. Layers on top of arduino-esp32's
  // built-in bootloader rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1) by
  // adding an NVS-tracked boot counter for the "alive but silently broken"
  // failure mode that bootloader rollback can't catch (because the app doesn't
  // actually crash). See ESP32Board.cpp for the full lifecycle comment.
  void beginBootSafety() override;
  void markBootValid() override;
  bool isBootValidationPending() const override;

  // Epic E (#64) / E1 #65: persistent SAFETY event log retrieval.
  // The append side is internal (safety_log_append in ESP32Board.cpp); these
  // getters expose it to the admin CLI (E4 #68) and to anything else that
  // needs to inspect the on-device forensic record.
  void getSafetyLog(char* buf, size_t buflen) override;
  void getSafetyState(char* buf, size_t buflen) override;
  // E3 #67: external entry point so non-board code (main.cpp wifi_telemetry_loop
  // OTA detector) can log without exposing the file-static safety_log_append.
  void appendSafetyEvent(uint8_t type, const char* detail) override;
  // E8 #72: enumerate app partitions + their states. Used to disambiguate
  // OTA boot-state mysteries that safety state (which only reports the running
  // partition) cannot answer.
  void getPartitionsInfo(char* buf, size_t buflen) override;

  void setInhibitSleep(bool inhibit) {
    inhibit_sleep = inhibit;
  }

  uint32_t getResetReason() const override {
    return esp_reset_reason();
  }

  // Map esp_reset_reason_t enum to a human-readable string. Does NOT touch any
  // radio or hardware state — pure switch on the cached reason enum.
  const char* getResetReasonString(uint32_t reason) override {
    switch (reason) {
    case ESP_RST_UNKNOWN:   return "Unknown or first boot";
    case ESP_RST_POWERON:   return "Power-on reset";
    case ESP_RST_EXT:       return "External reset";
    case ESP_RST_SW:        return "Software reset";
    case ESP_RST_PANIC:     return "Panic / exception reset";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog reset";
    case ESP_RST_TASK_WDT:  return "Task watchdog reset";
    case ESP_RST_WDT:       return "Other watchdog reset";
    case ESP_RST_DEEPSLEEP: return "Wake from deep sleep";
    case ESP_RST_BROWNOUT:  return "Brownout (low voltage)";
    case ESP_RST_SDIO:      return "SDIO reset";
    default:
      static char buf[40];
      snprintf(buf, sizeof(buf), "Unknown reset reason (%lu)", (unsigned long)reason);
      return buf;
    }
  }
};

class ESP32RTCClock : public mesh::RTCClock {
public:
  ESP32RTCClock() { }
  void begin() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_POWERON) {
      // start with some date/time in the recent past
      struct timeval tv;
      tv.tv_sec = 1715770351;  // 15 May 2024, 8:50pm
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
    }
  }
  uint32_t getCurrentTime() override {
    time_t _now;
    time(&_now);
    return _now;
  }
  void setCurrentTime(uint32_t time) override { 
    struct timeval tv;
    tv.tv_sec = time;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
  }
};

#endif
