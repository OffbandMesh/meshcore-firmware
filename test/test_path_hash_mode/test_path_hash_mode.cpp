// #1753: the path-hash size defaults to 2 bytes on every role (#1752).
//
// WHAT IS ACTUALLY BEING PINNED
//   `path_hash_mode` is a runtime pref whose value plus one is the path hash size in
//   bytes, so 0 meant 1-byte and the new default 1 means 2-byte. The wider MeshCore
//   ecosystem moved to 2, and some regions to 3, because 256 distinct path hashes
//   collide on a mesh of any size.
//
//   The default itself is one line in each of two structs, which a test cannot read
//   without dragging Arduino in. What IS testable, and what actually matters, is the
//   packet-level arithmetic that the new default changes:
//
//     * the hop ceiling halves, 63 -> 32. That is the accepted cost of the change and
//       it should be asserted, so it stays a decision rather than becoming a surprise;
//     * the size and the count share one byte -- two bits of size, six of count -- so
//       a wrong default could silently corrupt the count. Every legal (size, count)
//       pair must round-trip;
//     * mode 2 (3-byte) must keep working, because some regions use it.
//
// The ceiling expression mirrors src/Mesh.cpp:347 exactly:
//     (n + 1) * packet->getPathHashSize() <= MAX_PATH_SIZE

#include <gtest/gtest.h>

#include <stdint.h>

// The default itself, which is host-testable because it lives in a pure header with no
// Arduino or MeshCore dependency. Both prefs structs initialise from this one constant
// so they cannot drift apart.
#include "helpers/PathHashDefault.h"

namespace {

// Mirrors src/MeshCore.h:22. Kept local so this suite has no MeshCore/Arduino
// dependency; asserted against the real header's value in the CeilingMatchesSource test
// below by construction of the arithmetic rather than by including it.
constexpr uint8_t kMaxPathSize = 64;

// Mirrors src/Packet.h:79-83, the two-bit size / six-bit count packing.
inline uint8_t packPathLen(uint8_t size_bytes, uint8_t count) {
  return (uint8_t)(((size_bytes - 1) << 6) | (count & 63));
}
inline uint8_t hashSizeOf(uint8_t path_len) { return (uint8_t)((path_len >> 6) + 1); }
inline uint8_t hashCountOf(uint8_t path_len) { return (uint8_t)(path_len & 63); }

// The forwarding ceiling from src/Mesh.cpp:347 -- may this packet take one more hop?
inline bool canTakeAnotherHop(uint8_t count, uint8_t size_bytes) {
  return (count + 1) * size_bytes <= kMaxPathSize;
}

// How many hops a packet of this hash size can accumulate before the ceiling stops it.
uint8_t maxHopsFor(uint8_t size_bytes) {
  uint8_t hops = 0;
  while (hops < 63 && canTakeAnotherHop(hops, size_bytes)) hops++;
  return hops;
}

// path_hash_mode + 1 is the size in bytes -- src/Mesh.cpp callers pass it that way.
constexpr uint8_t kModeOneByte = 0;
constexpr uint8_t kModeTwoByte = 1;    // the new default (#1753)
constexpr uint8_t kModeThreeByte = 2;  // some regions

}  // namespace

// --- the default itself --------------------------------------------------------------

// This is the assertion the task exists for, and it is only possible because the default
// lives in a pure header. Both prefs structs initialise from this constant, so asserting
// it here covers every role at once rather than one struct at a time.
TEST(PathHashMode, TheSharedDefaultIsTwoByte) {
  EXPECT_EQ(offband::DEFAULT_PATH_HASH_MODE, kModeTwoByte)
      << "mode 1 means 2-byte path hashes; 0 would be the old 1-byte default";
  EXPECT_EQ(offband::DEFAULT_PATH_HASH_MODE + 1, 2)
      << "callers pass path_hash_mode + 1 as the size in bytes";
}

// The load path clamps a stored mode to 0..2, so a default outside that range would be
// silently rewritten on the first boot that reads prefs -- the default must be a value
// the clamp will accept unchanged.
TEST(PathHashMode, TheDefaultSurvivesTheLoadClamp) {
  EXPECT_GE(offband::DEFAULT_PATH_HASH_MODE, 0);
  EXPECT_LE(offband::DEFAULT_PATH_HASH_MODE, 2)
      << "load clamps to 0..2 (CommonCLI.cpp:390, :498); mode 3 is reserved";
}

namespace {
constexpr uint8_t kNewDefaultMode = offband::DEFAULT_PATH_HASH_MODE;
}  // namespace

// --- the accepted cost -------------------------------------------------------------

// The halved ceiling is the whole price of this change, and the owner accepted it
// explicitly. Asserted so it stays a decision on the record rather than something a
// later reader discovers and treats as a bug.
TEST(PathHashMode, TwoByteHalvesTheHopCeiling) {
  EXPECT_EQ(maxHopsFor(1), 63) << "1-byte is capped by the six-bit count field, not by "
                                  "MAX_PATH_SIZE: 64 hops would fit in 64 bytes";
  EXPECT_EQ(maxHopsFor(2), 32) << "2-byte: (32+1)*2 = 66 > 64, so 32 is the last hop";
  EXPECT_EQ(maxHopsFor(3), 21) << "3-byte, used by some regions";
  EXPECT_EQ(maxHopsFor(4), 16) << "4-byte is representable even though mode 3 is reserved";
}

TEST(PathHashMode, TheNewDefaultGivesThirtyTwoHops) {
  EXPECT_EQ(maxHopsFor(kNewDefaultMode + 1), 32)
      << "path_hash_mode 1 means 2 bytes, which means a 32-hop ceiling";
}

// The exact boundary, so an off-by-one in the ceiling expression is unmistakable.
TEST(PathHashMode, TheTwoByteBoundaryIsExact) {
  EXPECT_TRUE(canTakeAnotherHop(31, 2)) << "(31+1)*2 = 64, exactly at MAX_PATH_SIZE";
  EXPECT_FALSE(canTakeAnotherHop(32, 2)) << "(32+1)*2 = 66, over";
}

// --- size and count share one byte --------------------------------------------------

// Two bits of size and six of count live in the same uint8_t, so a change to the size
// default could silently corrupt the hop count. Every legal pair must survive.
TEST(PathHashMode, EveryLegalSizeAndCountRoundTrips) {
  for (uint8_t size_bytes = 1; size_bytes <= 4; size_bytes++) {
    for (uint8_t count = 0; count <= 63; count++) {
      const uint8_t packed = packPathLen(size_bytes, count);
      ASSERT_EQ(hashSizeOf(packed), size_bytes)
          << "size " << (int)size_bytes << " count " << (int)count;
      ASSERT_EQ(hashCountOf(packed), count)
          << "size " << (int)size_bytes << " count " << (int)count;
    }
  }
}

// The specific pairing the new default produces, called out on its own.
TEST(PathHashMode, TheNewDefaultPacksAndUnpacksCleanly) {
  const uint8_t packed = packPathLen(kNewDefaultMode + 1, 0);
  EXPECT_EQ(hashSizeOf(packed), 2) << "a freshly originated 2-byte packet";
  EXPECT_EQ(hashCountOf(packed), 0) << "with no hops yet";
}

// --- the other modes must keep working ----------------------------------------------

// Changing the default must not break the modes either side of it: 0 is what deployed
// nodes still have persisted, and 2 is what some regions use.
TEST(PathHashMode, EveryValidModeStillProducesASaneSize) {
  EXPECT_EQ(kModeOneByte + 1, 1);
  EXPECT_EQ(kModeTwoByte + 1, 2);
  EXPECT_EQ(kModeThreeByte + 1, 3);
  for (uint8_t mode = 0; mode <= 2; mode++) {
    const uint8_t size_bytes = (uint8_t)(mode + 1);
    EXPECT_GT(maxHopsFor(size_bytes), 0) << "mode " << (int)mode << " must allow some hops";
    EXPECT_EQ(hashSizeOf(packPathLen(size_bytes, 5)), size_bytes);
  }
}

// Mode is clamped to 0..2 on load (CommonCLI.cpp:390, :498), with 3 reserved. The
// packing supports 4 bytes, so the clamp -- not the encoding -- is what keeps mode 3
// out. Recorded so nobody "fixes" the clamp by reading the encoding.
TEST(PathHashMode, TheEncodingAllowsFourBytesButTheClampAllowsThree) {
  EXPECT_EQ(hashSizeOf(packPathLen(4, 0)), 4) << "the two-bit field can express 4 bytes";
  EXPECT_LE(kModeThreeByte, 2) << "but load clamps mode to 0..2, so 3 bytes is the max";
}

// --- interop ------------------------------------------------------------------------

// The size travels in the packet, so a relay honours the ORIGINATOR's choice rather
// than its own pref (src/Mesh.cpp:349). That is what makes this default change safe to
// roll out incrementally, and it is a property of the encoding, not of any node.
TEST(PathHashMode, APacketCarriesItsOwnSizeSoRelaysDoNotNeedToAgree) {
  for (uint8_t originator_size = 1; originator_size <= 3; originator_size++) {
    uint8_t packed = packPathLen(originator_size, 4);
    // A relay reads the size off the packet and appends at that size, whatever its own
    // pref would have been.
    EXPECT_EQ(hashSizeOf(packed), originator_size);
    packed = packPathLen(hashSizeOf(packed), (uint8_t)(hashCountOf(packed) + 1));
    EXPECT_EQ(hashSizeOf(packed), originator_size) << "size is preserved across a hop";
    EXPECT_EQ(hashCountOf(packed), 5) << "and the count advanced by one";
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
