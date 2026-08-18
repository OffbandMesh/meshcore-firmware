#pragma once

#include <stdint.h>
#include <string.h>

using ColorVal = uint16_t;

class UIColor {
public:
  // color definitions (by element _type_)
  static ColorVal window_bkg, title_bkg, title_txt, primary_txt, secondary_txt, warning_txt, popup_bkg, popup_txt, corp_blue;
};

class DisplayDriver {
  int _w, _h;
protected:
  DisplayDriver(int w, int h) { _w = w; _h = h; }
public:
  //enum Color { DARK=0, LIGHT, RED, GREEN, BLUE, YELLOW, ORANGE }; // on b/w screen, colors will be !=0 synonym of light

  int width() const { return _w; }
  int height() const { return _h; }

  virtual bool isOn() = 0;
  virtual bool isEink() { return false; } // default to non-eink, override in eink drivers
  virtual void turnOn() = 0;
  virtual void turnOff() = 0;
  // #148: runtime rotation in DEGREES (0/180 supported; other values ignored).
  // Default no-op so non-rotating drivers (e-ink, etc.) simply ignore it.
  virtual void setRotation(uint8_t deg) {}
  // #148: does this driver implement a *verified* runtime setRotation? Default
  // false, so the observer CLI reports "not supported on this display" instead
  // of silently no-op'ing. Only drivers with a verified override return true.
  virtual bool supportsRotation() const { return false; }
  virtual void clear() = 0;
  virtual void startFrame(ColorVal bkg = UIColor::window_bkg) = 0;
  virtual void setTextSize(int sz) = 0;
  virtual void setColor(ColorVal c) = 0;
  virtual void setCursor(int x, int y) = 0;
  virtual void print(const char* str) = 0;
  virtual void printWordWrap(const char* str, int max_width) { print(str); }   // fallback to basic print() if no override
  virtual void fillRect(int x, int y, int w, int h) = 0;
  virtual void drawRect(int x, int y, int w, int h) = 0;
  virtual void drawXbm(int x, int y, const uint8_t* bits, int w, int h) = 0;
  // #749: full-colour RGB565 image, in PHYSICAL panel pixels -- not the logical
  // width()/height() canvas the rest of this interface uses. Colour panels here
  // stretch the 128x64 logical canvas non-uniformly (the RC32's NV3001B by 1.72x
  // horizontally and 2.0x vertically), so art routed through logical coordinates
  // comes out distorted -- circles become ellipses. This is the escape hatch for
  // artwork that must land at native resolution.
  //
  // DEFAULTED, NOT PURE, and deliberately so: there are 12 drivers in the tree and
  // ~115 mono-OLED envs, none of which have anything to do with colour art. The
  // default draws NOTHING, so a mono/e-ink panel simply keeps its 1-bit XBM splash.
  // Dithering colour art down to 1-bit was considered and rejected -- at 128x64 it
  // reads worse than the purpose-made XBM. Contract tests:
  // test/test_display_rgb565_default.
  virtual void drawRGB565(int x, int y, const uint16_t* px, int w, int h) {}

  // #822: PHYSICAL panel size. width()/height() above are the 128x64 LOGICAL canvas
  // every driver presents; on a stretched panel (the RC32 is 220x128) they are not
  // the pixels you have. Deferred in #749 and again in #758 on the grounds that one
  // board's splash did not justify widening the interface; the shared splash
  // component is the third caller and cannot place artwork without it.
  // Defaults to the logical size, which is correct wherever they are the same.
  virtual int physicalWidth() const { return width(); }
  virtual int physicalHeight() const { return height(); }

  // #822: the driver's own colour splash artwork, or nullptr.
  //
  // Capability and asset are ONE declaration on purpose. The previous design gated
  // colour art on a per-variant -D flag, which is silent when omitted -- boards were
  // missed with it twice, and the failure looked like "this panel just has a mono
  // splash" rather than like a bug. A driver cannot now claim colour capability
  // without supplying the art, because they are the same override, and the artwork
  // lives with the driver that knows its own panel size.
  struct ColourArt { const uint16_t* px; int w, h, x, y; };
  virtual const ColourArt* colourSplashArt() const { return nullptr; }
  bool supportsColourArt() const { return colourSplashArt() != nullptr; }
  virtual uint16_t getTextWidth(const char* str) = 0;
  virtual void drawTextCentered(int mid_x, int y, const char* str) {   // helper method (override to optimise)
    int w = getTextWidth(str);
    setCursor(mid_x - w/2, y);
    print(str);
  }
  virtual void drawTextRightAlign(int x_anch, int y, const char* str) {
    int w = getTextWidth(str);
    setCursor(x_anch - w, y);
    print(str);
  }
  virtual void drawTextLeftAlign(int x_anch, int y, const char* str) {
    setCursor(x_anch, y);
    print(str);
  }
  
  // convert UTF-8 characters to displayable block characters for compatibility
  virtual void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] != 0 && j < dest_size - 1; i++) {
      unsigned char c = (unsigned char)src[i];
      if (c >= 32 && c <= 126) {
        dest[j++] = c;  // ASCII printable
      } else if (c >= 0x80) {
        dest[j++] = '\xDB';  // CP437 full block █
        while (src[i+1] && (src[i+1] & 0xC0) == 0x80) 
          i++;  // skip UTF-8 continuation bytes
      }
    }
    dest[j] = 0;
  }
  
  // draw text with ellipsis if it exceeds max_width
  virtual void drawTextEllipsized(int x, int y, int max_width, const char* str) {
    char temp_str[256];  // reasonable buffer size
    size_t len = strlen(str);
    if (len >= sizeof(temp_str)) len = sizeof(temp_str) - 1;
    memcpy(temp_str, str, len);
    temp_str[len] = 0;
    
    if (getTextWidth(temp_str) <= max_width) {
      setCursor(x, y);
      print(temp_str);
      return;
    }
    
    // for variable-width fonts (GxEPD), add space after ellipsis
    // for fixed-width fonts (OLED), keep tight spacing to save precious characters
    const char* ellipsis;
    // use a simple heuristic: if 'i' and 'l' have different widths, it's variable-width
    int i_width = getTextWidth("i");
    int l_width = getTextWidth("l");
    if (i_width != l_width) {
      ellipsis = "... ";  // variable-width fonts: add space
    } else {
      ellipsis = "...";   // fixed-width fonts: no space
    }
    
    int ellipsis_width = getTextWidth(ellipsis);
    int str_len = strlen(temp_str);
    
    while (str_len > 0 && getTextWidth(temp_str) > max_width - ellipsis_width) {
      temp_str[--str_len] = 0;
    }
    strcat(temp_str, ellipsis);
    
    setCursor(x, y);
    print(temp_str);
  }
  
  virtual void endFrame() = 0;
};
