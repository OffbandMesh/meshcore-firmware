#pragma once

// ---------------------------------------------------------------------------
// RC52-LOCAL NV3001B DRIVER (#949, epic #948)
//
// This is a deliberate, scoped duplicate of src/helpers/ui/NV3001BDisplay.{h,cpp}
// with one difference: the bus layer targets the nRF52840's SPIM3 instead of an
// ESP32 SPI host. Nothing under src/helpers/ui/ is modified by this variant --
// that is the acceptance criterion of #948, and it is why RC32 and RCC6 *cannot*
// regress from RC52 display work rather than merely *should not*.
//
// The panel logic below (init sequence, MADCTL, address window, glyph raster,
// back buffer, scaling) is carried over unchanged from the shared driver, which
// is in production on RC32 hardware. Only the six bus* methods differ. Keeping
// the panel half byte-identical is the point: a blank panel is then a bus
// question, not a "did the port drift" question.
//
// WHY DUPLICATE RATHER THAN PORT THE SHARED DRIVER (#948):
//   Porting means editing a file every board in the fleet compiles, regression
//   testing two shipping boards, and gating RC52 on all of it. Duplication
//   inside one variant is cheap and reversible; a fleet-wide refactor blocking
//   this board is neither.
// ---------------------------------------------------------------------------

// Path-qualified, unlike the shared driver's bare "DisplayDriver.h". That form
// only resolves because NV3001BDisplay.h sits in the same directory; this file
// does not, so it goes through the src/ include root like every other consumer.
#include <helpers/ui/DisplayDriver.h>
#include <SPI.h>
#include <helpers/RefCountedDigitalPin.h>

// Logical canvas every DisplayDriver presents. 128x64 is an OLED-sized canvas
// stretched onto a 220x128 panel; that distortion is inherited from the shared
// driver and tracked fleet-wide as #705. Not fixed here -- matching the shared
// driver's layout exactly is what makes this port reviewable.
#ifndef RC52_DISPLAY_LOGICAL_WIDTH
  #define RC52_DISPLAY_LOGICAL_WIDTH 128
#endif

#ifndef RC52_DISPLAY_LOGICAL_HEIGHT
  #define RC52_DISPLAY_LOGICAL_HEIGHT 64
#endif

// ---------------------------------------------------------------------------
// NO SOFTWARE-SPI PATH HERE, AND THAT IS DELIBERATE.
//
// The shared driver carries NV3001B_USE_SOFTWARE_SPI because the RCC6 has no
// SPI peripheral left for the panel -- SPI0/SPI1 drive flash and SPI2 goes to
// LoRa, so the vendor bit-bangs it.
//
// RC52 is not that board. The vendor BSP puts the TFT on SPI1 backed by SPIM3,
// the nRF52840's single 32 MHz instance, with LoRa on SPI0
// (SPI_INTERFACES_COUNT 2, SPI_32MHZ_INTERFACE 1). There is a dedicated
// hardware SPI here and no contention to avoid. Do NOT port the RCC6 workaround
// into this file reflexively -- it would be slower for no reason.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// BACK BUFFER STORAGE FORMAT (#856)
//
// The full-frame buffer is what removes flicker: without it startFrame() erases
// the physical panel over SPI before anything is drawn, and the repaint is
// visible. Measured on this board, unbuffered: ~167 ms per frame against ~13 ms
// buffered, and boot ~4 s slower.
//
// But at 16bpp it costs 220*128*2 = 56,320 B, and on this board that is the
// difference between `new HomeScreen` succeeding and failing (#973). RGB332
// halves it to 28,160 B and keeps the buffer -- so flicker stays fixed and 28 KB
// comes back.
//
// The cost is colour fidelity: 3 bits red, 3 green, 2 blue. Text antialiasing
// blends through fewer distinct shades, and the colour splash art quantises.
// Both are quality, not function.
//
// Default stays 16bpp. This is opt-in per env so the two can be compared on the
// same panel rather than argued about.
// ---------------------------------------------------------------------------
// RC52_DISPLAY_BUFFER_INDEXED -- 8 bits per pixel, but the byte is an INDEX into
// a 256-entry RGB565 table rather than a packed colour. Same 28,160 B as RGB332
// and the same flicker fix, without RGB332's colour error.
//
// Why not RGB332: measured on this palette, RGB332 shifts DEEP (#1A7A44, the
// title-bar green) by dR -24 dG -12 dB +16 -- it loses all its red and gains
// blue, reading visibly more blue-green. MUTED drifts +33 blue. Blue only gets
// 2 bits in 332, i.e. four levels across the whole channel, so any colour with a
// small blue component snaps to the nearest quarter. Owner spotted the green on
// hardware before this was computed.
//
// Indexed stores the brand colours EXACTLY. Only text-antialiasing blends are
// approximated, and only to the nearest entry in a purpose-built ramp.
#if defined(RC52_DISPLAY_BUFFER_INDEXED) || defined(RC52_DISPLAY_BUFFER_8BPP)
  typedef uint8_t  rc52_px_t;
#else
  typedef uint16_t rc52_px_t;
#endif

class RC52Display : public DisplayDriver {
  RefCountedDigitalPin* periph_power;
  bool is_on = false;
  uint16_t color = 0xffff;
  uint8_t text_size = 1;
  int cursor_x = 0;
  int cursor_y = 0;

  // Back buffer. Every panel write funnels through fillPhysicalRect(), so when
  // this is non-null the draw path writes here instead of over SPI and
  // endFrame() blits the whole thing in one transfer. Pixels are stored
  // BYTE-SWAPPED (the panel wants big-endian RGB565) so the blit is a plain
  // byte write with no per-pixel conversion.
  //
  // Null is a SUPPORTED state: allocation failure degrades to direct-to-panel
  // writes rather than losing the display (SAFELANE 6). On this board that is
  // not hypothetical -- 220*128*2 = 56,320 B against 237,568 B of region, and
  // the BLE companion role already spends ~162 KB of it. See #856.
  rc52_px_t* frame_buf = nullptr;

  void allocFrameBuffer();
  void blitFrameBuffer();

  // Bus abstraction. Every panel write goes through these six, so the whole
  // nRF52-vs-ESP32 difference between this file and the shared driver is
  // contained here.
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
  // NOTE: no SPIClass member. The Arduino core already instantiates
  //   SPIClass SPI1(_SPI1_DEV, PIN_SPI1_MISO, PIN_SPI1_SCK, PIN_SPI1_MOSI)
  // from this variant's own pins, and those pins ARE the panel's
  // (PIN_SPI1_MOSI == PIN_TFT_MOSI == P1.03, PIN_SPI1_SCK == PIN_TFT_SCK ==
  // P0.30). Constructing a second SPIClass over the same SPIM instance would
  // fight the core's. Reference it; do not construct one.
  RC52Display(RefCountedDigitalPin* power = nullptr) :
      DisplayDriver(RC52_DISPLAY_LOGICAL_WIDTH, RC52_DISPLAY_LOGICAL_HEIGHT), periph_power(power) { }

  bool begin();
  static const char* driverName() { return "RC52-NV3001B"; }

  // POST-ROTATION drawing size, not the panel's memory orientation.
  int physicalWidth() const override;
  int physicalHeight() const override;

  // This driver's own colour splash art. Capability and asset are one
  // declaration, so no per-variant flag can be forgotten.
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
  // Colour art at NATIVE panel resolution -- physical pixels, deliberately
  // bypassing scaleX()/scaleY().
  void drawRGB565(int x, int y, const uint16_t* px, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
};
