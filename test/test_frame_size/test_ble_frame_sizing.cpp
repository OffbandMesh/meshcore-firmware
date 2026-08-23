// #712: regression test for the BLE frame-size ceiling (#711, itself a regression of #450).
//
// THE INVARIANT UNDER TEST
//   No frame handed to writeFrame() may exceed what one ATT notification delivers,
//   i.e. effective_MTU - 3, for EVERY value of the reported MTU -- including values
//   larger than MAX_FRAME_SIZE + 3.
//
// That last clause is the entire regression. #454 replaced #450's hardcoded cap with a
// computed one that saturated at MAX_FRAME_SIZE, so any peer-reported MTU >= 179 produced
// a 176-byte frame on a link carrying 173 and silently lost 3 bytes per full frame. A test
// exercising only the low-MTU case (the T1000-E 169 link #454 was written for) passes on
// the broken code and catches nothing, which is why the field found this and CI did not.

#include <gtest/gtest.h>

#include "helpers/BleFrameSizing.h"

using namespace ble_frame;

namespace {

// Mirrors src/helpers/BaseSerialInterface.h. Kept as a local constant so this suite has no
// Arduino dependency; asserted against the real value in FrameBufferConstantUnchanged.
constexpr size_t kMaxFrame = 176;

// What begin() pins our own side to: NimBLEDevice::setMTU(MAX_FRAME_SIZE).
constexpr uint16_t kLocalMtu = 176;

// What NimBLE hands back when our setMTU() never took effect (#711 re-fix).
constexpr uint16_t kNimbleDefaultMtu = 256;

// Reported MTUs worth covering. 179 and 517 are the values that fail pre-fix; 517 is what
// Android reports in the field when the client's requestMtu() is rejected.
const uint16_t kReportedMtus[] = {0, 1, 3, 4, 22, 23, 24, 100, 169, 172, 176, 178, 179, 185, 247, 512, 517, 65535};

// The pre-#711 implementation, verbatim, so the test can prove it is actually broken.
// If this ever stops failing the assertions below, the test has lost its teeth.
size_t legacyMaxFrameSize(uint16_t cached_mtu) {
  uint16_t m = cached_mtu > 3 ? (uint16_t)(cached_mtu - 3) : 0;
  return m < kMaxFrame ? (size_t)m : (size_t)kMaxFrame;
}

}  // namespace

// --- the specific field failure -------------------------------------------------------

// madmax_2069, 2026-08-15: peer reported a large MTU, our side was pinned at 176, the link
// delivered 173. Three downloads each lost exactly 3 bytes x 49 full chunks = 147 bytes.
TEST(BleFrameSizing, RegressionSeventyOneOne_PeerMtuMustNotRaiseTheLinkCeiling) {
  const uint16_t effective = effectiveMtu(/*reported=*/517, /*local=*/kLocalMtu);
  EXPECT_EQ(effective, 176) << "connection MTU is min(local, peer), not the peer's alone";

  const size_t deliverable = deliverableFrame(effective, kMaxFrame);
  EXPECT_EQ(deliverable, 173u) << "a 176-MTU link carries MTU-3 = 173 on-wire bytes";
  EXPECT_LT(deliverable, kMaxFrame)
      << "on this transport the MAX_FRAME_SIZE ceiling is unreachable: begin() pins our "
         "local MTU to MAX_FRAME_SIZE, so the deliverable is always at most MAX_FRAME_SIZE-3";

  // The payload a 2-byte-header chunk builder (caplog CHUNK) may emit.
  const size_t payload = deliverable > 2 ? deliverable - 2 : 0;
  EXPECT_EQ(payload, 171u);
  EXPECT_EQ(2 + payload, 173u) << "the emitted frame must fit one notification exactly";
}

// Proves the above is a real behaviour change: the old code returns the full frame here.
TEST(BleFrameSizing, RegressionSeventyOneOne_LegacyImplementationIsDemonstrablyBroken) {
  EXPECT_EQ(legacyMaxFrameSize(517), kMaxFrame)
      << "pre-#711 cached the peer MTU and saturated at MAX_FRAME_SIZE";
  EXPECT_EQ(legacyMaxFrameSize(517) - deliverableFrame(effectiveMtu(517, kLocalMtu), kMaxFrame), 3u)
      << "exactly the 3 bytes per full frame the field lost";
}

// The tester's arithmetic, end to end: an 8608-byte buffer chunked at the correct cap must
// deliver 8608 bytes. At the broken cap the sum comes up 147 short, which is what the
// client reported as "8461 of 8608 bytes in 50 chunks".
TEST(BleFrameSizing, RegressionSeventyOneOne_DeliveredTotalEqualsAnnouncedTotal) {
  constexpr size_t kTotal = 8608;
  constexpr size_t kHeader = 2;

  const size_t good_cap = deliverableFrame(effectiveMtu(517, kLocalMtu), kMaxFrame) - kHeader;
  size_t delivered = 0, chunks = 0;
  for (size_t off = 0; off < kTotal; off += good_cap, ++chunks) {
    const size_t n = (kTotal - off) < good_cap ? (kTotal - off) : good_cap;
    ASSERT_LE(kHeader + n, 173u) << "chunk " << chunks << " would be clipped on the wire";
    delivered += n;
  }
  EXPECT_EQ(delivered, kTotal);

  // Same walk at the pre-fix cap, counting what the wire would actually carry.
  const size_t bad_cap = legacyMaxFrameSize(517) - kHeader;   // 174
  size_t received = 0, full_chunks = 0;
  for (size_t off = 0; off < kTotal; off += bad_cap) {
    const size_t n = (kTotal - off) < bad_cap ? (kTotal - off) : bad_cap;
    const size_t on_wire = (kHeader + n) > 173 ? 173 : (kHeader + n);   // ATT clips the tail
    if (n == bad_cap) ++full_chunks;
    received += on_wire - kHeader;
  }
  EXPECT_EQ(kTotal - received, 147u) << "the exact field shortfall";
  EXPECT_EQ(3 * full_chunks, 147u) << "3 bytes per FULL chunk; the short final chunk survives";
}

// --- the general invariant ------------------------------------------------------------

TEST(BleFrameSizing, EmittedFrameNeverExceedsWhatTheLinkDelivers) {
  for (uint16_t reported : kReportedMtus) {
    const uint16_t effective = effectiveMtu(reported, kLocalMtu);
    const size_t deliverable = deliverableFrame(effective, kMaxFrame);

    // The link's true ceiling, derived from the INPUTS rather than from `effective`.
    // Deriving it from `effective` would make this assertion circular: a bug that
    // inflates the effective MTU also inflates the ceiling it is checked against, and
    // the test passes while the wire clips. That is exactly how this escaped before --
    // verified by reverting the fix and watching only the explicit regression cases fail.
    const uint16_t true_mtu = reported < MIN_ATT_MTU
                                ? MIN_ATT_MTU
                                : (kLocalMtu < reported ? kLocalMtu : reported);
    const size_t link_ceiling = true_mtu > 3 ? (size_t)(true_mtu - 3) : 0;

    EXPECT_EQ(effective, true_mtu)
        << "reported=" << reported << ": effective MTU must be min(local, peer)";

    EXPECT_LE(deliverable, link_ceiling)
        << "reported=" << reported << " effective=" << effective
        << ": frame would be clipped on the wire";
    EXPECT_LE(deliverable, kMaxFrame)
        << "reported=" << reported << ": frame would overrun the send buffer";

    // And for a builder with a header, header + payload must still fit.
    for (size_t header : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(4), size_t(6)}) {
      const size_t payload = deliverable > header ? deliverable - header : 0;
      EXPECT_LE(header + payload, link_ceiling)
          << "reported=" << reported << " header=" << header;
    }
  }
}

TEST(BleFrameSizing, EffectiveMtuIsTheMinimumOfBothSides) {
  EXPECT_EQ(effectiveMtu(517, 176), 176);   // peer larger  -> our side wins
  EXPECT_EQ(effectiveMtu(100, 176), 100);   // peer smaller -> peer wins
  EXPECT_EQ(effectiveMtu(176, 176), 176);   // equal
  EXPECT_EQ(effectiveMtu(169, 512), 169);   // T1000-E class link, generous local
}

// An unknown or nonsensical report must be conservative. It must NEVER fall through to the
// local MTU, which would license a frame the link may not carry. This is the same lesson
// the client already encodes ("an unknown MTU must fall back to that floor").
TEST(BleFrameSizing, UnusableReportFallsBackToTheFloorNotToLocal) {
  for (uint16_t bad : {uint16_t(0), uint16_t(1), uint16_t(3), uint16_t(22)}) {
    EXPECT_EQ(effectiveMtu(bad, kLocalMtu), MIN_ATT_MTU)
        << "reported=" << bad << " must not inherit the local MTU";
    EXPECT_EQ(deliverableFrame(effectiveMtu(bad, kLocalMtu), kMaxFrame), 20u);
  }
}

// Gemini flagged this during #454 and it must stay guarded: a tiny MTU must yield 0, never
// a wrapped-huge size_t that would send a builder off the end of its buffer.
TEST(BleFrameSizing, TinyMtuCannotUnderflow) {
  for (uint16_t tiny : {uint16_t(0), uint16_t(1), uint16_t(2), uint16_t(3)}) {
    EXPECT_EQ(deliverableFrame(tiny, kMaxFrame), 0u) << "mtu=" << tiny;
  }
  EXPECT_EQ(deliverableFrame(4, kMaxFrame), 1u);
}

// RENAMED + CORRECTED in the #711 re-fix. This was `SerialAndTcpAreUnaffected` and
// asserted deliverableFrame() returns the FULL frame for a generous MTU -- which is
// the defect itself, written down as an expectation. It also did not test what its
// name claimed: deliverableFrame() is called ONLY from the two BLE interfaces
// (esp32/nrf52 SerialBLEInterface); serial and TCP use the BaseSerialInterface
// default maxFrameSize() -> MAX_FRAME_SIZE and never reach this function, so they
// are unaffected structurally, not by anything this function returns.
TEST(BleFrameSizing, GenerousMtuIsCappedByTheFrameBuffer) {
  // A link that can carry more cannot be exploited: the frame buffer is
  // uint8_t buf[MAX_FRAME_SIZE]. This is why an nRF52 at MTU 247 (244 deliverable)
  // still sends 176-byte frames. Was GenerousMtuStillRespectsTheHardCeiling and
  // expected kMaxFrame-3; the hard ceiling was reverted (#711) once the real
  // defect turned out to be MultiSerialInterface never delegating at all.
  EXPECT_EQ(deliverableFrame(kMaxFrame + ATT_NOTIFY_HEADER, kMaxFrame), kMaxFrame);
  EXPECT_EQ(deliverableFrame(65535, kMaxFrame), kMaxFrame);
  EXPECT_EQ(deliverableFrame(kNimbleDefaultMtu, kMaxFrame), kMaxFrame);
}

// Guards the local mirror of MAX_FRAME_SIZE: if the real constant moves, this suite's
// numbers must be revisited rather than silently drifting out of sync.
TEST(BleFrameSizing, FrameBufferConstantUnchanged) {
  EXPECT_EQ(kMaxFrame, 176u)
      << "MAX_FRAME_SIZE changed; revisit the expected values in this suite";
}

// --- #711: the invariant, corrected ------------------------------------------
//
// Three tester reports, all ESP32, all the same shape:
//   madmax_2069   8461 of  8608 in 50 chunks  (147 = 3 x 49)
//   schill       12751 of 12973 in 75 chunks  (222 = 3 x 74)
//   hv4-bench-1  14246 of 14495 in 84 chunks  (249 = 3 x 83)
//
// The bench client log settled the cause: the link negotiated MTU 176 and was
// REPORTED accurately as 176. begin() pins our preferred MTU to MAX_FRAME_SIZE,
// so an ESP32 link can never exceed 176 -> 173 deliverable. The firmware still
// emitted 174-byte payloads because MultiSerialInterface never overrode
// maxFrameSize() and returned the buffer size instead (see test_multiserial).
//
// An earlier revision added a hard ceiling of max_frame - 3 here, on the theory
// that a stack might over-report its MTU. That theory was WRONG and is reverted:
// the MTU was honest, and the ceiling cost 3 bytes/frame on nRF52 (MTU 247) for
// no benefit. min(link, max_frame) is correct on both.

constexpr uint16_t kEsp32NegotiatedMtu = 176;   // pinned by begin(); cannot exceed

// THE REGRESSION. On the link every field report was taken on, the payload must
// be 171. 174 is what shipped and what clipped.
TEST(BleFrameSizing, RegressionSevenEleven_Esp32LinkYields171NotThePreFix174) {
  const size_t deliverable =
      deliverableFrame(effectiveMtu(kEsp32NegotiatedMtu, kLocalMtu), kMaxFrame);
  EXPECT_EQ(deliverable, 173u) << "MTU 176 delivers MTU-3";
  EXPECT_EQ(deliverable > 2 ? deliverable - 2 : 0, 171u)
      << "caplog CHUNK payload; 174 is the pre-fix size that lost 3 B per full frame";
}

// The field arithmetic at the correct cap: nothing lost, and the chunk COUNT is
// itself the tell -- 76 rather than the 75 schill saw, 85 rather than 84 on bench.
TEST(BleFrameSizing, RegressionSevenEleven_FieldCapturesDeliverInFull) {
  const size_t cap =
      deliverableFrame(effectiveMtu(kEsp32NegotiatedMtu, kLocalMtu), kMaxFrame) - 2;
  ASSERT_EQ(cap, 171u);

  struct Case { size_t total; size_t expect_chunks; } cases[] = {
      {8608, 51}, {12973, 76}, {14495, 85},
  };
  for (const auto& c : cases) {
    size_t chunks = 0, delivered = 0;
    for (size_t off = 0; off < c.total; off += cap, ++chunks) {
      const size_t n = (c.total - off) < cap ? (c.total - off) : cap;
      ASSERT_LE(2 + n, 173u) << "chunk " << chunks << " would be clipped on the wire";
      delivered += n;
    }
    EXPECT_EQ(delivered, c.total) << "total " << c.total;
    EXPECT_EQ(chunks, c.expect_chunks) << "total " << c.total;
  }
}

// The general invariant: for ANY (reported, local), the emitted frame must fit
// both the link and the buffer. Sweeping `local` is what the original suite
// missed -- it pinned local at 176 and went green on a fix that did not work.
TEST(BleFrameSizing, NoReportedLocalPairExceedsLinkOrBuffer) {
  const uint16_t locals[] = {0, 1, 22, 23, 100, 169, 176, 179, 185, 247, 256, 512, 517, 65535};
  for (uint16_t reported : kReportedMtus) {
    for (uint16_t local : locals) {
      const uint16_t eff = effectiveMtu(reported, local);
      const size_t deliverable = deliverableFrame(eff, kMaxFrame);
      const size_t link = eff > ATT_NOTIFY_HEADER ? (size_t)(eff - ATT_NOTIFY_HEADER) : 0;
      EXPECT_LE(deliverable, link)
          << "reported=" << reported << " local=" << local << ": frame would be clipped";
      EXPECT_LE(deliverable, kMaxFrame)
          << "reported=" << reported << " local=" << local << ": frame would overrun the buffer";
    }
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
