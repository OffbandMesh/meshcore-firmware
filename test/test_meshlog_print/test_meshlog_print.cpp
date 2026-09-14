// Native unit tests for mesh_log_print (#1211): a line that has always gone to the
// serial console now also goes through mesh_log_line(), so it reaches the capture
// ring and the raw UART mirror the bench rig reads. SafeBoot's battery lines use
// it. Whatever the capture and echo flags say, the console sees the line once.

#include <gtest/gtest.h>
#include <Arduino.h>
#include <string>
#include "MeshLog.h"

namespace {

std::string ring() {
  uint8_t buf[1024];
  const size_t n = meshLogSnapshot(buf, sizeof(buf));
  return std::string(reinterpret_cast<const char*>(buf), n);
}

size_t count(const std::string& hay, const std::string& needle) {
  size_t n = 0;
  for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + needle.size())) n++;
  return n;
}

const char* const kText = "[SafeBoot] Vbat=4100 mV stable";
const char* const kLine = "[SafeBoot] Vbat=4100 mV stable\r\n";

class MeshLogPrint : public ::testing::Test {
protected:
  void SetUp() override {
    meshLogClear();
    meshLogSetLevel(MLOG_DEBUG);
    Serial.captured.clear();
    Serial.record = true;
  }
  void TearDown() override {
    Serial.record = false;
    Serial.captured.clear();
    meshLogSetEnabled(false);
    meshLogSetMirror(true);
    meshLogSetLevel(MLOG_DEBUG);
    meshLogClear();
  }
  static void printSafeBootLine() { mesh_log_print(MLOG_BOOT, "[SafeBoot] Vbat=%u mV stable\r\n", 4100u); }
};

}  // namespace

// Capture is off on every boot until the CLI turns it on, which is after
// SafeBoot runs: the console gets the line exactly as before, and nothing is
// recorded.
TEST_F(MeshLogPrint, CaptureOffPrintsTheLineOnceAsBefore) {
  meshLogSetEnabled(false);
  printSafeBootLine();
  EXPECT_EQ(kLine, Serial.captured);
  EXPECT_EQ("", ring());
}

// Capture on with the echo live: the echo is the console's copy, so the line
// must not print a second time. The ring records it.
TEST_F(MeshLogPrint, CaptureOnWithEchoPrintsOnceAndRecords) {
  meshLogSetEnabled(true);
  meshLogSetMirror(true);
  printSafeBootLine();
  EXPECT_EQ(1u, count(Serial.captured, kText));
  EXPECT_EQ(1u, count(ring(), kText));
}

// Capture on with the echo off, as on a USB-serial companion: the ring records
// the line, and the console still gets the one raw copy it has always had.
TEST_F(MeshLogPrint, CaptureOnWithEchoOffStillPrintsOnce) {
  meshLogSetEnabled(true);
  meshLogSetMirror(false);
  printSafeBootLine();
  EXPECT_EQ(kLine, Serial.captured);
  EXPECT_EQ(1u, count(ring(), kText));
}

// A line the capture level filters out is not recorded, but it still prints.
TEST_F(MeshLogPrint, LevelFilteredLineStillPrints) {
  meshLogSetEnabled(true);
  meshLogSetLevel(MLOG_BOOT);
  mesh_log_print(MLOG_DEBUG, "debug %d\r\n", 7);
  EXPECT_EQ("debug 7\r\n", Serial.captured);
  EXPECT_EQ("", ring());
}

// An over-long line is cut to fit the line buffer, never overrun.
TEST_F(MeshLogPrint, LongLineIsTruncated) {
  meshLogSetEnabled(false);
  const std::string longText(400, 'x');
  mesh_log_print(MLOG_BOOT, "%s", longText.c_str());
  EXPECT_FALSE(Serial.captured.empty());
  EXPECT_LT(Serial.captured.size(), longText.size());
  EXPECT_EQ(std::string(Serial.captured.size(), 'x'), Serial.captured);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
