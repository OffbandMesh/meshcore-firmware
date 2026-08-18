#pragma once

#include "DisplayDriver.h"
#include <SPI.h>
#include <helpers/RefCountedDigitalPin.h>

#ifndef NV3001B_LOGICAL_WIDTH
  #define NV3001B_LOGICAL_WIDTH 128
#endif

#ifndef NV3001B_LOGICAL_HEIGHT
  #define NV3001B_LOGICAL_HEIGHT 64
#endif

#ifndef NV3001B_PANEL_WIDTH
  #define NV3001B_PANEL_WIDTH 128
#endif

#ifndef NV3001B_PANEL_HEIGHT
  #define NV3001B_PANEL_HEIGHT 220
#endif

#ifndef NV3001B_SPI_HOST
  #define NV3001B_SPI_HOST HSPI
#endif

// ---------------------------------------------------------------------------
// SOFTWARE (bit-banged) SPI -- required on some carriers, notably the RCC6.
//
// WHY THIS EXISTS. Heltec's own board header for the RCC6
// (HelTecAutomation/RadioCore_Library, src/boards/heltec_rcc6.h) sets:
//
//     RADIOCORE_NV3001B_USE_ESP32_SPI     0
//     RADIOCORE_NV3001B_USE_HARDWARE_SPI  0
//     RADIOCORE_NV3001B_USE_SOFTWARE_SPI  1
//
// i.e. the vendor drives this panel with bit-banged SPI on that board and
// explicitly disables both hardware paths. The reason is visible in the RCC6
// pinout: the panel's MOSI is GPIO15, which -- alone among the header pins --
// carries NO FSPI function, and no TFT pin maps to FSPID. The hardware SPI
// peripheral's native mapping simply does not fit this pin set.
//
// Symptom when you get this wrong: backlight lights (plain GPIO) and the panel
// stays blank, with no error anywhere, because begin() always returns true and
// the panel is write-only with no readback to fail on.
//
// DEFAULT IS OFF. heltec_rc32 keeps hardware SPI on HSPI exactly as before --
// this compiles out entirely there, so that board's behaviour is unchanged.
// ---------------------------------------------------------------------------
#ifndef NV3001B_USE_SOFTWARE_SPI
  #define NV3001B_USE_SOFTWARE_SPI 0
#endif

class NV3001BDisplay : public DisplayDriver {
  SPIClass spi;
  RefCountedDigitalPin* periph_power;
  bool is_on = false;
  uint16_t color = 0xffff;
  uint8_t text_size = 1;
  int cursor_x = 0;
  int cursor_y = 0;

  // #743: back buffer. Every panel write funnels through fillPhysicalRect(), so
  // when this is non-null the draw path writes here instead of over SPI and
  // endFrame() blits the whole thing in one transfer. Pixels are stored
  // BYTE-SWAPPED (panel wants big-endian RGB565) so the blit is a plain
  // writeBytes with no per-pixel conversion.
  // Null is a supported state: allocation failure degrades to the original
  // direct-to-panel behaviour rather than losing the display (SAFELANE 6).
  uint16_t* frame_buf = nullptr;

  void allocFrameBuffer();
  void blitFrameBuffer();

  // Bus abstraction. Every panel write goes through these four, so the
  // hardware/software SPI choice lives in exactly one place instead of being
  // smeared across five call sites.
  void busBegin();
  void busBeginTransaction();
  void busEndTransaction();
  void busWrite8(uint8_t b);
  void busWriteBytes(const uint8_t* data, size_t len);
  void busWritePattern(const uint8_t* pattern, size_t plen, uint32_t count);

  void writeCommand(uint8_t cmd);
  void writeBytes(const uint8_t* data, size_t len);
  void writeCommandData(uint8_t cmd, const uint8_t* data, size_t len);
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void writeColor(uint16_t rgb, uint32_t count);
  void fillPhysicalRect(int x, int y, int w, int h);
  void initPanel();
  void drawChar(int x, int y, char ch);

public:
  NV3001BDisplay(RefCountedDigitalPin* power = nullptr) :
      DisplayDriver(NV3001B_LOGICAL_WIDTH, NV3001B_LOGICAL_HEIGHT), spi(NV3001B_SPI_HOST), periph_power(power) { }

  bool begin();
  static const char* driverName() { return "NV3001B"; }
  // #822: real overrides, not statics. These were dead static members with zero
  // callers; left in place they would SHADOW DisplayDriver's new virtuals of the
  // same name -- the identical trap as #812. Note they now report the POST-ROTATION
  // drawing size (SCREEN), not NV3001B_PANEL_*, which is the memory orientation.
  int physicalWidth() const override;
  int physicalHeight() const override;

  // #822: this driver's own colour splash art. Capability and asset in one place,
  // so no per-variant flag can be forgotten.
  const ColourArt* colourSplashArt() const override;

  bool isOn() override { return is_on; }
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
  // #749: colour art at NATIVE panel resolution. Coordinates are physical pixels,
  // deliberately bypassing scaleX()/scaleY() -- see the note in Rgb565Blit.h.
  void drawRGB565(int x, int y, const uint16_t* px, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
};
