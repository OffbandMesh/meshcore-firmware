#pragma once

// #1230: layout for the badge screens in the owner's design (the QCC mockups). The
// display's built-in 6 x 8 font makes a 21 x 8 grid. Pure, so it is unit-tested
// natively; the screens draw what it decides.
//
// Text here is display-ready: one byte per character, as translateUTF8ToBlocks()
// leaves it.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../TimeZones.h"

namespace badgeui {

constexpr int kCols = 21;         // 6 px cells across 128 px
constexpr int kTagCols = 5;       // a channel message's sender tag
constexpr int kIndent = 6;        // text after the tag and a space
constexpr int kMineEnd = 19;      // my messages end before this column...
constexpr int kMarkCol = 20;      // ...and their status mark sits here
constexpr int kCycleStops = 4;    // SW1's cycle: Messages, Contacts, Nearby, Status

// "now", "4m", "2h", "3d": how old something `secs` old is. "old" past 99 days.
inline void formatAge(uint32_t secs, char* out, size_t n) {
  if (secs < 60) snprintf(out, n, "now");
  else if (secs < 3600) snprintf(out, n, "%um", (unsigned)(secs / 60));
  else if (secs < 86400) snprintf(out, n, "%uh", (unsigned)(secs / 3600));
  else if (secs < 100u * 86400u) snprintf(out, n, "%ud", (unsigned)(secs / 86400));
  else snprintf(out, n, "old");
}

// #1233: when something at `t` happened, as a row shows it. With a time zone set and a
// `trusted` clock, today's times read as the clock does ("13:07", the design's); anything
// older, or with no zone yet, reads as an age. The clock is trusted once the phone or the
// GPS has set it since boot: until then it runs from the newest contact's last-heard
// time, which can be hours or days behind, and only an age is true.
inline void formatWhen(int zone, uint32_t t, uint32_t now, bool trusted, char* out, size_t n) {
  namespace tz = offband::tz;
  if (trusted && tz::isSet(zone) && tz::clockSet(now) && tz::clockSet(t) && t <= now && tz::sameLocalDay(zone, t, now)) {
    tz::clock(zone, t, out, n);
  } else {
    formatAge(now > t ? now - t : 0, out, n);
  }
}

// #1235: one coordinate as the GPS screen shows it, "39.1031N": four decimals (about
// 11 m), rounded. `e6` is millionths of a degree, as the location provider reports it.
// A value that rounds to zero takes the positive hemisphere's letter.
inline int formatDegrees(long e6, char pos, char neg, char* out, size_t n) {
  const unsigned long mag = e6 < 0 ? 0UL - (unsigned long)e6 : (unsigned long)e6;
  const unsigned long t = (mag + 50) / 100;   // ten-thousandths of a degree
  return snprintf(out, n, "%lu.%04lu%c", t / 10000, t % 10000, (e6 < 0 && t != 0) ? neg : pos);
}

// "39.1031N  84.5120W": at most 19 characters.
inline void formatPosition(long lat_e6, long lon_e6, char* out, size_t n) {
  char lat[16], lon[16];
  formatDegrees(lat_e6, 'N', 'S', lat, sizeof(lat));
  formatDegrees(lon_e6, 'E', 'W', lon, sizeof(lon));
  snprintf(out, n, "%s  %s", lat, lon);
}

// "265m" from millimeters, rounded; below sea level, "-86m".
inline void formatAltitude(long alt_mm, char* out, size_t n) {
  const unsigned long mag = alt_mm < 0 ? 0UL - (unsigned long)alt_mm : (unsigned long)alt_mm;
  const unsigned long m = (mag + 500) / 1000;
  snprintf(out, n, "%s%lum", (alt_mm < 0 && m != 0) ? "-" : "", m);
}

// #1235: the GPS in a few words, for the GPS screen's title and Settings' GPS row:
// "off"; "No GPS Module" (the owner's wording) while it's on and no module answered;
// "no fix"; "fix 9", or "fix" before any satellites are counted. A fix proves there's
// a module, whatever the check said.
inline void formatGpsState(bool on, bool module, bool fix, long sats, char* out, size_t n) {
  if (!on) snprintf(out, n, "off");
  else if (fix && sats > 0) snprintf(out, n, "fix %ld", sats);
  else if (fix) snprintf(out, n, "fix");
  else if (!module) snprintf(out, n, "No GPS Module");
  else snprintf(out, n, "no fix");
}

// "17:07:42": the time of day, UTC, from UTC seconds.
inline void formatUtcTime(uint32_t t, char* out, size_t n) {
  const uint32_t s = t % 86400;
  snprintf(out, n, "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
}

struct Span {
  uint16_t start, len;
};

// Splits text into lines of at most `width` characters. A line breaks at its last
// space if it has one, else mid-word; spaces at a break are dropped. Returns the line
// count, at most max_lines.
inline int wrap(const char* text, int width, Span* out, int max_lines) {
  const int total = (int)strlen(text);
  int pos = 0, n = 0;
  while (n < max_lines) {
    while (pos < total && text[pos] == ' ') pos++;
    if (pos >= total) break;
    int len;
    int next;
    if (total - pos <= width) {
      len = total - pos;
      next = total;
    } else {
      int sp = -1;
      for (int i = pos + width; i > pos; i--) {
        if (text[i] == ' ') {
          sp = i;
          break;
        }
      }
      if (sp > pos) {
        len = sp - pos;
        next = sp + 1;
      } else {
        len = width;
        next = pos + width;
      }
    }
    while (len > 0 && text[pos + len - 1] == ' ') len--;
    out[n].start = (uint16_t)pos;
    out[n].len = (uint16_t)len;
    n++;
    pos = next;
  }
  return n;
}

// One message, for the thread layout. `sender` is empty in a DM.
struct MsgView {
  bool outgoing;
  const char* sender;
  const char* text;
};

enum class RowKind : uint8_t { Text, Meta };

// One screen row of a thread.
struct Row {
  uint8_t msg;      // index into the MsgView array
  RowKind kind;
  bool first;       // the message's first row: an incoming channel message's tag goes here
  bool last;        // its last text row: my message's status mark goes here
  bool caret;       // the message is selected: a caret in column 0
  uint8_t col;      // where the text starts
  Span span;        // into the message's text
};

// Lays a thread's messages (oldest first) out as rows, oldest first.
// - An incoming channel message has its sender's tag in columns 0-4 of its first row,
//   and its text from column 6 on every row.
// - An incoming DM has one sender, so no tag: its text starts at column 0.
// - My messages are right-aligned, ending before column 19; the mark goes in 20.
// - The selected message (or -1) has a caret in column 0 on each row, with everything
//   else one column right, then one Meta row the screen fills in.
// Returns the row count, at most max_rows.
inline int layoutThread(const MsgView* msgs, int count, bool channel, int selected, Row* out, int max_rows) {
  int n = 0;
  for (int m = 0; m < count && n < max_rows; m++) {
    const bool sel = (m == selected);
    const int shift = sel ? 1 : 0;
    int col, width;
    if (msgs[m].outgoing) {
      col = 0;
      width = kMineEnd - shift;
    } else if (channel) {
      col = kIndent + shift;
      width = kCols - col;
    } else {
      col = shift;
      width = kCols - col;
    }
    Span spans[16];
    const int lines = wrap(msgs[m].text, width, spans, 16);
    for (int i = 0; i < lines && n < max_rows; i++) {
      Row& r = out[n++];
      r.msg = (uint8_t)m;
      r.kind = RowKind::Text;
      r.first = (i == 0);
      r.last = (i == lines - 1);
      r.caret = sel;
      r.col = (uint8_t)(msgs[m].outgoing ? kMineEnd - spans[i].len : col);
      r.span = spans[i];
    }
    if (sel && n < max_rows) {
      Row& r = out[n++];
      r.msg = (uint8_t)m;
      r.kind = RowKind::Meta;
      r.first = r.last = false;
      r.caret = false;
      r.col = 0;
      r.span = {0, 0};
    }
  }
  return n;
}

// The first row to show, `visible` rows at a time. With nothing selected the newest
// rows sit at the bottom; a selected message scrolls into view, as low as it can sit.
inline int threadTop(const Row* rows, int n, int visible, int selected) {
  int top = n > visible ? n - visible : 0;
  if (selected >= 0) {
    for (int i = 0; i < n; i++) {
      if (rows[i].msg == selected) {
        if (i < top) top = i;
        break;
      }
    }
  }
  return top;
}

// The first list item to show, `rows` at a time, keeping one item of context above
// the selection.
inline int listTop(int sel, int count, int rows) {
  int top = sel - 1;
  if (top > count - rows) top = count - rows;
  if (top < 0) top = 0;
  return top;
}

// A selected row too wide for its box holds still, slides left until its end shows,
// holds again, and starts over.
constexpr uint32_t kMarqueeHoldMs = 1000;
constexpr uint32_t kMarqueeMsPerPx = 40;   // 25 px a second

// How many pixels to slide a `text_px` row in a `box_px` box, `ms` into its marquee.
inline int marqueeOffset(int text_px, int box_px, uint32_t ms) {
  if (text_px <= box_px) return 0;
  const uint32_t travel = (uint32_t)(text_px - box_px);
  const uint32_t slide = travel * kMarqueeMsPerPx;
  const uint32_t t = ms % (kMarqueeHoldMs + slide + kMarqueeHoldMs);
  if (t < kMarqueeHoldMs) return 0;
  if (t < kMarqueeHoldMs + slide) return (int)((t - kMarqueeHoldMs) / kMarqueeMsPerPx);
  return (int)travel;
}

}  // namespace badgeui
