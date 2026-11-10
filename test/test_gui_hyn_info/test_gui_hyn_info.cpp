// #2108: which Hynitron touch chip answered, from its info block. The T-Deck
// Pro V1.1 has a CST328 (cst3xx family) or a CST3530 (cst66xx family), both at
// 0x1A; a block from the wrong family, or a bus that returns junk, must not
// read as a chip.

#include <gtest/gtest.h>
#include <string.h>

#include "helpers/gui/HynInfo.h"

using namespace offband::gui;

static void put16(uint8_t* b, uint16_t v) {
  b[0] = uint8_t(v);
  b[1] = uint8_t(v >> 8);
}
static void put32(uint8_t* b, uint32_t v) {
  for (int i = 0; i < 4; i++) b[i] = uint8_t(v >> (8 * i));
}

// A cst66xx block as the chip lays it out: type 0xCACA3530, 240x320.
static void make66xx(uint8_t* b) {
  memset(b, 0, kCst66xxInfoLen);
  put32(b + 0, 0xCACA3530);
  put16(b + 28, 240);
  put16(b + 30, 320);
  put32(b + 32, 0x01020304);
  put32(b + 36, 0x0000ABCD);
}

// A cst3xx block: type 0x0328, 240x320.
static void make3xx(uint8_t* b) {
  memset(b, 0, kCst3xxInfoLen);
  put16(b + 4, 240);
  put16(b + 6, 320);
  put16(b + 16, 0x1234);
  put16(b + 18, 0x0328);
  put32(b + 20, 0x00050607);
}

TEST(HynInfo, Cst66xxBlockDecodes) {
  uint8_t b[kCst66xxInfoLen];
  make66xx(b);
  HynInfo i;
  ASSERT_TRUE(parseCst66xxInfo(b, sizeof(b), &i));
  EXPECT_EQ(i.family, HynFamily::Cst66xx);
  EXPECT_EQ(i.chip_type, 0xCACA3530u);
  EXPECT_EQ(i.res_x, 240);
  EXPECT_EQ(i.res_y, 320);
  EXPECT_EQ(i.fw_ver, 0x01020304u);
  EXPECT_EQ(i.project_id, 0x0000ABCDu);
}

TEST(HynInfo, Cst66xxNeedsItsMarker) {
  uint8_t b[kCst66xxInfoLen];
  make66xx(b);
  b[2] = 0x35;   // a block without 0xCA 0xCA is not the chip's
  EXPECT_FALSE(parseCst66xxInfo(b, sizeof(b), nullptr));
  make66xx(b);
  b[3] = 0x00;
  EXPECT_FALSE(parseCst66xxInfo(b, sizeof(b), nullptr));
}

TEST(HynInfo, Cst66xxRejectsShortOrMissing) {
  uint8_t b[kCst66xxInfoLen];
  make66xx(b);
  EXPECT_FALSE(parseCst66xxInfo(b, sizeof(b) - 1, nullptr));
  EXPECT_FALSE(parseCst66xxInfo(nullptr, sizeof(b), nullptr));
}

TEST(HynInfo, Cst3xxBlockDecodes) {
  uint8_t b[kCst3xxInfoLen];
  make3xx(b);
  HynInfo i;
  ASSERT_TRUE(parseCst3xxInfo(b, sizeof(b), &i));
  EXPECT_EQ(i.family, HynFamily::Cst3xx);
  EXPECT_EQ(i.chip_type, 0x0328u);
  EXPECT_EQ(i.res_x, 240);
  EXPECT_EQ(i.res_y, 320);
  EXPECT_EQ(i.project_id, 0x1234u);
  EXPECT_EQ(i.fw_ver, 0x00050607u);
}

TEST(HynInfo, Cst3xxRejectsBusJunk) {
  uint8_t b[kCst3xxInfoLen];
  memset(b, 0xFF, sizeof(b));   // nothing driving the bus
  EXPECT_FALSE(parseCst3xxInfo(b, sizeof(b), nullptr));
  memset(b, 0x00, sizeof(b));
  EXPECT_FALSE(parseCst3xxInfo(b, sizeof(b), nullptr));
}

TEST(HynInfo, Cst3xxRejectsImplausibleResolution) {
  uint8_t b[kCst3xxInfoLen];
  make3xx(b);
  put16(b + 4, 0);
  EXPECT_FALSE(parseCst3xxInfo(b, sizeof(b), nullptr));
  make3xx(b);
  put16(b + 6, 5000);
  EXPECT_FALSE(parseCst3xxInfo(b, sizeof(b), nullptr));
}

TEST(HynInfo, Cst3xxRejectsShortOrMissing) {
  uint8_t b[kCst3xxInfoLen];
  make3xx(b);
  EXPECT_FALSE(parseCst3xxInfo(b, sizeof(b) - 1, nullptr));
  EXPECT_FALSE(parseCst3xxInfo(nullptr, sizeof(b), nullptr));
}

TEST(HynInfo, Cst3xxRejectsTinyResolution) {
  uint8_t b[kCst3xxInfoLen];
  make3xx(b);
  put16(b + 4, 1);   // a 1x1 "panel" is bus noise
  put16(b + 6, 1);
  EXPECT_FALSE(parseCst3xxInfo(b, sizeof(b), nullptr));
}

TEST(HynInfo, EachParserRejectsTheOtherFamilysBlock) {
  // The probe reads both families on one chip; the wrong one must not pass.
  uint8_t b66[kCst66xxInfoLen];
  make66xx(b66);
  EXPECT_FALSE(parseCst3xxInfo(b66, kCst3xxInfoLen, nullptr));
  uint8_t b3[kCst66xxInfoLen] = {};
  make3xx(b3);
  EXPECT_FALSE(parseCst66xxInfo(b3, sizeof(b3), nullptr));
}

TEST(HynInfo, FailedParseLeavesOutputAlone) {
  uint8_t b[kCst3xxInfoLen];
  memset(b, 0xFF, sizeof(b));
  HynInfo i;
  i.chip_type = 0x77;
  EXPECT_FALSE(parseCst3xxInfo(b, sizeof(b), &i));
  EXPECT_EQ(i.chip_type, 0x77u);
  EXPECT_EQ(i.family, HynFamily::None);
}

TEST(HynInfo, FamilyNames) {
  EXPECT_STREQ(hynFamilyName(HynFamily::Cst66xx), "cst66xx");
  EXPECT_STREQ(hynFamilyName(HynFamily::Cst3xx), "cst3xx");
  EXPECT_STREQ(hynFamilyName(HynFamily::None), "none");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
