// Epic G (#1431): how Settings read and what they may be set to, pinned before
// they are built (G1, #1566). Design of record: ObScreen.dc.html and the
// owner-approved gap mockups (epic-g-gap-mockups.html, 2026-10-17).

#include <gtest/gtest.h>

#include <string.h>

#include "helpers/gui/SettingsViews.h"

using namespace offband::gui;

namespace {
const RadioValues kUsa{910525, 62500, 7, 5, 22, 0, false};
}

// --------------------------------------------------------------- values

TEST(Values, RadioRowsReadAsDrawn) {
  char b[32];
  freqText(910525, b, sizeof b);
  EXPECT_STREQ(b, "910.525");
  freqText(433000, b, sizeof b);
  EXPECT_STREQ(b, "433.000");
  bwText(62500, b, sizeof b);
  EXPECT_STREQ(b, "62.5 kHz");
  bwText(125000, b, sizeof b);
  EXPECT_STREQ(b, "125 kHz");
  bwText(31250, b, sizeof b);
  EXPECT_STREQ(b, "31.25 kHz");
  bwText(7800, b, sizeof b);
  EXPECT_STREQ(b, "7.8 kHz");
  sfText(7, b, sizeof b);
  EXPECT_STREQ(b, "SF7");
  crText(5, b, sizeof b);
  EXPECT_STREQ(b, "4/5");
  crText(8, b, sizeof b);
  EXPECT_STREQ(b, "4/8");
  dbmText(22, b, sizeof b);
  EXPECT_STREQ(b, "22 dBm");
  dbmText(-9, b, sizeof b);
  EXPECT_STREQ(b, "-9 dBm");
  pctText(70, b, sizeof b);
  EXPECT_STREQ(b, "70%");
}

TEST(Values, Durations) {
  char b[16];
  durationText(30, b, sizeof b);
  EXPECT_STREQ(b, "30 s");
  durationText(120, b, sizeof b);
  EXPECT_STREQ(b, "2 min");
  durationText(90, b, sizeof b);
  EXPECT_STREQ(b, "90 s");
  durationText(3600, b, sizeof b);
  EXPECT_STREQ(b, "1 h");
  durationText(0, b, sizeof b);
  EXPECT_STREQ(b, "Never");
}

TEST(Values, PathHashMode) {
  char b[24];
  pathHashText(0, b, sizeof b);
  EXPECT_STREQ(b, "1-byte · 63 hops");
  pathHashText(1, b, sizeof b);
  EXPECT_STREQ(b, "2-byte · 32 hops");
  pathHashText(2, b, sizeof b);
  EXPECT_STREQ(b, "3-byte · 21 hops");
}

TEST(Values, SummaryClockTelemetry) {
  char b[24];
  radioSummary(910525, 7, b, sizeof b);
  EXPECT_STREQ(b, "910.525 · SF7");
  clockText(21, 7, false, b, sizeof b);
  EXPECT_STREQ(b, "21:07");
  clockText(21, 7, true, b, sizeof b);
  EXPECT_STREQ(b, "9:07 PM");
  clockText(0, 5, true, b, sizeof b);
  EXPECT_STREQ(b, "12:05 AM");
  clockText(12, 0, true, b, sizeof b);
  EXPECT_STREQ(b, "12:00 PM");
  clockText(24, 0, false, b, sizeof b);   // out of range wraps (Gemini G1)
  EXPECT_STREQ(b, "00:00");
  clockText(24, 0, true, b, sizeof b);
  EXPECT_STREQ(b, "12:00 AM");
  EXPECT_STREQ(telemetryText(0), "Never");
  EXPECT_STREQ(telemetryText(1), "Contacts");
  EXPECT_STREQ(telemetryText(2), "Everyone");
}

// ------------------------------------------------------------ the limits

TEST(Limits, FrequencyRangeAsThePhonePath) {
  EXPECT_TRUE(freqOk(150000));
  EXPECT_TRUE(freqOk(910525));
  EXPECT_TRUE(freqOk(2500000));
  EXPECT_FALSE(freqOk(149999));
  EXPECT_FALSE(freqOk(2500001));
}

TEST(Limits, StepperStaysInside) {
  const StepSpec tx{0, 22, 1};
  EXPECT_EQ(stepBy(21, 1, tx), 22);
  EXPECT_EQ(stepBy(22, 1, tx), 22);
  EXPECT_EQ(stepBy(0, -1, tx), 0);
  const StepSpec pct{10, 100, 10};
  EXPECT_EQ(stepBy(70, 1, pct), 80);
  EXPECT_EQ(stepBy(95, 1, pct), 100);   // a value off the grid steps to it
  EXPECT_EQ(stepBy(15, -1, pct), 10);
}

TEST(Limits, OptionLists) {
  EXPECT_EQ(kBandwidthCount, 10u);
  EXPECT_EQ(kBandwidthsHz[6], 62500u);
  EXPECT_EQ(kSfMin, 5);
  EXPECT_EQ(kSfMax, 12);
  EXPECT_EQ(kCrMin, 5);
  EXPECT_EQ(kCrMax, 8);
}

// ------------------------------------------------------- Display choices

TEST(Display, ThemeNamesAndAccents) {
  EXPECT_STREQ(themeName(0), "Offband");
  EXPECT_STREQ(themeName(1), "Steel");
  EXPECT_STREQ(themeName(2), "Mono HC");
  EXPECT_STREQ(themeName(3), "Accent");
  EXPECT_STREQ(themeName(9), "Offband");
  ASSERT_EQ(kAccentCount, 8u);
  EXPECT_EQ(kAccents[0], 0xF2C14Eu);   // the default accent first
  for (size_t i = 0; i < kAccentCount; i++) EXPECT_EQ(kAccents[i] & 0xFF000000u, 0u);
}

TEST(Display, TimerChoices) {
  // Dim after: 10 s to 5 min, or never (0). Screen off: 30 s to 30 min.
  EXPECT_EQ(kDimChoices[kDimCount - 1], 0u);
  EXPECT_EQ(choiceIndex(kDimChoices, kDimCount, 30), 1);
  EXPECT_EQ(choiceIndex(kDimChoices, kDimCount, 0), int(kDimCount) - 1);
  EXPECT_EQ(choiceIndex(kOffChoices, kOffCount, 120), 2);
  EXPECT_EQ(choiceIndex(kOffChoices, kOffCount, 45), -1);   // not one of them
}

// ---------------------------------------------- Node, stats, Contacts (G4)

TEST(Stats, RowsReadAsDrawn) {
  char b[24];
  airtimeText(43, 3600, b, sizeof b);   // 43 s of 1 h
  EXPECT_STREQ(b, "1.2%");
  airtimeText(0, 0, b, sizeof b);
  EXPECT_STREQ(b, "0.0%");
  sentText(412, 96, b, sizeof b);
  EXPECT_STREQ(b, "412 · 96");
  sentText(2318, 0, b, sizeof b);
  EXPECT_STREQ(b, "2,318 · 0");
  lastPacketText(25, -97, b, sizeof b);   // SNR x4
  EXPECT_STREQ(b, "+6.3 · -97");
  lastPacketText(-10, -120, b, sizeof b);
  EXPECT_STREQ(b, "-2.5 · -120");
}

TEST(Contacts, HopLimitChoices) {
  // autoadd_max_hops: 0 any, 1 direct only, N up to N-1 hops.
  ASSERT_EQ(kHopCount, 6u);
  char b[16];
  hopLimitText(0, b, sizeof b);
  EXPECT_STREQ(b, "Any");
  hopLimitText(1, b, sizeof b);
  EXPECT_STREQ(b, "Direct");
  hopLimitText(2, b, sizeof b);
  EXPECT_STREQ(b, "1 hop");
  hopLimitText(4, b, sizeof b);
  EXPECT_STREQ(b, "3 hops");
  EXPECT_EQ(choiceIndex(kHopChoices, kHopCount, 4), 4);
}

TEST(Contacts, AutoAddSummary) {
  // Bits: chat 0x02, repeater 0x04, room 0x08, sensor 0x10.
  char b[24];
  autoAddText(false, 0x1E, b, sizeof b);
  EXPECT_STREQ(b, "Auto-add: all");
  autoAddText(true, 0x1E, b, sizeof b);
  EXPECT_STREQ(b, "Auto-add: all");
  autoAddText(true, 0x06, b, sizeof b);
  EXPECT_STREQ(b, "Auto-add: some");
  autoAddText(true, 0x00, b, sizeof b);
  EXPECT_STREQ(b, "Auto-add: off");
}

TEST(Node, PositionEntrySigned) {
  NumberEntry e;
  e.start("", 6, 12, true);
  EXPECT_TRUE(e.key('-'));   // the sign toggles, at the front
  for (char c : {'8', '4', '.', '4', '0', '0', '0', '0', '0'}) EXPECT_TRUE(e.key(c));
  EXPECT_STREQ(e.text(), "-84.400000");
  int32_t v = 0;
  ASSERT_TRUE(e.scaled(6, &v));
  EXPECT_EQ(v, -84400000);
  EXPECT_TRUE(e.key('-'));
  EXPECT_STREQ(e.text(), "84.400000");
  e.start("", 3, 10);   // unsigned entry refuses a sign
  EXPECT_FALSE(e.key('-'));
}

TEST(Node, PositionLimits) {
  EXPECT_TRUE(latOk(90000000));
  EXPECT_FALSE(latOk(90000001));
  EXPECT_TRUE(lonOk(-180000000));
  EXPECT_FALSE(lonOk(180000001));
  char b[32];
  positionText(39500000, -84400000, b, sizeof b);
  EXPECT_STREQ(b, "39.5000, -84.4000");
  positionText(0, 0, b, sizeof b);
  EXPECT_STREQ(b, "Not set");
}

// --------------------------------------------------------------- presets

TEST(Presets, TableFromTheApp) {
  ASSERT_EQ(radioPresetCount(), 59u);
  EXPECT_STREQ(radioPreset(0).title, "Australia");
  int usa = -1;
  for (size_t i = 0; i < radioPresetCount(); i++) {
    if (strcmp(radioPreset(i).title, "USA") == 0) usa = int(i);
  }
  ASSERT_GE(usa, 0);
  EXPECT_EQ(radioPreset(usa).freq_khz, 910525u);
  EXPECT_EQ(radioPreset(usa).bw_hz, 62500u);
}

TEST(Presets, MatchKeepsThePickedOneAmongTwins) {
  // Brazil and Australia: SA, WA are the same radio values.
  int brazil = -1, sawa = -1;
  for (size_t i = 0; i < radioPresetCount(); i++) {
    if (strcmp(radioPreset(i).title, "Brazil") == 0) brazil = int(i);
    if (strcmp(radioPreset(i).title, "Australia: SA, WA") == 0) sawa = int(i);
  }
  ASSERT_GE(brazil, 0);
  ASSERT_GE(sawa, 0);
  const RadioValues v{923125, 62500, 8, 8, 22, 0, false};
  EXPECT_EQ(presetFor(v, brazil), brazil);
  EXPECT_EQ(presetFor(v, -1), sawa);   // first in the list
  EXPECT_EQ(presetFor(v, 0), sawa);    // a hint that doesn't match is ignored
}

TEST(Presets, APresetsPathHashDecidesToo) {
  // Bench (owner, 2026-10-17): USA values read "Canada". Canada is the same
  // radio values with 3-byte hashes, and comes first in the table.
  int usa = -1, canada = -1;
  for (size_t i = 0; i < radioPresetCount(); i++) {
    if (strcmp(radioPreset(i).title, "USA") == 0) usa = int(i);
    if (strcmp(radioPreset(i).title, "Canada") == 0) canada = int(i);
  }
  ASSERT_GE(usa, 0);
  ASSERT_GE(canada, 0);
  RadioValues v = kUsa;   // 1-byte hashes
  EXPECT_EQ(presetFor(v, -1), usa);
  v.path_hash_mode = 1;
  EXPECT_EQ(presetFor(v, -1), usa);   // USA sets no hash size: any matches
  v.path_hash_mode = 2;               // 3-byte: Canada's
  EXPECT_EQ(presetFor(v, -1), canada);
  EXPECT_EQ(presetFor(v, usa), usa);  // the one picked stays
}

TEST(Presets, CustomWhenNothingMatches) {
  RadioValues v = kUsa;
  v.sf = 9;
  EXPECT_EQ(presetFor(v, -1), -1);
  v = kUsa;
  v.tx_dbm = 10;   // TX power doesn't decide
  EXPECT_GE(presetFor(v, -1), 0);
}

TEST(Presets, ApplyHoldsTxToTheBoard) {
  int philly = -1;
  for (size_t i = 0; i < radioPresetCount(); i++) {
    if (strcmp(radioPreset(i).title, "USA - Philly Mesh") == 0) philly = int(i);
  }
  ASSERT_GE(philly, 0);
  RadioValues v = applyPreset(kUsa, size_t(philly), 22);
  EXPECT_EQ(v.freq_khz, 919500u);
  EXPECT_EQ(v.bw_hz, 500000u);
  EXPECT_EQ(v.sf, 10);
  EXPECT_EQ(v.cr, 5);
  EXPECT_EQ(v.tx_dbm, 20);
  EXPECT_EQ(v.path_hash_mode, 1);   // 2-byte
  v = applyPreset(kUsa, size_t(philly), 14);
  EXPECT_EQ(v.tx_dbm, 14);   // held to this board
  // An upstream preset leaves TX power and path hash alone.
  RadioValues s = kUsa;
  s.tx_dbm = 17;
  s.path_hash_mode = 2;
  s = applyPreset(s, 0, 22);
  EXPECT_EQ(s.tx_dbm, 17);
  EXPECT_EQ(s.path_hash_mode, 2);
}

// --------------------------------------------------------------- discard

TEST(Discard, OneLinePerChange) {
  RadioValues now = kUsa;
  now.tx_dbm = 20;
  char lines[3][48];
  ASSERT_EQ(radioChanges(kUsa, now, lines, 3), 1u);
  EXPECT_STREQ(lines[0], "TX power 22 → 20 dBm");
  now.freq_khz = 869618;
  now.sf = 8;
  now.repeat = true;
  now.path_hash_mode = 1;
  EXPECT_EQ(radioChanges(kUsa, now, lines, 3), 5u);   // all counted, 3 written
  EXPECT_STREQ(lines[0], "Frequency 910.525 → 869.618");
  EXPECT_STREQ(lines[1], "Spreading factor SF7 → SF8");
  EXPECT_STREQ(lines[2], "TX power 22 → 20 dBm");
  EXPECT_EQ(radioChanges(kUsa, kUsa, lines, 3), 0u);
}

// ------------------------------------------------------------ number pad

TEST(Pad, DigitsOnePointThreeDecimals) {
  NumberEntry e;
  e.start("", 3, 10);
  for (char c : {'9', '1', '0', '.', '5', '2', '5'}) EXPECT_TRUE(e.key(c));
  EXPECT_STREQ(e.text(), "910.525");
  EXPECT_FALSE(e.key('1'));   // a fourth decimal
  EXPECT_FALSE(e.key('.'));   // a second point
  uint32_t v = 0;
  ASSERT_TRUE(e.thousandths(&v));
  EXPECT_EQ(v, 910525u);
}

TEST(Pad, BackspaceAndPartialValues) {
  NumberEntry e;
  e.start("910.525", 3, 10);
  e.backspace();
  e.backspace();
  EXPECT_STREQ(e.text(), "910.5");
  uint32_t v = 0;
  ASSERT_TRUE(e.thousandths(&v));
  EXPECT_EQ(v, 910500u);
  e.start("", 3, 10);
  EXPECT_FALSE(e.thousandths(&v));
  e.key('.');
  EXPECT_FALSE(e.thousandths(&v));   // a bare point
  e.start("433", 3, 10);
  ASSERT_TRUE(e.thousandths(&v));
  EXPECT_EQ(v, 433000u);
}

TEST(Pad, TooBigIsNoValue) {
  // 5,000,000 MHz in thousandths doesn't fit 32 bits (Gemini G1).
  NumberEntry e;
  e.start("5000000", 3, 10);
  uint32_t v = 7;
  EXPECT_FALSE(e.thousandths(&v));
  EXPECT_EQ(v, 7u);
  e.start("4294967", 3, 10);   // the largest that fits: 4,294,967,000
  ASSERT_TRUE(e.thousandths(&v));
  EXPECT_EQ(v, 4294967000u);
}

TEST(Pad, LengthCapAndOnlyDigits) {
  NumberEntry e;
  e.start("", 3, 4);
  for (char c : {'1', '2', '3', '4'}) EXPECT_TRUE(e.key(c));
  EXPECT_FALSE(e.key('5'));
  EXPECT_FALSE(e.key('x'));
  EXPECT_STREQ(e.text(), "1234");
}

// ------------------------------------- Connectivity, Power, Debug, About (G5)

TEST(G5, Volts) {
  char b[24];
  voltsText(4020, b, sizeof b);
  EXPECT_STREQ(b, "4.02 V");
  voltsText(3400, b, sizeof b);
  EXPECT_STREQ(b, "3.40 V");
  voltsText(4155, b, sizeof b);   // to the nearest hundredth
  EXPECT_STREQ(b, "4.16 V");
  voltsText(0, b, sizeof b);      // no reading
  EXPECT_STREQ(b, "—");
  batteryNowText(4020, 87, b, sizeof b);
  EXPECT_STREQ(b, "4.02 V · 87%");
}

TEST(G5, Pin) {
  EXPECT_TRUE(pinOk(123456));
  EXPECT_TRUE(pinOk(999999));
  EXPECT_FALSE(pinOk(99999));     // five digits
  EXPECT_FALSE(pinOk(1000000));
  EXPECT_FALSE(pinOk(0));
  char b[24];
  pinText(654321, 654321, b, sizeof b);   // a set PIN shows masked (the fonts have no •)
  EXPECT_STREQ(b, "******");
  pinText(0, 482913, b, sizeof b);        // a new one each boot: shown, to pair with
  EXPECT_STREQ(b, "482913");
  pinText(0, 0, b, sizeof b);             // no PIN in this build
  EXPECT_STREQ(b, "None");
}

TEST(G5, LogLevels) {
  // Off, then the capture levels MeshLog keeps: 1 errors, 2 debug, 3 packets.
  EXPECT_EQ(kLogCount, 4u);
  EXPECT_EQ(logIndex(false, 2), 0);
  EXPECT_EQ(logIndex(true, 1), 1);
  EXPECT_EQ(logIndex(true, 2), 2);
  EXPECT_EQ(logIndex(true, 3), 3);
  EXPECT_EQ(logIndex(true, 0), 1);   // boot lines only: under Errors, which holds them
  EXPECT_EQ(logIndex(true, 9), 3);
  EXPECT_STREQ(logName(0), "Off");
  EXPECT_STREQ(logName(1), "Errors");
  EXPECT_STREQ(logName(2), "Debug");
  EXPECT_STREQ(logName(3), "Packets");
  EXPECT_STREQ(logName(7), "Off");
}

TEST(G5, BootsBuildAndCounts) {
  char b[32];
  ofText(1, 3, b, sizeof b);
  EXPECT_STREQ(b, "1 / 3");
  ofText(412, 1000, b, sizeof b);
  EXPECT_STREQ(b, "412 / 1,000");
  buildText("37f145c", "2026-10-17", b, sizeof b);
  EXPECT_STREQ(b, "37f145c · Oct 17");
  buildText("37f145c", "2026-01-05", b, sizeof b);
  EXPECT_STREQ(b, "37f145c · Jan 5");
  buildText("37f145c", "", b, sizeof b);   // no date: the SHA alone
  EXPECT_STREQ(b, "37f145c");
  buildText("37f145c", "2026-13-01", b, sizeof b);
  EXPECT_STREQ(b, "37f145c");
}

TEST(G5, ResetNames) {
  EXPECT_STREQ(resetName(9), "Brownout");
  EXPECT_STREQ(resetName(1), "Power on");
  EXPECT_STREQ(resetName(2), "RST button");
  EXPECT_STREQ(resetName(3), "Software");
  EXPECT_STREQ(resetName(4), "Crash");
  EXPECT_STREQ(resetName(6), "Watchdog");
  EXPECT_STREQ(resetName(11), "USB");
  EXPECT_STREQ(resetName(0), "Unknown");
  EXPECT_STREQ(resetName(99), "Unknown");
}

TEST(G5, FullChargeRange) {
  // The learner's own band (BatteryGauge.h): 3.90 - 4.25 V, by 10 mV.
  EXPECT_EQ(kFullStep.min, 3900);
  EXPECT_EQ(kFullStep.max, 4250);
  EXPECT_EQ(kFullStep.step, 10);
  EXPECT_EQ(stepBy(4250, 1, kFullStep), 4250);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
