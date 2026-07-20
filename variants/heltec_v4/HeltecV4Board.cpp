#include "HeltecV4Board.h"

void HeltecV4Board::begin() {
    ESP32Board::begin();


    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, LOW); // Initially inactive

    loRaFEMControl.init();

    periph_power.begin();
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
    }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }
  }

  void HeltecV4Board::onBeforeTransmit(void) {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
    loRaFEMControl.setTxModeEnable();
  }

  void HeltecV4Board::onAfterTransmit(void) {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
    loRaFEMControl.setRxModeEnable();
  }

  void HeltecV4Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Make sure the DIO1 and NSS GPIOs are hold on required levels during deep sleep
    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    loRaFEMControl.setRxModeEnableWhenMCUSleep();//It also needs to be enabled in receive mode

    if (pin_wake_btn < 0) {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet
    } else {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1) | (1L << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet OR wake btn
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    // Finally set ESP32 into sleep
    esp_deep_sleep_start();   // CPU halts here and never returns!
  }

  void HeltecV4Board::powerOff()  {
    enterDeepSleep(0);
  }

  uint16_t HeltecV4Board::getBattMilliVolts()  {
    analogReadResolution(10);
    digitalWrite(PIN_ADC_CTRL, HIGH);
    delay(10);
    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / 8;

    digitalWrite(PIN_ADC_CTRL, LOW);

    return (adc_mult * (3.3 / 1024.0) * raw) * 1000;
  }

  // #327: report the FEM PART, not a board revision.
  //
  // This previously returned "Heltec V4.3 ..." vs "Heltec V4 ...". Both arms were
  // wrong: "V4.3" is a Heltec SCHEMATIC filename, not a revision Heltec publishes
  // (their hardware update log lists V4.0 and V4.3.1 only), and bare "V4" understated
  // a GC1109 board whose silkscreen reads "HTIT-WB32LAF V4.2".
  //
  // The detect reads the FEM strap (see LoRaFEMControl::init) -- so the FEM part is
  // the only thing this function actually knows. It is also the useful fact: it is
  // what determines whether the LNA setting appears in the client. Naming a revision
  // would be asserting something unverified; a KCT8103L unit's printed revision has
  // not been read on real hardware.
  //
  // To identify a board with certainty, read the back silkscreen.
  // See docs/architecture/2026-07-19-heltec-v4-fem-lna-research.md.
  //
  // Downstream consumers of this string, all checked (#327):
  //   - MqttPayload: becomes the "model" field of the status JSON, sanitized by
  //     makeSafeToken() (non-alnum -> '_'). A DISPLAY field only -- it is not part of
  //     any topic or unique_id, so HA device identity is unaffected; the model shown
  //     on the device card changes.
  //   - MyMesh device-info frame: 40-byte field, longest new string is 25. Fits.
  //   - ESP32Board OTA page (AsyncElegantOTA.setID): cosmetic, and the sprintf there
  //     was made bounded in this change -- the longer string leaves zero margin.
  //   - CommonCLI: printed verbatim in a CLI reply.
  // Nothing parses it for identity. Client-side display is a separate repo.
  const char* HeltecV4Board::getManufacturerName() const {
#ifdef HELTEC_LORA_V4_TFT
    return loRaFEMControl.getFEMType() == KCT8103L_PA ? "Heltec V4 TFT (KCT8103L)" : "Heltec V4 TFT (GC1109)";
#else
    return loRaFEMControl.getFEMType() == KCT8103L_PA ? "Heltec V4 OLED (KCT8103L)" : "Heltec V4 OLED (GC1109)";
#endif
  }

  // Runtime control of the external FEM LNA. The TX/RX mode switching happens
  // automatically on each transmit (via onBeforeTransmit/onAfterTransmit);
  // changing the LNA enable flag here takes effect on the next RX-mode entry.
  // To make the change immediate, re-enter RX mode now.
  bool HeltecV4Board::setLoRaFemLnaEnabled(bool enable) {
    loRaFEMControl.setLNAEnable(enable);
    // Apply the change to the current chip state if we're not mid-TX.
    // setRxModeEnable() drives the CTX/PA pins per the new lna_enabled flag.
    loRaFEMControl.setRxModeEnable();
    return loRaFEMControl.isLnaCanControl();
  }

  bool HeltecV4Board::canControlLoRaFemLna() const {
    // const_cast: LoRaFEMControl::isLnaCanControl is non-const by upstream design.
    return const_cast<LoRaFEMControl&>(loRaFEMControl).isLnaCanControl();
  }

  bool HeltecV4Board::isLoRaFemLnaEnabled() const {
    return loRaFEMControl.isLnaEnabled();
  }
