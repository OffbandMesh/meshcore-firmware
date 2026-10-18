// Epic G (#1431) G6 (#1571): the boot screen's rows and lines, pinned before
// the screen is built. Design of record: ObScreen.dc.html (boot) and mockup 18
// of epic-g-gap-mockups.html (owner-approved 2026-10-17).

#include <gtest/gtest.h>

#include <string.h>

#include "helpers/gui/BootViews.h"

using namespace offband::gui;

namespace {
BootFacts healthy() {
  BootFacts f{};
  f.prefs = PrefsSource::File;
  f.radio = "SX1262";
  f.gps_built = true;
  f.gps_on = true;
  f.gps_seen = true;
  f.contacts = 302;
  f.contacts_max = 350;
  f.ble_built = true;
  f.ble_on = true;
  return f;
}
}  // namespace

TEST(Boot, Tags) {
  EXPECT_STREQ(checkTag(Check::Ok), "[ok]");
  EXPECT_STREQ(checkTag(Check::Pending), "[..]");
  EXPECT_STREQ(checkTag(Check::Off), "[--]");
}

TEST(Boot, RowsAsDrawn) {
  BootRow rows[kBootRowsMax];
  const size_t n = bootRows(healthy(), rows, kBootRowsMax);
  ASSERT_EQ(n, 5u);
  EXPECT_EQ(rows[0].state, Check::Ok);
  EXPECT_STREQ(rows[0].text, "Settings");
  EXPECT_STREQ(rows[1].text, "SX1262 radio");   // a failed radio halts boot: always ok here
  EXPECT_EQ(rows[1].state, Check::Ok);
  EXPECT_STREQ(rows[2].text, "GPS");
  EXPECT_STREQ(rows[3].text, "Contacts 302 / 350");
  EXPECT_STREQ(rows[4].text, "BLE advertising");
  EXPECT_EQ(rows[4].state, Check::Ok);
}

TEST(Boot, RowsSayWhatIsNotReady) {
  BootFacts f = healthy();
  f.prefs = PrefsSource::Defaults;
  f.gps_seen = false;
  f.ble_on = false;
  BootRow rows[kBootRowsMax];
  bootRows(f, rows, kBootRowsMax);
  EXPECT_EQ(rows[0].state, Check::Off);
  EXPECT_STREQ(rows[0].text, "Settings: defaults");
  EXPECT_EQ(rows[2].state, Check::Pending);   // GPS on, not heard from yet
  EXPECT_EQ(rows[4].state, Check::Off);
  EXPECT_STREQ(rows[4].text, "Bluetooth off");

  f.prefs = PrefsSource::Migrated;
  f.gps_on = false;
  f.phone = true;
  f.ble_on = true;
  bootRows(f, rows, kBootRowsMax);
  EXPECT_EQ(rows[0].state, Check::Ok);
  EXPECT_STREQ(rows[0].text, "Settings migrated");
  EXPECT_EQ(rows[2].state, Check::Off);
  EXPECT_STREQ(rows[2].text, "GPS off");
  EXPECT_STREQ(rows[4].text, "Phone connected");
}

TEST(Boot, RowsLeaveOutWhatIsNotBuilt) {
  BootFacts f = healthy();
  f.gps_built = false;
  f.ble_built = false;
  BootRow rows[kBootRowsMax];
  const size_t n = bootRows(f, rows, kBootRowsMax);
  ASSERT_EQ(n, 3u);
  EXPECT_STREQ(rows[2].text, "Contacts 302 / 350");
  EXPECT_EQ(bootRows(healthy(), rows, 2), 2u);   // never past max
}

TEST(Boot, UncleanResetOnly) {
  // A clean start leaves the line out (mockup 18).
  EXPECT_FALSE(uncleanBoot(1, 1));    // power on
  EXPECT_FALSE(uncleanBoot(3, 1));    // a reboot from Settings or the app
  EXPECT_FALSE(uncleanBoot(2, 1));    // RST button
  EXPECT_FALSE(uncleanBoot(11, 1));   // USB host
  EXPECT_FALSE(uncleanBoot(0, 1));    // unknown: IDF 4.4's name for a USB host reset
  EXPECT_TRUE(uncleanBoot(9, 1));     // brownout
  EXPECT_TRUE(uncleanBoot(4, 1));     // crash
  EXPECT_TRUE(uncleanBoot(6, 1));     // watchdog
  EXPECT_TRUE(uncleanBoot(1, 2));     // a second try at booting
}

TEST(Boot, Lines) {
  char b[48];
  bootLine(1, 3, 9, b, sizeof b);
  EXPECT_STREQ(b, "boot 1/3 · last reset: brownout");
  bootLine(2, 0, 6, b, sizeof b);     // no SafeBoot count on this board
  EXPECT_STREQ(b, "last reset: watchdog");
  // Offband first, then MeshCore (owner, 2026-10-17).
  bootVersionLine("v1.17.0", "v1.5.0", b, sizeof b);
  EXPECT_STREQ(b, "Offband 1.5.0 - MC 1.17.0");   // MeshCore in full (owner, 2026-10-17)
  bootVersionLine("v1.17.1", "v1.5.0-beta7+9", b, sizeof b);
  EXPECT_STREQ(b, "Offband 1.5.0-beta7+9 - MC 1.17.1");
  bootVersionLine("1.18.0", "", b, sizeof b);
  EXPECT_STREQ(b, "MC 1.18.0");
}

TEST(Boot, MorseBar) {
  // "offband" in Morse is the bar (mockup 18b, owner pick 2026-10-17).
  EXPECT_STREQ(kOffbandMorse, "--- ..-. ..-. -... .- -. -..");
  EXPECT_EQ(morseMarks(kOffbandMorse), 22u);
  EXPECT_EQ(morseLit(0, 22), 0u);
  EXPECT_EQ(morseLit(60, 22), 13u);
  EXPECT_EQ(morseLit(100, 22), 22u);
  EXPECT_EQ(morseLit(150, 22), 22u);
}

TEST(Boot, Progress) {
  EXPECT_EQ(bootProgress(0, 2500), 0);
  EXPECT_EQ(bootProgress(1250, 2500), 50);
  EXPECT_EQ(bootProgress(9000, 2500), 100);
  EXPECT_EQ(bootProgress(10, 0), 100);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
