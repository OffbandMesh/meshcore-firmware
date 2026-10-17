#pragma once

// The Map tab's radar (Epic F, #1430): geometry and what it shows, free of
// LVGL and the mesh so it runs in the native tests. Design of record:
// ObScreen.dc.html (radar) and the owner-approved gap mockups
// (epic-f-gap-mockups.html, 2026-10-17).

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

// A position as ContactInfo keeps it: degrees x 1e6. {0, 0} is "none".
struct GeoPos {
  int32_t lat_e6;
  int32_t lon_e6;
};
bool hasPos(GeoPos p);

// Great-circle distance in metres, and the initial bearing from `from` to
// `to` in whole degrees, 0 = north, clockwise.
uint32_t distanceM(GeoPos a, GeoPos b);
uint16_t bearingDeg(GeoPos from, GeoPos to);
// "N", "NE", ... "NW": eight points, each 45 degrees wide.
const char* compass8(uint16_t deg);
// "1.8 km"; whole km from 100 km.
void distText(uint32_t m, char* out, size_t n);
// The selection sheet's second line: "1 hop · 1.8 km NE" (mockup 1).
void sheetSub(uint8_t out_path_len, uint32_t dist_m, uint16_t bearing, char* out, size_t n);

// The ring scale: the outer ring's radius per step; the inner rings are a
// third and two thirds of it. Opens at 3 km (rings 1·2·3, as drawn).
constexpr size_t kRadarStepCount = 5;
constexpr size_t kRadarStartStep = 1;
uint32_t radarStepM(size_t step);
// "1·2·3 km", for the header.
void ringText(size_t step, char* out, size_t n);
// The smallest step whose outer ring reaches dist_m; -1 past the largest.
int stepFor(uint32_t dist_m);
// A tap on the scale: the next step, from the largest back to the smallest.
size_t nextStep(size_t step);

// Where a node plots, in pixels from the centre: x east, y south (screen
// down). A node past the outer ring is put on it, inside = false.
struct PlotPt {
  int16_t x;
  int16_t y;
  bool inside;
};
PlotPt plotPoint(uint32_t dist_m, uint16_t bearing, uint32_t outer_m, int16_t radius_px);

// The nodes the radar shows: those with a position inside the outer ring,
// nearest first, at most `max`. *beyond counts those with a position past
// the ring (the corner's "+12 beyond 3 km").
struct RadarItem {
  uint32_t dist_m;
  bool has_pos;
};
size_t radarPick(const RadarItem* items, size_t n, uint32_t outer_m, uint16_t* out, size_t max, size_t* beyond);
// "+12 beyond 3 km"; empty when none.
void beyondText(size_t beyond, size_t step, char* out, size_t n);

// Labels that collide: the selected one always shows, then the others
// nearest first, each only if it overlaps no label already shown. Boxes in
// nearest-first order; selected = -1 for none.
struct LabelBox {
  int16_t x, y, w, h;
};
void placeLabels(const LabelBox* boxes, size_t n, int selected, bool* show);

}  // namespace gui
}  // namespace offband
