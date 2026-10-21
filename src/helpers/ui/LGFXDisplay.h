#pragma once

#include <helpers/ui/DisplayDriver.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#ifndef UI_ZOOM
  #define UI_ZOOM 1
#endif

class LGFXDisplay : public DisplayDriver {
protected:
  LGFX_Device* display;
  LGFX_Sprite buffer;

  bool _isOn = false;
  int _color = TFT_WHITE;

public:
  LGFXDisplay(int w, int h, LGFX_Device &disp)
    : DisplayDriver(w/UI_ZOOM, h/UI_ZOOM), display(&disp) {}
  bool begin();
  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  // #868: full-color art, pushed into the same sprite as everything else.
  void drawRGB565(int x, int y, const uint16_t* px, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;

  // The sprite IS the coordinate space here -- the panel is UI_ZOOM times larger
  // and endFrame scales onto it -- so physical and logical are the same numbers.
  // They are still both reported, so the splash needs no special case.
  int physicalWidth() const override { return width(); }
  int physicalHeight() const override { return height(); }
  virtual bool getTouch(int *x, int *y);
};
