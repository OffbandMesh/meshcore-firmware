#include <Arduino.h>
#include "TDeckBoard.h"

uint32_t deviceOnline = 0x00;

// The micro-SD's chip-select (LilyGo's schematic, #1724). Every T-Deck has
// the slot, whether or not its env uses the card.
#ifdef PIN_SD_CS
static constexpr int kSdCardCs = PIN_SD_CS;
#else
static constexpr int kSdCardCs = 39;
#endif

void TDeckBoard::begin() {
  
  ESP32Board::begin();
  
  // Enable peripheral power
  pinMode(PIN_PERF_POWERON, OUTPUT);
  digitalWrite(PIN_PERF_POWERON, HIGH);

  // Configure user button
  pinMode(PIN_USER_BTN, INPUT);

  // Configure LoRa Pins
  pinMode(P_LORA_MISO, INPUT_PULLUP);
  // pinMode(P_LORA_DIO_1, INPUT_PULLUP);

  #ifdef P_LORA_TX_LED
    digitalWrite(P_LORA_TX_LED, HIGH); // inverted pin for SX1276 - HIGH for off
  #endif

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    long wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1 << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET; // received a LoRa packet (while in deep sleep)
    }

    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }

  // The radio, the display and the micro-SD share one SPI bus. Every
  // chip-select is driven high before anything talks on it: a card whose CS
  // floats can answer on MISO while the radio is being read (#1728). After
  // the radio's sleep hold is released, so its line is free to drive. The SD
  // driver takes its CS over when a card mounts.
  for (int cs : {P_LORA_NSS, PIN_TFT_CS, kSdCardCs}) {
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
  }
}