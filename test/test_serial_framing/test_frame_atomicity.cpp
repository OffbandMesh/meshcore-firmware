// #718: the framed companion protocol must never emit a PARTIAL frame.
//
// THE DEFECT THIS PINS
//   ArduinoSerialInterface::writeFrame() writes a 3-byte header declaring `len`,
//   then writes the payload. On a companion USB build the underlying Stream is
//   deliberately NON-BLOCKING (examples/companion_radio/main.cpp:171 sets
//   Serial.setTxTimeoutMs(0) per #149, so a stalled host cannot starve BLE
//   servicing). A non-blocking write returns SHORT when the TX FIFO is full.
//
//   The header is already on the wire at that point, so the receiver waits for a
//   payload that never fully arrives and consumes the NEXT frame's header as
//   filler. Observed on rc32-bench-1 over USB serial: a caplog download announced
//   1067 bytes, delivered 545, and the delivered bytes contained a spliced
//   '>' 0xB0 0x00 0xC4 0x02 -- a complete frame header sitting inside a payload.
//
//   Dropping bytes is CORRECT for the debug mirror and CORRUPTING for a
//   length-prefixed protocol. The fix must be atomic without becoming blocking:
//   refuse the write entirely when the whole frame will not fit.

#include <gtest/gtest.h>

#include <vector>
#include <cstdint>

#include "Stream.h"
#include "helpers/ArduinoSerialInterface.h"

namespace {

/// A Stream that accepts at most `capacity` bytes, then short-writes like a full
/// non-blocking TX FIFO. Records everything actually emitted so a test can prove
/// no partial frame ever reached the wire.
class ConstrainedStream : public Stream {
public:
  size_t capacity = 0;             // bytes this stream will accept right now
  std::vector<uint8_t> wire;       // everything actually written

  int availableForWrite() override { return (int)capacity; }

  size_t write(const uint8_t* buffer, size_t size) override {
    const size_t n = size < capacity ? size : capacity;   // short write, no blocking
    wire.insert(wire.end(), buffer, buffer + n);
    capacity -= n;
    return n;
  }

  size_t write(uint8_t b) override { return write(&b, 1); }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

/// Walk `wire` as the host decoder does: '>' + uint16 len + len payload bytes.
/// Returns false if any frame is incomplete or a header appears inside a payload.
bool wireIsWellFramed(const std::vector<uint8_t>& wire, size_t* frames_out = nullptr) {
  size_t i = 0, frames = 0;
  while (i < wire.size()) {
    if (wire[i] != '>') return false;                       // stray byte where a frame must start
    if (i + 3 > wire.size()) return false;                  // truncated header
    const size_t len = (size_t)wire[i + 1] | ((size_t)wire[i + 2] << 8);
    if (i + 3 + len > wire.size()) return false;            // header promises more than was emitted
    i += 3 + len;
    ++frames;
  }
  if (frames_out) *frames_out = frames;
  return true;
}

const uint8_t kPayload[] = {0xC4, 0x02, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
constexpr size_t kPayloadLen = sizeof(kPayload);
constexpr size_t kFrameLen = 3 + kPayloadLen;

}  // namespace

// A stream with room for the whole frame emits it intact and reports the payload size.
TEST(SerialFrameAtomicity, FullCapacityEmitsOneCompleteFrame) {
  ConstrainedStream s;
  s.capacity = 1024;
  ArduinoSerialInterface iface;
  iface.begin(s);

  const size_t written = iface.writeFrame(kPayload, kPayloadLen);

  EXPECT_EQ(written, kPayloadLen);
  EXPECT_EQ(s.wire.size(), kFrameLen);
  size_t frames = 0;
  EXPECT_TRUE(wireIsWellFramed(s.wire, &frames));
  EXPECT_EQ(frames, 1u);
}

// THE REGRESSION CASE. Capacity is enough for the header and part of the payload.
// Pre-fix this emits '>' + len + a partial payload, which desyncs the receiver.
// The frame must be refused outright: nothing on the wire, and a 0 return so the
// caller knows not to advance.
TEST(SerialFrameAtomicity, RegressionSevenEighteen_PartialCapacityMustEmitNothing) {
  ConstrainedStream s;
  s.capacity = 3 + (kPayloadLen / 2);        // header fits, payload does not
  ArduinoSerialInterface iface;
  iface.begin(s);

  const size_t written = iface.writeFrame(kPayload, kPayloadLen);

  EXPECT_EQ(written, 0u) << "a frame that cannot be fully emitted must report failure";
  EXPECT_TRUE(s.wire.empty())
      << "emitted " << s.wire.size() << " bytes of a partial frame; the receiver will "
         "consume the next frame's header as payload filler (#718)";
}

// Not even the header may go out on its own: a lone header is exactly what makes
// the receiver start counting toward a payload that never arrives.
TEST(SerialFrameAtomicity, RegressionSevenEighteen_HeaderAloneIsNeverEmitted) {
  ConstrainedStream s;
  s.capacity = 3;                             // room for the header and nothing else
  ArduinoSerialInterface iface;
  iface.begin(s);

  EXPECT_EQ(iface.writeFrame(kPayload, kPayloadLen), 0u);
  EXPECT_TRUE(s.wire.empty()) << "header emitted with no payload behind it";
}

TEST(SerialFrameAtomicity, ZeroCapacityEmitsNothing) {
  ConstrainedStream s;
  s.capacity = 0;
  ArduinoSerialInterface iface;
  iface.begin(s);

  EXPECT_EQ(iface.writeFrame(kPayload, kPayloadLen), 0u);
  EXPECT_TRUE(s.wire.empty());
}

// The end-to-end property the field failure violated: a sequence of frames emitted
// against a stream that keeps running out of room must leave the wire parseable and
// every refused frame must be re-offered by the caller, so no payload byte is lost.
TEST(SerialFrameAtomicity, RegressionSevenEighteen_WireStaysParseableUnderRepeatedRefusal) {
  ConstrainedStream s;
  ArduinoSerialInterface iface;
  iface.begin(s);

  constexpr size_t kFrames = 12;
  size_t delivered = 0, refusals = 0, attempts = 0;

  // Capacity is keyed off the ATTEMPT, not the frame, so a refused frame sees more
  // room on its retry -- modelling a FIFO that drains between passes. Keying it off
  // the frame index would deadlock: a refusal that does not advance `f` would keep
  // recomputing the same too-small capacity forever.
  for (size_t f = 0; f < kFrames; ++attempts) {
    ASSERT_LT(attempts, kFrames * 4u) << "drain made no progress; refusal path deadlocked";
    s.capacity = (attempts % 3 == 0) ? (kFrameLen - 2) : (kFrameLen * 2);

    const size_t n = iface.writeFrame(kPayload, kPayloadLen);
    if (n == 0) {
      ++refusals;                 // caller must retry the SAME frame, not skip it
      continue;
    }
    delivered += n;
    ++f;
  }

  EXPECT_GT(refusals, 0u) << "test did not exercise the refusal path";
  EXPECT_EQ(delivered, kFrames * kPayloadLen) << "payload bytes were lost";

  size_t frames = 0;
  ASSERT_TRUE(wireIsWellFramed(s.wire, &frames))
      << "wire is not parseable: a partial frame desynced the stream";
  EXPECT_EQ(frames, kFrames);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
