#pragma once

// #1230: drawing the owner's design (the QCC mockups) on a 1-bit 128 x 64 display.
// #1237: text is drawn in a face (BadgeFonts.h) rather than the display's own 6 x 8
// font, and positions are pixels. Where things go is decided in BadgeLayout.h; this
// only draws.
//
// The faces cover 0x20-0x7E, so the design's marks -- the pinned dot, the scroll
// arrows and the delivered tick -- are drawn as shapes instead of printed.

#include <string.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/BadgeLayout.h>

namespace badgeui {

inline void lit(DisplayDriver& d) { d.setColor(UIColor::primary_txt); }
inline void dark(DisplayDriver& d) { d.setColor(UIColor::window_bkg); }

inline int rowY(const Face& f, int row) { return row * f.row_px; }

// Text with its top-left at (x, y) in face `f`: lit, or dark when `on_lit` (drawn over
// a lit block the caller filled). Only whole characters inside the screen are drawn: a
// character that would cross the right edge is left out, and one entirely off the left
// is skipped, which is what lets a marquee start at a negative x.
inline void textAt(DisplayDriver& d, const Face& f, int x, int y, const char* s, bool on_lit = false) {
  d.setFace(f.id);
  while (*s != 0) {
    const int w = charPx(f, *s);
    if (x + w > 0) break;
    x += w;
    s++;
  }
  if (on_lit) dark(d); else lit(d);
  char run[64];
  size_t n = 0;
  int run_x = x;
  for (; *s != 0; s++) {
    const uint8_t u = (uint8_t)*s;
    const int w = charPx(f, *s);
    if (x + w > kScreenPx) break;
    if (u >= kFaceFirst && u <= kFaceLast && n < sizeof(run) - 1) {
      if (n == 0) run_x = x;
      run[n++] = (char)u;
    } else {
      // The faces stop at 0x7E. translateUTF8ToBlocks turns anything else into a block,
      // and so does this, which is what the display's own font drew.
      if (n > 0) {
        run[n] = 0;
        d.setCursor(run_x, y);
        d.print(run);
        n = 0;
      }
      d.fillRect(x, y + 1, w - 1, f.row_px - 2);
    }
    x += w;
  }
  if (n > 0) {
    run[n] = 0;
    d.setCursor(run_x, y);
    d.print(run);
  }
  lit(d);
}

// Text at the left of a row.
inline void line(DisplayDriver& d, const Face& f, int row, const char* s, bool on_lit = false) {
  textAt(d, f, 0, rowY(f, row), s, on_lit);
}

// Text flush right on a row, clear of the edge.
inline void lineRight(DisplayDriver& d, const Face& f, int row, const char* s, bool on_lit = false) {
  textAt(d, f, kScreenPx - kEdgePx - textPx(f, s), rowY(f, row), s, on_lit);
}

// A whole row lit, for a title bar, a selection or a flash.
inline void fillRow(DisplayDriver& d, const Face& f, int row) {
  lit(d);
  d.fillRect(0, rowY(f, row), kScreenPx, f.row_px);
}

// An inverted bar across a row: `left` from the left edge, `right` flush right.
inline void bar(DisplayDriver& d, const Face& f, int row, const char* left, const char* right) {
  fillRow(d, f, row);
  textAt(d, f, 0, rowY(f, row), left, true);
  if (right != nullptr && right[0] != 0) {
    textAt(d, f, kScreenPx - textPx(f, right) - kEdgePx, rowY(f, row), right, true);
  }
}

// An unread count in a box of the opposite colour, ending at x_end. Returns its left
// edge.
inline int countBox(DisplayDriver& d, const Face& f, int x_end, int y, const char* n, bool on_lit) {
  const int w = textPx(f, n) + 2;
  const int x = x_end - w;
  if (on_lit) dark(d); else lit(d);
  d.fillRect(x, y, w, f.row_px);
  textAt(d, f, x + 1, y, n, !on_lit);
  return x;
}

// The dotted rule the design puts over a compose line or a footer.
inline void dottedRule(DisplayDriver& d, int y) {
  lit(d);
  for (int x = 0; x < kScreenPx; x += 2) d.fillRect(x, y, 1, 1);
}

// The design's marks. The faces have no glyphs for them, and shapes stay the same size
// whichever face a row is in.
inline void markPin(DisplayDriver& d, int x, int y) {   // pinned: a small square
  lit(d);
  d.fillRect(x + 1, y + 2, 2, 2);
}

inline void markUp(DisplayDriver& d, int x, int y, bool on_lit = false) {   // more above
  if (on_lit) dark(d); else lit(d);
  for (int i = 0; i < 3; i++) d.fillRect(x + 2 - i, y + 2 + i, 1 + i * 2, 1);
  lit(d);
}

inline void markDown(DisplayDriver& d, int x, int y, bool on_lit = false) {   // more below
  if (on_lit) dark(d); else lit(d);
  for (int i = 0; i < 3; i++) d.fillRect(x + i, y + 2 + i, 5 - i * 2, 1);
  lit(d);
}

inline void markTick(DisplayDriver& d, int x, int y) {   // delivered, or a repeat heard
  lit(d);
  d.fillRect(x, y + 3, 1, 2);
  d.fillRect(x + 1, y + 4, 1, 2);
  d.fillRect(x + 2, y + 3, 1, 2);
  d.fillRect(x + 3, y + 1, 1, 2);
  d.fillRect(x + 4, y, 1, 2);
}

inline void markCross(DisplayDriver& d, int x, int y) {   // a send that failed
  lit(d);
  for (int i = 0; i < 5; i++) {
    d.fillRect(x + i, y + 1 + i, 1, 1);
    d.fillRect(x + 4 - i, y + 1 + i, 1, 1);
  }
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

// #1231: where SW1's cycle is (design 1f): the lists, the current one inverted. Shown
// for 2 s after each move. #1234 made it four stops, and four full names don't fit, so
// each is four letters.
inline void breadcrumb(DisplayDriver& d, const Face& f, int row, int pos) {
  static const char* const kLabels[kCycleStops] = {"MSGS", "CONT", "NEAR", "STAT"};
  const int y = rowY(f, row);
  int x = 2;
  for (int i = 0; i < kCycleStops; i++) {
    const int w = textPx(f, kLabels[i]);
    if (i == pos) {
      lit(d);
      d.fillRect(x - 1, y, w + 2, f.row_px);
    }
    textAt(d, f, x, y, kLabels[i], i == pos);
    x += w + 5;
  }
}

// "2 of 4": the title's right slot while the breadcrumb shows.
inline void cycleTitle(char* out, size_t n, int pos) { snprintf(out, n, "%d of %d", pos + 1, kCycleStops); }

// Three dots: a message still sending.
inline void sendingDots(DisplayDriver& d, int x, int y) {
  lit(d);
  for (int i = 0; i < 3; i++) d.fillRect(x + i * 2, y + 4, 1, 1);
}

}  // namespace badgeui
