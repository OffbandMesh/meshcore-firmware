// #1671 (Epic M): the rules EnvironmentSensorManager::begin() starts sensors
// by, run against a fake bus. Each test seats chips at addresses; a driver's
// init succeeds only on its own chip, and every init call is logged so a test
// can prove a driver never touched a chip it doesn't own.

#include <gtest/gtest.h>

#include <string.h>

#include <vector>

#include "helpers/sensors/SensorProbe.h"

using namespace offband::sensors;

namespace {

enum Driver : uintptr_t { BME680 = 1, BME280, BMP280, SHT4X, INA3221, BMP085 };

// The real probe's shape (EnvironmentSensorManager.cpp): the table (0x76
// Bosch entries, the others, BMP085 at 0x77), then the Bosch 0x77 fallbacks.
const ProbeEntry kTable[] = {
    {0x76, BME680, false}, {0x76, BME280, false}, {0x76, BMP280, false}, {0x44, SHT4X, false},
    {0x40, INA3221, false}, {0x77, BMP085, false}, {0x77, BME680, true},  {0x77, BME280, true},
    {0x77, BMP280, true},
};
constexpr size_t kN = sizeof kTable / sizeof kTable[0];

struct Bus {
  const ProbeEntry* table = kTable;
  size_t n = kN;
  Driver chip[128] = {};      // which part sits at each address (0: none)
  bool detected[128] = {};
  bool broken[128] = {};      // answers the scan, but its start fails
  std::vector<size_t> calls;  // every init, by table index

  void seat(uint8_t addr, Driver d) {
    chip[addr] = d;
    detected[addr] = true;
  }
  size_t run(ProbeHit* out, size_t max) {
    return runProbe(table, n, detected,
                    [this](size_t i) -> uint8_t {
                      calls.push_back(i);
                      const ProbeEntry& e = table[i];
                      if (broken[e.address] || chip[e.address] != Driver(e.driver)) return 0;
                      return e.driver == INA3221 ? 3 : 1;
                    },
                    out, max);
  }
  bool touched(uint8_t addr, Driver d) const {
    for (size_t i : calls) {
      if (table[i].address == addr && table[i].driver == d) return true;
    }
    return false;
  }
};

}  // namespace

TEST(Probe, Bme280At0x77IsFound) {
  Bus b;
  b.seat(0x77, BME280);
  ProbeHit h[16];
  ASSERT_EQ(b.run(h, 16), 1u);
  EXPECT_EQ(kTable[h[0].entry].address, 0x77);
  EXPECT_EQ(kTable[h[0].entry].driver, uintptr_t(BME280));
}

TEST(Probe, ExistingOrderAndChannelsDontMove) {
  // A node with a BME280 at 0x76 and an SHT4x keeps them in that order, as
  // it reported them before the 0x77 entries existed.
  Bus b;
  b.seat(0x76, BME280);
  b.seat(0x44, SHT4X);
  ProbeHit h[16];
  ASSERT_EQ(b.run(h, 16), 2u);
  EXPECT_EQ(kTable[h[0].entry].driver, uintptr_t(BME280));
  EXPECT_EQ(kTable[h[1].entry].driver, uintptr_t(SHT4X));
}

TEST(Probe, Bmp085At0x77IsNeverProbedByABoschDriver) {
  Bus b;
  b.seat(0x77, BMP085);
  ProbeHit h[16];
  ASSERT_EQ(b.run(h, 16), 1u);
  EXPECT_EQ(kTable[h[0].entry].driver, uintptr_t(BMP085));
  EXPECT_FALSE(b.touched(0x77, BME680));   // its begin() would reset the BMP085
  EXPECT_FALSE(b.touched(0x77, BME280));
  EXPECT_FALSE(b.touched(0x77, BMP280));
}

TEST(Probe, FallbacksWaitWhereverTheyAreListed) {
  // The safety must not hang on table order: with the Bosch 0x77 fallbacks
  // listed first, the BMP085 is still started by its own driver and never
  // probed by theirs.
  const ProbeEntry first[] = {
      {0x77, BME680, true}, {0x77, BME280, true}, {0x77, BMP280, true},
      {0x76, BME280, false}, {0x77, BMP085, false},
  };
  Bus b;
  b.table = first;
  b.n = sizeof first / sizeof first[0];
  b.seat(0x77, BMP085);
  ProbeHit h[16];
  ASSERT_EQ(b.run(h, 16), 1u);
  EXPECT_EQ(first[h[0].entry].driver, uintptr_t(BMP085));
  EXPECT_FALSE(b.touched(0x77, BME680));
  EXPECT_FALSE(b.touched(0x77, BME280));
  EXPECT_FALSE(b.touched(0x77, BMP280));
}

TEST(Probe, OneSensorPerDriver) {
  // Two BME280s: the driver has one instance, so the second is left alone.
  Bus b;
  b.seat(0x76, BME280);
  b.seat(0x77, BME280);
  ProbeHit h[16];
  ASSERT_EQ(b.run(h, 16), 1u);
  EXPECT_EQ(kTable[h[0].entry].address, 0x76);
  EXPECT_FALSE(b.touched(0x77, BME280));   // never re-begun on the other chip
}

TEST(Probe, AMultiChannelPartGetsOneHitPerChannel) {
  Bus b;
  b.seat(0x40, INA3221);
  ProbeHit h[16];
  ASSERT_EQ(b.run(h, 16), 3u);
  for (uint8_t s = 0; s < 3; s++) {
    EXPECT_EQ(kTable[h[s].entry].driver, uintptr_t(INA3221));
    EXPECT_EQ(h[s].sub, s);
  }
}

TEST(Probe, NothingAnsweredNothingIsTouched) {
  Bus b;
  ProbeHit h[16];
  EXPECT_EQ(b.run(h, 16), 0u);
  EXPECT_TRUE(b.calls.empty());   // no driver ran against an empty bus
}

TEST(Probe, AFailedStartLeavesTheAddressForTheNextDriver) {
  // At 0x76 the BME680 and BME280 entries come first; a BMP280 there is found
  // by its own entry after the other two decline it.
  Bus b;
  b.seat(0x76, BMP280);
  ProbeHit h[16];
  ASSERT_EQ(b.run(h, 16), 1u);
  EXPECT_EQ(kTable[h[0].entry].driver, uintptr_t(BMP280));
  // A part that answers the scan but won't start is never counted.
  Bus c;
  c.seat(0x44, SHT4X);
  c.broken[0x44] = true;
  EXPECT_EQ(c.run(h, 16), 0u);
}

TEST(Probe, NeverMoreHitsThanRoom) {
  Bus b;
  b.seat(0x40, INA3221);
  b.seat(0x44, SHT4X);
  ProbeHit h[2];
  ASSERT_EQ(b.run(h, 2), 2u);
  // In table order: the SHT4x, then the INA3221's first channel; its other
  // two channels don't fit and are dropped, not written past the end.
  EXPECT_EQ(kTable[h[0].entry].driver, uintptr_t(SHT4X));
  EXPECT_EQ(h[0].sub, 0);
  EXPECT_EQ(kTable[h[1].entry].driver, uintptr_t(INA3221));
  EXPECT_EQ(h[1].sub, 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
