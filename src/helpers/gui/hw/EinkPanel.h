#pragma once

// GuiPanel on a black-and-white e-paper panel (GDEQ031T10, T-Deck Pro, #2009).
// GuiPanel.h includes this in place of the TFT class when GUI_PANEL_EINK is
// set, so UITask and the LVGL port use one name for either panel.
//
// LVGL draws into the panel's frame buffer through setInk(); push() sends it
// to the glass. GxEPD2 stays inside EinkPanel.cpp, so nothing that includes
// this header pulls in its templates.

#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ui/DisplayDriver.h>
#include <stddef.h>
#include <stdint.h>

#ifndef GUI_PANEL_WIDTH
  #define GUI_PANEL_WIDTH 240
#endif
#ifndef GUI_PANEL_HEIGHT
  #define GUI_PANEL_HEIGHT 320
#endif
// UITask passes this to lvglSetRotation(). An e-paper panel has no MADCTL;
// the port turns it in software, so the value is unused.
#ifndef GUI_PANEL_MADCTL
  #define GUI_PANEL_MADCTL 0x00
#endif

class GuiPanel : public DisplayDriver {
public:
  explicit GuiPanel(RefCountedDigitalPin* peripher_power = nullptr)
      : DisplayDriver(GUI_PANEL_WIDTH, GUI_PANEL_HEIGHT), _peripher_power(peripher_power) {}

  // Brings up the panel and clears it with a full refresh. The size is the
  // TFT's DMA limit, which e-paper doesn't have.
  bool begin();
  bool begin(size_t max_transfer) { (void)max_transfer; return begin(); }
  bool ready() const { return _ready; }

  bool isOn() override { return _on; }
  // The image stays on the glass with no power. Off puts the controller to
  // sleep and the front-light out; on wakes it, and its next refresh is full
  // because a sleeping controller forgets the image it diffs against.
  void turnOn() override;
  void turnOff() override;

  // The front-light, 0-100 %, by PWM.
  void setBrightness(uint8_t pct);

  // One pixel into the frame buffer, in the coordinates LVGL draws in.
  void setInk(int16_t x, int16_t y, bool ink);
  // Sends the frame buffer to the glass: the whole panel, or the rectangle
  // given (inclusive). Blocks until the panel is done.
  void push(bool full, int16_t x1, int16_t y1, int16_t x2, int16_t y2);
  // 0, 90 or 270, as the GUI's Rotation setting gives it.
  void setRotation(uint16_t rotation);
  // True once after a wake: the next refresh has to be full.
  bool takeNeedFull() {
    const bool n = _need_full;
    _need_full = false;
    return n;
  }

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
  RefCountedDigitalPin* _peripher_power;
  bool _ready = false;
  bool _on = false;
  bool _need_full = false;
};
