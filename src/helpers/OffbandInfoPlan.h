#pragma once

// Offband #2214: WHICH records the 0xC5 0x1C advert carries, and IN WHAT ORDER.
//
// WHY THIS IS ITS OWN HEADER, and not five `if` statements at the call site -- because it
// was five `if` statements at the call site, and they shipped in the wrong order.
//
// #2067 claimed tags 0x04 and 0x05, and #2068's emission was appended after the existing
// block, which ended at 0x03. So the order on the wire became 0x01, 0x02, 0x04, 0x05, 0x03.
// InfoAdvert::add() refuses a non-ascending tag by design -- the resume cursor names a tag,
// so an unordered advert makes the cursor silently drop records -- and it refuses by
// returning false and writing nothing. The call site discarded that return. The result went
// out in 1.7.1: every board with a display or a controllable LED sent an advert with
// ACK_CAPACITY missing and NO CONTINUE record saying so. A client read a short list as a
// complete one, which is precisely the silent-wrong-answer failure the advert replaced the
// absolute-offset scheme to remove.
//
// Two things make that unrepeatable, and both are here rather than at the call site:
//
//   1. The order is DATA, exhaustively testable. buildInfoPlan() emits records in tag order
//      for every combination of inputs, and the host test walks all of them. An ordering
//      mistake now fails a test instead of shipping; a reader adding a tag edits one list
//      where the ascending order is visible on the page.
//   2. The refusal is CONSUMED. writeInfoPlan() distinguishes "ran out of room", which is
//      normal and is announced by the CONTINUE record, from "add() refused while there was
//      still space", which can only be a plan bug. The caller must refuse rather than send
//      an advert that is quietly short -- see InfoPlanOutcome::Misordered.
//
// Pure and Arduino-free on purpose, same as OffbandInfoAdvert.h and for the same reason:
// the whole of the advert's assembly has to be reachable from the host test suite.

#include <stdint.h>
#include <stddef.h>

#include <helpers/IndicatorCaps.h>
#include <helpers/OffbandInfoAdvert.h>

namespace offband {

// Tag values MIRRORED from OFFBAND_INFO_TAG_* in examples/companion_radio/
// OffbandConfigProtocol.h, which is companion-only and pulls Arduino. Same device as
// InfoAdvert::kInfoTagContinue: MyMesh.cpp static_asserts each one against the registry, so
// a drift breaks the build rather than the behaviour.
constexpr uint8_t kPlanTagChanNotify      = 0x01;   // #606
constexpr uint8_t kPlanTagDisplayRotation = 0x02;   // #1990
constexpr uint8_t kPlanTagAckCapacity     = 0x03;   // #1001, the one record with a value
constexpr uint8_t kPlanTagDisplayControl  = 0x04;   // #2067
constexpr uint8_t kPlanTagLedControl      = 0x05;   // #2067

// One planned record. `has_value` rather than a length because every value carried in this
// frame is one byte and the contract keeps it that way: a value rides here only where it is
// one or two bytes, fixed for the build, and has no sub-code of its own to read it from.
struct InfoRecord {
  uint8_t tag;
  bool has_value;
  uint8_t value;      // meaningful only when has_value
};

// What a radio's own state says about each tag. Collected into a struct so the plan can be
// built in a test from values a host can set -- the point of the exercise, since the old
// gate was `#ifdef` and no host test can reach a preprocessor branch.
struct InfoPlanInputs {
  // PIN_BUZZER, a BUILD flag rather than a part that is fitted. Deliberately permissive:
  // an unpopulated buzzer on a QCC pyramid badge is meant to be soldered on. See the
  // longer note at the caps2 CHAN_NOTIFY bit.
  bool buzzer_fitted = false;
  // The UI's own answer, so a fixed-orientation display omits the tag rather than claiming
  // a capability it will refuse.
  bool supports_rotation = false;
  // main.cpp's answer: a display CAME UP, not a display class was compiled in. See
  // IndicatorCaps.h on why that distinction is load-bearing.
  bool has_display = false;
  // MainBoard::canControlLed().
  bool can_control_led = false;
  uint8_t ack_capacity = 0;
};

// Every tag this build can emit. ACK_CAPACITY is unconditional; the other four are each
// gated, so a plan holds between one and five records.
constexpr size_t kInfoPlanMax = 5;

// Builds the record plan IN ASCENDING TAG ORDER.
//
// THE ORDER OF THESE STATEMENTS IS THE WIRE ORDER. A new tag goes in at its numeric
// position, not at the end -- appending is the #2214 defect. The host test asserts the
// result ascends for every combination of inputs, so getting this wrong fails a test.
//
// Returns the number of records written to `out`, which must hold kInfoPlanMax.
inline size_t buildInfoPlan(const InfoPlanInputs& in, InfoRecord out[kInfoPlanMax]) {
  size_t n = 0;
  if (in.buzzer_fitted) {
    out[n++] = InfoRecord{kPlanTagChanNotify, false, 0};
  }
  if (in.supports_rotation) {
    out[n++] = InfoRecord{kPlanTagDisplayRotation, false, 0};
  }
  // #1001: carries its byte by the contract's one exception -- fixed for the build, with no
  // sub-code of its own to read it from. Unconditional: every build has an ACK table.
  out[n++] = InfoRecord{kPlanTagAckCapacity, true, in.ack_capacity};
  if (advertisesDisplayControl(in.has_display)) {
    out[n++] = InfoRecord{kPlanTagDisplayControl, false, 0};
  }
  if (advertisesLedControl(in.can_control_led)) {
    out[n++] = InfoRecord{kPlanTagLedControl, false, 0};
  }
  return n;
}

// How a plan's write ended. Four outcomes, because three of them need different answers
// from the caller and collapsing any two is how #2214 shipped.
enum class InfoPlanOutcome : uint8_t {
  // Every record at or above the cursor is on the wire.
  Complete,
  // Capacity ran out. NORMAL, and not an error: finish() writes the CONTINUE record naming
  // the tag to resume from, and the client asks again.
  Truncated,
  // Nothing fit at all. The caller MUST return an error rather than an advert: an empty
  // advert reads as "this device has no capabilities", and a CONTINUE here would name a tag
  // the device refuses again at the same capacity, looping a contract-following client
  // forever. See InfoAdvert::stalled().
  Stalled,
  // add() refused a record while there was still room for it. The only ways that happens
  // are a non-ascending tag, a zero tag, or a null value with a non-zero length -- all of
  // them bugs in the plan above, none of them anything the client did. The caller MUST
  // refuse: this is the state that shipped in 1.7.1, and it shipped precisely because the
  // false return was dropped on the floor and the advert went out one record short with
  // nothing saying so.
  Misordered,
};

// Writes a plan into an advert, skipping what a previous round already delivered.
//
// `cursor` is the tag to resume FROM, 0 on a first request. The filter lives here rather
// than in buildInfoPlan() so the plan always describes the DEVICE and the cursor only
// describes the CLIENT: the test can then assert the full plan ascends, and separately that
// any cursor yields a suffix of it.
//
// A cursor above every planned tag yields an empty, Complete advert. That is a client
// asking for records past the end of a list it was never given a CONTINUE for, and it is
// left as it was -- in scope for the contract, not for this fix.
inline InfoPlanOutcome writeInfoPlan(InfoAdvert& advert, const InfoRecord* plan, size_t n,
                                     uint8_t cursor) {
  if (plan == nullptr) return InfoPlanOutcome::Misordered;
  for (size_t i = 0; i < n; i++) {
    if (plan[i].tag < cursor) continue;          // already delivered
    const bool ok = plan[i].has_value ? advert.addByte(plan[i].tag, plan[i].value)
                                      : advert.addFlag(plan[i].tag);
    if (!ok) {
      // complete() is still true only when add() refused WITHOUT recording a truncation --
      // it reserves room for the CONTINUE record and sets the truncation flag whenever the
      // refusal was about space. So this branch is the plan bug, and the one below is the
      // ordinary end of a full buffer, with the resume cursor already recorded.
      if (advert.complete()) return InfoPlanOutcome::Misordered;
      break;
    }
  }
  if (advert.stalled()) return InfoPlanOutcome::Stalled;
  return advert.complete() ? InfoPlanOutcome::Complete : InfoPlanOutcome::Truncated;
}

}  // namespace offband
