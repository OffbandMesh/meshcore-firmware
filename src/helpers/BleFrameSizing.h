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
  // Bound 1: what one ATT notification carries on this link.
  const size_t link = effective_mtu > ATT_NOTIFY_HEADER
                        ? (size_t)(effective_mtu - ATT_NOTIFY_HEADER)
                        : 0;
  // Bound 2: the frame BUFFER (uint8_t buf[MAX_FRAME_SIZE]) -- a link that can
  // carry more cannot be exploited without changing MAX_FRAME_SIZE itself, which
  // is a protocol constant the client shares. An nRF52 at MTU 247 delivers 244 and
  // still sends 176-byte frames for this reason.
  //
  // #711: an earlier revision made bound 2 `max_frame - ATT_NOTIFY_HEADER` as a
  // hard ceiling, on the theory that a stack could over-report its MTU. That
  // theory was WRONG -- the bench client log showed the MTU reported accurately
  // (176, negotiated because begin() pins our preferred to MAX_FRAME_SIZE). The
  // real defect was that MultiSerialInterface never overrode maxFrameSize(), so
  // this function was not reached. With that fixed, min(link, max_frame) is
  // correct on both: ESP32 at MTU 176 -> 173, nRF52 at MTU 247 -> 176. The extra
  // ceiling only cost 3 bytes/frame on nRF52 and bought nothing.
  return link < max_frame ? link : max_frame;
}

/// Largest frame deliverable over a BLE link WITHOUT knowing its MTU, given a frame
/// buffer of `max_frame`. Floored at 0.
///
/// READ THE NAME NARROWLY. "Safe" here means safe against the ATT notification header
/// on a link that negotiated up to our own pinned MTU -- NOT an unconditional
/// guarantee. A peer that negotiates BELOW MAX_FRAME_SIZE will still clip a frame built
/// to this bound, and nothing computed without the connection can prevent that. Use
/// maxFramePayload() instead wherever the frame goes straight out over a live link;
/// this exists only for the builder that has no link to ask.
///
/// WHY A CONNECTION-INDEPENDENT BOUND IS NEEDED (#1621)
/// ---------------------------------------------------
/// `deliverableFrame()` answers "what fits RIGHT NOW", which is the correct question
/// for a builder that hands its frame straight to writeFrame() while connected. It is
/// the WRONG question for a builder that stores a frame for later -- the companion's
/// offline queue is filled precisely when no client is connected, and
/// SerialBLEInterface::maxFrameSize() deliberately reports the MIN_ATT_MTU floor
/// (23 - 3 = 20) until an MTU is negotiated. Sizing a stored message against that
/// would clamp it to a handful of bytes and the text would be gone for good, long
/// before any link got a say.
///
/// So a queued builder needs a bound that holds for EVERY link it might later be
/// delivered over, computed with no connection at all. Our own side pins its preferred
/// MTU to MAX_FRAME_SIZE (NimBLEDevice::setMTU / Bluefruit), so the negotiated MTU is
/// at most MAX_FRAME_SIZE and one notification carries at most MAX_FRAME_SIZE - 3.
/// That is this value: 173 for a 176-byte buffer.
///
/// It costs up to ATT_NOTIFY_HEADER bytes of payload on a link that could have carried
/// more (an nRF52 at MTU 247 delivers the full 176). That is the same trade the nRF52
/// maxFrameSize() comment already settles in favour of a fixed ceiling: three bytes per
/// frame, against an entire board class silently losing the tail of a message.
///
/// It does NOT make a frame deliverable over a link that negotiated BELOW
/// MAX_FRAME_SIZE -- nothing computed without the connection can. That case needs a
/// decision at delivery time and is tracked separately.
///
/// IT IS A BLE-SHAPED BOUND APPLIED WITHOUT ASKING THE TRANSPORT, and that costs
/// something: the companion's `_serial` is a MultiSerialInterface, so a queued frame may
/// end up delivered over USB or TCP, where the full MAX_FRAME_SIZE would have fitted and
/// these three bytes buy nothing. Kept unconditional anyway. Making it depend on whether
/// BLE is compiled in would turn a payload limit the client shares into a
/// build-dependent one, and a protocol constant that varies by build is the harder thing
/// to reason about -- which is the whole defect class this bound exists to close. Three
/// bytes is the cheaper problem.
/// constexpr so a caller can static_assert on it: the point of this bound is that it is
/// knowable without a connection, so it is knowable at compile time, and a builder that
/// derives a protocol limit from it can have that limit pinned by the compiler in every
/// build rather than by a comment.
constexpr size_t bleSafeFrame(size_t max_frame) {
  return max_frame > ATT_NOTIFY_HEADER ? max_frame - ATT_NOTIFY_HEADER : 0;
}

}  // namespace ble_frame
