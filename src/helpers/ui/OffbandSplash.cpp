#include "OffbandSplash.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "OffbandLogo.h"

namespace offband {

void shortVersionInto(char* out, size_t out_len, const char* describe,
                      const char* build_tag) {
  if (!out || out_len == 0) return;
  out[0] = 0;
  if (!describe) return;

  const char* cw = describe;
  if (strncmp(cw, "offband-", 8) == 0) cw += 8;
  const bool dirty = (strstr(cw, "-dirty") != nullptr);

  int commits = 0, taglen;
  const char* gmark = strstr(cw, "-g");
  if (gmark) {
    // Walk back over the commit count that precedes "-g<sha>".
    const char* nstart = gmark;
    while (nstart > cw && nstart[-1] >= '0' && nstart[-1] <= '9') nstart--;
    if (nstart == gmark) {          // "-g" with no count: not a describe suffix
      const char* d = strstr(cw, "-dirty");
      taglen = d ? (int)(d - cw) : (int)strlen(cw);
    } else {
      commits = atoi(nstart);
      taglen  = (int)((nstart > cw && nstart[-1] == '-') ? (nstart - 1 - cw) : (nstart - cw));
    }
  } else {
    const char* d = strstr(cw, "-dirty");
    taglen = d ? (int)(d - cw) : (int)strlen(cw);
  }
  if (taglen < 0) taglen = 0;

  if (commits > 0) snprintf(out, out_len, "%.*s+%d%s", taglen, cw, commits, dirty ? "*" : "");
  else             snprintf(out, out_len, "%.*s%s",    taglen, cw,          dirty ? "*" : "");

  if (build_tag && build_tag[0]) {
    size_t l = strlen(out);
    if (l + 2 < out_len) snprintf(out + l, out_len - l, " %s", build_tag);
  }
}

const char* offbandShortVersion() {
  static char buf[40];
  static bool done = false;
  if (!done) {
    done = true;
#ifdef OFFBAND_VERSION
#ifdef OFFBAND_BUILD_TAG
    shortVersionInto(buf, sizeof(buf), OFFBAND_VERSION, OFFBAND_BUILD_TAG);
#else
    shortVersionInto(buf, sizeof(buf), OFFBAND_VERSION, nullptr);
#endif
#else
    buf[0] = 0;
#endif
  }
  return buf;
}

// Line positions in LOGICAL coordinates. Two arrangements, because they have no
// slack in common -- this is the #758 exception, now in ONE place rather than
// duplicated per UI file:
//
//   mono   : the 1-bit lockup occupies y=1..30 and a line of text is 8px, so line 3
//            at 56 ends exactly at 64, the last row of the canvas.
//   colour : the artwork ends higher, and the panel's text cell is taller (18px on
//            the RC32), so the same y=56 would end at physical 130 on a 128px panel
//            and clip descenders.
static const int MONO_L1 = 35, MONO_L2 = 46, MONO_L3 = 56;
static const int COLOUR_L1 = 29, COLOUR_L2 = 41, COLOUR_L3 = 53;

int splashLastLineY() {
  return MONO_L3 > COLOUR_L3 ? MONO_L3 : COLOUR_L3;
}

// A 72x40 OLED (lilygo_techo_card, ui-tiny) cannot take the full lockup: the
// mark+wordmark strip is 119px wide and the three text lines end at y=64. Rather
// than let it overflow silently -- the failure mode that put MeshCore artwork on
// five roles in the first place -- the layout is chosen from the panel's own
// height, and small panels get the mark alone plus the version line. Branding
// degrades; it never disappears.
static bool isCompact(DisplayDriver& display) {
  return display.height() < 64 || display.width() < 128;
}

int drawBrandLockup(DisplayDriver& display, int y) {
#ifdef OFFBAND_MARK_W
  display.setColor(UIColor::primary_txt);
  if (isCompact(display)) {
    if (display.width() >= OFFBAND_MARK_W) {
      display.drawXbm((display.width() - OFFBAND_MARK_W) / 2, y,
                      offband_mark, OFFBAND_MARK_W, OFFBAND_MARK_H);
      return y + OFFBAND_MARK_H;
    }
    return y;
  }
  const int strip_w = OFFBAND_MARK_W + 4 + OFFBAND_WORD_W;
  const int logo_x = (display.width() - strip_w) / 2;
  display.drawXbm(logo_x, y, offband_mark, OFFBAND_MARK_W, OFFBAND_MARK_H);
  display.drawXbm(logo_x + OFFBAND_MARK_W + 4,
                  y + (OFFBAND_MARK_H - OFFBAND_WORD_H) / 2,
                  offband_word, OFFBAND_WORD_W, OFFBAND_WORD_H);
  return y + OFFBAND_MARK_H;
#else
  return y;
#endif
}

void drawSplash(DisplayDriver& display, const SplashInfo& info) {
  const DisplayDriver::ColourArt* art = display.colourSplashArt();
  const bool colour = (art != nullptr);

  display.setColor(UIColor::primary_txt);

  if (isCompact(display)) {
    const int mid = display.width() / 2;
#ifdef OFFBAND_MARK_W
    // Mark only, centred. 29x30 leaves one 8px text line on a 40px panel.
    if (display.width() >= OFFBAND_MARK_W && display.height() > OFFBAND_MARK_H) {
      display.drawXbm((display.width() - OFFBAND_MARK_W) / 2, 0,
                      offband_mark, OFFBAND_MARK_W, OFFBAND_MARK_H);
    }
#endif
    display.setTextSize(1);
    const char* v = info.offband_ver ? info.offband_ver : offbandShortVersion();
    if (v && *v) {
      int y = display.height() - 9;
      if (y < 0) y = 0;
      display.drawTextCentered(mid, y, v);
    }
    return;
  }

  if (colour) {
    // PHYSICAL pixels -- deliberately not the logical canvas, which is stretched
    // non-uniformly on these panels and would turn the mark's arcs into ellipses.
    display.drawRGB565(art->x, art->y, art->px, art->w, art->h);
  } else {
    drawBrandLockup(display, 1);
  }

  const int l1 = colour ? COLOUR_L1 : MONO_L1;
  const int l2 = colour ? COLOUR_L2 : MONO_L2;
  const int l3 = colour ? COLOUR_L3 : MONO_L3;
  const int mid = display.width() / 2;

  display.setTextSize(1);
  const char* ver = info.offband_ver ? info.offband_ver : offbandShortVersion();
  if (ver && *ver) display.drawTextCentered(mid, l1, ver);

  // "on MeshCore <ver>" -- the MIT attribution, as text rather than their artwork.
  char line[40];
  const char* mc = info.meshcore_ver ? info.meshcore_ver : "";
  if (*mc == 'v' || *mc == 'V') mc++;
  // FIRMWARE_VERSION is "1.2.3-abcdef"; show the release, not the commit hash.
  // Four of the six call sites strdup'd and trimmed this by hand -- doing it here
  // is the point of having one component.
  int mc_len = 0;
  while (mc[mc_len] && mc[mc_len] != '-') mc_len++;
  snprintf(line, sizeof(line), "on MeshCore %.*s", mc_len, mc);
  // drawTextCentered does not clip; ask the driver what fits rather than assuming a
  // character width, which is wrong for any font but the one it was written against.
  while (strlen(line) > 1 && display.getTextWidth(line) > (uint16_t)display.width()) {
    line[strlen(line) - 1] = '\0';
  }
  display.drawTextCentered(mid, l2, line);

  // Third line: the role where one is given (repeater/room-server/sensor), else the
  // build date (companion). Three lines is what the taller lockup affords.
  const char* last = info.role_line ? info.role_line : info.build_date;
  if (last) display.drawTextCentered(mid, l3, last);
}

}  // namespace offband
