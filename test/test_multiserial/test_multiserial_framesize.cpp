// #711: MultiSerialInterface must delegate maxFrameSize() to its interfaces.
//
// THE DEFECT THIS PINS
//   MultiSerialInterface overrides nine BaseSerialInterface methods -- enable,
//   disable, isEnabled, isConnected, loop, isWriteBusy, writeFrame,
//   checkRecvFrame -- and NOT maxFrameSize(). So every caller asking the wrapper
//   "how big a frame may I build?" got BaseSerialInterface's default,
//   MAX_FRAME_SIZE, no matter how constrained the underlying transport was.
//
//   companion_radio holds a MultiSerialInterface as its `_serial` (main.cpp:49,
//   `interface_manager`), so caplogDrain()'s maxFramePayload(2) returned 174 on
//   every board and every link. The BLE interfaces' own MTU-aware maxFrameSize()
//   was never consulted -- which made #453, #454 and both #711 attempts dead code
//   on this path. Introduced by the 1.17.0 MultiSerialInterface port (#668).
//
//   Field cost: three tester reports of identical shape, all ESP32, where the
//   link negotiates MTU 176 (we pin setMTU(MAX_FRAME_SIZE)) and therefore
//   delivers 173 -- so a 176-byte frame lost exactly 3 bytes, every full frame.
//     madmax_2069  8461 of  8608 in 50 chunks  (147 = 3 x 49)
//     schill      12751 of 12973 in 75 chunks  (222 = 3 x 74)
//     hv4-bench-1 14246 of 14495 in 84 chunks  (249 = 3 x 83)
//
//   It was HARMLESS on links delivering >= MAX_FRAME_SIZE (an nRF52 at MTU 247
//   delivers 244, and a 176-byte frame fits), which is why no nRF52 report exists
//   and why the defect stayed invisible for months.

#include <gtest/gtest.h>

#include "helpers/MultiSerialInterface.h"

namespace {

/// A transport that reports a fixed deliverable frame size.
class FakeInterface : public BaseSerialInterface {
public:
  explicit FakeInterface(size_t max_frame, bool enabled = true)
      : _max(max_frame), _enabled(enabled) {}

  void enable() override { _enabled = true; }
  void disable() override { _enabled = false; }
  bool isEnabled() const override { return _enabled; }
  bool isConnected() const override { return _enabled; }
  bool isWriteBusy() const override { return false; }
  size_t writeFrame(const uint8_t*, size_t len) override { return len; }
  size_t checkRecvFrame(uint8_t*) override { return 0; }
  size_t maxFrameSize() const override { return _max; }

private:
  size_t _max;
  bool _enabled;
};

constexpr size_t kMaxFrame = 176;

}  // namespace

// THE REGRESSION. A BLE interface on an MTU-176 link delivers 173. The wrapper
// must report 173, not MAX_FRAME_SIZE.
TEST(MultiSerialFrameSize, RegressionSevenEleven_WrapperMustNotReportTheDefault) {
  FakeInterface ble(173);
  MultiSerialInterface multi;
  multi.addInterface(InterfaceType::Bluetooth, &ble);
  multi.enable();

  EXPECT_EQ(multi.maxFrameSize(), 173u)
      << "wrapper returned its inherited default instead of delegating; a chunk "
         "builder would emit a 176-byte frame onto a 173-byte pipe (#711)";
  EXPECT_EQ(multi.maxFramePayload(2), 171u)
      << "caplog CHUNK payload must be 171 on an MTU-176 link, not the 174 that "
         "produced the madmax / schill / bench truncations";
}

// writeFrame() fans a frame out to EVERY enabled interface, so the frame must fit
// the most constrained one. The minimum is the only safe answer.
TEST(MultiSerialFrameSize, ReportsTheMinimumAcrossEnabledInterfaces) {
  FakeInterface ble(173), usb(kMaxFrame), wifi(120);
  MultiSerialInterface multi;
  multi.addInterface(InterfaceType::Bluetooth, &ble);
  multi.addInterface(InterfaceType::USB, &usb);
  multi.addInterface(InterfaceType::WiFi, &wifi);
  multi.enable();

  EXPECT_EQ(multi.maxFrameSize(), 120u) << "must be the smallest, not the first or the largest";
}

// A disabled interface receives nothing from writeFrame(), so it must not drag
// the reported size down. The predicate has to match writeFrame() exactly.
TEST(MultiSerialFrameSize, DisabledInterfacesAreIgnored) {
  // NOTE: MultiSerialInterface::enable() enables EVERY registered interface, so a
  // fake constructed disabled comes back up. The realistic state this models is an
  // interface disabled at runtime after the wrapper started (e.g. BLE turned off
  // while USB stays up), so disable it after enable(). Caught by this test failing
  // for the wrong reason -- the premise was wrong, not the implementation.
  FakeInterface ble(173), wifi(64);
  MultiSerialInterface multi;
  multi.addInterface(InterfaceType::Bluetooth, &ble);
  multi.addInterface(InterfaceType::WiFi, &wifi);
  multi.enable();
  wifi.disable();

  EXPECT_EQ(multi.maxFrameSize(), 173u)
      << "a disabled interface gets no frames and must not constrain the size";
}

// Nothing registered / nothing enabled: writeFrame sends nothing, so the buffer
// bound is the honest answer and callers never see a surprising 0.
TEST(MultiSerialFrameSize, NoEnabledInterfacesFallsBackToTheBufferBound) {
  MultiSerialInterface multi;
  multi.enable();
  EXPECT_EQ(multi.maxFrameSize(), kMaxFrame);
}

// An interface reporting more than the frame buffer cannot raise the ceiling --
// the buffer is uint8_t buf[MAX_FRAME_SIZE] and caps every frame regardless of
// how generous the link is. This is why an nRF52 at MTU 247 gets 176, not 244.
TEST(MultiSerialFrameSize, GenerousInterfaceCannotExceedTheFrameBuffer) {
  FakeInterface big(4096);
  MultiSerialInterface multi;
  multi.addInterface(InterfaceType::Bluetooth, &big);
  multi.enable();

  EXPECT_EQ(multi.maxFrameSize(), kMaxFrame);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
