#include "QccSplashLayout.h"
#include "QccSplashArt.h"

#include <helpers/ui/OffbandSplash.h>
#include <helpers/ui/OLEDDisplayFonts.h>
#include <helpers/ui/TpFont.h>

#include <stdio.h>
#include <string.h>

namespace qcc {

static void fitWidth(char* s, int max_w) {
  size_t n = strlen(s);
  while (n > 0 && offband::tpfont::textWidth(ArialMT_Plain_10, s) > max_w) s[--n] = '\0';
}

void splitCornerVersion(const char* ver, char* l1, size_t n1, char* l2, size_t n2, int max_w) {
  if (n1) l1[0] = '\0';
  if (n2) l2[0] = '\0';
  if (!ver || !n1 || !n2) return;

  const size_t cut = strcspn(ver, "-+ ");
  snprintf(l1, n1, "%.*s", (int)cut, ver);

  const char* rest = ver + cut;
  if (*rest == '-') {
    rest++;
    snprintf(l2, n2, "%.*s", (int)strcspn(rest, " "), rest);
    if (offband::tpfont::textWidth(ArialMT_Plain_10, l2) > max_w) {
      snprintf(l2, n2, "%.*s", (int)strcspn(rest, "+ "), rest);
    }
  } else if (*rest == '+') {
    snprintf(l2, n2, "%.*s", (int)strcspn(rest, " "), rest);
  }
  fitWidth(l1, max_w);
  fitWidth(l2, max_w);
}

}  // namespace qcc

namespace offband {

void drawEventSplash(DisplayDriver& display, const SplashInfo& info) {
  display.setColor(UIColor::primary_txt);
  display.drawXbm(qcc::kEyeX, qcc::kEyeY, qcc_eye, QCC_EYE_W, QCC_EYE_H);

  tpfont::drawText(display, 0, qcc::kCornerLine1Y, ArialMT_Plain_10, "Offband");

  char l1[24], l2[24];
  const char* ver = info.offband_ver ? info.offband_ver : offbandShortVersion();
  qcc::splitCornerVersion(ver, l1, sizeof(l1), l2, sizeof(l2), qcc::kCornerMaxW);
  const int right = qcc::kRightEdgeX + 1;
  tpfont::drawText(display, right - tpfont::textWidth(ArialMT_Plain_10, l1),
                   qcc::kCornerLine1Y, ArialMT_Plain_10, l1);
  tpfont::drawText(display, right - tpfont::textWidth(ArialMT_Plain_10, l2),
                   qcc::kCornerLine2Y, ArialMT_Plain_10, l2);

  static const char kConName[] = "QueenCityCon";
  tpfont::drawText(display, (display.width() - tpfont::textWidth(ArialMT_Plain_16, kConName)) / 2,
                   qcc::kNameY, ArialMT_Plain_16, kConName);
}

}  // namespace offband
