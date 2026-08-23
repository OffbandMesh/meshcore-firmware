// ---------------------------------------------------------------------------
// RC52-LOCAL NV3001B DRIVER (#949, epic #948)
//
// See RC52Display.h for why this file duplicates src/helpers/ui/NV3001BDisplay.cpp
// instead of porting it. Short version: porting edits a file the whole fleet
// compiles; duplicating inside one variant cannot regress RC32 or RCC6.
//
// EVERYTHING except the bus layer is carried over unchanged from the shared
// driver. If a symptom appears in the panel half, compare against that file
// first -- a divergence there is a porting bug, not an RC52 finding.
// ---------------------------------------------------------------------------

// The RC52 base build_src_filter compiles this whole variant directory, so this
// file is fed to the headless envs too. It must collapse to nothing there:
// the UIColor definitions below would otherwise collide at link with the ones
// in NullDisplayDriver.cpp, which the headless envs legitimately compile.
// Guarding the translation unit is more robust than relying on src-filter
// ordering, and it fails loudly at link rather than silently drawing nowhere.
#if defined(HELTEC_RC52_WITH_DISPLAY)

#include "RC52Display.h"
#include <helpers/ui/Rgb565Blit.h>          // shared clip + native-resolution blit
#include <helpers/ui/Rc32Palette.h>         // dark theme colour roles
#include <helpers/ui/JbmFont.h>             // antialiased JetBrains Mono glyph table
#include <helpers/ui/OffbandLogoRGB565.h>   // this panel's colour splash art
#include <Arduino.h>
#include <string.h>

// These four headers are READ, not modified. #948's acceptance criterion is that
// `git diff` touches nothing under src/helpers/ui/ -- including a shared header
// is not a change to it, and it keeps the palette, font and artwork identical to
// the sibling boards rather than forking a second copy of each.

#ifndef SPI_FREQUENCY
  // SPIM3 is the nRF52840's 32 MHz instance, but the panel is the limit, not the
  // bus. 8 MHz matches what the shared driver runs on RC32 with this same glass;
  // raising it is a separate, measured change, not a free win to assume.
  #define SPI_FREQUENCY 8000000
#endif

#ifndef PIN_TFT_SCK
  #error "PIN_TFT_SCK must be defined (variants/heltec_rc52/variant.h)"
#endif

#ifndef PIN_TFT_MOSI
  #error "PIN_TFT_MOSI must be defined (variants/heltec_rc52/variant.h)"
#endif

#ifndef PIN_TFT_CS
  #error "PIN_TFT_CS must be defined (variants/heltec_rc52/variant.h)"
#endif

#ifndef PIN_TFT_DC
  #error "PIN_TFT_DC must be defined (variants/heltec_rc52/variant.h)"
#endif

#ifndef PIN_TFT_RST
  #define PIN_TFT_RST -1
#endif

// Panel power and backlight. NOTE THE OPPOSITE POLARITIES -- this is the single
// easiest thing to get backwards on this board, and either mistake presents as a
// dead panel with no error anywhere (the panel is write-only; there is nothing
// to fail on). Values come from the vendor BSP via variant.h, not from a sibling
// board.
#ifndef PIN_TFT_VDD_CTL
  #define PIN_TFT_VDD_CTL -1
#endif

#ifndef TFT_VDD_ENABLE
  #define TFT_VDD_ENABLE LOW     // ACTIVE LOW
#endif

#ifndef PIN_TFT_LEDA_CTL
  #define PIN_TFT_LEDA_CTL -1
#endif

#ifndef TFT_LEDA_ENABLE
  #define TFT_LEDA_ENABLE HIGH   // ACTIVE HIGH
#endif

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 0
#endif

// Post-rotation drawing surface. The glass is physically 128x220 portrait; the
// UI orientation is landscape, which is what MADCTL's MV bit produces.
#ifndef RC52_SCREEN_WIDTH
  #define RC52_SCREEN_WIDTH 220
#endif

#ifndef RC52_SCREEN_HEIGHT
  #define RC52_SCREEN_HEIGHT 128
#endif

#ifndef DISPLAY_SCALE_X
  #define DISPLAY_SCALE_X ((float)RC52_SCREEN_WIDTH / RC52_DISPLAY_LOGICAL_WIDTH)
#endif

#ifndef DISPLAY_SCALE_Y
  #define DISPLAY_SCALE_Y ((float)RC52_SCREEN_HEIGHT / RC52_DISPLAY_LOGICAL_HEIGHT)
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

#ifndef RC52_TEXT_SIZE1_SCALE_X
  #define RC52_TEXT_SIZE1_SCALE_X 1
#endif

#ifndef RC52_TEXT_SIZE1_SCALE_Y
  #define RC52_TEXT_SIZE1_SCALE_Y 2
#endif

// Colour scheme -- shared with RC32/RCC6 via Rc32Palette.h so the family looks
// like one product. Values and rationale live in that header because it can be
// unit-tested; this file cannot be compiled natively.
//
// These nine symbols are declared in DisplayDriver.h and must be defined exactly
// once per link. NullDisplayDriver.cpp also defines them, which is why the
// with-display env excludes it -- see the note at the top of this file.
ColorVal UIColor::window_bkg    = rc32_palette::WINDOW_BKG;
ColorVal UIColor::title_bkg     = rc32_palette::TITLE_BKG;
ColorVal UIColor::title_txt     = rc32_palette::TITLE_TXT;
ColorVal UIColor::primary_txt   = rc32_palette::PRIMARY_TXT;
ColorVal UIColor::secondary_txt = rc32_palette::SECONDARY_TXT;
ColorVal UIColor::warning_txt   = rc32_palette::WARNING_TXT;
ColorVal UIColor::popup_bkg     = rc32_palette::POPUP_BKG;
ColorVal UIColor::popup_txt     = rc32_palette::POPUP_TXT;
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
// Bus layer -- nRF52840 SPIM3 via the core's SPI1 instance.
//
// THIS IS THE ONLY PART THAT DIFFERS FROM THE SHARED DRIVER. Three nRF52 facts
// drive it, all read from the framework rather than assumed:
//
// 1. SPIClass::begin() TAKES NO PINS. The ESP32 form is begin(SCK, MISO, MOSI,
//    CS); on this core the pins come from the SPIClass constructor, and the core
//    already built SPI1 from PIN_SPI1_* -- which are the panel's pins.
//    [framework-arduinoadafruitnrf52/libraries/SPI/SPI.h:81, SPI.cpp:289]
//
// 2. transfer(void* buf, size_t) IS DESTRUCTIVE. It is transfer(buf, buf, count)
//    and overwrites the caller's buffer. Used on the back buffer it would
//    corrupt the frame on every flush and look exactly like display noise rather
//    than like a bus fault. The three-argument form with rx_buf == NULL is the
//    correct write-only call: the implementation explicitly sets rx_length to 0
//    when rx_buf is null, so nothing is written back.
//    [SPI.h:69-70, SPI.cpp transfer(tx,rx,count)]
//
// 3. EasyDMA SOURCES FROM RAM ONLY, and this core does NOT bounce-buffer -- it
//    passes the caller's pointer straight to nrfx_spim_xfer. A flash-resident
//    buffer therefore transfers garbage, silently. Every bulk write here is
//    checked and bounced if needed rather than trusting call sites to remember.
// ---------------------------------------------------------------------------

// nRF52840 SRAM is 0x20000000..0x2003FFFF (256 KB). Flash -- and so any data not
// explicitly copied into RAM -- is below 0x20000000 and cannot be a DMA source.
//
// This is a RANGE check, not a mask on the top byte. A mask would accept the
// whole 16 MB 0x20xxxxxx block, i.e. any address from 0x20040000 up, which is
// not memory on this part. The failure a mask allows is a silent DMA read from a
// nonexistent address -- garbage on the panel with nothing to trace it to.
// (Gemini review finding 3, #949.)
static inline bool isRamPointer(const void* p) {
  const uintptr_t addr = (uintptr_t)p;
  return addr >= 0x20000000u && addr < 0x20040000u;
}

// Bounce/expansion buffer size for the two bulk paths below. Deliberately a
// STACK buffer at each call site rather than one file-static shared by both.
//
// A shared static is not safe here and locking it would be the wrong fix. The
// Gemini review (#949) raised two mechanisms; only one survived checking:
//
//   REFUTED -- "reused while a previous transfer is still in flight". This core
//   calls nrfx_spim_init(&_spim, &cfg, NULL, NULL); a NULL event handler selects
//   nrfx's BLOCKING mode, so nrfx_spim_xfer returns only once the transfer has
//   completed. There is no in-flight window.
//   [verified: framework-arduinoadafruitnrf52/libraries/SPI/SPI.cpp:101]
//
//   REAL -- two contexts rendering concurrently would interleave writes into one
//   shared buffer and corrupt each other's pixels silently. Today nothing does:
//   the only callers of `display` in this role are UITask.cpp and main.cpp, both
//   on the Arduino loop task. But that is a property of today's call sites, not
//   of this driver, and it is exactly the kind of assumption that stops being
//   true without anyone noticing.
//
// A stack buffer is re-entrant by construction, needs no mutex, has no
// initialisation order to get wrong, and costs 128 B on a render path that
// already carries a 64 B chunk buffer. Removing the shared state beats guarding
// it.
#define RC52_BOUNCE_BYTES 128

void RC52Display::busBegin() {
  // Pins come from the SPI1 constructor (PIN_SPI1_MISO/SCK/MOSI in variant.h),
  // which is why there is no pin list here. PIN_SPI1_MISO is P0.12 and is NOT
  // wired to the panel -- the core needs a valid pin number to construct with,
  // and this panel is write-only, so nothing ever reads it.
  SPI1.begin();

  // CS is driven by hand rather than by the peripheral: the panel needs CS held
  // across a command-plus-data pair, which a per-transfer hardware CS would
  // break between them.
  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
}

void RC52Display::busBeginTransaction() {
  SPI1.beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, SPI_MODE0));
}

void RC52Display::busEndTransaction() {
  SPI1.endTransaction();
}

void RC52Display::busWrite8(uint8_t b) {
  SPI1.transfer(b);
}

void RC52Display::busWriteBytes(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;

  if (isRamPointer(data)) {
    // Write-only: rx_buf NULL means rx_length 0, so `data` is never written to.
    SPI1.transfer((const void*)data, (void*)nullptr, len);
    return;
  }

  // Flash-resident source. Bounce through RAM in chunks rather than handing
  // EasyDMA an address it cannot read. Slower, but correct -- and it only
  // happens if a caller passes a genuinely flash-resident array, which the
  // panel-half code above does not (its CMD1/CMD2 payloads are function-local
  // arrays, i.e. stack, i.e. already RAM).
  uint8_t bounce[RC52_BOUNCE_BYTES];
  size_t done = 0;
  while (done < len) {
    const size_t n = (len - done) < sizeof(bounce) ? (len - done) : sizeof(bounce);
    memcpy(bounce, data + done, n);
    SPI1.transfer((const void*)bounce, (void*)nullptr, n);
    done += n;
  }
}

// The ESP32 driver uses SPIClass::writePattern(), which packs repeats into the
// hardware FIFO. This core has no equivalent, so the pattern is expanded into
// the RAM scratch once and that buffer is re-sent -- same idea, same goal of not
// paying per-byte call overhead on a full-screen fill.
//
// This runs only in the degraded unbuffered mode; with the back buffer present
// (the normal case) fills are memory writes and never reach the bus.
void RC52Display::busWritePattern(const uint8_t* pattern, size_t plen, uint32_t count) {
  if (!pattern || plen == 0 || count == 0) return;

  uint8_t expanded[RC52_BOUNCE_BYTES];

  if (plen > sizeof(expanded)) {
    // Not expected -- callers use a 2-byte pixel pattern. Degrade to a plain
    // per-repeat write rather than silently truncating (SAFELANE 6).
    while (count--) busWriteBytes(pattern, plen);
    return;
  }

  const size_t reps_per_load = sizeof(expanded) / plen;   // >= 1, since plen <= sizeof
  for (size_t i = 0; i < reps_per_load; i++) {
    memcpy(expanded + i * plen, pattern, plen);
  }
  const size_t load_bytes = reps_per_load * plen;

  while (count >= reps_per_load) {
    SPI1.transfer((const void*)expanded, (void*)nullptr, load_bytes);
    count -= (uint32_t)reps_per_load;
  }
  // Remainder: count < reps_per_load, so count*plen <= load_bytes and the read
  // stays inside `expanded`. A partial prefix of whole repeats is exactly what
  // is wanted -- the buffer holds nothing but back-to-back copies of `pattern`.
  if (count) {
    SPI1.transfer((const void*)expanded, (void*)nullptr, (size_t)count * plen);
  }
}

void RC52Display::writeCommand(uint8_t cmd) {
  busBeginTransaction();
  digitalWrite(PIN_TFT_DC, LOW);
  digitalWrite(PIN_TFT_CS, LOW);
  busWrite8(cmd);
  digitalWrite(PIN_TFT_CS, HIGH);
  busEndTransaction();
}

void RC52Display::writeBytes(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;

  busBeginTransaction();
  digitalWrite(PIN_TFT_DC, HIGH);
  digitalWrite(PIN_TFT_CS, LOW);
  // Byte-at-a-time, as in the shared driver. These are init-sequence payloads of
  // one or two bytes; routing them through DMA would cost more than it saves and
  // would need the flash bounce above.
  for (size_t i = 0; i < len; i++) {
    busWrite8(data[i]);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  busEndTransaction();
}

void RC52Display::writeCommandData(uint8_t cmd, const uint8_t* data, size_t len) {
  writeCommand(cmd);
  writeBytes(data, len);
}

void RC52Display::setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
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

void RC52Display::writeColor(uint16_t rgb, uint32_t count) {
  if (count == 0) return;

  const uint8_t pattern[2] = { (uint8_t)(rgb >> 8), (uint8_t)(rgb & 0xff) };

  busBeginTransaction();
  digitalWrite(PIN_TFT_DC, HIGH);
  digitalWrite(PIN_TFT_CS, LOW);
  busWritePattern(pattern, sizeof(pattern), count);
  digitalWrite(PIN_TFT_CS, HIGH);
  busEndTransaction();
}

// Panel init sequence -- carried over from the shared driver BYTE FOR BYTE.
// These are NV3001B register values from the vendor; they are not derived and
// must not be "tidied". If the panel is blank, suspect the bus layer or the
// EN/BL polarities before suspecting this block, which is proven on RC32.
void RC52Display::initPanel() {
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

// Back buffer: 220 * 128 * 2 = 56,320 B.
//
// NO PSRAM PATH HERE. The shared driver tries ps_malloc first because the RC32's
// S3 has 8 MB embedded; the nRF52840 has none, so internal SRAM is the only
// option and the allocation is a real question on this board rather than a
// formality. Repeater and room server have roughly 190 KB of region free and
// this fits comfortably; the BLE companion has roughly 75 KB and does not
// obviously fit. That is #856, and the failure is handled, not assumed away.
void RC52Display::allocFrameBuffer() {
  const size_t px = (size_t)RC52_SCREEN_WIDTH * RC52_SCREEN_HEIGHT;
  const size_t bytes = px * sizeof(uint16_t);

  frame_buf = (uint16_t*)malloc(bytes);

  if (!frame_buf) {
    // Loud, not silent (SAFELANE 6). The display still works, just unbuffered
    // and flickering, so this must not later be diagnosed as "the driver is
    // broken" or as a bus fault.
    Serial.print("RC52Display: back buffer alloc FAILED (");
    Serial.print((unsigned long)bytes);
    Serial.println(" B) -- direct panel writes, expect flicker");
    return;
  }

  // EasyDMA cannot source from flash; malloc'd memory is SRAM, but assert it
  // rather than assume, because the whole blit silently transfers garbage if it
  // is ever not true.
  if (!isRamPointer(frame_buf)) {
    Serial.println("RC52Display: back buffer is not in SRAM -- refusing to DMA from it");
    free(frame_buf);
    frame_buf = nullptr;
  }
}

// One transfer for the whole frame. Pixels are already byte-swapped in the
// buffer, so this is a straight byte blit with no per-pixel work.
void RC52Display::blitFrameBuffer() {
  if (!frame_buf || !is_on) return;

  setAddrWindow(0, 0, RC52_SCREEN_WIDTH, RC52_SCREEN_HEIGHT);

  busBeginTransaction();
  digitalWrite(PIN_TFT_DC, HIGH);
  digitalWrite(PIN_TFT_CS, LOW);
  busWriteBytes((const uint8_t*)frame_buf,
                (size_t)RC52_SCREEN_WIDTH * RC52_SCREEN_HEIGHT * sizeof(uint16_t));
  digitalWrite(PIN_TFT_CS, HIGH);
  busEndTransaction();
}

void RC52Display::fillPhysicalRect(int x, int y, int w, int h) {
  if (!is_on || w <= 0 || h <= 0) return;

  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > RC52_SCREEN_WIDTH) w = RC52_SCREEN_WIDTH - x;
  if (y + h > RC52_SCREEN_HEIGHT) h = RC52_SCREEN_HEIGHT - y;
  if (w <= 0 || h <= 0) return;

  // Buffered path. This is the ONLY function that touches the panel, so
  // intercepting here buffers every draw -- including drawChar(), which calls it
  // once per lit font pixel.
  if (frame_buf) {
    const uint16_t swapped = (uint16_t)((color >> 8) | (color << 8));
    for (int row = 0; row < h; row++) {
      uint16_t* p = frame_buf + (size_t)(y + row) * RC52_SCREEN_WIDTH + x;
      for (int col = 0; col < w; col++) p[col] = swapped;
    }
    return;
  }

  setAddrWindow(x, y, w, h);
  writeColor(color, (uint32_t)w * h);
}

// Colour art at native panel resolution. Coordinates are PHYSICAL pixels and
// deliberately do not pass through scaleX()/scaleY(): the logical canvas is
// 128x64 against a 220x128 panel -- 1.72x horizontally, 2.0x vertically -- so art
// drawn logically comes out non-uniformly stretched and circular arcs become
// ellipses.
void RC52Display::drawRGB565(int x, int y, const uint16_t* px, int w, int h) {
  if (!is_on || !px) return;

  if (frame_buf) {
    rgb565::blitSwapped(frame_buf, RC52_SCREEN_WIDTH, RC52_SCREEN_HEIGHT, x, y, px, w, h);
    return;
  }

  // Unbuffered fallback. A failed back-buffer allocation is a supported degraded
  // state rather than a lost display, so this path is real and must draw rather
  // than silently skip (SAFELANE 6). It shares clip() with the buffered path so
  // the two cannot disagree about geometry.
  //
  // NOTE this is the path #856 says has never executed on hardware on ANY board.
  // On RC52 it is reachable for real (56 KB against a companion's ~75 KB free),
  // which makes it the first board where it must actually be exercised rather
  // than assumed.
  const rgb565::Clip c = rgb565::clip(RC52_SCREEN_WIDTH, RC52_SCREEN_HEIGHT, x, y, w, h);
  if (!c.visible) return;

  // Scratch buffer, deliberately SMALL and FIXED, on a path reached only after a
  // heap allocation has already failed -- the moment to be frugal.
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
      // `chunk` is stack -- SRAM -- so this is a legal DMA source.
      busWriteBytes((const uint8_t*)chunk, (size_t)n * sizeof(uint16_t));
      digitalWrite(PIN_TFT_CS, HIGH);
      busEndTransaction();
    }
  }
}

// Blend one RGB565 pair by a 0..15 coverage. Channels are blended separately in
// their own bit widths; doing it on the packed value would bleed carries between
// red, green and blue.
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

int RC52Display::physicalWidth() const { return RC52_SCREEN_WIDTH; }
int RC52Display::physicalHeight() const { return RC52_SCREEN_HEIGHT; }

const DisplayDriver::ColourArt* RC52Display::colourSplashArt() const {
#ifdef OFFBAND_SPLASH_RGB565_W
  static const ColourArt art{ offband_splash_rgb565,
                              OFFBAND_SPLASH_RGB565_W, OFFBAND_SPLASH_RGB565_H,
                              OFFBAND_SPLASH_RGB565_X, OFFBAND_SPLASH_RGB565_Y };
  return &art;
#else
  return nullptr;
#endif
}

// Antialiased JetBrains Mono. Coverage is blended against what is already in the
// back buffer, so text antialiases correctly over any background. When the
// buffer is absent there is nothing to read back, so coverage is thresholded
// instead: visibly worse, but the alternative is invisible or wrongly-blended
// text.
void RC52Display::drawChar(int x, int y, char ch) {
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
          if (ty < 0 || ty >= RC52_SCREEN_HEIGHT) continue;
          for (int dx = 0; dx < scale; dx++) {
            const int tx = px + dx;
            if (tx < 0 || tx >= RC52_SCREEN_WIDTH) continue;
            uint16_t* dst = frame_buf + (size_t)ty * RC52_SCREEN_WIDTH + tx;
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

bool RC52Display::begin() {
  if (is_on) return true;

  if (periph_power) periph_power->claim();

  // TFT_VDD_CTL is ACTIVE LOW and TFT_LEDA_CTL is ACTIVE HIGH. Both are asserted
  // through the same two helpers, so the polarity lives in exactly one place per
  // pin and cannot drift between begin() and turnOff().
  setupOptionalOutput(PIN_TFT_VDD_CTL, TFT_VDD_ENABLE);
  setupOptionalOutput(PIN_TFT_LEDA_CTL, !TFT_LEDA_ENABLE);   // backlight off until the panel has content
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
  allocFrameBuffer();   // before the first fill, so it lands in the buffer
  color = 0x0000;
  fillPhysicalRect(0, 0, RC52_SCREEN_WIDTH, RC52_SCREEN_HEIGHT);
  blitFrameBuffer();    // push the initial clear -- begin() has no endFrame()
  color = 0xffff;
  text_size = 1;
  cursor_x = 0;
  cursor_y = 0;
  writeOptionalPin(PIN_TFT_LEDA_CTL, TFT_LEDA_ENABLE);
  return true;
}

void RC52Display::turnOn() {
  begin();
}

void RC52Display::turnOff() {
  if (!is_on) return;

  writeOptionalPin(PIN_TFT_LEDA_CTL, !TFT_LEDA_ENABLE);
  writeOptionalPin(PIN_TFT_VDD_CTL, !TFT_VDD_ENABLE);
  is_on = false;
  if (periph_power) periph_power->release();
}

void RC52Display::clear() {
  uint16_t saved = color;
  color = UIColor::window_bkg;
  fillPhysicalRect(0, 0, RC52_SCREEN_WIDTH, RC52_SCREEN_HEIGHT);
  color = saved;
  // clear() is a standalone operation, not part of a startFrame/endFrame pair,
  // so it must push its own result -- otherwise with the back buffer active it
  // would silently do nothing visible.
  blitFrameBuffer();
}

void RC52Display::startFrame(ColorVal bkg) {
  color = bkg;
  fillPhysicalRect(0, 0, RC52_SCREEN_WIDTH, RC52_SCREEN_HEIGHT);
  color = UIColor::primary_txt;
  text_size = 1;
  cursor_x = 0;
  cursor_y = 0;
}

void RC52Display::setTextSize(int sz) {
  text_size = sz < 1 ? 1 : sz;
}

void RC52Display::setColor(ColorVal c) {
  color = c;
}

void RC52Display::setCursor(int x, int y) {
  cursor_x = scaleX(x);
  cursor_y = scaleY(y);
}

void RC52Display::print(const char* str) {
  if (!str || !is_on) return;

  // Metrics come from the glyph table, not a 5x7 assumption. Must stay in step
  // with getTextWidth() or every centring decision drifts.
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

void RC52Display::fillRect(int x, int y, int w, int h) {
  fillPhysicalRect(scaleX(x), scaleY(y), scaleWidth(x, w), scaleHeight(y, h));
}

void RC52Display::drawRect(int x, int y, int w, int h) {
  int x1 = scaleX(x);
  int y1 = scaleY(y);
  int sw = scaleWidth(x, w);
  int sh = scaleHeight(y, h);

  fillPhysicalRect(x1, y1, sw, 1);
  fillPhysicalRect(x1, y1 + sh - 1, sw, 1);
  fillPhysicalRect(x1, y1, 1, sh);
  fillPhysicalRect(x1 + sw - 1, y1, 1, sh);
}

void RC52Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
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

uint16_t RC52Display::getTextWidth(const char* str) {
  if (!str) return 0;

  uint16_t len = 0;
  while (str[len] && str[len] != '\n' && str[len] != '\r') len++;
  // Must agree with print()'s advance or every centring decision drifts.
  const int scale = text_size <= 1 ? 1 : text_size;
  return (uint16_t)((len * JBM_ADVANCE * scale) / DISPLAY_SCALE_X);
}

// The frame is drawn into RAM by fillPhysicalRect(); push it here in one
// transfer. An empty endFrame() is what erases and redraws the panel in front of
// the user on every update.
void RC52Display::endFrame() {
  blitFrameBuffer();
}

#endif  // HELTEC_RC52_WITH_DISPLAY
