#pragma once

#include <stdint.h>
#include <stddef.h>

// #711: pure ATT frame-sizing arithmetic, deliberately free of Arduino/NimBLE/Bluefruit
// dependencies so it is unit-testable natively (test/test_frame_size).
//
// WHY THIS EXISTS AS A SEPARATE, TESTABLE UNIT
// -------------------------------------------
// #450 fixed "a full MAX_FRAME_SIZE frame is clipped to MTU-3 over BLE" with a hardcoded
// constant. #454 generalised it to a computed cap and reintroduced the bug the same day,
// because the computation trusted the PEER's MTU as if it were the connection's. Nothing
// in the tree asserted the ceiling, so the regression shipped and was only caught in the
// field two weeks later. These functions are the logic that broke; keeping them pure is
// what lets a test pin the invariant instead of a comment asking future authors to be
// careful.

namespace ble_frame {

/// BLE spec minimum ATT MTU. Every link supports at least this.
static const uint16_t MIN_ATT_MTU = 23;

/// Bytes an ATT notification spends on its own header, unavailable to the payload.
static const uint16_t ATT_NOTIFY_HEADER = 3;

/// The connection's EFFECTIVE ATT MTU.
///
/// ATT negotiation settles on the MINIMUM of the two sides, so a peer advertising a
/// large MTU (Android commonly reports 517) does not raise what the link can actually
/// carry when our own side is configured lower. Treating the peer's number as the
/// connection's is precisely the #711 regression.
///
/// `reported` -- what the stack tells us about the peer / the connection.
/// `local`    -- the MTU we configured for ourselves (e.g. NimBLEDevice::setMTU()).
///
/// A `reported` value below the BLE minimum is not a usable measurement (no exchange
/// yet, or an error), so we fall back to the conservative floor rather than to `local`:
/// an unknown MTU must never license a larger frame.
inline uint16_t effectiveMtu(uint16_t reported, uint16_t local) {
  if (reported < MIN_ATT_MTU) return MIN_ATT_MTU;
  if (local >= MIN_ATT_MTU && local < reported) return local;
  return reported;
}

/// Largest on-wire FRAME deliverable in ONE notification on a link of `effective_mtu`,
/// bounded by `max_frame` (the sender's frame buffer size).
///
/// The `max_frame` bound is a BUFFER limit, not a link limit. It must never be able to
/// raise the result above what the link carries, which is what the previous
/// `min(mtu - 3, MAX_FRAME_SIZE)` form did once the cached MTU was too large.
inline size_t deliverableFrame(uint16_t effective_mtu, size_t max_frame) {
  // What the link says it can carry, from the (possibly untrustworthy) MTU.
  const size_t link = effective_mtu > ATT_NOTIFY_HEADER
                        ? (size_t)(effective_mtu - ATT_NOTIFY_HEADER)
                        : 0;

  // #711 RE-FIX: the HARD ceiling, derived from OUR OWN pinned preference and from
  // nothing the stack reported. begin() pins the local preferred MTU to max_frame,
  // so a link at that preference delivers at most max_frame - 3 -- clamping to
  // max_frame (the BUFFER size) was always one ATT header too generous.
  //
  // The first fix computed min(reported_mtu - 3, max_frame) and was a no-op whenever
  // the reported value was >= max_frame + 3. It shipped in beta4 and STILL clipped:
  // NimBLEDevice::getMTU() returns ble_att_preferred_mtu_val, whose NimBLE default is
  // 256, so a device where our setMTU() did not take effect computed
  // min(253, 176) = 176 -- exactly the pre-fix size. Tester schill, Heltec V4.3:
  // 12751 of 12973 bytes in 75 chunks, shortfall 222 = 3 x 74 full chunks.
  //
  // Taking the min of both bounds is safe BY CONSTRUCTION, which is the property
  // #450's hardcoded constant had and every computed replacement since has lost:
  //   - setMTU() took effect  -> link is the real bound, and own == link. Exact.
  //   - setMTU() did NOT take effect -> we may under-use a larger link by 3 bytes.
  //     Conservative, never clipped. A 1.7% throughput cost buys correctness on
  //     every board and every stack, including ones that misreport.
  const size_t own = max_frame > ATT_NOTIFY_HEADER
                       ? max_frame - ATT_NOTIFY_HEADER
                       : 0;

  return link < own ? link : own;
}

}  // namespace ble_frame
