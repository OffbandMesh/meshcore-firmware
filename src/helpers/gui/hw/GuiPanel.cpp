#include "GuiPanel.h"

#include <Arduino.h>

// Backlight PWM. Nothing else in the firmware uses LEDC, and the V4 envs
// define no buzzer; channel 3 keeps clear of channel 0, Arduino's default.
static constexpr uint8_t kBacklightChannel = 3;
static constexpr uint32_t kBacklightHz = 5000;
static constexpr uint8_t kBacklightBits = 8;

static void backlightBegin() {
#if defined(PIN_TFT_LEDA_CTL) && PIN_TFT_LEDA_CTL >= 0
  ledcSetup(kBacklightChannel, kBacklightHz, kBacklightBits);
  ledcAttachPin(PIN_TFT_LEDA_CTL, kBacklightChannel);
  // Dark until the first frame is drawn: the panel's RAM powers up as noise.
  ledcWrite(kBacklightChannel, 0);
#endif
}

void GuiPanel::setBrightness(uint8_t pct) {
#if defined(PIN_TFT_LEDA_CTL) && PIN_TFT_LEDA_CTL >= 0
  if (pct > 100) pct = 100;
  ledcWrite(kBacklightChannel, uint32_t(pct) * 255 / 100);
#else
  (void)pct;
#endif
}

bool GuiPanel::begin(size_t max_transfer) {
  if (_ready) return true;
  if (_peripher_power) _peripher_power->claim();

  backlightBegin();

  St7789Dma::Pins pins;
  pins.sck = PIN_TFT_SCL;
  pins.mosi = PIN_TFT_SDA;
  pins.cs = PIN_TFT_CS;
  pins.dc = PIN_TFT_DC;
  pins.rst = PIN_TFT_RST;
#ifdef PIN_TFT_MISO
  pins.miso = PIN_TFT_MISO;   // the SD card on the same bus reads through it (#1677)
#endif
  // SPI2: the radio owns SPI3. target.cpp's SPIClass defaults to HSPI, which
  // is SPI3 on the S3, and Arduino's SPI driver writes that peripheral's
  // registers directly, wiping the interrupt setup the IDF driver relies on.
  // SPI2 is also Arduino's global `SPI` (FSPI), so nothing in a GUI build may
  // call SPI.begin().
  _ready = _panel.begin(SPI2_HOST, pins, GUI_PANEL_SPI_HZ, GUI_PANEL_MADCTL, max_transfer);
  _on = _ready;
  return _ready;
}

void GuiPanel::turnOn() {
  if (!_ready || _on) return;
  _panel.sleep(false);
  _panel.displayOn(true);
  setBrightness(100);
  _on = true;
}

void GuiPanel::turnOff() {
  if (!_ready || !_on) return;
  setBrightness(0);
  _panel.displayOn(false);
  _panel.sleep(true);
  _on = false;
}
