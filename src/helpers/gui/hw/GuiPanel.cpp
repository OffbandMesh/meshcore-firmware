#include "GuiPanel.h"

#include <Arduino.h>

bool GuiPanel::begin(size_t max_transfer) {
  if (_ready) return true;
  if (_peripher_power) _peripher_power->claim();

#if defined(PIN_TFT_LEDA_CTL) && PIN_TFT_LEDA_CTL >= 0
  pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
  digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
#endif

  St7789Dma::Pins pins;
  pins.sck = PIN_TFT_SCL;
  pins.mosi = PIN_TFT_SDA;
  pins.cs = PIN_TFT_CS;
  pins.dc = PIN_TFT_DC;
  pins.rst = PIN_TFT_RST;
  // SPI3: the radio owns SPI2 (target.cpp's default SPIClass).
  _ready = _panel.begin(SPI3_HOST, pins, GUI_PANEL_SPI_HZ, GUI_PANEL_MADCTL, max_transfer);
  _on = _ready;
  return _ready;
}

void GuiPanel::turnOn() {
  if (!_ready || _on) return;
  _panel.sleep(false);
  _panel.displayOn(true);
#if defined(PIN_TFT_LEDA_CTL) && PIN_TFT_LEDA_CTL >= 0
  digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
#endif
  _on = true;
}

void GuiPanel::turnOff() {
  if (!_ready || !_on) return;
#if defined(PIN_TFT_LEDA_CTL) && PIN_TFT_LEDA_CTL >= 0
  digitalWrite(PIN_TFT_LEDA_CTL, LOW);
#endif
  _panel.displayOn(false);
  _panel.sleep(true);
  _on = false;
}
