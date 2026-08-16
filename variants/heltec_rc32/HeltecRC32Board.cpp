#include "HeltecRC32Board.h"

void HeltecRC32Board::begin() {
  ESP32Board::begin();

  pinMode(PIN_ADC_CTRL, OUTPUT);
  digitalWrite(PIN_ADC_CTRL, !ADC_CTRL_ENABLED);

#ifdef SENSOR_RST_PIN
  pinMode(SENSOR_RST_PIN, OUTPUT);
  digitalWrite(SENSOR_RST_PIN, HIGH);
#endif

#ifdef LED_POWER
  pinMode(LED_POWER, OUTPUT);
  digitalWrite(LED_POWER, LOW);
#endif

  // #719: begin() ONLY -- do NOT claim(). SENSOR_POWER_CTRL_PIN is GPIO46, an
  // ESP32-S3 BOOT STRAPPING PIN (the S3's straps are GPIO0, GPIO3, GPIO45,
  // GPIO46). begin() drives it to its inactive level, which is LOW here and is
  // the correct strap value; claim() drove it HIGH and nothing ever released
  // it, so GPIO46 sat HIGH from setup() until power was physically pulled.
  //
  // Consequence, owner-reproduced 2026-08-15: any reset that does not let the
  // rail discharge latches GPIO46 HIGH at strap sampling and the chip never
  // boots the application -- no display, no radio, no BLE, no serial. RST
  // (a true CHIP_PU assertion per Heltec's RC32_V1.0 schematic) and a fast
  // unplug/replug both failed; only a slow power cycle recovered the board.
  //
  // The LCD does NOT depend on this rail: NV3001BDisplay's ctor defaults
  // power=nullptr (NV3001BDisplay.h:46) and target.cpp constructs a bare
  // `DISPLAY_CLASS display;`, so the driver never claims it. The rail is for
  // I2C sensors, none of which are fitted (boot probes 14 addresses, finds none).
  //
  // ⚠ If a sensor is ever fitted here, do NOT simply restore claim() -- that
  // reintroduces this bug. Power the rail AFTER boot and release it before any
  // reset, or move the control to a non-strapping pin.
  //
  // Same defect class as #704 (GPIO45/VDD_SPI via PIN_GPS_EN) and #211
  // (RAK3401 floating WB_IO2). See the strapping-pin warning in
  // variants/heltec_rc32/platformio.ini.
  periph_power.begin();

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    long wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1L << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET;
    }

    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }
}

void HeltecRC32Board::powerOff() {
  enterDeepSleep(0);
}

void HeltecRC32Board::onBeforeTransmit() {
  digitalWrite(P_LORA_TX_LED, HIGH);
}

void HeltecRC32Board::onAfterTransmit() {
  digitalWrite(P_LORA_TX_LED, LOW);
}

uint16_t HeltecRC32Board::getBattMilliVolts() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_2_5db);
  digitalWrite(PIN_ADC_CTRL, ADC_CTRL_ENABLED);
  delay(10);
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) {
    raw += analogReadMilliVolts(PIN_VBAT_READ);
  }
  raw = raw / 8;
  digitalWrite(PIN_ADC_CTRL, !ADC_CTRL_ENABLED);

  return (adc_mult * raw);
}

bool HeltecRC32Board::setAdcMultiplier(float multiplier) {
  adc_mult = multiplier == 0.0f ? ADC_MULTIPLIER : multiplier;
  return true;
}

const char* HeltecRC32Board::getManufacturerName() const {
  return "Heltec RC32";
}
