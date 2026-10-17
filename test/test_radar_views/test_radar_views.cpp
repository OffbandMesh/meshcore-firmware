// Epic F (#1430): the radar's geometry, pinned before it is built (F1,
// #1562). Design of record: ObScreen.dc.html (radar), and the owner-approved
// gap mockups (epic-f-gap-mockups.html, 2026-10-17).

#include <gtest/gtest.h>

#include <math.h>
#include <string.h>

#include "helpers/gui/RadarViews.h"

using namespace offband::gui;

namespace {

// A point at lat 39.5 N, lon 84.4 W; the others are offsets from it. One
// degree of latitude is 111.195 km; one of longitude here is that times
// cos(39.5) = 85.80 km.
const GeoPos kHome{39500000, -84400000};
GeoPos offset(double north_km, double east_km) {
  const double lat = 39.5 + north_km / 111.195;
  const double lon = -84.4 + east_km / (111.195 * cos(39.5 * 3.14159265358979 / 180.0));
  return GeoPos{int32_t(lround(lat * 1e6)), int32_t(lround(lon * 1e6))};
}

}  // namespace

// ------------------------------------------------------------ positions

TEST(Geo, ZeroIsNoPosition) {
  EXPECT_FALSE(hasPos(GeoPos{0, 0}));
  EXPECT_TRUE(hasPos(kHome));
  EXPECT_TRUE(hasPos(GeoPos{0, 1}));   // on the equator is still a position
}

TEST(Geo, DistanceOneKmEachWay) {
  EXPECT_NEAR(distanceM(kHome, offset(1, 0)), 1000, 5);
  EXPECT_NEAR(distanceM(kHome, offset(0, 1)), 1000, 5);
  EXPECT_NEAR(distanceM(kHome, offset(-3, -4)), 5000, 20);
  EXPECT_EQ(distanceM(kHome, kHome), 0u);
}

TEST(Geo, DistanceFarAndAcrossTheDateLine) {
  // 30 km: the largest ring step.
  EXPECT_NEAR(distanceM(kHome, offset(30, 0)), 30000, 60);
  // Across 180: two points 0.2 degrees of longitude apart on the equator.
  const GeoPos w{0, 179900000}, e{0, -179900000};
  EXPECT_NEAR(distanceM(w, e), 22239, 50);
  // Antipodes: half the circumference, never NaN (Gemini F1).
  const GeoPos a{10000000, 20000000}, b{-10000000, -160000000};
  EXPECT_NEAR(distanceM(a, b), 20015087, 1000);
}

TEST(Geo, BearingByQuadrant) {
  EXPECT_EQ(bearingDeg(kHome, offset(1, 0)), 0);
  EXPECT_EQ(bearingDeg(kHome, offset(0, 1)), 90);
  EXPECT_EQ(bearingDeg(kHome, offset(-1, 0)), 180);
  EXPECT_EQ(bearingDeg(kHome, offset(0, -1)), 270);
  EXPECT_NEAR(bearingDeg(kHome, offset(1, 1)), 45, 1);
  EXPECT_NEAR(bearingDeg(kHome, offset(-1, -1)), 225, 1);
}

TEST(Geo, CompassEightPoints) {
  EXPECT_STREQ(compass8(0), "N");
  EXPECT_STREQ(compass8(22), "N");
  EXPECT_STREQ(compass8(23), "NE");
  EXPECT_STREQ(compass8(45), "NE");
  EXPECT_STREQ(compass8(90), "E");
  EXPECT_STREQ(compass8(180), "S");
  EXPECT_STREQ(compass8(270), "W");
  EXPECT_STREQ(compass8(337), "NW");
  EXPECT_STREQ(compass8(338), "N");
  EXPECT_STREQ(compass8(359), "N");
}

TEST(Geo, DistanceText) {
  char b[16];
  distText(600, b, sizeof b);
  EXPECT_STREQ(b, "0.6 km");
  distText(1840, b, sizeof b);
  EXPECT_STREQ(b, "1.8 km");
  distText(11600, b, sizeof b);
  EXPECT_STREQ(b, "11.6 km");
  distText(49, b, sizeof b);
  EXPECT_STREQ(b, "0.0 km");
  distText(142300, b, sizeof b);
  EXPECT_STREQ(b, "142 km");   // whole km from 100 km
}

TEST(Geo, SheetLine) {
  // "1 hop · 1.8 km NE" (mockup 1), hops as the Nodes rows give them.
  char b[48];
  sheetSub(1, 1840, 45, b, sizeof b);
  EXPECT_STREQ(b, "1 hop · 1.8 km NE");
  sheetSub(0xFF, 600, 180, b, sizeof b);
  EXPECT_STREQ(b, "flood · 0.6 km S");
  sheetSub(0, 11600, 30, b, sizeof b);
  EXPECT_STREQ(b, "direct · 11.6 km NE");
}

// --------------------------------------------------------------- rings

TEST(Rings, StepsAndText) {
  ASSERT_EQ(kRadarStepCount, 5u);
  const uint32_t want[] = {1500, 3000, 6000, 15000, 30000};
  const char* const text[] = {"0.5·1·1.5 km", "1·2·3 km", "2·4·6 km", "5·10·15 km", "10·20·30 km"};
  char b[24];
  for (size_t i = 0; i < kRadarStepCount; i++) {
    EXPECT_EQ(radarStepM(i), want[i]);
    ringText(i, b, sizeof b);
    EXPECT_STREQ(b, text[i]);
  }
  EXPECT_EQ(kRadarStartStep, 1u);   // opens at 3 km, as drawn
}

TEST(Rings, StepThatFits) {
  EXPECT_EQ(stepFor(0), 0);
  EXPECT_EQ(stepFor(1500), 0);
  EXPECT_EQ(stepFor(1501), 1);
  EXPECT_EQ(stepFor(2900), 1);
  EXPECT_EQ(stepFor(11600), 3);
  EXPECT_EQ(stepFor(30000), 4);
  EXPECT_EQ(stepFor(30001), -1);   // beyond the largest step
}

TEST(Rings, TapCyclesAndWraps) {
  EXPECT_EQ(nextStep(1), 2u);
  EXPECT_EQ(nextStep(4), 0u);   // from 30 km back to 1.5 km
}

// ---------------------------------------------------------------- plot

TEST(Plot, NorthUpEastRight) {
  // Radius 90 px, outer ring 3 km: 1.5 km north is half way up.
  PlotPt p = plotPoint(1500, 0, 3000, 90);
  EXPECT_EQ(p.x, 0);
  EXPECT_EQ(p.y, -45);
  EXPECT_TRUE(p.inside);
  p = plotPoint(3000, 90, 3000, 90);
  EXPECT_EQ(p.x, 90);
  EXPECT_EQ(p.y, 0);
  EXPECT_TRUE(p.inside);
  p = plotPoint(1000, 225, 2000, 100);   // half way, south-west
  EXPECT_NEAR(p.x, -35, 1);
  EXPECT_NEAR(p.y, 35, 1);
}

TEST(Plot, BeyondTheRingIsClampedToIt) {
  const PlotPt p = plotPoint(9000, 180, 3000, 90);
  EXPECT_EQ(p.x, 0);
  EXPECT_EQ(p.y, 90);
  EXPECT_FALSE(p.inside);
}

// ----------------------------------------------------------- which nodes

TEST(Pick, NearestFirstInsideTheRingWithACap) {
  // dist_m, has a position
  const RadarItem items[] = {{2500, true}, {900, true}, {0, false}, {4000, true}, {1800, true},
                             {600, true},  {3000, true}, {35000, true}};
  uint16_t out[8];
  size_t beyond = 0;
  size_t n = radarPick(items, 8, 3000, out, 8, &beyond);
  ASSERT_EQ(n, 5u);   // 600, 900, 1800, 2500, 3000
  const uint16_t want[] = {5, 1, 4, 0, 6};
  for (size_t i = 0; i < n; i++) EXPECT_EQ(out[i], want[i]);
  EXPECT_EQ(beyond, 2u);   // 4 km and 35 km; the one with no position isn't counted

  n = radarPick(items, 8, 3000, out, 3, &beyond);   // capped at 3: the nearest
  ASSERT_EQ(n, 3u);
  EXPECT_EQ(out[0], 5);
  EXPECT_EQ(out[2], 4);
  EXPECT_EQ(beyond, 2u);   // the cap doesn't make inside nodes "beyond"
}

TEST(Pick, NothingWithAPosition) {
  const RadarItem items[] = {{0, false}, {0, false}};
  uint16_t out[4];
  size_t beyond = 9;
  EXPECT_EQ(radarPick(items, 2, 30000, out, 4, &beyond), 0u);
  EXPECT_EQ(beyond, 0u);
  EXPECT_EQ(radarPick(nullptr, 0, 3000, out, 4, &beyond), 0u);
}

TEST(Pick, BeyondLine) {
  char b[32];
  beyondText(12, 1, b, sizeof b);
  EXPECT_STREQ(b, "+12 beyond 3 km");
  beyondText(3, 3, b, sizeof b);
  EXPECT_STREQ(b, "+3 beyond 15 km");
  beyondText(2, 0, b, sizeof b);
  EXPECT_STREQ(b, "+2 beyond 1.5 km");
  beyondText(0, 1, b, sizeof b);
  EXPECT_STREQ(b, "");
}

// -------------------------------------------------------------- labels

TEST(Labels, NearerKeepsItsLabel) {
  // Boxes in nearest-first order; the selected one (index 2) always shows.
  const LabelBox boxes[] = {{10, 10, 40, 10}, {30, 14, 40, 10}, {35, 12, 40, 10}, {100, 100, 30, 10}};
  bool show[4];
  placeLabels(boxes, 4, 2, show);
  EXPECT_TRUE(show[2]);    // selected
  EXPECT_FALSE(show[0]);   // overlaps the selected one
  EXPECT_FALSE(show[1]);   // overlaps the selected one
  EXPECT_TRUE(show[3]);    // clear of everything

  placeLabels(boxes, 4, -1, show);   // nothing selected
  EXPECT_TRUE(show[0]);
  EXPECT_FALSE(show[1]);   // overlaps 0, which is nearer
  EXPECT_FALSE(show[2]);
  EXPECT_TRUE(show[3]);
}

TEST(Labels, TouchingEdgesDontOverlap) {
  const LabelBox boxes[] = {{0, 0, 10, 10}, {10, 0, 10, 10}, {0, 10, 10, 10}};
  bool show[3];
  placeLabels(boxes, 3, -1, show);
  EXPECT_TRUE(show[0] && show[1] && show[2]);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
