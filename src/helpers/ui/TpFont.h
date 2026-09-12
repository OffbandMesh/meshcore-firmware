#pragma once

#include <stdint.h>
#include "DisplayDriver.h"

namespace offband {
namespace tpfont {

// Fonts in the ThingPulse OLEDDisplay format -- the ArialMT_Plain_* tables in
// OLEDDisplayFonts.cpp. Byte 0 is the max width, byte 1 the height, byte 2 the first
// char, byte 3 the char count. Then one 4-byte jump entry per char (glyph offset MSB,
// offset LSB, glyph byte count, advance), then the glyph bytes: column by column,
// (height + 7) / 8 bytes per column, bit 0 at the top. Offset 0xFFFF marks a char
// with no ink (space). Reads the tables directly: on nRF52 and ESP32 flash is
// memory-mapped, so no pgm_read_byte is needed.

uint8_t height(const uint8_t* font);

// Advance of c in pixels. A char outside the font is drawn, and measured, as '?'.
int charAdvance(const uint8_t* font, char c);

int textWidth(const uint8_t* font, const char* s);

// Draws s with its top-left at (x, y) in the display's current color, one fillRect
// per vertical run of ink, so it works on any DisplayDriver. Returns the x just past
// the last glyph.
int drawText(DisplayDriver& display, int x, int y, const uint8_t* font, const char* s);

}  // namespace tpfont
}  // namespace offband
