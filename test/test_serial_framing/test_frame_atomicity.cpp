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

// ===========================================================================
// #1315: a COMMITTED frame -- one whose header already reached the wire.
//
// The cases above all sit BEFORE the commit point: availableForWrite() tells the
// truth, writeFrame refuses, nothing is emitted. The cases below are the race the
// capacity check cannot cover -- availableForWrite() reports room, then write()
// short-writes because something else consumed the FIFO in between (the debug
// console shares this Stream on the USB-serial companion roles).
//
// Pre-#1315 those branches returned 0 under a comment claiming nothing had reached
// the wire. #1301 then taught every stream caller to retry a 0, so the retry wrote
// a whole frame BEHIND a partial one -- the desync #718 exists to prevent.
// ===========================================================================

namespace {

/// A transport whose behaviour is SCRIPTED rather than emergent: `accept[i]` caps what
/// write() will take on its i-th call, `default_accept` applies once the script runs out,
/// and every call advances the mock clock by `ms_per_call` (real time really passing,
/// which the frozen mock clock otherwise never models).
///
/// The first model here derived acceptance from a `room`/`refill_on_stall` pair, and that
/// made PayloadShortWriteIsPaddedToTheDeclaredLength pass for the WRONG REASON: the refill
/// landed before the retry, the whole REAL payload went out, and the assertion on the
/// frame's length was satisfied without any padding ever happening. Adversarial review
/// called the mock "too predictable". It was worse than that -- it was unfalsifiable for
/// the one behaviour it claimed to test. A script makes each case state exactly what the
/// transport does on each call, so a test cannot quietly stop testing its own subject.
class ScriptedStream : public Stream {
public:
  int claimed = 4096;                  // what availableForWrite() reports
  std::vector<size_t> accept;          // cap for each successive write() call
  size_t default_accept = 0;           // cap once the script is exhausted
  uint32_t ms_per_call = 0;            // wall time elapsing per write() call
  size_t calls = 0;
  std::vector<uint8_t> wire;

  int availableForWrite() override { return claimed; }

  size_t write(const uint8_t* buffer, size_t size) override {
    const size_t cap = calls < accept.size() ? accept[calls] : default_accept;
    ++calls;
    const size_t n = size < cap ? size : cap;
    wire.insert(wire.end(), buffer, buffer + n);
    g_mock_millis += ms_per_call;
    return n;
  }

  size_t write(uint8_t b) override { return write(&b, 1); }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

}  // namespace

// A transport that accepts one byte at a time still yields exactly one whole frame.
// This is the ordinary committed case: the room is there, it just arrives in dribbles.
TEST(CommittedFrame, DripFedTransportStillEmitsOneWholeFrame) {
  ScriptedStream s;
  s.default_accept = 1;           // one byte per write() call, forever
  s.ms_per_call = 0;              // no time passes, so only completion ends the loop
  ArduinoSerialInterface iface;
  iface.begin(s);

  const size_t written = iface.writeFrame(kPayload, kPayloadLen);

  EXPECT_EQ(written, kPayloadLen);
  EXPECT_EQ(s.wire.size(), kFrameLen) << "committed frame did not finish";
  size_t frames = 0;
  EXPECT_TRUE(wireIsWellFramed(s.wire, &frames));
  EXPECT_EQ(frames, 1u);
  EXPECT_EQ(iface.committedLossCount(), 0u) << "a frame that completed is not a loss";
}

// THE #1315 REGRESSION CASE. The header goes out, the payload short-writes, the bounded
// window expires, and only then does room appear. The declared length must still be
// honored -- with filler for the part we could not deliver -- so the receiver stays in
// sync for every later frame.
//
// The script is the point: header takes 3, the payload write takes only 4 of 10, the
// retry inside the window takes 0, and the filler write then succeeds. That is the one
// interleaving that actually exercises padding, and the old emergent mock never produced it.
TEST(CommittedFrame, PayloadShortWriteIsPaddedToTheDeclaredLength) {
  ScriptedStream s;
  s.accept = {3, 4, 0};           // header | 4 of 10 payload | retry takes nothing
  s.default_accept = 64;          // the filler write finds room
  s.ms_per_call = 5;              // each call blows the 2 ms window
  ArduinoSerialInterface iface;
  iface.begin(s);

  const size_t written = iface.writeFrame(kPayload, kPayloadLen);

  EXPECT_EQ(written, kPayloadLen)
      << "a committed frame must NOT report 0 -- a retry would write a second frame "
         "behind this one and desync the stream (#1315)";
  ASSERT_EQ(s.wire.size(), kFrameLen)
      << "wire holds " << s.wire.size() << " bytes against a header declaring "
      << kFrameLen << "; the receiver is left counting toward bytes that never come";
  size_t frames = 0;
  EXPECT_TRUE(wireIsWellFramed(s.wire, &frames)) << "padded frame is not parseable";
  EXPECT_EQ(frames, 1u);
  // Exact composition: the four real bytes that made it, then filler for the rest.
  EXPECT_EQ(memcmp(&s.wire[3], kPayload, 4), 0) << "delivered prefix was corrupted";
  for (size_t i = 3 + 4; i < kFrameLen; ++i) {
    EXPECT_EQ(s.wire[i], 0xFF) << "byte " << i << " should be filler";
  }
  EXPECT_EQ(iface.committedLossCount(), 1u)
      << "the right NUMBER of bytes went out but the wrong bytes -- that is still a loss";
}

// A committed frame must be consumed, never re-offered. A D1-style caller retries on 0,
// so if the committed path ever returned 0 the wire would grow a second frame.
TEST(CommittedFrame, IsNeverRetriedByAnHonorTheReturnCaller) {
  ScriptedStream s;
  s.accept = {3, 4, 0};
  s.default_accept = 64;
  s.ms_per_call = 5;
  ArduinoSerialInterface iface;
  iface.begin(s);

  // Exactly the loop shape D1 (#1301) put in every stream drain.
  size_t attempts = 0;
  for (size_t guard = 0; guard < 8; ++guard) {
    ++attempts;
    if (iface.writeFrame(kPayload, kPayloadLen) != 0) break;   // sent: advance the cursor
  }

  EXPECT_EQ(attempts, 1u) << "the committed frame was re-offered";
  size_t frames = 0;
  ASSERT_TRUE(wireIsWellFramed(s.wire, &frames));
  EXPECT_EQ(frames, 1u) << "a retry put a second frame behind a partial one -- the exact "
                           "corruption #1315 removes";
}

// The residual case, stated plainly rather than papered over: if the transport will take
// nothing more -- not even filler -- inside the window, the frame IS short on the wire and
// the receiver WILL desync. Bounded completion cannot prevent that; it reports it. What
// this test pins is that we terminate and consume the frame instead of spinning or
// retrying, which would make a wedged transport worse.
TEST(CommittedFrame, WedgedTransportIsReportedAndConsumedRatherThanRetried) {
  ScriptedStream s;
  s.accept = {3};                 // header only
  s.default_accept = 0;           // and nothing ever frees up again
  s.ms_per_call = 5;
  ArduinoSerialInterface iface;
  iface.begin(s);

  const size_t written = iface.writeFrame(kPayload, kPayloadLen);

  EXPECT_EQ(written, kPayloadLen) << "must be consumed: a retry adds a second fragment";
  EXPECT_EQ(s.wire.size(), 3u) << "nothing beyond the header could be delivered";
  EXPECT_EQ(iface.committedLossCount(), 1u) << "an admitted desync must still be counted";
}

// The spin backstop. millis() is frozen in the native mocks, which is also what a stalled
// hardware clock looks like from in here. A loop whose only exit is the deadline would
// hang the companion with a frame half on the wire; the attempt counter must end it.
TEST(CommittedFrame, FrozenClockCannotHangTheWritePath) {
  ScriptedStream s;
  s.accept = {3};
  s.default_accept = 0;
  s.ms_per_call = 0;              // the clock NEVER advances -- deadline can never fire
  ArduinoSerialInterface iface;
  iface.begin(s);

  const size_t written = iface.writeFrame(kPayload, kPayloadLen);   // must return at all

  EXPECT_EQ(written, kPayloadLen);
  EXPECT_EQ(s.wire.size(), 3u);
}

// Guard rail: none of the above may loosen the pre-commit contract. When the transport
// tells the truth about its free space, a frame that will not fit is still refused with
// an untouched wire -- #718's behaviour, unchanged.
TEST(CommittedFrame, TruthfulTransportStillRefusesBeforeCommitting) {
  ScriptedStream s;
  s.claimed = (int)(kFrameLen - 1);   // honest: not enough room for the whole frame
  s.default_accept = kFrameLen;       // would have accepted it, but must never be asked
  ArduinoSerialInterface iface;
  iface.begin(s);

  EXPECT_EQ(iface.writeFrame(kPayload, kPayloadLen), 0u);
  EXPECT_TRUE(s.wire.empty()) << "committed-frame handling must not bypass the capacity check";
  EXPECT_EQ(s.calls, 0u) << "write() must not be reached at all on a truthful refusal";
}

// ---------------------------------------------------------------------------
// Cases below come from the #1315 adversarial review. Each pins a specific
// objection that turned out to be right.
// ---------------------------------------------------------------------------

// Filler must NOT be 0x00. If the payload short-writes at n == 0 the whole payload is
// filler INCLUDING byte 0, the response code -- and RESP_CODE_OK is 0 (MyMesh.cpp:152),
// so zero filler would hand the client a spurious SUCCESS reply. 0xFF is not a response
// code at all, so the frame decodes as something the client ignores.
TEST(CommittedFrame, FillerIsNotAValidResponseCode) {
  ScriptedStream s;
  s.accept = {3, 0, 0};           // header | payload takes nothing | retry takes nothing
  s.default_accept = 64;          // then the filler write finds room
  s.ms_per_call = 5;
  ArduinoSerialInterface iface;
  iface.begin(s);

  EXPECT_EQ(iface.writeFrame(kPayload, kPayloadLen), kPayloadLen);
  ASSERT_EQ(s.wire.size(), kFrameLen);
  EXPECT_NE(s.wire[3], 0x00)
      << "a fully-filled payload begins with 0x00 == RESP_CODE_OK; the client would read "
         "a successful reply to whatever it last asked";
  EXPECT_EQ(s.wire[3], 0xFF) << "filler should be a code the protocol does not define";
  EXPECT_EQ(iface.committedLossCount(), 1u);
}

// The loss must be visible SOMEWHERE, since writeFrame has to return `len`. The counter is
// that somewhere: it moves for a degraded frame and stays put for a clean one, and it is
// cumulative across the interface rather than per-frame.
TEST(CommittedFrame, LossCounterDistinguishesDegradedFromClean) {
  ScriptedStream clean;
  clean.default_accept = 4096;    // plenty: the frame goes out whole
  ArduinoSerialInterface iface;
  iface.begin(clean);

  ASSERT_EQ(iface.writeFrame(kPayload, kPayloadLen), kPayloadLen);
  EXPECT_EQ(iface.committedLossCount(), 0u) << "a clean frame must not count as a loss";

  ScriptedStream wedged;          // same interface, a transport that now degrades
  wedged.accept = {3};
  wedged.default_accept = 0;
  wedged.ms_per_call = 5;
  iface.begin(wedged);

  ASSERT_EQ(iface.writeFrame(kPayload, kPayloadLen), kPayloadLen);
  EXPECT_EQ(iface.committedLossCount(), 1u)
      << "writeFrame returned success for a degraded frame and nothing recorded the loss";
}

// A write that takes ZERO bytes has not committed anything, so it is the ordinary
// retryable refusal -- not a committed loss. Returning `len` here would silently discard a
// frame that the next pass could still deliver.
TEST(CommittedFrame, ZeroByteHeaderWriteStaysRetryable) {
  ScriptedStream s;
  s.claimed = 4096;               // lies: says there is room
  s.default_accept = 0;           // takes nothing at all
  s.ms_per_call = 5;
  ArduinoSerialInterface iface;
  iface.begin(s);

  EXPECT_EQ(iface.writeFrame(kPayload, kPayloadLen), 0u)
      << "nothing reached the wire, so the caller must be told to retry";
  EXPECT_TRUE(s.wire.empty());
  EXPECT_EQ(iface.committedLossCount(), 0u) << "nothing was lost -- nothing was sent";
}

// The review claimed the spin backstop is fragile: with a frozen clock and progress on
// every call, `idle` never increments and the deadline never fires, so the loop "would hang
// forever". It does not -- progress on every call means the frame COMPLETES, which is the
// loop's other exit. This pins that, so the claim cannot be re-raised without evidence.
TEST(CommittedFrame, FrozenClockWithProgressCompletesRatherThanHanging) {
  ScriptedStream s;
  s.default_accept = 1;           // always progress, one byte at a time
  s.ms_per_call = 0;              // the clock NEVER advances
  ArduinoSerialInterface iface;
  iface.begin(s);

  const size_t written = iface.writeFrame(kPayload, kPayloadLen);

  EXPECT_EQ(written, kPayloadLen);
  EXPECT_EQ(s.wire.size(), kFrameLen) << "the frame completed, so the loop exited on completion";
  EXPECT_EQ(iface.committedLossCount(), 0u);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
