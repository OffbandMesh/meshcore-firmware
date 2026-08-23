// Native unit tests for the CrashLog ring's wrap and index arithmetic (#887).
//
// WHY THIS EXISTS. writeToRing() is the only writer into the crash ring: index
// arithmetic, wrap detection, and a split memcpy when a line straddles the end
// of the buffer. An error there corrupts every crash log SILENTLY -- old data
// over new, a wrong `wrapped` flag, or a dump that begins mid-line. Nothing
// reports it; the log is simply wrong, and it is wrong exactly when someone is
// relying on it to explain a crash.
//
// It had no test, and could not have one: writeToRing is static, and every
// reader of the ring is compiled out on a host build, so data went in and
// nothing could read it back. crashLogTestReadRing() (host-only, #887) closes
// that gap.
//
// Every expectation below is computed from the ring geometry, not from what the
// implementation happens to return.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "helpers/diagnostics/CrashLog.h"

using offband::crashLogClear;
using offband::crashLogf;
using offband::crashLogTestCapacity;
using offband::crashLogTestReadRing;

namespace {

// crashLogf() on a host build emits exactly "[host] " + body + "\n".
constexpr size_t kPrefix = 7;  // strlen("[host] ")

size_t lineLen(const std::string& body) { return kPrefix + body.size() + 1; }

// crashLogf truncates at kCrashLogLineMax (240) -- documented in CrashLog.h.
// A line can therefore never occupy more than 240 bytes of the ring.
constexpr size_t kMaxLine = 240;

// Write one line whose total ring footprint is exactly `total` bytes.
void writeLineOfSize(size_t total) {
  ASSERT_GT(total, kPrefix + 1) << "cannot make a line that small";
  ASSERT_LE(total, kMaxLine)
      << "crashLogf truncates above " << kMaxLine
      << " bytes, so this helper cannot produce a line that long";
  std::string body(total - kPrefix - 1, 'x');
  crashLogf("%s", body.c_str());
}

struct Ring {
  std::string bytes;
  bool wrapped = false;
  size_t write_index = 0;
};

Ring readRing() {
  Ring r;
  std::vector<char> buf(crashLogTestCapacity());
  size_t n = crashLogTestReadRing(buf.data(), buf.size(), &r.wrapped, &r.write_index);
  r.bytes.assign(buf.data(), n);
  return r;
}

class CrashLogRing : public ::testing::Test {
 protected:
  void SetUp() override { crashLogClear(); }
};

}  // namespace

// ---------------------------------------------------------------------------
// Below capacity: no wrap, contents in write order
// ---------------------------------------------------------------------------

TEST_F(CrashLogRing, EmptyRingReadsBackEmpty) {
  Ring r = readRing();
  EXPECT_EQ(0u, r.bytes.size());
  EXPECT_FALSE(r.wrapped);
  EXPECT_EQ(0u, r.write_index);
}

TEST_F(CrashLogRing, SingleLineReadsBackVerbatim) {
  crashLogf("%s", "hello");
  Ring r = readRing();
  EXPECT_EQ("[host] hello\n", r.bytes);
  EXPECT_FALSE(r.wrapped);
  EXPECT_EQ(lineLen("hello"), r.write_index);
}

TEST_F(CrashLogRing, SeveralLinesKeepWriteOrder) {
  crashLogf("%s", "one");
  crashLogf("%s", "two");
  crashLogf("%s", "three");
  Ring r = readRing();
  EXPECT_EQ("[host] one\n[host] two\n[host] three\n", r.bytes);
  EXPECT_FALSE(r.wrapped);
}

// ---------------------------------------------------------------------------
// The boundary itself -- the case most likely to be off by one
// ---------------------------------------------------------------------------

TEST_F(CrashLogRing, FillingExactlyToCapacityMarksWrappedAndIndexZero) {
  const size_t cap = crashLogTestCapacity();
  // 120 divides 4080 exactly, so the last line lands flush with the end without
  // needing an oversized final write (crashLogf would truncate one).
  const size_t line = 120;
  ASSERT_EQ(0u, cap % line) << "pick a line size that divides the ring exactly";
  for (size_t written = 0; written < cap; written += line) {
    writeLineOfSize(line);
  }

  Ring r = readRing();
  // Exactly full: the implementation sets wrapped once wi + n >= capacity, and
  // the next byte belongs at offset 0.
  EXPECT_TRUE(r.wrapped);
  EXPECT_EQ(0u, r.write_index);
  EXPECT_EQ(cap, r.bytes.size());
}

TEST_F(CrashLogRing, LineStraddlingTheEndIsSplitAndRejoinedInOrder) {
  const size_t cap = crashLogTestCapacity();
  const size_t line = 120;
  ASSERT_EQ(0u, cap % line);
  // Fill to 10 bytes short of the end: full lines, then one shortened by 10.
  for (size_t written = 0; written + line < cap; written += line) {
    writeLineOfSize(line);
  }
  writeLineOfSize(line - 10);               // exactly 10 bytes left at the end
  const size_t straddler = 60;              // 10 before the end, 50 after
  std::string body(straddler - kPrefix - 1, 'S');
  crashLogf("%s", body.c_str());

  Ring r = readRing();
  EXPECT_TRUE(r.wrapped);
  EXPECT_EQ(cap, r.bytes.size());
  EXPECT_EQ(straddler - 10, r.write_index) << "write_index must be the tail past the split";

  // The straddling line must come back contiguous and last, not torn in half.
  const std::string expected = "[host] " + body + "\n";
  EXPECT_NE(std::string::npos, r.bytes.find(expected))
      << "the split line did not rejoin in logical order";
  EXPECT_EQ(r.bytes.size() - expected.size(), r.bytes.rfind(expected))
      << "the newest line must end the logical ordering";
}

// ---------------------------------------------------------------------------
// Past capacity: oldest data is discarded, newest survives
// ---------------------------------------------------------------------------

TEST_F(CrashLogRing, WrappingDiscardsOldestAndKeepsNewest) {
  const size_t cap = crashLogTestCapacity();
  crashLogf("%s", "OLDEST-MARKER");
  size_t written = lineLen("OLDEST-MARKER");
  while (written < cap + 500) {  // comfortably past one full lap
    writeLineOfSize(200);
    written += 200;
  }
  crashLogf("%s", "NEWEST-MARKER");

  Ring r = readRing();
  EXPECT_TRUE(r.wrapped);
  EXPECT_EQ(cap, r.bytes.size());
  EXPECT_EQ(std::string::npos, r.bytes.find("OLDEST-MARKER"))
      << "the oldest line should have been overwritten";
  EXPECT_NE(std::string::npos, r.bytes.find("NEWEST-MARKER"))
      << "the newest line must survive";
}

TEST_F(CrashLogRing, MultipleLapsStayConsistent) {
  const size_t cap = crashLogTestCapacity();
  for (int lap = 0; lap < 3; ++lap) {
    size_t written = 0;
    while (written < cap) {
      writeLineOfSize(200);
      written += 200;
    }
  }
  crashLogf("%s", "LAP3-END");

  Ring r = readRing();
  EXPECT_TRUE(r.wrapped);
  EXPECT_EQ(cap, r.bytes.size()) << "a wrapped ring always reads back full";
  EXPECT_LT(r.write_index, cap) << "write_index must stay inside the buffer";
  EXPECT_NE(std::string::npos, r.bytes.find("LAP3-END"));
}

// ---------------------------------------------------------------------------
// Clearing
// ---------------------------------------------------------------------------

TEST_F(CrashLogRing, ClearResetsIndexAndWrapWithoutReturningStaleBytes) {
  const size_t cap = crashLogTestCapacity();
  size_t written = 0;
  while (written < cap + 200) {
    writeLineOfSize(200);
    written += 200;
  }
  ASSERT_TRUE(readRing().wrapped);

  crashLogClear();

  Ring r = readRing();
  EXPECT_FALSE(r.wrapped);
  EXPECT_EQ(0u, r.write_index);
  EXPECT_EQ(0u, r.bytes.size()) << "a cleared ring must not surface prior contents";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
