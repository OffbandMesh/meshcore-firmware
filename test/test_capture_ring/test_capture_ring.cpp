// Native unit tests for CaptureRing — the line-oriented byte ring behind the
// serial-capture sink (#393). Pure logic, no Arduino: append, ordered
// snapshot, whole-line eviction on overflow, oversized-line handling, clear.

#include <gtest/gtest.h>
#include <string.h>
#include "CaptureRing.h"

static void appendStr(CaptureRing& r, const char* s) {
  r.append(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

// Snapshot into a generous buffer and NUL-terminate for string compares.
static std::string snap(const CaptureRing& r) {
  char out[512];
  size_t n = r.snapshot(reinterpret_cast<uint8_t*>(out), sizeof(out) - 1);
  out[n] = '\0';
  return std::string(out, n);
}

TEST(CaptureRing, FreshRingIsEmpty) {
  uint8_t buf[64];
  CaptureRing r(buf, sizeof(buf));
  EXPECT_EQ(0u, r.bytesUsed());
  EXPECT_EQ(0u, r.snapshot(buf, sizeof(buf)));
}

TEST(CaptureRing, AppendOneLineIsReadBack) {
  uint8_t buf[64];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "hello\n");
  EXPECT_EQ(6u, r.bytesUsed());
  EXPECT_EQ("hello\n", snap(r));
}

TEST(CaptureRing, AppendMultipleLinesPreservesOrder) {
  uint8_t buf[64];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "one\n");
  appendStr(r, "two\n");
  appendStr(r, "three\n");
  EXPECT_EQ("one\ntwo\nthree\n", snap(r));
}

TEST(CaptureRing, ClearEmptiesTheRing) {
  uint8_t buf[64];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "data\n");
  r.clear();
  EXPECT_EQ(0u, r.bytesUsed());
  EXPECT_EQ("", snap(r));
}

// Overflow must evict WHOLE oldest lines — the snapshot must never begin with a
// partial (headless) line, or the downloaded log's first line is corrupt.
TEST(CaptureRing, OverflowEvictsWholeOldestLines) {
  uint8_t buf[16];  // tiny, forces eviction
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "aaaa\n");   // 5
  appendStr(r, "bbbb\n");   // 5  -> used 10
  appendStr(r, "cccc\n");   // 5  -> would be 15, fits (<=16)
  appendStr(r, "dddd\n");   // 5  -> 20 > 16, must evict "aaaa\n"
  std::string s = snap(r);
  // "aaaa\n" evicted; remaining lines intact and whole.
  EXPECT_EQ("bbbb\ncccc\ndddd\n", s);
  EXPECT_LE(r.bytesUsed(), 16u);
}

// A single line larger than the whole ring keeps only the tail that fits,
// never overflows the backing store.
TEST(CaptureRing, OversizedSingleLineKeepsTailWithinCapacity) {
  uint8_t buf[8];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "0123456789\n");  // 11 bytes into an 8-byte ring
  EXPECT_LE(r.bytesUsed(), 8u);
  std::string s = snap(r);
  EXPECT_LE(s.size(), 8u);
  // Keeps the most-recent bytes (the tail of the stream).
  EXPECT_EQ("6789\n", s.substr(s.size() >= 5 ? s.size() - 5 : 0));
}

// When the held bytes contain no newline to evict at, overflow must fall back
// to dropping just enough oldest bytes — never overflow the backing store.
TEST(CaptureRing, OverflowWithoutNewlineDropsOldestBytes) {
  uint8_t buf[8];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "AAAAA");   // 5, no newline
  appendStr(r, "BBBBB");   // 5 -> 10 > 8; no '\n' to cut at, drop 2 oldest bytes
  EXPECT_EQ(8u, r.bytesUsed());
  EXPECT_EQ("AAABBBBB", snap(r));  // last 8 bytes of the stream
}

TEST(CaptureRing, SnapshotRespectsOutputCapacity) {
  uint8_t buf[64];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "abcdefgh\n");  // 9 bytes
  uint8_t out[4];
  size_t n = r.snapshot(out, sizeof(out));
  EXPECT_EQ(4u, n);                       // copies at most out_cap
  EXPECT_EQ(0, memcmp(out, "abcd", 4));   // oldest-first
}

// Offset lets the download path read the buffer in chunks (#395 dump / #396).
TEST(CaptureRing, SnapshotFromOffsetReadsChunk) {
  uint8_t buf[64];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "0123456789\n");     // 11 bytes
  uint8_t out[4];
  size_t n = r.snapshot(out, sizeof(out), 4);  // 4 bytes from offset 4
  EXPECT_EQ(4u, n);
  EXPECT_EQ(0, memcmp(out, "4567", 4));
}

TEST(CaptureRing, SnapshotOffsetAtOrBeyondEndReturnsZero) {
  uint8_t buf[64];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "abc\n");            // 4 bytes
  uint8_t out[8];
  EXPECT_EQ(0u, r.snapshot(out, sizeof(out), 4));   // offset == count
  EXPECT_EQ(0u, r.snapshot(out, sizeof(out), 10));  // offset past end
}

TEST(CaptureRing, SnapshotOffsetClampsToRemaining) {
  uint8_t buf[64];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "hello\n");          // 6 bytes
  uint8_t out[16];
  size_t n = r.snapshot(out, sizeof(out), 3);  // only 3 bytes remain from offset 3
  EXPECT_EQ(3u, n);
  EXPECT_EQ(0, memcmp(out, "lo\n", 3));
}

// #561: consume() — the forwarder drain. Copies oldest whole lines that fit in
// out_cap AND removes them, so a drain loop pulls new lines without tracking
// offsets across eviction.
TEST(CaptureRing, ConsumeReturnsAndRemovesWholeLines) {
  uint8_t buf[64];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "one\ntwo\nthree\n");        // 14 bytes
  uint8_t out[64];
  size_t n = r.consume(out, sizeof(out));
  EXPECT_EQ(14u, n);
  EXPECT_EQ(0, memcmp(out, "one\ntwo\nthree\n", 14));
  EXPECT_EQ(0u, r.bytesUsed());              // consumed content is gone
}

TEST(CaptureRing, ConsumeTakesWholeLinesFittingInCap) {
  uint8_t buf[64];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "aaaa\nbbbb\ncccc\n");        // 15 bytes, three 5-byte lines
  uint8_t out[12];
  size_t n = r.consume(out, sizeof(out));    // cap 12 -> whole lines up to last '\n' <=12
  EXPECT_EQ(10u, n);
  EXPECT_EQ(0, memcmp(out, "aaaa\nbbbb\n", 10));
  EXPECT_EQ(5u, r.bytesUsed());              // "cccc\n" remains
  size_t n2 = r.consume(out, sizeof(out));
  EXPECT_EQ(5u, n2);
  EXPECT_EQ(0, memcmp(out, "cccc\n", 5));
  EXPECT_EQ(0u, r.bytesUsed());
}

TEST(CaptureRing, ConsumeEmptyReturnsZero) {
  uint8_t buf[16];
  CaptureRing r(buf, sizeof(buf));
  uint8_t out[16];
  EXPECT_EQ(0u, r.consume(out, sizeof(out)));
}

TEST(CaptureRing, ConsumeThenAppendContinues) {
  uint8_t buf[64];
  CaptureRing r(buf, sizeof(buf));
  uint8_t out[64];
  appendStr(r, "first\n");
  EXPECT_EQ(6u, r.consume(out, sizeof(out)));
  appendStr(r, "second\n");
  size_t n = r.consume(out, sizeof(out));
  EXPECT_EQ(7u, n);
  EXPECT_EQ(0, memcmp(out, "second\n", 7));
}

// A trailing unterminated line (e.g. a line truncated at MLOG_LINE_MAX) must
// not stall the drain — consume takes it rather than waiting forever.
TEST(CaptureRing, ConsumeNoNewlineForcesProgress) {
  uint8_t buf[16];
  CaptureRing r(buf, sizeof(buf));
  appendStr(r, "abc");                       // no newline
  uint8_t out[16];
  size_t n = r.consume(out, sizeof(out));
  EXPECT_EQ(3u, n);
  EXPECT_EQ(0, memcmp(out, "abc", 3));
  EXPECT_EQ(0u, r.bytesUsed());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
