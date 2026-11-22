// #2222: the PIN Bluetooth uses once a new one is set (helpers/BlePin.h).
// MyMesh::setBlePin() applies it at once, so the next pairing asks for it
// with no reboot; these pin down which PIN that is.

#include <gtest/gtest.h>

#include <helpers/BlePin.h>

using namespace offband;

namespace {
constexpr uint32_t kRandomThisBoot = 731554;   // what a display board drew at boot
constexpr uint32_t kFreshRandom = 260418;      // what it would draw now
}  // namespace

TEST(BlePinValid, ZeroAndSixDigitsAreAccepted) {
  EXPECT_TRUE(blePinValid(0));
  EXPECT_TRUE(blePinValid(100000));
  EXPECT_TRUE(blePinValid(482913));
  EXPECT_TRUE(blePinValid(999999));
}

// Negative: anything else is refused, and the caller changes nothing.
TEST(BlePinValid, FiveOrSevenDigitsAreRefused) {
  EXPECT_FALSE(blePinValid(99999));
  EXPECT_FALSE(blePinValid(1000000));
  EXPECT_FALSE(blePinValid(1));
  EXPECT_FALSE(blePinValid(0xFFFFFFFFu));
}

// The owner's bench case: a PIN typed into Settings is the one the next
// pairing asks for, not the random one shown at boot.
TEST(BlePinAfterSet, ASetPinIsUsedAtOnce) {
  EXPECT_EQ(blePinAfterSet(0, kRandomThisBoot, 482913, kFreshRandom), 482913u);
}

TEST(BlePinAfterSet, ChangingASetPinUsesTheNewOne) {
  EXPECT_EQ(blePinAfterSet(482913, 482913, 615270, kFreshRandom), 615270u);
}

// Clearing a set PIN ("new each boot") stops using it at once: the PIN a
// phone has to type is the board's unset one.
TEST(BlePinAfterSet, ClearingASetPinStopsUsingIt) {
  const uint32_t got = blePinAfterSet(482913, 482913, 0, kFreshRandom);
  EXPECT_NE(got, 482913u) << "a cleared PIN kept pairing";
  EXPECT_EQ(got, kFreshRandom);
}

// Clearing a PIN that was already clear keeps the one shown, so the number on
// the screen doesn't change under a phone about to pair.
TEST(BlePinAfterSet, ClearingWhenAlreadyClearKeepsThePinInUse) {
  EXPECT_EQ(blePinAfterSet(0, kRandomThisBoot, 0, kFreshRandom), kRandomThisBoot);
}

// A build with no PIN in use yet takes the unset one rather than 0, which
// would leave pairing with no passkey.
TEST(BlePinAfterSet, NothingInUseTakesTheUnsetPin) {
  EXPECT_EQ(blePinAfterSet(0, 0, 0, kFreshRandom), kFreshRandom);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
