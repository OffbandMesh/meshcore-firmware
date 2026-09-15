#pragma once

// #1230: drawing the owner's design (the QCC mockups) on a 1-bit 128 x 64 display:
// 8 px rows, the built-in 6 x 8 font, inverted bars and selections. Where things go is
// decided in helpers/ui/BadgeLayout.h; this only draws.

#include <string.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/BadgeLayout.h>

namespace badgeui {

constexpr int kRowPx = 8;
constexpr int kCellPx = 6;
constexpr int kScreenPx = 128;
constexpr int kScreenRowsPx = 64;
constexpr int kListRows = 7;   // under a title bar

// Code page 437 glyphs in the display's built-in font.
constexpr char kGlyphPin = '\x07';    // a bullet: pinned
constexpr char kGlyphUp = '\x1E';     // more above
constexpr char kGlyphDown = '\x1F';   // more below
constexpr char kGlyphTick = '\xFB';   // delivered

inline void lit(DisplayDriver& d) { d.setColor(UIColor::primary_txt); }
inline void dark(DisplayDriver& d) { d.setColor(UIColor::window_bkg); }

inline int textPx(const char* s) { return (int)strlen(s) * kCellPx; }

// Text with its top-left at (x, y): lit, or dark when `on_lit` (drawn over a lit
// block the caller filled). Only whole characters inside the screen are drawn: the
// font wraps a character that would cross the right edge onto the next row, and a
// character entirely off the left edge is skipped, which is what lets a marquee
// start at a negative x.
inline void textAt(DisplayDriver& d, int x, int y, const char* s, bool on_lit = false) {
  while (*s != 0 && x + kCellPx <= 0) {
    x += kCellPx;
    s++;
  }
  char buf[kScreenPx / kCellPx + 2];
  size_t n = 0;
  while (s[n] != 0 && n < sizeof(buf) - 1 && x + (int)(n + 1) * kCellPx <= kScreenPx) {
    buf[n] = s[n];
    n++;
  }
  buf[n] = 0;
  if (n == 0) return;
  if (on_lit) dark(d); else lit(d);
  d.setCursor(x, y);
  d.print(buf);
  lit(d);
}

// Text at a grid cell, on its own lit block when `inverted`.
inline void cell(DisplayDriver& d, int col, int row, const char* s, bool inverted = false) {
  const int x = col * kCellPx, y = row * kRowPx;
  if (inverted) {
    lit(d);
    d.fillRect(x, y, textPx(s), kRowPx);
  }
  textAt(d, x, y, s, inverted);
}

// A whole row lit, for a title bar, a selection or a flash.
inline void fillRow(DisplayDriver& d, int row) {
  lit(d);
  d.fillRect(0, row * kRowPx, kScreenPx, kRowPx);
}

// An inverted bar across a row: `left` from the left edge, `right` flush right.
inline void bar(DisplayDriver& d, int row, const char* left, const char* right) {
  fillRow(d, row);
  textAt(d, 0, row * kRowPx, left, true);
  if (right != nullptr && right[0] != 0) {
    textAt(d, kScreenPx - textPx(right) - 1, row * kRowPx, right, true);
  }
}

// An unread count in a box of the opposite colour, ending at x_end. Returns its left
// edge.
inline int countBox(DisplayDriver& d, int x_end, int y, const char* n, bool on_lit) {
  const int w = textPx(n) + 1;
  const int x = x_end - w;
  if (on_lit) dark(d); else lit(d);
  d.fillRect(x, y, w, kRowPx);
  textAt(d, x + 1, y, n, !on_lit);
  return x;
}

// The dotted rule the design puts over a compose line or a footer.
inline void dottedRule(DisplayDriver& d, int y) {
  lit(d);
  for (int x = 0; x < kScreenPx; x += 2) d.fillRect(x, y, 1, 1);
}

// Knocks out every other pixel: a 1-bit display's way of dimming a quiet row.
inline void dither(DisplayDriver& d, int x0, int y0, int w, int h) {
  dark(d);
  for (int y = y0; y < y0 + h; y++) {
    for (int x = x0 + ((y + x0) & 1); x < x0 + w; x += 2) d.fillRect(x, y, 1, 1);
  }
  lit(d);
}

// A 10 x 5 battery, filled to `pct`, drawn dark for a title bar.
inline void battery(DisplayDriver& d, int x, int y, int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  dark(d);
  d.drawRect(x, y, 10, 5);
  d.fillRect(x + 10, y + 1, 1, 3);   // the cap
  d.fillRect(x + 1, y + 1, (8 * pct) / 100, 3);
  lit(d);
}

// #1231: where SW1's cycle is (design 1f): the three lists, the current one inverted.
// Shown for 2 s after each move. " MSGS CONTACTS STATUS" is exactly 21 cells.
inline void breadcrumb(DisplayDriver& d, int row, int pos) {
  static const char* const kLabels[3] = {"MSGS", "CONTACTS", "STATUS"};
  static const int kCol[3] = {1, 6, 15};
  for (int i = 0; i < 3; i++) cell(d, kCol[i], row, kLabels[i], i == pos);
}

// Three dots: a message still sending.
inline void sendingDots(DisplayDriver& d, int x, int y) {
  lit(d);
  for (int i = 0; i < 3; i++) d.fillRect(x + i * 2, y + 5, 1, 1);
}

}  // namespace badgeui
