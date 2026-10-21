#include "HeltecV4R8Board.h"

void HeltecV4R8Board::begin() {
  ESP32Board::begin();

  periph_power.begin();
  periph_power.claim();  // R8 VEXT also feeds the LoRa antenna boost rail.

  loRaFEMControl.init();

  // On the Expansion Board V2, GPIO21 resets the panel and the touch chip
  // together (schematic V2.03, LCD_RST TP_RST). An env whose display driver
  // owns that pulse sets PIN_TOUCH_RST to -1 instead.
#if defined(PIN_TOUCH_RST) && PIN_TOUCH_RST >= 0
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(100);
  digitalWrite(PIN_TOUCH_RST, HIGH);
#endif

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    long wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1 << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET;
    }

    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }
}

void HeltecV4R8Board::onBeforeTransmit(void) {
  _tx_active = true;
#ifdef P_LORA_TX_LED
  if (_led_enabled) digitalWrite(P_LORA_TX_LED, HIGH);
#endif
  loRaFEMControl.setTxModeEnable();
}

void HeltecV4R8Board::onAfterTransmit(void) {
#ifdef P_LORA_TX_LED
  if (_led_enabled) digitalWrite(P_LORA_TX_LED, LOW);
#endif
  loRaFEMControl.setRxModeEnable();
  _tx_active = false;
}

void HeltecV4R8Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

  rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);
  loRaFEMControl.setRxModeEnableWhenMCUSleep();

  if (pin_wake_btn < 0) {
    esp_sleep_enable_ext1_wakeup((1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);
  } else {
    esp_sleep_enable_ext1_wakeup((1L << P_LORA_DIO_1) | (1L << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);
  }

  if (secs > 0) {
    esp_sleep_enable_timer_wakeup(secs * 1000000);
  }

  esp_deep_sleep_start();
}

// #1341: a power-off is off. Cut the FEM LDO and hold it low through deep
// sleep, then take the base path, which records the shutdown, turns the display
// and radio off and sleeps with no wake source -- as HeltecV4Board does. This
// used to call this class's enterDeepSleep(0), which skipped all of that, kept
// the FEM powered in RX and armed LoRa-packet wake: an auto-shutdown woke on
// the next packet and kept draining the battery.
//
// The board's own Vext claim from begin() is released here too; the display
// releases its claim when the base path turns it off, so Vext (GPIO40) ends
// inactive before sleep instead of keeping the OLED and the antenna boost rail
// powered.
void HeltecV4R8Board::powerOff() {
  digitalWrite(P_LORA_PA_POWER, LOW);
  rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);
  periph_power.release();

  ESP32Board::powerOff();
}

uint16_t HeltecV4R8Board::getBattMilliVolts() {
  analogReadResolution(12);

  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) {
    raw += analogReadMilliVolts(PIN_VBAT_READ);
  }
  raw = raw / 8;

  return (adc_mult * raw);
}

// #1339: the R8's FEM is always a KCT8103L, whose CTX line (GPIO5) selects
// LNA-in or LNA-bypass in RX while the chip stays enabled, so the LNA is always
// controllable. setRxModeEnable() applies the new setting at once instead of
// waiting for the next TX->RX turnaround -- except during a transmission, which
// is asynchronous (the loop keeps running while the packet is on air), so a
// command can arrive mid-TX. Writing the RX-mode CTX level then would take the
// FEM out of its TX state; the stored setting is applied by onAfterTransmit().
bool HeltecV4R8Board::setLoRaFemLnaEnabled(bool enable) {
  loRaFEMControl.setLNAEnable(enable);
  if (!_tx_active) loRaFEMControl.setRxModeEnable();
  return true;
}

bool HeltecV4R8Board::canControlLoRaFemLna() const {
  return loRaFEMControl.isLnaCanControl();
}

bool HeltecV4R8Board::isLoRaFemLnaEnabled() const {
  return loRaFEMControl.isLNAEnabled();
}

// #1340 (#542): the TX LED is a plain GPIO (46), always controllable. Disabled
// forces it LOW and the transmit hooks stop driving it. The R8 TFT GUI env
// gives GPIO46 to the Expansion Board V2's KEY button instead and undefines
// P_LORA_TX_LED, so there is no LED to control there.
bool HeltecV4R8Board::setLedEnabled(bool on) {
#ifdef P_LORA_TX_LED
  _led_enabled = on;
  if (!on) digitalWrite(P_LORA_TX_LED, LOW);
  return true;
#else
  (void)on;
  return false;
#endif
}

bool HeltecV4R8Board::canControlLed() const {
#ifdef P_LORA_TX_LED
  return true;
#else
  return false;
#endif
}

bool HeltecV4R8Board::isLedEnabled() const { return _led_enabled; }

const char* HeltecV4R8Board::getManufacturerName() const {
#ifdef HELTEC_V4_R8_TFT
  return "Heltec V4 R8 TFT";
#else
  return "Heltec V4 R8 OLED";
#endif
}
