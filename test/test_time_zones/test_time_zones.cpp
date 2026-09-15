// Native unit tests for the badge's time zones (#1233): the calendar arithmetic, when
// daylight saving starts and ends, and the zone suggested from a GPS position. The
// Unix times below were worked out independently (Python's datetime), not with the
// code under test.

#include <gtest/gtest.h>
#include <string>
#include "helpers/TimeZones.h"

using namespace offband::tz;

namespace {
std::string clockAt(int zone, uint32_t utc) {
  char b[8];
  clock(zone, utc, b, sizeof(b));
  return b;
}
std::string offsetAt(int zone, uint32_t utc) {
  char b[8];
  formatOffset(offsetMinutes(zone, utc), b, sizeof(b));
  return b;
}
constexpr uint32_t kSep15_1707 = 1789492020;   // 2026-09-15 17:07 UTC
}  // namespace

TEST(TimeZonesCivil, DaysFromCivil) {
  EXPECT_EQ(0, daysFromCivil(1970, 1, 1));
  EXPECT_EQ(10957, daysFromCivil(2000, 1, 1));
  EXPECT_EQ(20711, daysFromCivil(2026, 9, 15));
}

TEST(TimeZonesCivil, TheSundaysTheRulesUse) {
  EXPECT_EQ(8, nthSunday(2026, 3, 2));    // US: second Sunday in March
  EXPECT_EQ(1, nthSunday(2026, 11, 1));   // US: first Sunday in November
  EXPECT_EQ(29, lastSunday(2026, 3));     // EU: last Sunday in March
  EXPECT_EQ(25, lastSunday(2026, 10));    // EU: last Sunday in October
  EXPECT_EQ(27, lastSunday(2026, 12));    // December rolls into January for its last day
}

TEST(TimeZones, EasternInSummerIsFourHoursBehind) {
  EXPECT_EQ(-240, offsetMinutes(kEastern, kSep15_1707));
  EXPECT_EQ("13:07", clockAt(kEastern, kSep15_1707));
  EXPECT_EQ("-4:00", offsetAt(kEastern, kSep15_1707));
}

TEST(TimeZones, UsDaylightSavingStartsAtTwoOnTheSecondSundayOfMarch) {
  EXPECT_EQ("01:59", clockAt(kEastern, 1772953140));   // 2026-03-08 06:59Z: still EST
  EXPECT_EQ("03:00", clockAt(kEastern, 1772953200));   // 07:00Z: EDT
}

TEST(TimeZones, UsDaylightSavingEndsAtTwoOnTheFirstSundayOfNovember) {
  EXPECT_EQ("01:59", clockAt(kEastern, 1793512740));   // 2026-11-01 05:59Z: still EDT
  EXPECT_EQ("01:00", clockAt(kEastern, 1793512800));   // 06:00Z: EST again
}

TEST(TimeZones, EuRulesChangeAtOneUtc) {
  EXPECT_EQ("00:59", clockAt(kUk, 1774745940));          // 2026-03-29 00:59Z: GMT
  EXPECT_EQ("02:00", clockAt(kUk, 1774746000));          // 01:00Z: BST
  EXPECT_EQ("02:59", clockAt(kCentralEu, 1792889940));   // 2026-10-25 00:59Z: CEST
  EXPECT_EQ("02:00", clockAt(kCentralEu, 1792890000));   // 01:00Z: CET again
}

TEST(TimeZones, ArizonaAndHawaiiKeepStandardTime) {
  EXPECT_EQ("05:00", clockAt(kArizona, 1782907200));   // 2026-07-01 12:00Z
  EXPECT_EQ("02:00", clockAt(kHawaii, 1782907200));
}

TEST(TimeZones, FixedOffsets) {
  const int india = fixedZone(330);
  ASSERT_NE(kNotSet, india);
  EXPECT_EQ("17:30", clockAt(india, 1782907200));   // 12:00Z + 5:30
  EXPECT_EQ("+5:30", offsetAt(india, 1782907200));
  EXPECT_EQ("+0:00", offsetAt(kUtc, 1782907200));
  EXPECT_EQ(kNotSet, fixedZone(17));                 // no such zone
  EXPECT_STREQ("UTC-5", zone(fixedZone(-300)).name);
}

TEST(TimeZones, TheClockWrapsPastMidnight) {
  EXPECT_EQ("18:30", clockAt(kEastern, 1798759800));   // 2026-12-31 23:30Z is 18:30 EST
}

TEST(TimeZones, NotSetAndUnknownIndexes) {
  EXPECT_FALSE(isSet(kNotSet));
  EXPECT_TRUE(isSet(kEastern));
  EXPECT_FALSE(isSet(9999));
  EXPECT_STREQ(zone(kNotSet).name, zone(9999).name);   // an unknown index reads as not set
}

TEST(TimeZones, SameLocalDay) {
  // 2026-09-15 at 03:30Z is still the 14th in Eastern (23:30 EDT).
  const uint32_t late_14th_eastern = daysFromCivil(2026, 9, 15) * 86400u + 3 * 3600u + 1800u;
  EXPECT_FALSE(sameLocalDay(kEastern, late_14th_eastern, kSep15_1707));
  EXPECT_TRUE(sameLocalDay(kUtc, late_14th_eastern, kSep15_1707));
}

TEST(TimeZones, AClockBefore2024HasNotBeenSet) {
  EXPECT_FALSE(clockSet(1704067199));   // 2023-12-31 23:59:59Z
  EXPECT_TRUE(clockSet(1704067200));    // 2024-01-01 00:00:00Z
}

// A suggestion from a GPS fix. Coarse boundaries, so these are the cities it has to get
// right, including the ones near the lines.
TEST(TimeZonesSuggest, UnitedStates) {
  EXPECT_EQ(kEastern, suggest(39.10, -84.51));    // Cincinnati
  EXPECT_EQ(kEastern, suggest(38.25, -85.76));    // Louisville
  EXPECT_EQ(kEastern, suggest(39.77, -86.16));    // Indianapolis
  EXPECT_EQ(kEastern, suggest(35.05, -85.31));    // Chattanooga
  EXPECT_EQ(kEastern, suggest(33.75, -84.39));    // Atlanta
  EXPECT_EQ(kEastern, suggest(25.76, -80.19));    // Miami
  EXPECT_EQ(kCentral, suggest(30.42, -87.22));    // Pensacola
  EXPECT_EQ(kCentral, suggest(41.88, -87.63));    // Chicago
  EXPECT_EQ(kCentral, suggest(36.16, -86.78));    // Nashville
  EXPECT_EQ(kCentral, suggest(32.78, -96.80));    // Dallas
  EXPECT_EQ(kMountain, suggest(39.74, -104.99));  // Denver
  EXPECT_EQ(kMountain, suggest(40.76, -111.89));  // Salt Lake City
  EXPECT_EQ(kMountain, suggest(43.62, -116.20));  // Boise
  EXPECT_EQ(kMountain, suggest(31.76, -106.49));  // El Paso
  EXPECT_EQ(kArizona, suggest(33.45, -112.07));   // Phoenix
  EXPECT_EQ(kPacific, suggest(36.17, -115.14));   // Las Vegas
  EXPECT_EQ(kPacific, suggest(34.05, -118.24));   // Los Angeles
  EXPECT_EQ(kPacific, suggest(47.61, -122.33));   // Seattle
  EXPECT_EQ(kAlaska, suggest(61.22, -149.90));    // Anchorage
  EXPECT_EQ(kHawaii, suggest(21.31, -157.86));    // Honolulu
}

// Arizona keeps standard time; the Navajo Nation inside it keeps daylight saving, and
// the Hopi land inside that doesn't.
TEST(TimeZonesSuggest, ArizonaAndTheNavajoNation) {
  EXPECT_EQ(kArizona, suggest(35.20, -111.65));    // Flagstaff
  EXPECT_EQ(kMountain, suggest(36.73, -110.25));   // Kayenta, Navajo Nation
  EXPECT_EQ(kMountain, suggest(36.13, -111.24));   // Tuba City, Navajo Nation
  EXPECT_EQ(kArizona, suggest(35.80, -110.50));    // Second Mesa, Hopi
}

// MeshCore's clock is unsigned 32-bit seconds: good until 2106, not 2038.
TEST(TimeZones, PastTwentyThirtyEight) {
  EXPECT_EQ("08:00", clockAt(kEastern, 2224756800u));   // 2040-07-01 12:00Z, EDT
  EXPECT_EQ("07:00", clockAt(kEastern, 2237976000u));   // 2040-12-01 12:00Z, EST
}

// West of UTC, the first hours of 1970 are the evening before: no negative clock.
TEST(TimeZones, TheEpochWestOfUtc) {
  EXPECT_EQ("19:00", clockAt(kEastern, 0));
  EXPECT_EQ(kUtc, fixedZone(0));
}

TEST(TimeZonesSuggest, Europe) {
  EXPECT_EQ(kUk, suggest(51.51, -0.13));          // London
  EXPECT_EQ(kUk, suggest(53.35, -6.26));          // Dublin
  EXPECT_EQ(kUk, suggest(38.72, -9.14));          // Lisbon
  EXPECT_EQ(kCentralEu, suggest(48.86, 2.35));    // Paris
  EXPECT_EQ(kCentralEu, suggest(40.42, -3.70));   // Madrid
  EXPECT_EQ(kCentralEu, suggest(52.52, 13.40));   // Berlin
  EXPECT_EQ(kCentralEu, suggest(52.23, 21.01));   // Warsaw
  EXPECT_EQ(kEasternEu, suggest(60.17, 24.94));   // Helsinki
  EXPECT_EQ(kEasternEu, suggest(37.98, 23.73));   // Athens
}

// Anywhere else, the hour nearest the sun's: good enough to suggest, since it's
// confirmed before it's used.
TEST(TimeZonesSuggest, ElsewhereTheNearestHour) {
  EXPECT_STREQ("UTC+9", zone(suggest(35.68, 139.69)).name);    // Tokyo
  EXPECT_STREQ("UTC+10", zone(suggest(-33.87, 151.21)).name);  // Sydney
  EXPECT_STREQ("UTC-3", zone(suggest(-23.55, -46.63)).name);   // Sao Paulo
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
