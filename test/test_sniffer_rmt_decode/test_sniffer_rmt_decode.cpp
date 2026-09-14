#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "../../tools/diag/sniffer/rc_uart_sniffer_v3_wroom/uart_rmt_decode.h"

// rmtuart::Run is written out in full everywhere: inside a TEST body, plain `Run`
// is gtest's Test::Run().

namespace {

const uint32_t kTickHz = 10000000;   // the sniffer's RMT clock: 100 ns ticks
const uint32_t kBitX100 = rmtuart::bitX100(kTickHz, 115200);

// 8N1 bits, least significant first, with `gap` idle bits after each byte.
std::vector<uint8_t> bitsOf(const std::vector<uint8_t>& bytes, int gap = 1) {
  std::vector<uint8_t> bits;
  for (uint8_t b : bytes) {
    bits.push_back(0);
    for (int i = 0; i < 8; i++) bits.push_back((b >> i) & 1);
    bits.push_back(1);
    for (int i = 0; i < gap; i++) bits.push_back(1);
  }
  return bits;
}

// What the RMT reports: runs of equal level starting at the first falling edge,
// timed at `bit_ticks` per bit, closed by a zero-length end marker on idle.
std::vector<rmtuart::Run> runsOf(const std::vector<uint8_t>& bits, double bit_ticks) {
  std::vector<rmtuart::Run> runs;
  size_t i = 0;
  while (i < bits.size() && bits[i] == 1) i++;   // capture starts at the first edge
  while (i < bits.size()) {
    size_t j = i;
    while (j < bits.size() && bits[j] == bits[i]) j++;
    runs.push_back({bits[i], (uint32_t)((j - i) * bit_ticks + 0.5)});
    i = j;
  }
  if (!runs.empty() && runs.back().level == 1) runs.back().ticks = 0;   // idle closes the frame
  return runs;
}

std::vector<uint8_t> bytesOf(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

std::string decode(const std::vector<rmtuart::Run>& runs, uint32_t* bad, size_t cap = 256) {
  std::vector<uint8_t> out(cap);
  const size_t n = rmtuart::decodeFrame(runs.data(), runs.size(), kBitX100, out.data(), cap, bad);
  return std::string(out.begin(), out.begin() + n);
}

}  // namespace

TEST(SnifferRmtDecode, ReadsABeaconLineFromASlightlySlowSender) {
  // 0.1% slow, a little worse than any real 115200 transmitter on the bench.
  const std::string line = "PAD GPIO34 P1.02 #17\r\n";
  uint32_t bad = 0;
  EXPECT_EQ(line, decode(runsOf(bitsOf(bytesOf(line)), kTickHz / 115085.0), &bad));
  EXPECT_EQ(0u, bad);
}

TEST(SnifferRmtDecode, ToleratesPerRunTimingJitter) {
  // Stretch and shrink alternate runs by a fifth of a bit: edge jitter on a wire.
  const std::string line = "PAD GPIO39 P1.07 #4294967295\r\n";
  std::vector<rmtuart::Run> runs = runsOf(bitsOf(bytesOf(line)), kTickHz / 115200.0);
  const uint32_t fifth = (uint32_t)(kTickHz / 115200.0 / 5);
  for (size_t i = 0; i + 1 < runs.size(); i++) runs[i].ticks += (i % 2) ? fifth : -fifth;
  uint32_t bad = 0;
  EXPECT_EQ(line, decode(runs, &bad));
  EXPECT_EQ(0u, bad);
}

TEST(SnifferRmtDecode, ALowStopBitIsCountedAndTheNextByteStillReads) {
  std::vector<uint8_t> bits = bitsOf({'A'}, 0);
  bits.back() = 0;                                   // break A's stop bit
  const std::vector<uint8_t> tail = bitsOf({'B'}, 2);
  bits.insert(bits.end(), {1, 1});                   // the line recovers, then B
  bits.insert(bits.end(), tail.begin(), tail.end());
  uint32_t bad = 0;
  EXPECT_EQ("B", decode(runsOf(bits, kTickHz / 115200.0), &bad));
  EXPECT_EQ(1u, bad);
}

TEST(SnifferRmtDecode, AByteCutOffMidWayIsCountedNotGuessed) {
  std::vector<uint8_t> bits = bitsOf(bytesOf("OK"));
  bits.resize(bits.size() - 6);                      // the frame ends inside the K
  uint32_t bad = 0;
  const std::vector<rmtuart::Run> runs = runsOf(bits, kTickHz / 115200.0);
  EXPECT_EQ("O", decode(runs, &bad));
  EXPECT_EQ(1u, bad);
}

TEST(SnifferRmtDecode, AsciiSurvivesTheEndMarkerHidingTheLastStretch) {
  // Bit 7 of every ASCII byte is low, so the final stop bit always starts the last
  // high stretch, and the end marker hides only idle.
  const std::string line = "PAD GPIO33 P1.01 #1\r\n";
  const std::vector<rmtuart::Run> runs = runsOf(bitsOf(bytesOf(line)), kTickHz / 115200.0);
  ASSERT_EQ(0u, runs.back().ticks);
  uint32_t bad = 0;
  EXPECT_EQ(line, decode(runs, &bad));
  EXPECT_EQ(0u, bad);
}

TEST(SnifferRmtDecode, AHighTopBitBehindTheEndMarkerIsCountedNotGuessed) {
  // 0xC0 ends in two high data bits, which merge into the final stretch the end
  // marker hides. The earlier bytes decode; the last is counted, never invented.
  uint32_t bad = 0;
  EXPECT_EQ("hi", decode(runsOf(bitsOf({'h', 'i', 0xC0}), kTickHz / 115200.0), &bad));
  EXPECT_EQ(1u, bad);
}

TEST(SnifferRmtDecode, OutputStopsAtItsCapacity) {
  uint32_t bad = 0;
  EXPECT_EQ("HEL", decode(runsOf(bitsOf(bytesOf("HELLO")), kTickHz / 115200.0), &bad, 3));
  EXPECT_EQ(0u, bad);
}

TEST(SnifferRmtDecode, TheBitLengthMatchesTheSniffersClock) {
  EXPECT_EQ(8681u, kBitX100);   // 10 MHz / 115200 = 86.81 ticks
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
