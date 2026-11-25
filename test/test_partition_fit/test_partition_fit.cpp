// #2297: the two partition decisions for the 4 MB ESP32 boards (PartitionFit.h).
//
// Pre-fix reference, kept here so each test shows it differs from it:
// * the OTA handler started every upload with Update.begin(UPDATE_SIZE_UNKNOWN),
//   so it accepted any size up front and only failed once a write overran the
//   slot, after erasing it. legacyAcceptsUpFront() is that behavior.
// * nothing compared the running table with the one the build expects, so an
//   old-table unit was never reported. legacyReportsOldTable() is that behavior.

#include <gtest/gtest.h>
#include "helpers/PartitionFit.h"

using partition_fit::OtaVerdict;
using partition_fit::kMultipartOverhead;
using partition_fit::otaImageFits;
using partition_fit::isOldTable;
using partition_fit::Layout;

namespace {

constexpr size_t kOldC3Slot = 0x140000;     // board default.csv: 1,310,720
constexpr size_t kNewSlot = 0x1B0000;       // offband_4MB.csv: 1,769,472
constexpr uint32_t kNewSpiffs = 0x80000;    // offband_4MB.csv: 512 KB
constexpr uint32_t kOldC3Spiffs = 0x160000; // default.csv: 1,408 KB
constexpr uint32_t kOldC6Spiffs = 0x20000;  // min_spiffs.csv: 128 KB

bool legacyAcceptsUpFront(size_t, size_t) { return true; }
bool legacyReportsOldTable(uint32_t, uint32_t) { return false; }

}  // namespace

// --- OTA image against the target slot ---------------------------------------

TEST(OtaFit, The55xC3RepeaterOnAnOldTableUnitIsRefusedUpFront) {
  // B6 measured the 55.x C3 repeater at 1,405,009 bytes.
  const size_t request = 1405009 + 300;
  EXPECT_EQ(OtaVerdict::TooBig, otaImageFits(request, kOldC3Slot));
  EXPECT_TRUE(legacyAcceptsUpFront(request, kOldC3Slot));  // the defect
}

TEST(OtaFit, TheSameImageFitsTheNewSlot) {
  EXPECT_EQ(OtaVerdict::Fits, otaImageFits(1405009 + 300, kNewSlot));
}

TEST(OtaFit, ExactlyTheSlotSizeFits) {
  EXPECT_EQ(OtaVerdict::Fits, otaImageFits(kOldC3Slot, kOldC3Slot));
}

TEST(OtaFit, SlotPlusOneIsNotAcceptedAsFitting) {
  // One byte over is within the framing band: not provably too big, so the
  // updater's own size check decides. It must never be reported as Fits.
  EXPECT_EQ(OtaVerdict::Uncertain, otaImageFits(kOldC3Slot + 1, kOldC3Slot));
}

TEST(OtaFit, TheFramingBandEdges) {
  EXPECT_EQ(OtaVerdict::Uncertain, otaImageFits(kOldC3Slot + kMultipartOverhead, kOldC3Slot));
  EXPECT_EQ(OtaVerdict::TooBig, otaImageFits(kOldC3Slot + kMultipartOverhead + 1, kOldC3Slot));
}

TEST(OtaFit, UnknownLengthDecidesNothing) {
  EXPECT_EQ(OtaVerdict::Uncertain, otaImageFits(0, kOldC3Slot));
}

TEST(OtaFit, SmallSlotDoesNotUnderflow) {
  // A request smaller than the framing ceiling against a tiny slot must not
  // wrap around into "too big" or "fits".
  EXPECT_EQ(OtaVerdict::Uncertain, otaImageFits(600, 500));
  EXPECT_EQ(OtaVerdict::TooBig, otaImageFits(500 + kMultipartOverhead + 1, 500));
}

TEST(OtaFit, TodaysC3ImagesStillFitTheOldSlot) {
  // Today's platform: the C3 repeater is 1,142,256 bytes. An old-table unit
  // can take it by OTA, so the refusal must not block it.
  EXPECT_EQ(OtaVerdict::Fits, otaImageFits(1142256 + 300, kOldC3Slot));
}

// --- the running table against the one the build expects ---------------------

// The three tables, as the running firmware reads them.
const Layout kNew = {kNewSlot, 0x370000, kNewSpiffs};             // offband_4MB.csv
const Layout kOldC3 = {kOldC3Slot, 0x290000, kOldC3Spiffs};       // default.csv
const Layout kOldC6 = {0x1E0000, 0x3D0000, kOldC6Spiffs};         // min_spiffs.csv

TEST(OldTable, AnOldC6UnitOnMinSpiffsIsReported) {
  EXPECT_TRUE(isOldTable(kOldC6, kNew));
  EXPECT_FALSE(legacyReportsOldTable(kOldC6.spiffs_size, kNew.spiffs_size));  // the defect
}

TEST(OldTable, AnOldC3UnitIsReportedThoughItHasMoreSpiffs) {
  // The plan's first rule, "SPIFFS smaller than expected", misses this unit:
  // the C3 default table has MORE SPIFFS than the new one.
  ASSERT_GT(kOldC3.spiffs_size, kNew.spiffs_size);
  EXPECT_TRUE(isOldTable(kOldC3, kNew));
}

TEST(OldTable, AnOldC6UnitIsReportedThoughItHasABiggerAppSlot) {
  ASSERT_GT(kOldC6.app_size, kNew.app_size);
  EXPECT_TRUE(isOldTable(kOldC6, kNew));
}

TEST(OldTable, TheNewTableIsNotReported) {
  EXPECT_FALSE(isOldTable(kNew, kNew));
}

TEST(OldTable, EachFieldAloneIsEnough) {
  Layout l = kNew;
  l.app_size -= 0x10000;
  EXPECT_TRUE(isOldTable(l, kNew));
  l = kNew;
  l.spiffs_offset += 0x1000;
  EXPECT_TRUE(isOldTable(l, kNew));
  l = kNew;
  l.spiffs_size -= 0x1000;
  EXPECT_TRUE(isOldTable(l, kNew));
}

TEST(OldTable, NoExpectationMeansNeverOld) {
  const Layout none = {0, 0, 0};
  EXPECT_FALSE(isOldTable(kOldC6, none));
  EXPECT_FALSE(isOldTable(kNew, none));
}

TEST(OldTable, AMissingSpiffsPartitionIsReported) {
  // No SPIFFS partition found reads as offset and size 0: report it.
  const Layout no_spiffs = {kNewSlot, 0, 0};
  EXPECT_TRUE(isOldTable(no_spiffs, kNew));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
