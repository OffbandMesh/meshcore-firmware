#pragma once

#include <MeshCore.h>
#include <Arduino.h>

class STM32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  virtual void begin() {
    startup_reason = BD_STARTUP_NORMAL;
  }

  uint8_t getStartupReason() const override { return startup_reason; }

  uint16_t getBattMilliVolts() override {
    return 0;  // not supported
  }

  const char* getManufacturerName() const override {
    return "Generic STM32";
  }

  void reboot() override {
    NVIC_SystemReset(); 
  }

  void powerOff() override {
    HAL_PWREx_DisableInternalWakeUpLine();
    __disable_irq();
    HAL_PWREx_EnterSHUTDOWNMode();
  }

// #2074: the TX LED becomes user-controllable. THIS BOARD IS ACTIVE-LOW -- dark is HIGH, not
// LOW. `ledDark()` carries that fact once, and both onAfterTransmit() and setLedEnabled(false)
// use it, so "off" can never be implemented as the LOW that would light this LED.
#if defined(P_LORA_TX_LED)
  bool canControlLed() const override { return true; }
  bool setLedEnabled(bool on) override {
    _led_enabled = on;
    // #2075: no darken here -- the on-write gate below is enough, because a TX LED is only
    // lit during a transmit and onAfterTransmit() drives it dark with this board's own
    // polarity. See the longer note in ESP32Board.h.
    return true;
  }
  void onBeforeTransmit() override {
    if (_led_enabled) digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED on (active-low)
  }
  void onAfterTransmit() override {
    ledDark();
  }

private:
  void ledDark() { digitalWrite(P_LORA_TX_LED, HIGH); }   // active-LOW board: HIGH is off

public:
#endif

  bool startOTAUpdate(const char* id, char reply[]) override { return false; };
};