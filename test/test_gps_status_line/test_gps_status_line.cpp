// test/test_gps_status_line/test_gps_status_line.cpp -- Offband #1247
//
// The GPS status line has two forms. The one the badge writes to its log every five
// seconds -- into the caplog, and out of the UART mirror on a diag build -- carries no
// coordinates; the one the paired client asks for does. These test the rendered text,
// not the inputs, because the text is what leaves the device.
//
// Folder MUST stay `test_`-prefixed and this file provides its own main() -- this repo
// does not link gtest_main.

#include <gtest/gtest.h>
#include <limits>
#include <string>
#include "../../src/helpers/sensors/GpsStatusLine.h"

using namespace offband;

namespace {

// A badge standing in Cincinnati with a fix.
GpsSnapshot located() {
  GpsSnapshot g;
  g.detected = true;
  g.active = true;
  g.fix = true;
  g.baud = 9600;
  g.lat_ud = 39103100L;
  g.lon_ud = -84512000L;
  g.alt_cm = 14800L;
  g.sats = 7;
  g.epoch = 1789000000L;
  return g;
}

std::string render(const GpsSnapshot& g, bool with_position) {
  char buf[200];
  const size_t n = formatGpsStatus(g, with_position, buf, sizeof buf);
  EXPECT_EQ(n, strlen(buf));
  return buf;
}

}  // namespace

TEST(GpsStatusLine, TheLogFormCarriesNoCoordinates) {
  const std::string s = render(located(), false);
  EXPECT_EQ(std::string::npos, s.find("lat="));
  EXPECT_EQ(std::string::npos, s.find("lon="));
  // and no bare digits of the position leak through some other field
  EXPECT_EQ(std::string::npos, s.find("39103100"));
  EXPECT_EQ(std::string::npos, s.find("84512000"));
}

TEST(GpsStatusLine, TheLogFormStillAnswersEveryDiagnosticQuestion) {
  const std::string s = render(located(), false);
  EXPECT_NE(std::string::npos, s.find("detected=1"));   // is a module wired
  EXPECT_NE(std::string::npos, s.find("active=1"));     // is it powered
  EXPECT_NE(std::string::npos, s.find("baud=9600"));    // is it talking
  EXPECT_NE(std::string::npos, s.find("fix=1"));        // is it locked
  EXPECT_NE(std::string::npos, s.find("sats=7"));
  EXPECT_NE(std::string::npos, s.find("alt_cm=14800"));
  EXPECT_NE(std::string::npos, s.find("time=1789000000"));
  EXPECT_NE(std::string::npos, s.find("pos=valid"));
}

TEST(GpsStatusLine, TheClientQueryKeepsTheCoordinates) {
  const std::string s = render(located(), true);
  EXPECT_NE(std::string::npos, s.find("lat=39103100"));
  EXPECT_NE(std::string::npos, s.find("lon=-84512000"));
  EXPECT_NE(std::string::npos, s.find("pos=valid"));
}

// #1247, the owner: 0,0 is his example of a position that is not sane.
TEST(GpsStatusLine, NullIslandIsNotAPosition) {
  EXPECT_FALSE(positionIsSane(0, 0));
  GpsSnapshot g = located();
  g.lat_ud = 0;
  g.lon_ud = 0;
  EXPECT_NE(std::string::npos, render(g, false).find("pos=invalid"));
  EXPECT_NE(std::string::npos, render(g, true).find("pos=invalid"));
}

TEST(GpsStatusLine, ZeroInOneAxisIsStillAPosition) {
  EXPECT_TRUE(positionIsSane(0, -84512000L));       // on the equator
  EXPECT_TRUE(positionIsSane(39103100L, 0));        // on the prime meridian
}

TEST(GpsStatusLine, OutOfRangeIsNotAPosition) {
  EXPECT_FALSE(positionIsSane(90000001L, 0));
  EXPECT_FALSE(positionIsSane(-90000001L, 0));
  EXPECT_FALSE(positionIsSane(0, 180000001L));
  EXPECT_FALSE(positionIsSane(0, -180000001L));
  EXPECT_TRUE(positionIsSane(90000000L, 180000000L));     // the corners are in range
  EXPECT_TRUE(positionIsSane(-90000000L, -180000000L));
}

// A fix is a different question from whether the stored position is believable. A badge
// that has lost its fix still carries the last real one, and that pairing is the useful
// reading.
TEST(GpsStatusLine, ALostFixKeepsAValidPosition) {
  GpsSnapshot g = located();
  g.fix = false;
  g.active = false;
  const std::string s = render(g, false);
  EXPECT_NE(std::string::npos, s.find("fix=0"));
  EXPECT_NE(std::string::npos, s.find("active=0"));
  EXPECT_NE(std::string::npos, s.find("pos=valid"));
}

// A badge with no GPS at all: nothing stored, nothing claimed.
TEST(GpsStatusLine, NoModuleReadsAsNothing) {
  GpsSnapshot g{};
  const std::string s = render(g, false);
  EXPECT_NE(std::string::npos, s.find("detected=0"));
  EXPECT_NE(std::string::npos, s.find("fix=0"));
  EXPECT_NE(std::string::npos, s.find("pos=invalid"));
  EXPECT_NE(std::string::npos, s.find("sats=0"));
}

// #1247: the degrees-to-microdegrees conversion is checked before the cast, not after.
// Casting a NaN, an infinity or a value too large for a long is undefined, and on ARM
// comes back as a plausible-looking number that would have read as a real position.
TEST(GpsStatusLine, NothingBelievableConvertsToNothing) {
  const double nan_d = std::numeric_limits<double>::quiet_NaN();
  const double inf_d = std::numeric_limits<double>::infinity();
  long lat = 1, lon = 1;

  EXPECT_FALSE(toSanePosition(nan_d, -84.512, lat, lon));
  EXPECT_EQ(0, lat);
  EXPECT_EQ(0, lon);                       // and the good half goes with the bad one

  EXPECT_FALSE(toSanePosition(39.1031, nan_d, lat, lon));
  EXPECT_EQ(0, lat);
  EXPECT_EQ(0, lon);

  EXPECT_FALSE(toSanePosition(inf_d, inf_d, lat, lon));
  EXPECT_FALSE(toSanePosition(-inf_d, 0.0, lat, lon));
  EXPECT_FALSE(toSanePosition(1e300, 1e300, lat, lon));
  EXPECT_FALSE(toSanePosition(91.0, 0.0, lat, lon));
  EXPECT_FALSE(toSanePosition(0.0, 181.0, lat, lon));
}

TEST(GpsStatusLine, ARealPositionConverts) {
  long lat = 0, lon = 0;
  EXPECT_TRUE(toSanePosition(39.1031, -84.512, lat, lon));
  EXPECT_EQ(39103100L, lat);
  EXPECT_EQ(-84512000L, lon);

  EXPECT_TRUE(toSanePosition(90.0, 180.0, lat, lon));      // the corners convert
  EXPECT_EQ(90000000L, lat);
  EXPECT_EQ(180000000L, lon);
}

// A coordinate inside a tenth of a micro-degree of 0,0 truncates to the null island and
// reads invalid. That is about 11 cm square, in the Gulf of Guinea, and the owner's rule
// is that 0,0 is not a position.
TEST(GpsStatusLine, TheNullIslandRuleCostsAHandsBreadthOfOcean) {
  long lat = 1, lon = 1;
  EXPECT_FALSE(toSanePosition(0.0000001, 0.0000001, lat, lon));
  EXPECT_TRUE(toSanePosition(0.000002, 0.000002, lat, lon));   // two micro-degrees out
  EXPECT_EQ(2L, lat);
  EXPECT_EQ(2L, lon);
}

// #1247: `pos=` is appended, not inserted, so the form that keeps the coordinates is the
// line that was there before with one more field on the end. A client reading it by
// position sees what it always saw.
TEST(GpsStatusLine, TheQueryFormOnlyGrewAtTheEnd) {
  const std::string s = render(located(), true);
  EXPECT_EQ(0u, s.find("detected=1 active=1 fix=1 baud=9600 lat=39103100 lon=-84512000 "
                       "alt_cm=14800 sats=7 time=1789000000"));
  EXPECT_EQ(s.size() - strlen(" pos=valid"), s.find(" pos=valid"));
}

// snprintf answers with what it would have written; this answers with what it did, and
// the base SensorManager::getGpsStatusText answers 0 for nothing written.
TEST(GpsStatusLine, TruncationReportsTheBytesWritten) {
  char buf[16];
  const size_t n = formatGpsStatus(located(), true, buf, sizeof buf);
  EXPECT_LT(n, sizeof buf);
  EXPECT_EQ(n, strlen(buf));
  EXPECT_EQ(0, formatGpsStatus(located(), true, buf, 0));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
