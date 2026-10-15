#pragma once

// The display the GUI env constructs as DISPLAY_CLASS (#1441). It owns the
// panel hardware and hands frames to LVGL; the text-drawing half of
// DisplayDriver belongs to the old status-screen UI, which GUI builds don't
// compile, so those calls are deliberately empty.

#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ui/DisplayDriver.h>

#include "St7789Dma.h"

// Class A portrait canvas (design spec v0.3).
#ifndef GUI_PANEL_WIDTH
  #define GUI_PANEL_WIDTH 240
#endif
#ifndef GUI_PANEL_HEIGHT
  #define GUI_PANEL_HEIGHT 320
#endif
// MADCTL for portrait: MX|MY with RGB order, the same register value the
// Adafruit driver uses for rotation 0 on this panel. Which way is "up" in the
// enclosure is confirmed on the bench (A7).
#ifndef GUI_PANEL_MADCTL
  #define GUI_PANEL_MADCTL 0xC0
#endif
// The SPI clock the existing TFT driver runs at (ST7789LCDDisplay::begin).
#ifndef GUI_PANEL_SPI_HZ
  #define GUI_PANEL_SPI_HZ 40000000
#endif

class GuiPanel : public DisplayDriver {
public:
  explicit GuiPanel(RefCountedDigitalPin* peripher_power = nullptr)
      : DisplayDriver(GUI_PANEL_WIDTH, GUI_PANEL_HEIGHT), _peripher_power(peripher_power) {}

  // Brings up the backlight and the panel. `max_transfer` is the size in
  // bytes of the largest pixel write (one LVGL draw buffer).
  bool begin() { return begin(GUI_PANEL_WIDTH * 32 * 2); }
  bool begin(size_t max_transfer);

  St7789Dma& panel() { return _panel; }
  bool ready() const { return _ready; }

  bool isOn() override { return _on; }
  void turnOn() override;
  void turnOff() override;

  void clear() override {}
  void startFrame(ColorVal bkg = UIColor::window_bkg) override { (void)bkg; }
  void setTextSize(int sz) override { (void)sz; }
  void setColor(ColorVal c) override { (void)c; }
  void setCursor(int x, int y) override { (void)x; (void)y; }
  void print(const char* str) override { (void)str; }
  void fillRect(int x, int y, int w, int h) override { (void)x; (void)y; (void)w; (void)h; }
  void drawRect(int x, int y, int w, int h) override { (void)x; (void)y; (void)w; (void)h; }
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override {
    (void)x; (void)y; (void)bits; (void)w; (void)h;
  }
  uint16_t getTextWidth(const char* str) override { (void)str; return 0; }
  void endFrame() override {}

private:
  St7789Dma _panel;
  RefCountedDigitalPin* _peripher_power;
  bool _ready = false;
  bool _on = false;
};
