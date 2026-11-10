#ifdef GUI_PANEL_EINK

#include "EinkPanel.h"

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <MeshLog.h>
#include <SPI.h>

// The panel, the SX1262 and the micro-SD share one SPI bus, and a pin routes
// to one SPI peripheral at a time. The radio uses this same SPIClass
// (target.cpp): whichever begins it first, the other's begin() returns early.
SPIClass SPI1 = SPIClass(FSPI);

namespace {

GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT> s_epd(
    EINK_DISPLAY_MODEL(PIN_DISPLAY_CS, PIN_DISPLAY_DC, PIN_DISPLAY_RST, PIN_DISPLAY_BUSY));

// Front-light PWM. Nothing else on this board uses LEDC; channel 3 keeps
// clear of channel 0, Arduino's default, as GuiPanel's backlight does.
constexpr uint8_t kFrontlightChannel = 3;
constexpr uint32_t kFrontlightHz = 5000;
constexpr uint8_t kFrontlightBits = 8;

// GxEPD2 gives up on a BUSY wait after this long and says so only on Serial,
// which the capture never records. A refresh is about a second, so one that
// reaches this had a wait time out or stall (10 s is GxEPD2's value for
// GDEQ031T10).
#ifndef EINK_BUSY_TIMEOUT_MS
  #define EINK_BUSY_TIMEOUT_MS 10000UL
#endif

void noteRefresh(const char* what, uint32_t ms) {
#ifdef OFFBAND_EINK_TIMING
  mesh_log_line(MLOG_BOOT, "[gui] eink: %s %lu ms\n", what, (unsigned long)ms);
#endif
  if (ms >= EINK_BUSY_TIMEOUT_MS) {
    mesh_log_line(MLOG_BOOT, "[gui] eink: BUSY timeout or stall in %s (limit %lu ms)\n", what,
                  (unsigned long)EINK_BUSY_TIMEOUT_MS);
  }
}

}  // namespace

bool GuiPanel::begin() {
  if (_ready) return true;
  if (_peripher_power) _peripher_power->claim();

#if defined(PIN_DISPLAY_FRONTLIGHT) && PIN_DISPLAY_FRONTLIGHT >= 0
  ledcSetup(kFrontlightChannel, kFrontlightHz, kFrontlightBits);
  ledcAttachPin(PIN_DISPLAY_FRONTLIGHT, kFrontlightChannel);
  ledcWrite(kFrontlightChannel, 0);
#endif

  const uint32_t t0 = millis();
  s_epd.epd2.selectSPI(SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  SPI1.begin(PIN_DISPLAY_SCLK, PIN_DISPLAY_MISO, PIN_DISPLAY_MOSI, PIN_DISPLAY_CS);
  // No serial diag (GxEPD2 would print to Serial), a first init, a 2 ms
  // reset pulse, RST driven rather than pulled.
  s_epd.init(0, true, 2, false);
  s_epd.setRotation(0);
  s_epd.setFullWindow();
  s_epd.fillScreen(GxEPD_WHITE);
  s_epd.display(false);   // a full refresh clears whatever was on the glass
  _init_ms = millis() - t0;
  noteRefresh("init + full refresh", _init_ms);

  _ready = true;
  _on = true;
  return true;
}

void GuiPanel::replayInitTiming() {
  if (_ready) noteRefresh("init + full refresh", _init_ms);
}

void GuiPanel::setBrightness(uint8_t pct) {
#if defined(PIN_DISPLAY_FRONTLIGHT) && PIN_DISPLAY_FRONTLIGHT >= 0
  if (pct > 100) pct = 100;
  ledcWrite(kFrontlightChannel, uint32_t(pct) * 255 / 100);
#else
  (void)pct;
#endif
}

void GuiPanel::turnOn() {
  if (!_ready || _on) return;
  _on = true;
  _need_full = true;   // GxEPD2 resets the controller on its next write
}

void GuiPanel::turnOff() {
  if (!_ready || !_on) return;
  setBrightness(0);
  s_epd.hibernate();
  _on = false;
}

void GuiPanel::setInk(int16_t x, int16_t y, bool ink) { s_epd.drawPixel(x, y, ink ? GxEPD_BLACK : GxEPD_WHITE); }

void GuiPanel::push(bool full, int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
  if (!_ready) return;
  const uint32_t t0 = millis();
  if (full) {
    s_epd.display(false);
    noteRefresh("full refresh", millis() - t0);
  } else {
    // GxEPD2 widens the window to the controller's 8-pixel addressing.
    s_epd.displayWindow(uint16_t(x1), uint16_t(y1), uint16_t(x2 - x1 + 1), uint16_t(y2 - y1 + 1));
    noteRefresh("partial refresh", millis() - t0);
  }
}

void GuiPanel::setRotation(uint16_t rotation) {
  // GxEPD2 counts quarter turns. 180 isn't offered (GuiLayout.h).
  s_epd.setRotation(rotation == 90 ? 1 : rotation == 270 ? 3 : 0);
  s_epd.setFullWindow();
}

#endif  // GUI_PANEL_EINK
