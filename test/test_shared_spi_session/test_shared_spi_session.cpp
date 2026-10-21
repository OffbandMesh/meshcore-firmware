// Epic J (#1434), J0 (#1723) and J2 (#1725): the bus lock around the radio's
// SPI on the T-Deck's shared bus. A display flush must never land between two
// transfers of one radio command, and the lock must always be given back.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "helpers/radiolib/SharedSpiSession.h"

using offband::SharedSpiSession;
using offband::SpiBusOps;

namespace {

// Records every call in order: "A" acquire, "R" release, "T<n>" a transfer.
class FakeBus : public SpiBusOps {
public:
  std::string log;
  bool held = false;
  bool acquire_ok = true;
  bool transfer_ok = true;
  int transfers_unlocked = 0;
  bool acquire() override {
    log += "A";
    held = acquire_ok;
    return acquire_ok;
  }
  void release() override {
    log += "R";
    held = false;
  }
  bool transfer(const uint8_t*, uint8_t*, size_t len) override {
    if (!held) transfers_unlocked++;
    log += "T" + std::to_string(len);
    return transfer_ok;
  }
};

const uint8_t kCmd[4] = {0x1D, 0x08, 0x40, 0x00};

}  // namespace

TEST(SharedSpiSession, ACommandHoldsTheBusFromBeginToEnd) {
  FakeBus bus;
  SharedSpiSession s(bus);
  uint8_t in[4];
  s.begin();
  EXPECT_TRUE(s.transfer(kCmd, in, 2));
  EXPECT_TRUE(s.transfer(kCmd + 2, in, 2));
  s.end();
  EXPECT_EQ("AT2T2R", bus.log) << "two transfers of one command, one acquisition around both";
  EXPECT_EQ(0, bus.transfers_unlocked);
  EXPECT_FALSE(bus.held);
}

TEST(SharedSpiSession, NestedBeginsShareOneAcquisition) {
  FakeBus bus;
  SharedSpiSession s(bus);
  s.begin();
  s.begin();
  s.transfer(kCmd, nullptr, 4);
  s.end();
  EXPECT_TRUE(bus.held) << "the inner end must not give the bus back";
  s.end();
  EXPECT_EQ("AT4R", bus.log);
}

TEST(SharedSpiSession, AStrayTransferLocksForItself) {
  FakeBus bus;
  SharedSpiSession s(bus);
  EXPECT_TRUE(s.transfer(kCmd, nullptr, 4));
  EXPECT_EQ("AT4R", bus.log);
  EXPECT_EQ(0, bus.transfers_unlocked);
}

TEST(SharedSpiSession, AnUnmatchedEndChangesNothing) {
  FakeBus bus;
  SharedSpiSession s(bus);
  s.end();
  EXPECT_EQ("", bus.log);
  EXPECT_EQ(0, s.depth());
  s.begin();
  s.end();
  EXPECT_EQ("AR", bus.log);
}

TEST(SharedSpiSession, AFailedAcquireNeverReachesTheWire) {
  FakeBus bus;
  bus.acquire_ok = false;
  SharedSpiSession s(bus);
  s.begin();
  EXPECT_FALSE(s.transfer(kCmd, nullptr, 4));
  s.end();
  EXPECT_EQ("A", bus.log) << "no transfer and no release of a bus it never got";
}

TEST(SharedSpiSession, AFailedTransferStillReleases) {
  FakeBus bus;
  bus.transfer_ok = false;
  SharedSpiSession s(bus);
  s.begin();
  EXPECT_FALSE(s.transfer(kCmd, nullptr, 4));
  s.end();
  EXPECT_EQ("AT4R", bus.log);
  EXPECT_FALSE(bus.held);
}

TEST(SharedSpiSession, AnEmptyTransferTouchesNothing) {
  FakeBus bus;
  SharedSpiSession s(bus);
  EXPECT_TRUE(s.transfer(kCmd, nullptr, 0));
  EXPECT_EQ("", bus.log);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
