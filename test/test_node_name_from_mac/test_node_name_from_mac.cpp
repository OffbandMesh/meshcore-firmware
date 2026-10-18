// Native tests for the fresh-flash node name (#1598).
//
// The thing worth testing is not the happy path -- that is an snprintf. It is the two ways
// this can go wrong silently at a 700-badge conference:
//
//   1. Taking the wrong end of the MAC. "QCC" + five hex characters looks correct whichever
//      end they came from, and nobody would notice until a badge's name did not match the
//      phone's pairing list.
//   2. Naming from a MAC that failed to read. An all-zero MAC yields QCC00000 on EVERY
//      badge -- strictly worse than the pubkey default it replaced, because that one is at
//      least unique per device.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include <helpers/NodeNameFromMac.h>

using namespace offband;

namespace {
// A MAC whose every hex character is distinct, so a test failure says WHICH end was taken
// rather than just "not equal".
const uint8_t kMac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};  // "123456789ABC"

std::string name_of(const uint8_t* mac, size_t len, const char* prefix = "QCC",
                    size_t want = 5) {
  char out[32];
  std::memset(out, '?', sizeof(out));
  if (!formatNodeNameFromMac(out, sizeof(out), mac, len, prefix, want)) return "<false>";
  return std::string(out);
}
}  // namespace

// ---- the format the owner specified ----------------------------------------------------

TEST(NodeName, QccPlusTheLastFiveOfTheMac) {
  EXPECT_EQ("QCC89ABC", name_of(kMac, sizeof(kMac)));
}

TEST(NodeName, TheNameIsEightCharactersLikeThePubkeyDefault) {
  // The old default was toHex(pub_key, 4) == 8 hex characters. Anything sized against that
  // width still has to fit, which is the whole reason the format is 3 + 5.
  EXPECT_EQ(8u, name_of(kMac, sizeof(kMac)).size());
}

TEST(NodeName, ItIsTheLastFiveAndNotTheFirstFive) {
  // The failure that would survive a casual read: right shape, wrong end.
  const std::string n = name_of(kMac, sizeof(kMac));
  EXPECT_EQ("QCC89ABC", n);
  EXPECT_NE("QCC12345", n);
}

TEST(NodeName, HexIsUppercaseLikeUtilsToHex) {
  // Utils::toHex uses "0123456789ABCDEF"; the owner's prediction is uppercase too. A
  // lower-case name would be a cosmetic mismatch against every other hex this tree prints.
  const std::string n = name_of(kMac, sizeof(kMac));
  for (char c : n) EXPECT_FALSE(c >= 'a' && c <= 'z') << "lower-case in " << n;
}

TEST(NodeName, TheOwnersPredictionShapeHolds) {
  // The bench pyramid badge was predicted to end 030E. Pin the mechanism that produces
  // that: the last four characters of the name are the last two bytes of the MAC.
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0x03, 0x0E};
  EXPECT_EQ("QCCD030E", name_of(mac, sizeof(mac)));
}

// ---- degradation, which is the part that must never ship wrong -------------------------

TEST(NodeName, AnAllZeroMacIsRefusedRatherThanNamingEveryBadgeAlike) {
  const uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
  EXPECT_FALSE(bleMacIsUsable(zero, sizeof(zero)));
  EXPECT_EQ("<false>", name_of(zero, sizeof(zero)));
}

TEST(NodeName, AnAllOnesMacIsRefusedToo) {
  // The other thing a failed read returns.
  const uint8_t ones[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  EXPECT_FALSE(bleMacIsUsable(ones, sizeof(ones)));
  EXPECT_EQ("<false>", name_of(ones, sizeof(ones)));
}

TEST(NodeName, OneNonZeroByteIsEnoughToBeUsable) {
  const uint8_t almost[6] = {0, 0, 0, 0, 0, 0x01};
  EXPECT_TRUE(bleMacIsUsable(almost, sizeof(almost)));
  EXPECT_EQ("QCC00001", name_of(almost, sizeof(almost)));
}

TEST(NodeName, NullAndWrongLengthMacsAreRefused) {
  EXPECT_FALSE(bleMacIsUsable(nullptr, 6));
  EXPECT_FALSE(bleMacIsUsable(kMac, 4));
  EXPECT_EQ("<false>", name_of(nullptr, 6));
  EXPECT_EQ("<false>", name_of(kMac, 5));
}

// ---- the blank-FICR check, which the 0xC0 forcing would otherwise defeat ---------------

// This is the bug this check exists for, pinned so it cannot come back. On nRF52 the
// written-out address has the top two bits of its MSB forced to 1 for a static random
// address. So a blank, all-zero part does NOT produce an all-zero MAC -- it produces
// {C0,00,00,00,00,00}, which bleMacIsUsable() accepts, and every badge is then named
// QCC00000. The address has to be judged before the forcing, not after.
TEST(NodeName, TheForcedTopByteWouldDefeatAnAfterTheFactZeroCheck) {
  const uint8_t forced_blank[6] = {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00};
  EXPECT_TRUE(bleMacIsUsable(forced_blank, sizeof(forced_blank)))
      << "if this ever becomes false the raw check below is no longer load-bearing";
  EXPECT_EQ("QCC00000", name_of(forced_blank, sizeof(forced_blank)))
      << "which is the same name on every badge -- hence bleRawAddrIsBlank()";

  // Judged on the raw register halves instead, it is correctly rejected.
  EXPECT_TRUE(bleRawAddrIsBlank(0x00000000u, 0x0000));
}

TEST(NodeName, AnUnprogrammedPartIsBlankToo) {
  EXPECT_TRUE(bleRawAddrIsBlank(0xFFFFFFFFu, 0xFFFF));
}

TEST(NodeName, ARealAddressIsNotBlank) {
  EXPECT_FALSE(bleRawAddrIsBlank(0x789ABCDEu, 0x1234));
  // Partially-blank halves are real addresses, not blanks: only a wholly blank pair counts.
  EXPECT_FALSE(bleRawAddrIsBlank(0x00000000u, 0x1234));
  EXPECT_FALSE(bleRawAddrIsBlank(0x789ABCDEu, 0x0000));
  EXPECT_FALSE(bleRawAddrIsBlank(0xFFFFFFFFu, 0x0000));
  EXPECT_FALSE(bleRawAddrIsBlank(0x00000001u, 0x0000));
}

// ---- the buffer contract ----------------------------------------------------------------

TEST(NodeName, ATooSmallBufferRefusesInsteadOfTruncating) {
  // Half a name is worse than falling back to the unique pubkey default, so this returns
  // false rather than writing what fits.
  char out[8];  // needs 9 for "QCC89ABC" + NUL
  EXPECT_FALSE(formatNodeNameFromMac(out, sizeof(out), kMac, sizeof(kMac), "QCC", 5));
  char exact[9];
  EXPECT_TRUE(formatNodeNameFromMac(exact, sizeof(exact), kMac, sizeof(kMac), "QCC", 5));
  EXPECT_STREQ("QCC89ABC", exact);
}

TEST(NodeName, ARefusedCallLeavesTheBufferAlone) {
  // The caller falls through to the pubkey default on false, so a partial write here would
  // be silently overwritten -- but a caller that checked the buffer instead of the return
  // would see garbage. Nothing is written at all.
  char out[4];
  std::memcpy(out, "ZZZ", 4);
  EXPECT_FALSE(formatNodeNameFromMac(out, sizeof(out), kMac, sizeof(kMac), "QCC", 5));
  EXPECT_STREQ("ZZZ", out);
}

TEST(NodeName, DegenerateArgumentsAreRefused) {
  char out[32];
  EXPECT_FALSE(formatNodeNameFromMac(nullptr, 32, kMac, 6, "QCC", 5));
  EXPECT_FALSE(formatNodeNameFromMac(out, 0, kMac, 6, "QCC", 5));
  EXPECT_FALSE(formatNodeNameFromMac(out, sizeof(out), kMac, 6, nullptr, 5));
  EXPECT_FALSE(formatNodeNameFromMac(out, sizeof(out), kMac, 6, "QCC", 0));
  EXPECT_FALSE(formatNodeNameFromMac(out, sizeof(out), kMac, 6, "QCC", 13));  // > 12 chars
}

TEST(NodeName, TheWholeMacIsAddressable) {
  EXPECT_EQ("123456789ABC", name_of(kMac, sizeof(kMac), "", 12));
}

TEST(NodeName, AnEmptyPrefixIsAllowed) {
  EXPECT_EQ("89ABC", name_of(kMac, sizeof(kMac), "", 5));
}

// ---- the badge's own wrapper -------------------------------------------------------------

TEST(NodeName, TheQccWrapperAppliesBothConstants) {
  // So the two numbers live in one place rather than at each call site.
  char out[32];
  ASSERT_TRUE(formatQccNodeName(out, sizeof(out), kMac, sizeof(kMac)));
  EXPECT_STREQ("QCC89ABC", out);
  EXPECT_STREQ("QCC", kQccNamePrefix);
  EXPECT_EQ(5u, kQccNameMacChars);
}

TEST(NodeName, ItFitsThePrefsField) {
  // _prefs.node_name is char[32]; the name must fit with room to spare.
  EXPECT_LT(std::strlen(kQccNamePrefix) + kQccNameMacChars + 1, 32u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
