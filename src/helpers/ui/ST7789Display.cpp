#ifdef ST7789

#include "ST7789Display.h"

// #868: this board's color splash art. Three panels, three geometries, selected
// the same way the constructor in ST7789Display.h selects them.
#if defined(HELTEC_VISION_MASTER_T190)
  #include "OffbandLogoST7789T190.h"
  #define OB_ART_PX  offband_splash_st7789_t190
  #define OB_ART_W   OFFBAND_SPLASH_ST7789_T190_W
  #define OB_ART_H   OFFBAND_SPLASH_ST7789_T190_H
  #define OB_ART_X   OFFBAND_SPLASH_ST7789_T190_X
  #define OB_ART_Y   OFFBAND_SPLASH_ST7789_T190_Y
  #define OB_PANEL_W 320
  #define OB_PANEL_H 170
#elif defined(THINKNODE_M9)
  #include "OffbandLogoST7789M9.h"
  #define OB_ART_PX  offband_splash_st7789_m9
  #define OB_ART_W   OFFBAND_SPLASH_ST7789_M9_W
  #define OB_ART_H   OFFBAND_SPLASH_ST7789_M9_H
  #define OB_ART_X   OFFBAND_SPLASH_ST7789_M9_X
  #define OB_ART_Y   OFFBAND_SPLASH_ST7789_M9_Y
  #define OB_PANEL_W 320
  #define OB_PANEL_H 240
#else
  #include "OffbandLogoST7789T114.h"
  #define OB_ART_PX  offband_splash_st7789_t114
  #define OB_ART_W   OFFBAND_SPLASH_ST7789_T114_W
  #define OB_ART_H   OFFBAND_SPLASH_ST7789_T114_H
  #define OB_ART_X   OFFBAND_SPLASH_ST7789_T114_X
  #define OB_ART_Y   OFFBAND_SPLASH_ST7789_T114_Y
  #define OB_PANEL_W 240
  #define OB_PANEL_H 135
#endif

#ifndef X_OFFSET
#define X_OFFSET 0  // No offset needed for landscape
#endif

#ifndef Y_OFFSET
#define Y_OFFSET 1  // Vertical offset to prevent top row cutoff
#endif

#ifdef HELTEC_VISION_MASTER_T190
  #define SCALE_X  2.5f        // 320 / 128
  #define SCALE_Y  2.65625f    // 170 / 64
#else
  #define SCALE_X  1.875f      // 240 / 128
  #define SCALE_Y  2.109375f   // 135 / 64
#endif

#ifdef DISPLAY_SCALE_X
  #define SCALE_X DISPLAY_SCALE_X
#endif

#ifdef DISPLAY_SCALE_Y
  #define SCALE_Y DISPLAY_SCALE_Y
#endif

// Color scheme
ColorVal UIColor::window_bkg = OLEDDISPLAY_COLOR::BLACK;
ColorVal UIColor::title_bkg = OLEDDISPLAY_COLOR::BLACK;
ColorVal UIColor::title_txt = OLEDDISPLAY_COLOR::WHITE;
ColorVal UIColor::primary_txt = OLEDDISPLAY_COLOR::WHITE;
ColorVal UIColor::secondary_txt = OLEDDISPLAY_COLOR::WHITE;
ColorVal UIColor::warning_txt = OLEDDISPLAY_COLOR::WHITE;
ColorVal UIColor::popup_bkg = OLEDDISPLAY_COLOR::BLACK;
ColorVal UIColor::popup_txt = OLEDDISPLAY_COLOR::WHITE;
ColorVal UIColor::corp_blue = OLEDDISPLAY_COLOR::WHITE;

bool ST7789Display::begin() {
  if(!_isOn) {
    pinMode(PIN_TFT_VDD_CTL, OUTPUT);
    pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
    digitalWrite(PIN_TFT_VDD_CTL, LOW);
  #ifdef PIN_TFT_LEDA_CTL_ACTIVE
    digitalWrite(PIN_TFT_LEDA_CTL, PIN_TFT_LEDA_CTL_ACTIVE);
  #else
    digitalWrite(PIN_TFT_LEDA_CTL, LOW);
  #endif
    digitalWrite(PIN_TFT_RST, HIGH);

    display.init();
    display.landscapeScreen();
    #ifdef DISPLAY_FLIP_VERTICALLY
    display.flipScreenVertically();
    #endif
    display.displayOn();
    setCursor(0,0);

    _isOn = true;
  }
  return true;
}

void ST7789Display::turnOn() {
  if (!_isOn) {
    // Restore power to the display but keep backlight off
    digitalWrite(PIN_TFT_VDD_CTL, LOW);
    digitalWrite(PIN_TFT_RST, HIGH);
    
    // Re-initialize the display
    display.init();
    display.displayOn();
    #ifdef DISPLAY_FLIP_VERTICALLY
    display.flipScreenVertically();
    #endif
    delay(20);

    // Now turn on the backlight
  #ifdef PIN_TFT_LEDA_CTL_ACTIVE
    digitalWrite(PIN_TFT_LEDA_CTL, PIN_TFT_LEDA_CTL_ACTIVE);
  #else
    digitalWrite(PIN_TFT_LEDA_CTL, LOW);
  #endif    
    _isOn = true;
  }
}

// #148: 0 deg = the begin() default (landscapeScreen, RGB|MV|MX); 180 deg =
// flipScreenVertically (RGB|MV|MY) -- a true 180 flip of both axes. Only 0/180
// supported. NOTE: 180 on the TFT is UNVERIFIED pending real-TFT hardware test.
void ST7789Display::setRotation(uint8_t deg) {
  if (deg == 0)        display.landscapeScreen();
  else if (deg == 180) display.flipScreenVertically();
}

void ST7789Display::turnOff() {
  digitalWrite(PIN_TFT_VDD_CTL, HIGH);
#ifdef PIN_TFT_LEDA_CTL_ACTIVE
  digitalWrite(PIN_TFT_LEDA_CTL, !PIN_TFT_LEDA_CTL_ACTIVE);
#else
  digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
#endif
  digitalWrite(PIN_TFT_RST, LOW);
  _isOn = false;
}

void ST7789Display::clear() {
  display.clear();
}

void ST7789Display::startFrame(ColorVal bkg) {
  display.clear();  // TODO: use bkg
  // Art lives for one frame: whatever drew it must draw it again next frame, the
  // same contract the 1-bit buffer has.
  display.clearColorArt();
  setColor(UIColor::primary_txt);
  display.setFont(ArialMT_Plain_16);
}

int ST7789Display::physicalWidth() const { return OB_PANEL_W; }
int ST7789Display::physicalHeight() const { return OB_PANEL_H; }

const DisplayDriver::ColorArt* ST7789Display::colorSplashArt() const {
#ifdef OB_ART_W
  static const ColorArt art = { OB_ART_PX, OB_ART_W, OB_ART_H, OB_ART_X, OB_ART_Y };
  return &art;
#else
  return nullptr;
#endif
}

void ST7789Display::drawRGB565(int x, int y, const uint16_t* px, int w, int h) {
  if (!px) return;
  // Handed to the blit rather than drawn: see the note on ST7789Spi::artPixel.
  // Coordinates are PHYSICAL -- the buffer is the panel here, so SCALE_X/Y and
  // the X/Y_OFFSET nudges that every other primitive applies do not belong.
  display.setColorArt(px, x, y, w, h);
}

void ST7789Display::setTextSize(int sz) {
  switch(sz) {
    case 1 :
      display.setFont(ArialMT_Plain_16);
      break;
    case 2 :
      display.setFont(ArialMT_Plain_24);
      break;
    default:
      display.setFont(ArialMT_Plain_16);
  }
}

void ST7789Display::setColor(ColorVal c) {
  _color = c;
  display.setColor((OLEDDISPLAY_COLOR)_color);
  display.setRGB(_color == OLEDDISPLAY_COLOR::WHITE ? ST77XX_WHITE : ST77XX_BLACK);
}

void ST7789Display::setCursor(int x, int y) {
  _x = x*SCALE_X + X_OFFSET;
  _y = y*SCALE_Y + Y_OFFSET;
}

void ST7789Display::print(const char* str) {
  display.drawString(_x, _y, str);
}

void ST7789Display::printWordWrap(const char* str, int max_width) {
  display.drawStringMaxWidth(_x, _y, max_width*SCALE_X, str);
}

void ST7789Display::fillRect(int x, int y, int w, int h) {
  display.fillRect(x*SCALE_X + X_OFFSET, y*SCALE_Y + Y_OFFSET, w*SCALE_X, h*SCALE_Y);
}

void ST7789Display::drawRect(int x, int y, int w, int h) {
  display.drawRect(x*SCALE_X + X_OFFSET, y*SCALE_Y + Y_OFFSET, w*SCALE_X, h*SCALE_Y);
}

void ST7789Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  // Calculate the base position in display coordinates
  uint16_t startX = x * SCALE_X + X_OFFSET;
  uint16_t startY = y * SCALE_Y + Y_OFFSET;
  
  // Width in bytes for bitmap processing
  uint16_t widthInBytes = (w + 7) / 8;
  
  // Process the bitmap row by row
  for (uint16_t by = 0; by < h; by++) {
    // Calculate the target y-coordinates for this logical row
    int y1 = startY + (int)(by * SCALE_Y);
    int y2 = startY + (int)((by + 1) * SCALE_Y);
    int block_h = y2 - y1;
    
    // Scan across the row bit by bit
    for (uint16_t bx = 0; bx < w; bx++) {
      // Calculate the target x-coordinates for this logical column
      int x1 = startX + (int)(bx * SCALE_X);
      int x2 = startX + (int)((bx + 1) * SCALE_X);
      int block_w = x2 - x1;
      
      // Get the current bit
      uint16_t byteOffset = (by * widthInBytes) + (bx / 8);
      uint8_t bitMask = 0x80 >> (bx & 7);
      bool bitSet = pgm_read_byte(bits + byteOffset) & bitMask;
      
      // If the bit is set, draw a block of pixels
      if (bitSet) {
        // Draw the block as a filled rectangle
        display.fillRect(x1, y1, block_w, block_h);
      }
    }
  }
}

uint16_t ST7789Display::getTextWidth(const char* str) {
  return display.getStringWidth(str) / SCALE_X;
}

void ST7789Display::endFrame() {
  display.display();
}

#endif