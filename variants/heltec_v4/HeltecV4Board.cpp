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
    if (_led_enabled) digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
    loRaFEMControl.setTxModeEnable();
  }

  void HeltecV4Board::onAfterTransmit(void) {
    if (_led_enabled) digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
    loRaFEMControl.setRxModeEnable();
  }

  void HeltecV4Board::powerOff() {
    // Turn off PA
    digitalWrite(P_LORA_PA_POWER, LOW);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);

    ESP32Board::powerOff();
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

  // Runtime control of the external FEM LNA. TX/RX mode switching happens
  // automatically on each transmit (via onBeforeTransmit/onAfterTransmit), so
  // changing the enable flag alone would only take effect on the next RX-mode
  // entry -- setRxModeEnable() below applies it immediately.
  //
  // Upstream 1.17.0 guards on isLnaCanControl() before touching the FEM. Kept:
  // the Offband version drove setLNAEnable()/setRxModeEnable() even on a GC1109
  // (V4.2), which has no independent LNA path, so the pin writes were pointless
  // RF state changes. Return value is equivalent for the caller either way.
  bool HeltecV4Board::setLoRaFemLnaEnabled(bool enable) {
    if (!loRaFEMControl.isLnaCanControl()) {
      return false;
    }

    loRaFEMControl.setLNAEnable(enable);
    loRaFEMControl.setRxModeEnable();
    return true;
  }

  bool HeltecV4Board::canControlLoRaFemLna() const {
    // Upstream made LoRaFEMControl::isLnaCanControl() const in 1.17.0, so the
    // const_cast the Offband version needed here is gone.
    return loRaFEMControl.isLnaCanControl();
  }

  bool HeltecV4Board::isLoRaFemLnaEnabled() const {
    return loRaFEMControl.isLNAEnabled();
  }

  // #542: status/traffic LED control. OFFBAND-ONLY -- upstream's MainBoard has no
  // LED control interface at all, so these have no upstream counterpart and must
  // survive the merge. HeltecV4Board.h declares all three `override`, so dropping
  // them here is a link error, not a silent loss.
  // When disabled, the TX LED is forced LOW and the transmit hooks stop driving
  // it. The LED is a plain GPIO, always controllable on this board, so
  // canControlLed() is unconditionally true.
  bool HeltecV4Board::setLedEnabled(bool on) {
    _led_enabled = on;
    if (!on) digitalWrite(P_LORA_TX_LED, LOW);  // ensure it's dark immediately
    return true;
  }

  bool HeltecV4Board::canControlLed() const { return true; }

  bool HeltecV4Board::isLedEnabled() const { return _led_enabled; }
