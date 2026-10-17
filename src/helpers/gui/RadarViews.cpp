#include "RadarViews.h"

#include <math.h>
#include <string.h>

#include "GuiViews.h"
#include "NodeViews.h"

namespace offband {
namespace gui {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthM = 6371008.8;   // mean radius
constexpr uint32_t kSteps[kRadarStepCount] = {1500, 3000, 6000, 15000, 30000};

double rad(int32_t e6) { return double(e6) / 1e6 * kPi / 180.0; }

// "1.5", "3", "0.5": km with a tenth only when it isn't whole.
void kmText(uint32_t m, char* out, size_t n) {
  if (m % 1000 == 0) textf(out, n, "%lu", (unsigned long)(m / 1000));
  else textf(out, n, "%lu.%lu", (unsigned long)(m / 1000), (unsigned long)(m % 1000 / 100));
}

bool overlap(const LabelBox& a, const LabelBox& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

}  // namespace

bool hasPos(GeoPos p) { return p.lat_e6 != 0 || p.lon_e6 != 0; }

// Haversine: good to a metre at radar ranges, and right across 180.
uint32_t distanceM(GeoPos a, GeoPos b) {
  const double p1 = rad(a.lat_e6), p2 = rad(b.lat_e6);
  const double dp = p2 - p1, dl = rad(b.lon_e6) - rad(a.lon_e6);
  double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  h = h < 0 ? 0 : h > 1 ? 1 : h;   // rounding near antipodes can pass 1: sqrt(1 - h) would be NaN
  const double d = 2 * kEarthM * atan2(sqrt(h), sqrt(1 - h));
  return d >= 4.0e9 ? 0xFFFFFFFFu : uint32_t(lround(d));
}

uint16_t bearingDeg(GeoPos from, GeoPos to) {
  const double p1 = rad(from.lat_e6), p2 = rad(to.lat_e6), dl = rad(to.lon_e6) - rad(from.lon_e6);
  const double y = sin(dl) * cos(p2);
  const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double deg = atan2(y, x) * 180.0 / kPi;
  if (deg < 0) deg += 360.0;
  const long r = lround(deg);
  return uint16_t(r >= 360 ? r - 360 : r);
}

const char* compass8(uint16_t deg) {
  static const char* const kPoints[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return kPoints[((deg % 360) + 22) / 45 % 8];
}

void distText(uint32_t m, char* out, size_t n) {
  if (m >= 100000) {
    textf(out, n, "%lu km", (unsigned long)((m + 500) / 1000));
    return;
  }
  const uint32_t tenths = m / 100;   // cut, not rounded: 1,840 m reads 1.8
  textf(out, n, "%lu.%lu km", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
}

void sheetSub(uint8_t out_path_len, uint32_t dist_m, uint16_t bearing, char* out, size_t n) {
  char hops[16], dist[16];
  hopsText(out_path_len, hops, sizeof hops);
  distText(dist_m, dist, sizeof dist);
  textf(out, n, "%s · %s %s", hops, dist, compass8(bearing));
}

uint32_t radarStepM(size_t step) { return kSteps[step < kRadarStepCount ? step : kRadarStepCount - 1]; }

void ringText(size_t step, char* out, size_t n) {
  const uint32_t outer = radarStepM(step);
  char a[8], b[8], c[8];
  kmText(outer / 3, a, sizeof a);
  kmText(outer * 2 / 3, b, sizeof b);
  kmText(outer, c, sizeof c);
  textf(out, n, "%s·%s·%s km", a, b, c);
}

int stepFor(uint32_t dist_m) {
  for (size_t i = 0; i < kRadarStepCount; i++) {
    if (dist_m <= kSteps[i]) return int(i);
  }
  return -1;
}

size_t nextStep(size_t step) { return (step + 1) % kRadarStepCount; }

PlotPt plotPoint(uint32_t dist_m, uint16_t bearing, uint32_t outer_m, int16_t radius_px) {
  PlotPt p{0, 0, dist_m <= outer_m};
  if (outer_m == 0) return p;
  const double r = p.inside ? double(dist_m) * radius_px / outer_m : double(radius_px);
  const double b = bearing * kPi / 180.0;
  p.x = int16_t(lround(r * sin(b)));
  p.y = int16_t(lround(-r * cos(b)));   // north is up the screen
  return p;
}

size_t radarPick(const RadarItem* items, size_t n, uint32_t outer_m, uint16_t* out, size_t max, size_t* beyond) {
  size_t k = 0, far = 0;
  for (size_t i = 0; items && i < n; i++) {
    if (!items[i].has_pos) continue;
    if (items[i].dist_m > outer_m) {
      far++;
      continue;
    }
    if (max == 0) continue;
    // Insert by distance, keeping the nearest `max`; ties keep list order.
    size_t at = k < max ? k++ : max;
    if (at == max) {
      if (items[i].dist_m >= items[out[max - 1]].dist_m) continue;
      at = max - 1;
    }
    while (at > 0 && items[i].dist_m < items[out[at - 1]].dist_m) {
      out[at] = out[at - 1];
      at--;
    }
    out[at] = uint16_t(i);
  }
  if (beyond) *beyond = far;
  return k;
}

void beyondText(size_t beyond, size_t step, char* out, size_t n) {
  if (beyond == 0) {
    textf(out, n, "%s", "");
    return;
  }
  char km[8];
  kmText(radarStepM(step), km, sizeof km);
  textf(out, n, "+%u beyond %s km", unsigned(beyond), km);
}

void placeLabels(const LabelBox* boxes, size_t n, int selected, bool* show) {
  for (size_t i = 0; i < n; i++) show[i] = false;
  if (selected >= 0 && size_t(selected) < n) show[selected] = true;
  for (size_t i = 0; i < n; i++) {
    if (show[i]) continue;
    bool clear = true;
    for (size_t j = 0; j < n && clear; j++) {
      if (show[j] && overlap(boxes[i], boxes[j])) clear = false;
    }
    show[i] = clear;
  }
}

}  // namespace gui
}  // namespace offband
