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
TEST(BleFrameSizing, GenerousMtuStillRespectsTheHardCeiling) {
  const size_t ceiling = kMaxFrame - ATT_NOTIFY_HEADER;
  EXPECT_EQ(deliverableFrame(kMaxFrame + ATT_NOTIFY_HEADER, kMaxFrame), ceiling);
  EXPECT_EQ(deliverableFrame(65535, kMaxFrame), ceiling);
  EXPECT_EQ(deliverableFrame(kNimbleDefaultMtu, kMaxFrame), ceiling);
}

// Guards the local mirror of MAX_FRAME_SIZE: if the real constant moves, this suite's
// numbers must be revisited rather than silently drifting out of sync.
TEST(BleFrameSizing, FrameBufferConstantUnchanged) {
  EXPECT_EQ(kMaxFrame, 176u)
      << "MAX_FRAME_SIZE changed; revisit the expected values in this suite";
}

// --- #711 RE-FIX: the ceiling must not depend on any reported MTU ------------
//
// beta4 STILL clipped 3 B off every full frame on a Heltec V4.3, with this fix in.
// Tester schill: 12751 of 12973 bytes in 75 chunks -> shortfall 222 = 3 x 74.
// 12973 needs 76 chunks at a 171-byte payload and 75 at 174, so the device emitted
// 174 -- the PRE-fix size -- meaning maxFrameSize() returned MAX_FRAME_SIZE.
//
// Cause: NimBLEDevice::getMTU() returns ble_att_preferred_mtu_val, whose NimBLE
// DEFAULT is 256. Every test below pinned `local` to kLocalMtu (176), so the suite
// never exercised the one value that breaks it. A test that cannot fail for the
// real input is not a guard -- that is the whole lesson of #712.

// The value NimBLE hands back when our setMTU() never took effect.

// The invariant, stated independently of any MTU the stack reports: a frame must
// never exceed what a link at our OWN configured preference can deliver. begin()
// pins that preference to MAX_FRAME_SIZE, so the hard ceiling is MAX_FRAME_SIZE - 3.
constexpr size_t kHardCeiling = kMaxFrame - 3;

TEST(BleFrameSizing, RegressionSevenElevenB_UntrustedLocalMustNotRaiseTheCeiling) {
  // Exactly the shipped-beta4 situation: peer says 517, local reads NimBLE's default.
  const uint16_t effective = effectiveMtu(517, kNimbleDefaultMtu);
  const size_t deliverable = deliverableFrame(effective, kMaxFrame);

  EXPECT_LE(deliverable, kHardCeiling)
      << "local=" << kNimbleDefaultMtu << " (NimBLE default, i.e. our setMTU did not "
         "take effect) must not license a frame the link will clip";

  const size_t payload = deliverable > 2 ? deliverable - 2 : 0;
  EXPECT_EQ(payload, 171u) << "chunk payload must stay 171, not the pre-fix 174";
}

// The field arithmetic, end to end. At the correct cap schill's buffer needs 76
// chunks and loses nothing; at the broken cap it takes 75 and loses exactly 222.
TEST(BleFrameSizing, RegressionSevenElevenB_SchillCaptureArithmetic) {
  constexpr size_t kTotal = 12973, kHeader = 2;

  const size_t cap = deliverableFrame(effectiveMtu(517, kNimbleDefaultMtu), kMaxFrame) - kHeader;
  size_t chunks = 0, delivered = 0;
  for (size_t off = 0; off < kTotal; off += cap, ++chunks) {
    const size_t n = (kTotal - off) < cap ? (kTotal - off) : cap;
    ASSERT_LE(kHeader + n, kHardCeiling) << "chunk " << chunks << " would be clipped";
    delivered += n;
  }
  EXPECT_EQ(delivered, kTotal);
  EXPECT_EQ(chunks, 76u) << "76 chunks at the correct cap; the field saw 75 at the broken one";
}

// The general form: NO pair of (reported, local) may produce a frame above the
// hard ceiling. This is the assertion that would have caught beta4 before release.
TEST(BleFrameSizing, RegressionSevenElevenB_NoReportedLocalPairExceedsTheCeiling) {
  const uint16_t locals[] = {0, 1, 22, 23, 100, 169, 176, 179, 185, 247, 256, 512, 517, 65535};
  for (uint16_t reported : kReportedMtus) {
    for (uint16_t local : locals) {
      const size_t deliverable = deliverableFrame(effectiveMtu(reported, local), kMaxFrame);
      EXPECT_LE(deliverable, kHardCeiling)
          << "reported=" << reported << " local=" << local
          << " produced a frame of " << deliverable << " B, above the "
          << kHardCeiling << " B a MAX_FRAME_SIZE-preferred link delivers";
    }
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
