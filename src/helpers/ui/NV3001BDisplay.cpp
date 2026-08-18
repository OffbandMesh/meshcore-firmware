#include "NV3001BDisplay.h"
#include "Rgb565Blit.h"   // #749: shared clip + native-resolution blit
#include "Rc32Palette.h"  // #757: dark theme colour roles
#include "JbmFont.h"
#include "OffbandLogoRGB565.h"   // #822: this panel's colour splash art      // #758: antialiased JetBrains Mono glyph table
#include <Arduino.h>
#include <string.h>

#ifndef SPI_FREQUENCY
  #define SPI_FREQUENCY 8000000
#endif

#ifndef PIN_TFT_SCL
  #error "PIN_TFT_SCL must be defined"
#endif

#ifndef PIN_TFT_SDA
  #error "PIN_TFT_SDA must be defined"
#endif

#ifndef PIN_TFT_CS
  #error "PIN_TFT_CS must be defined"
#endif

#ifndef PIN_TFT_DC
  #error "PIN_TFT_DC must be defined"
#endif

#ifndef PIN_TFT_MISO
  #define PIN_TFT_MISO -1
#endif

#ifndef PIN_TFT_RST
  #define PIN_TFT_RST -1
#endif

#ifndef PIN_TFT_EN
  #define PIN_TFT_EN -1
#endif

#ifndef PIN_TFT_BL
  #define PIN_TFT_BL -1
#endif

#ifndef PIN_TFT_EN_ACTIVE
  #define PIN_TFT_EN_ACTIVE LOW
#endif

#ifndef PIN_TFT_BL_ACTIVE
  #define PIN_TFT_BL_ACTIVE HIGH
#endif

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 0
#endif

#ifndef NV3001B_SCREEN_WIDTH
  #define NV3001B_SCREEN_WIDTH 220
#endif

#ifndef NV3001B_SCREEN_HEIGHT
  #define NV3001B_SCREEN_HEIGHT 128
#endif

#ifndef DISPLAY_SCALE_X
  #define DISPLAY_SCALE_X ((float)NV3001B_SCREEN_WIDTH / NV3001B_LOGICAL_WIDTH)
#endif

#ifndef DISPLAY_SCALE_Y
  #define DISPLAY_SCALE_Y ((float)NV3001B_SCREEN_HEIGHT / NV3001B_LOGICAL_HEIGHT)
#endif

#define NV3001B_SWRESET 0x01
#define NV3001B_SLPOUT  0x11
#define NV3001B_DISPON  0x29
#define NV3001B_CASET   0x2A
#define NV3001B_RASET   0x2B
#define NV3001B_RAMWR   0x2C
#define NV3001B_MADCTL  0x36
#define NV3001B_COLMOD  0x3A

#define NV3001B_MADCTL_MY  0x80
#define NV3001B_MADCTL_MX  0x40
#define NV3001B_MADCTL_MV  0x20
#define NV3001B_MADCTL_RGB 0x00

#ifndef NV3001B_TEXT_SIZE1_SCALE_X
  #define NV3001B_TEXT_SIZE1_SCALE_X 1
#endif

#ifndef NV3001B_TEXT_SIZE1_SCALE_Y
  #define NV3001B_TEXT_SIZE1_SCALE_Y 2
#endif

#ifndef NV3001B_TEXT_SIZE2_SCALE_X
  #define NV3001B_TEXT_SIZE2_SCALE_X 2
#endif

#ifndef NV3001B_TEXT_SIZE2_SCALE_Y
  #define NV3001B_TEXT_SIZE2_SCALE_Y 3
#endif

// Color scheme — #757: dark theme, Offband brand colours, no blue anywhere.
// Values and their rationale live in Rc32Palette.h so they can be unit-tested;
// this file cannot be compiled natively. See test/test_rc32_palette.
ColorVal UIColor::window_bkg    = rc32_palette::WINDOW_BKG;
ColorVal UIColor::title_bkg     = rc32_palette::TITLE_BKG;
ColorVal UIColor::title_txt     = rc32_palette::TITLE_TXT;
ColorVal UIColor::primary_txt   = rc32_palette::PRIMARY_TXT;
ColorVal UIColor::secondary_txt = rc32_palette::SECONDARY_TXT;
ColorVal UIColor::warning_txt   = rc32_palette::WARNING_TXT;
ColorVal UIColor::popup_bkg     = rc32_palette::POPUP_BKG;
ColorVal UIColor::popup_txt     = rc32_palette::POPUP_TXT;
// Not a blue any more. The symbol is declared in DisplayDriver.h and defined by all
// 12 drivers, so it cannot be removed without a tree-wide change; what changes here
// is its value on THIS panel.
ColorVal UIColor::corp_blue     = rc32_palette::CORP_ACCENT;

static int scaleX(int x) {
  return (int)(x * DISPLAY_SCALE_X);
}

static int scaleY(int y) {
  return (int)(y * DISPLAY_SCALE_Y);
}

static int scaleWidth(int x, int w) {
  if (w <= 0) return 0;
  int scaled = scaleX(x + w) - scaleX(x);
  return scaled > 0 ? scaled : 1;
}

static int scaleHeight(int y, int h) {
  if (h <= 0) return 0;
  int scaled = scaleY(y + h) - scaleY(y);
  return scaled > 0 ? scaled : 1;
}

static uint8_t nv3001bMADCTL(uint8_t rotation) {
  uint8_t madctl;
  switch (rotation & 3) {
    case 0:
      madctl = NV3001B_MADCTL_MY | NV3001B_MADCTL_MV | NV3001B_MADCTL_RGB;
      break;
    case 1:
      madctl = NV3001B_MADCTL_MY | NV3001B_MADCTL_MX | NV3001B_MADCTL_RGB;
      break;
    case 2:
      madctl = NV3001B_MADCTL_RGB;
      break;
    default:
      madctl = NV3001B_MADCTL_MX | NV3001B_MADCTL_MV | NV3001B_MADCTL_RGB;
      break;
  }
  return madctl;
}

// #758: font5x7 and its 1x2 pixel-scaling helpers were retired here -- the
// antialiased JetBrains Mono table in JbmFont.h replaces them. Other drivers
// keep their own copies of the 5x7 font; this removal is NV3001B-only.

static void setupOptionalOutput(int pin, int level) {
  if (pin < 0) return;

  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
}

static void writeOptionalPin(int pin, int level) {
  if (pin < 0) return;

  digitalWrite(pin, level);
}

// ---------------------------------------------------------------------------
// Bus layer -- hardware SPI, or bit-banged when NV3001B_USE_SOFTWARE_SPI.
//
// Mode 0, MSB first: data is presented on the falling edge of SCK and latched by
// the panel on the rising edge, so SCK idles LOW and each bit is set-then-pulse.
// The panel is write-only (no MISO), so there is nothing to sample.
//
// SPEED. digitalWrite() on ESP32 is ~1 us; a full 128x220 frame is 56,320 bytes,
// which at that rate is minutes, not milliseconds. So the bit-bang uses the GPIO
// set/clear registers directly -- roughly an order of magnitude faster and the
// difference between a usable display and an unusable one. Every TFT pin on this
// carrier is < 32, so the single-word W1TS/W1TC registers are sufficient and no
// high-bank handling is needed. A non-ESP32 target falls back to digitalWrite,
// which is correct if slow.
// ---------------------------------------------------------------------------
#if NV3001B_USE_SOFTWARE_SPI
  #if defined(ESP32)
    #include "soc/gpio_reg.h"
    #define NV_PIN_HIGH(p) REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << (p)))
    #define NV_PIN_LOW(p)  REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << (p)))
  #else
    #define NV_PIN_HIGH(p) digitalWrite((p), HIGH)
    #define NV_PIN_LOW(p)  digitalWrite((p), LOW)
  #endif
#endif

void NV3001BDisplay::busBegin() {
#if NV3001B_USE_SOFTWARE_SPI
  // No SPI peripheral is claimed at all in this mode -- that is the point. On
  // the RCC6 the LoRa radio owns its own SPI host, and bit-banging the panel
  // means the two can never contend for one.
  pinMode(PIN_TFT_SCL, OUTPUT);
  pinMode(PIN_TFT_SDA, OUTPUT);
  NV_PIN_LOW(PIN_TFT_SCL);   // mode 0 idles clock low
#else
  spi.begin(PIN_TFT_SCL, PIN_TFT_MISO, PIN_TFT_SDA, PIN_TFT_CS);
#endif
}

void NV3001BDisplay::busBeginTransaction() {
#if !NV3001B_USE_SOFTWARE_SPI
  spi.beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, SPI_MODE0));
#endif
}

void NV3001BDisplay::busEndTransaction() {
#if !NV3001B_USE_SOFTWARE_SPI
  spi.endTransaction();
#endif
}

void NV3001BDisplay::busWrite8(uint8_t b) {
#if NV3001B_USE_SOFTWARE_SPI
  for (int i = 7; i >= 0; i--) {
    if ((b >> i) & 1) NV_PIN_HIGH(PIN_TFT_SDA); else NV_PIN_LOW(PIN_TFT_SDA);
    NV_PIN_HIGH(PIN_TFT_SCL);
    NV_PIN_LOW(PIN_TFT_SCL);
  }
#else
  spi.transfer(b);
#endif
}

void NV3001BDisplay::busWriteBytes(const uint8_t* data, size_t len) {
#if NV3001B_USE_SOFTWARE_SPI
  for (size_t i = 0; i < len; i++) busWrite8(data[i]);
#else
  spi.writeBytes(data, (uint32_t)len);
#endif
}

void NV3001BDisplay::busWritePattern(const uint8_t* pattern, size_t plen, uint32_t count) {
#if NV3001B_USE_SOFTWARE_SPI
  while (count--) {
    for (size_t i = 0; i < plen; i++) busWrite8(pattern[i]);
  }
#else
  // NOTE writePattern() refuses sizes > 64 bytes (max FIFO); callers stay well
  // inside that.
  spi.writePattern(pattern, plen, count);
#endif
}

void NV3001BDisplay::writeCommand(uint8_t cmd) {
  busBeginTransaction();
  digitalWrite(PIN_TFT_DC, LOW);
  digitalWrite(PIN_TFT_CS, LOW);
  busWrite8(cmd);
  digitalWrite(PIN_TFT_CS, HIGH);
  busEndTransaction();
}

void NV3001BDisplay::writeBytes(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;

  busBeginTransaction();
  digitalWrite(PIN_TFT_DC, HIGH);
  digitalWrite(PIN_TFT_CS, LOW);
  for (size_t i = 0; i < len; i++) {
    busWrite8(data[i]);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  busEndTransaction();
}

void NV3001BDisplay::writeCommandData(uint8_t cmd, const uint8_t* data, size_t len) {
  writeCommand(cmd);
  writeBytes(data, len);
}

void NV3001BDisplay::setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  uint16_t x2 = x + w - 1;
  uint16_t y2 = y + h - 1;
  uint8_t data[4];

  data[0] = x >> 8;
  data[1] = x & 0xff;
  data[2] = x2 >> 8;
  data[3] = x2 & 0xff;
  writeCommandData(NV3001B_CASET, data, sizeof(data));

  data[0] = y >> 8;
  data[1] = y & 0xff;
  data[2] = y2 >> 8;
  data[3] = y2 & 0xff;
  writeCommandData(NV3001B_RASET, data, sizeof(data));

  writeCommand(NV3001B_RAMWR);
}

// #745: bulk fill. This is the primitive behind startFrame(), clear() and
// fillRect(), so it runs on every UI update -- and startFrame() blanks the whole
// panel, which is 220 x 128 = 28,160 px.
//
// It used to push ONE BYTE PER spi.transfer() call:
//
//     while (count--) { spi.transfer(hi); spi.transfer(lo); }
//
// i.e. 56,320 separate transfer calls for a single full-screen fill, each with
// its own per-call overhead on top of the 8 MHz clock. That cost is paid on
// every render and is a large part of the visible flicker.
//
// SPIClass::writePattern() is the core primitive built for exactly this case: it
// packs whole copies of the pattern into the 64-byte hardware FIFO and loops
// (framework-arduinoespressif32/libraries/SPI/src/SPI.cpp:306). A 2-byte pattern
// gives 32 pixels per FIFO load, so the same full-screen fill becomes ~880 FIFO
// transactions instead of 56,320 byte transfers -- and it needs no intermediate
// buffer, so there is no RAM cost.
//
// NOTE writePattern() refuses sizes > 64 bytes (max FIFO); 2 is well inside that.
// A DMA path could go faster still, but that wants the back buffer from #743 to
// be worth it -- this change is deliberately buffer-free and self-contained.
void NV3001BDisplay::writeColor(uint16_t rgb, uint32_t count) {
  if (count == 0) return;

  const uint8_t pattern[2] = { (uint8_t)(rgb >> 8), (uint8_t)(rgb & 0xff) };

  busBeginTransaction();
  digitalWrite(PIN_TFT_DC, HIGH);
  digitalWrite(PIN_TFT_CS, LOW);
  busWritePattern(pattern, sizeof(pattern), count);
  digitalWrite(PIN_TFT_CS, HIGH);
  busEndTransaction();
}

void NV3001BDisplay::initPanel() {
#define CMD0(C) do { writeCommand(C); } while (0)
#define CMD1(C, A) do { const uint8_t d[] = { A }; writeCommandData(C, d, sizeof(d)); } while (0)
#define CMD2(C, A, B) do { const uint8_t d[] = { A, B }; writeCommandData(C, d, sizeof(d)); } while (0)

  CMD0(NV3001B_SWRESET);
  delay(120);
  CMD1(0xFF, 0xA5);
  CMD1(0x41, 0x00);
  CMD1(0x50, 0x02);
  CMD1(0x52, 0x6E);
  CMD1(0x57, 0x02);
  CMD1(0x46, 0x11);
  CMD2(0x47, 0x00, 0x01);
  CMD2(0x8F, 0x22, 0x03);
  CMD1(0x9A, 0x78);
  CMD1(0x9B, 0x78);
  CMD1(0x9C, 0xA0);
  CMD1(0x9D, 0x17);
  CMD1(0x9E, 0xC1);
  CMD1(0x83, 0x5A);
  CMD1(0x84, 0xB6);
  CMD1(0xFF, 0xA5);
  CMD1(0x85, 0x5F);
  CMD1(0x6E, 0x0F);
  CMD1(0x7E, 0x0F);
  CMD1(0x60, 0x00);
  CMD1(0x70, 0x00);
  CMD1(0x6D, 0x33);
  CMD1(0x7D, 0x37);
  CMD1(0x61, 0x09);
  CMD1(0x71, 0x0A);
  CMD1(0x6C, 0x2A);
  CMD1(0x7C, 0x36);
  CMD1(0x62, 0x11);
  CMD1(0x72, 0x10);
  CMD1(0x68, 0x4E);
  CMD1(0x78, 0x4E);
  CMD1(0x66, 0x36);
  CMD1(0x76, 0x3C);
  CMD1(0x1A, 0x1C);
  CMD1(0x7B, 0x14);
  CMD1(0x63, 0x0D);
  CMD1(0x73, 0x0A);
  CMD1(0x6A, 0x16);
  CMD1(0x7A, 0x12);
  CMD1(0x64, 0x0B);
  CMD1(0x74, 0x0A);
  CMD1(0x69, 0x08);
  CMD1(0x79, 0x0A);
  CMD1(0x65, 0x06);
  CMD1(0x75, 0x07);
  CMD1(0x67, 0x23);
  CMD1(0x77, 0x44);
  CMD1(0xE0, 0x00);
  CMD1(0xE9, 0x30);
  CMD1(0xEB, 0xB7);
  CMD1(0xEC, 0x00);
  CMD1(0xED, 0x11);
  CMD1(0xF0, 0xB7);
  CMD1(0x53, 0x04);
  CMD1(0x54, 0x04);
  CMD1(0x55, 0x40);
  CMD1(0x56, 0x40);
  CMD2(0xA0, 0x60, 0x01);
  CMD1(0xA1, 0x84);
  CMD1(0xA2, 0x85);
  CMD2(0xAB, 0x00, 0x02);
  CMD2(0xAC, 0x00, 0x06);
  CMD2(0xAD, 0x00, 0x03);
  CMD2(0xAE, 0x00, 0x07);
  CMD1(0xC7, 0x01);
  CMD1(0xB9, 0x82);
  CMD1(0xBA, 0x83);
  CMD1(0xBB, 0x00);
  CMD1(0xBC, 0x81);
  CMD1(0xBD, 0x02);
  CMD1(0xBE, 0x01);
  CMD1(0xBF, 0x04);
  CMD1(0xC0, 0x03);
  CMD1(0xC8, 0x55);
  CMD1(0xC9, 0xC9);
  CMD1(0xCA, 0xC8);
  CMD1(0xCB, 0xCB);
  CMD1(0xCC, 0xCA);
  CMD1(0xCD, 0x55);
  CMD1(0xCE, 0xCE);
  CMD1(0xCF, 0xCD);
  CMD1(0xD0, 0xD0);
  CMD1(0xD1, 0xCF);
  CMD1(0xF2, 0x46);
  CMD1(0xA8, 0x04);
  CMD1(0xA9, 0xB0);
  CMD1(0xAA, 0xA3);
  CMD1(0xB6, 0x00);
  CMD1(0xB7, 0xB0);
  CMD1(0xB8, 0xA3);
  CMD1(0xC4, 0x03);
  CMD1(0xC5, 0xB0);
  CMD1(0xC6, 0xA3);
  CMD1(0x80, 0x10);
  CMD1(0xFF, 0x00);
  CMD1(0x35, 0x00);
  CMD0(NV3001B_SLPOUT);
  delay(120);
  CMD1(NV3001B_COLMOD, 0x05);
  CMD1(NV3001B_MADCTL, nv3001bMADCTL(DISPLAY_ROTATION));
  CMD0(NV3001B_DISPON);
  delay(10);

#undef CMD0
#undef CMD1
#undef CMD2
}

// #743: allocate the back buffer. PSRAM first -- the RC32 has 8 MB embedded and
// SPI at 8 MHz (~1.1 MB/s) is the bottleneck for the blit, not PSRAM read speed,
// so this costs nothing measurable and keeps 55 KB of internal DRAM free.
// Falls back to internal, then to direct-to-panel if both fail.
void NV3001BDisplay::allocFrameBuffer() {
  const size_t px = (size_t)NV3001B_SCREEN_WIDTH * NV3001B_SCREEN_HEIGHT;
  const size_t bytes = px * sizeof(uint16_t);

#if defined(BOARD_HAS_PSRAM)
  frame_buf = (uint16_t*)ps_malloc(bytes);
#endif
  if (!frame_buf) frame_buf = (uint16_t*)malloc(bytes);

  if (!frame_buf) {
    // Loud, not silent (SAFELANE 6). The display still works, just unbuffered
    // and flickering, so this must not be diagnosed as "the fix didn't work".
    Serial.printf("NV3001B: back buffer alloc FAILED (%u B) -- direct panel writes, expect flicker\n",
                  (unsigned)bytes);
  }
}

// One transfer for the whole frame. Pixels are already byte-swapped in the
// buffer, so this is a straight byte blit with no per-pixel work.
void NV3001BDisplay::blitFrameBuffer() {
  if (!frame_buf || !is_on) return;

  setAddrWindow(0, 0, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT);

  busBeginTransaction();
  digitalWrite(PIN_TFT_DC, HIGH);
  digitalWrite(PIN_TFT_CS, LOW);
  busWriteBytes((const uint8_t*)frame_buf,
                (size_t)NV3001B_SCREEN_WIDTH * NV3001B_SCREEN_HEIGHT * sizeof(uint16_t));
  digitalWrite(PIN_TFT_CS, HIGH);
  busEndTransaction();
}

void NV3001BDisplay::fillPhysicalRect(int x, int y, int w, int h) {
  if (!is_on || w <= 0 || h <= 0) return;

  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > NV3001B_SCREEN_WIDTH) w = NV3001B_SCREEN_WIDTH - x;
  if (y + h > NV3001B_SCREEN_HEIGHT) h = NV3001B_SCREEN_HEIGHT - y;
  if (w <= 0 || h <= 0) return;

  // #743: buffered path. This is the ONLY function that touches the panel, so
  // intercepting it here buffers every draw -- including drawChar(), which calls
  // this once per lit font pixel and previously cost up to 35 SPI transactions
  // per character.
  if (frame_buf) {
    const uint16_t swapped = (uint16_t)((color >> 8) | (color << 8));
    for (int row = 0; row < h; row++) {
      uint16_t* p = frame_buf + (size_t)(y + row) * NV3001B_SCREEN_WIDTH + x;
      for (int col = 0; col < w; col++) p[col] = swapped;
    }
    return;
  }

  setAddrWindow(x, y, w, h);
  writeColor(color, (uint32_t)w * h);
}

// #749: colour art at native panel resolution.
//
// Coordinates are PHYSICAL pixels and deliberately do not pass through
// scaleX()/scaleY(). The logical canvas is 128x64 against a 220x128 panel -- 1.72x
// horizontally, 2.0x vertically -- so art drawn logically is stretched
// non-uniformly and the brand mark's circular arcs would come out as ellipses.
//
// With #747's back buffer present (the normal case) this is a pure memory write
// and costs no SPI at all; the frame is pushed once by endFrame() like everything
// else. The buffer is indexed in physical pixels already, so it IS the
// native-resolution surface -- no second address-window path is needed.
void NV3001BDisplay::drawRGB565(int x, int y, const uint16_t* px, int w, int h) {
  if (!is_on || !px) return;

  if (frame_buf) {
    rgb565::blitSwapped(frame_buf, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT, x, y, px, w, h);
    return;
  }

  // Unbuffered fallback. #747 treats a failed back-buffer allocation as a
  // supported degraded state rather than losing the display, so this path is real
  // and must draw rather than silently skip (SAFELANE 6). It shares clip() with the
  // buffered path above so the two cannot disagree about geometry.
  const rgb565::Clip c = rgb565::clip(NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT, x, y, w, h);
  if (!c.visible) return;

  // Scratch buffer, deliberately SMALL and FIXED. A full-width 220-pixel row would
  // be 440 B of stack on a path reached only after a heap allocation has already
  // failed -- the moment to be frugal, not to take the largest frame in the driver.
  // 64 B is a rounding error on any task stack here.
  //
  // (The #749 review proposed a VLA sized by c.w. Rejected: a VLA is still stack,
  // sized by a runtime value, so it bounds nothing a reviewer can check -- and VLAs
  // are a GCC extension in C++, not standard. Chunking costs a few more SPI
  // transactions on a path that runs once at boot in a degraded mode.)
  static const int CHUNK_PX = 32;
  uint16_t chunk[CHUNK_PX];

  for (int r = 0; r < c.h; r++) {
    const uint16_t* s = px + (size_t)(c.sy + r) * w + c.sx;
    for (int done = 0; done < c.w; done += CHUNK_PX) {
      const int n = (c.w - done) < CHUNK_PX ? (c.w - done) : CHUNK_PX;
      for (int i = 0; i < n; i++) {
        const uint16_t v = s[done + i];
        chunk[i] = (uint16_t)((v >> 8) | (v << 8));
      }
      setAddrWindow(c.dx + done, c.dy + r, n, 1);
      busBeginTransaction();
      digitalWrite(PIN_TFT_DC, HIGH);
      digitalWrite(PIN_TFT_CS, LOW);
      busWriteBytes((const uint8_t*)chunk, (size_t)n * sizeof(uint16_t));
      digitalWrite(PIN_TFT_CS, HIGH);
      busEndTransaction();
    }
  }
}

// #758: blend one RGB565 pair by a 0..15 coverage. Channels are blended
// separately in their own bit widths; doing it on the packed value would bleed
// carries between red, green and blue.
static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t cov) {
  if (cov >= 15) return fg;
  if (cov == 0) return bg;
  const uint16_t fr = (fg >> 11) & 0x1F, fgn = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  const uint16_t br = (bg >> 11) & 0x1F, bgn = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  const uint16_t r = (uint16_t)((fr * cov + br * (15 - cov)) / 15);
  const uint16_t g = (uint16_t)((fgn * cov + bgn * (15 - cov)) / 15);
  const uint16_t b = (uint16_t)((fb * cov + bb * (15 - cov)) / 15);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// #758: antialiased JetBrains Mono.
//
// Replaces a 5x7 bitmap whose every pixel was scaled into a rectangle (1 wide x 2
// tall at size 1) -- so glyphs occupied 5x14 physical pixels while carrying 7 rows
// of information. The table is native-height with per-pixel coverage instead.
//
// Coverage is blended against WHAT IS ALREADY THERE, read back out of #747's
// buffer, so text antialiases correctly over any background -- the title bar as
// well as the window. That read is why this needs the back buffer; over SPI a
// read-back per pixel would be absurd. When the buffer is absent (allocation
// failed) there is nothing to read, so coverage is thresholded instead: visibly
// worse, but the alternative is invisible or wrongly-blended text.
int NV3001BDisplay::physicalWidth() const { return NV3001B_SCREEN_WIDTH; }
int NV3001BDisplay::physicalHeight() const { return NV3001B_SCREEN_HEIGHT; }

// #822: the RC32's colour splash. Placement constants are emitted by
// scripts/gen-offband-logo.py alongside the pixels, so artwork and position cannot
// drift apart.
const DisplayDriver::ColourArt* NV3001BDisplay::colourSplashArt() const {
#ifdef OFFBAND_SPLASH_RGB565_W
  static const ColourArt art{ offband_splash_rgb565,
                              OFFBAND_SPLASH_RGB565_W, OFFBAND_SPLASH_RGB565_H,
                              OFFBAND_SPLASH_RGB565_X, OFFBAND_SPLASH_RGB565_Y };
  return &art;
#else
  return nullptr;
#endif
}

void NV3001BDisplay::drawChar(int x, int y, char ch) {
  if (ch < JBM_FIRST_CH || ch > JBM_LAST_CH) ch = '?';

  const int scale = text_size <= 1 ? 1 : text_size;
  const uint8_t* glyph = jbm_glyphs + (size_t)(ch - JBM_FIRST_CH) * JBM_BYTES_PER_GLYPH;

  for (int row = 0; row < JBM_CELL_H; row++) {
    for (int col = 0; col < JBM_CELL_W; col++) {
      const int k = row * JBM_CELL_W + col;
      const uint8_t byte = pgm_read_byte(glyph + (k >> 1));
      const uint8_t cov = (k & 1) ? (byte & 0x0F) : (byte >> 4);
      if (cov == 0) continue;

      const int px = x + col * scale;
      const int py = y + row * scale;

      if (frame_buf) {
        for (int dy = 0; dy < scale; dy++) {
          const int ty = py + dy;
          if (ty < 0 || ty >= NV3001B_SCREEN_HEIGHT) continue;
          for (int dx = 0; dx < scale; dx++) {
            const int tx = px + dx;
            if (tx < 0 || tx >= NV3001B_SCREEN_WIDTH) continue;
            uint16_t* dst = frame_buf + (size_t)ty * NV3001B_SCREEN_WIDTH + tx;
            // Buffer holds byte-swapped pixels; unswap, blend, re-swap.
            const uint16_t bg = (uint16_t)((*dst >> 8) | (*dst << 8));
            const uint16_t out = blend565(color, bg, cov);
            *dst = (uint16_t)((out >> 8) | (out << 8));
          }
        }
      } else if (cov >= 8) {
        fillPhysicalRect(px, py, scale, scale);
      }
    }
  }
}

bool NV3001BDisplay::begin() {
  if (is_on) return true;

  if (periph_power) periph_power->claim();

  setupOptionalOutput(PIN_TFT_EN, PIN_TFT_EN_ACTIVE);
  setupOptionalOutput(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_TFT_DC, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_TFT_DC, HIGH);
  delay(20);

  busBegin();
  if (PIN_TFT_RST >= 0) {
    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(10);
    digitalWrite(PIN_TFT_RST, LOW);
    delay(20);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(120);
  }

  initPanel();
  is_on = true;
  allocFrameBuffer();   // #743: before the first fill, so it lands in the buffer
  color = 0x0000;
  fillPhysicalRect(0, 0, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT);
  blitFrameBuffer();    // push the initial clear -- begin() has no endFrame()
  color = 0xffff;
  text_size = 1;
  cursor_x = 0;
  cursor_y = 0;
  writeOptionalPin(PIN_TFT_BL, PIN_TFT_BL_ACTIVE);
  return true;
}

void NV3001BDisplay::turnOn() {
  begin();
}

void NV3001BDisplay::turnOff() {
  if (!is_on) return;

  writeOptionalPin(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
  writeOptionalPin(PIN_TFT_EN, !PIN_TFT_EN_ACTIVE);
  is_on = false;
  if (periph_power) periph_power->release();
}

void NV3001BDisplay::clear() {
  uint16_t saved = color;
  color = UIColor::window_bkg;
  fillPhysicalRect(0, 0, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT);
  color = saved;
  // #743: clear() is a standalone operation, not part of a startFrame/endFrame
  // pair, so it must push its own result -- otherwise with the back buffer
  // active it would silently do nothing visible.
  blitFrameBuffer();
}

void NV3001BDisplay::startFrame(ColorVal bkg) {
  color = bkg;
  fillPhysicalRect(0, 0, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT);
  color = UIColor::primary_txt;
  text_size = 1;
  cursor_x = 0;
  cursor_y = 0;
}

void NV3001BDisplay::setTextSize(int sz) {
  text_size = sz < 1 ? 1 : sz;
}

void NV3001BDisplay::setColor(ColorVal c) {
  color = c;
}

void NV3001BDisplay::setCursor(int x, int y) {
  cursor_x = scaleX(x);
  cursor_y = scaleY(y);
}

void NV3001BDisplay::print(const char* str) {
  if (!str || !is_on) return;

  // #758: metrics come from the glyph table, not a 5x7 assumption. At size 1
  // JBM_ADVANCE is 6 and JBM_CELL_H is 14 -- exactly what font5x7-at-1x2 occupied
  // -- so nothing in the UI relayouts. Must stay in step with getTextWidth().
  const int scale = text_size <= 1 ? 1 : text_size;
  while (*str) {
    if (*str == '\n') {
      cursor_x = 0;
      cursor_y += JBM_CELL_H * scale;
    } else if (*str == '\r') {
      cursor_x = 0;
    } else {
      drawChar(cursor_x, cursor_y, *str);
      cursor_x += JBM_ADVANCE * scale;
    }
    str++;
  }
}

void NV3001BDisplay::fillRect(int x, int y, int w, int h) {
  fillPhysicalRect(scaleX(x), scaleY(y), scaleWidth(x, w), scaleHeight(y, h));
}

void NV3001BDisplay::drawRect(int x, int y, int w, int h) {
  int x1 = scaleX(x);
  int y1 = scaleY(y);
  int sw = scaleWidth(x, w);
  int sh = scaleHeight(y, h);

  fillPhysicalRect(x1, y1, sw, 1);
  fillPhysicalRect(x1, y1 + sh - 1, sw, 1);
  fillPhysicalRect(x1, y1, 1, sh);
  fillPhysicalRect(x1 + sw - 1, y1, 1, sh);
}

void NV3001BDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  if (!bits || !is_on) return;

  int byte_width = (w + 7) / 8;
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      uint8_t byte = pgm_read_byte(bits + j * byte_width + i / 8);
      if (byte & (0x80 >> (i & 7))) {
        fillPhysicalRect(scaleX(x + i), scaleY(y + j), scaleWidth(x + i, 1), scaleHeight(y + j, 1));
      }
    }
  }
}

uint16_t NV3001BDisplay::getTextWidth(const char* str) {
  if (!str) return 0;

  uint16_t len = 0;
  while (str[len] && str[len] != '\n' && str[len] != '\r') len++;
  // #758: must agree with print()'s advance or every centring decision drifts.
  const int scale = text_size <= 1 ? 1 : text_size;
  return (uint16_t)((len * JBM_ADVANCE * scale) / DISPLAY_SCALE_X);
}

// #743: the frame is drawn into RAM by fillPhysicalRect(); push it here in one
// transfer. Previously empty -- which is why the panel was erased and redrawn in
// front of the user on every update. Every sibling driver (SSD1306, SH1106,
// ST7735, LGFX) already does this; NV3001B and ST7789LCD were the exceptions.
void NV3001BDisplay::endFrame() {
  blitFrameBuffer();
}
