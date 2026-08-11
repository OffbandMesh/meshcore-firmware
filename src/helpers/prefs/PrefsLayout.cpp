// src/helpers/prefs/PrefsLayout.cpp -- Offband #627. See PrefsLayout.h for rationale.

#include "PrefsLayout.h"

namespace offband {

namespace {

// ---- Path A (/com_prefs): record lengths at RELEASED versions -------------
//
// Derived by extracting the record end offset at every release tag, not from
// dev-tree commits (the tree passed through lengths no release ever shipped).
//
//   Offband  v0.17.0 .. v0.19.0-rc1 -> 292
//            v1.0.0  .. v1.2.0      -> 294
//            v1.3.0                 -> 364
//   upstream companion-v1.15.0      -> 291
//            companion-v1.16.0      -> 293
//            companion-v1.17.0      -> 295
//
// Disjoint across releases. 295/296/298 additionally existed in unreleased
// Offband dev/bench builds; 295 is the one that collides with upstream.

constexpr size_t kOffbandLengthsA[]  = {292, 294, 364};
constexpr size_t kUpstreamLengthsA[] = {291, 293, 295};
// Offband-only dev/bench lengths with no upstream counterpart. 295 is
// deliberately EXCLUDED -- it is the collision and must go to the tiebreakers.
constexpr size_t kOffbandBenchOnlyA[] = {296, 298};

// ---- Path B (/new_prefs): companion record --------------------------------
//
// Upstream's record ends at 137 (after default_scope_key @121). Offband
// appends radio_fem_rxgain(137), caplog(138,139), notify_scope(140),
// button_actions[4](141..144), ui_led_enabled(145), ui_display_mode(146),
// ending at 147. Anything longer than upstream's end is necessarily Offband.

constexpr size_t kUpstreamLengthB = 137;
constexpr size_t kOffbandLengthB  = 147;

template <size_t N>
bool contains(const size_t (&arr)[N], size_t v) {
  for (size_t i = 0; i < N; ++i) {
    if (arr[i] == v) return true;
  }
  return false;
}

// Field-plausibility tiebreak for the ambiguous Path-A length.
//
//   byte 291 : fem_rxgain   (constrain 0..1) under Offband
//              flood_max_unscoped (default 64) under upstream
//   byte 292 : flood_max_unscoped (default 64) under Offband
//              flood_max_advert   (default 8)  under upstream
//
// A value > 1 at 291 cannot be an Offband record -- fem_rxgain is clamped to
// 0..1 on every load and save. Symmetrically, 291 <= 1 together with 292 > 1
// fits Offband (a boolean then a flood limit) and not upstream (which would
// require flood_max_unscoped <= 1, i.e. unscoped flooding all but disabled).
//
// Inconclusive when both bytes are <= 1: that is a node whose flood limits
// were deliberately set to 0 or 1, and the two layouts become
// indistinguishable by content alone.
bool contentSaysUpstream(const PrefsTail& t, bool& conclusive) {
  conclusive = false;
  if (!t.valid) return false;
  if (t.b291 > 1) { conclusive = true; return true; }   // flood limit -> upstream
  if (t.b292 > 1) { conclusive = true; return false; }  // bool then limit -> Offband
  return false;                                          // both <= 1: cannot tell
}

}  // namespace

PrefsLayoutResult detectCommonPrefsLayout(const PrefsLayoutEvidence& ev) {
  PrefsLayoutResult r;

  // 1. Length unique to one side.
  if (contains(kOffbandLengthsA, ev.length) || contains(kOffbandBenchOnlyA, ev.length)) {
    r.layout = PrefsLayout::Offband;
    r.reason = PrefsLayoutReason::LengthUniqueOffband;
    return r;
  }
  if (contains(kUpstreamLengthsA, ev.length) && ev.length != 295) {
    r.layout = PrefsLayout::Upstream;
    r.reason = PrefsLayoutReason::LengthUniqueUpstream;
    return r;
  }

  // 2. The collision (295): upstream 1.17.0 vs an unreleased Offband build.
  if (ev.length == 295) {
    if (!ev.tail.valid) {
      r.layout = PrefsLayout::Unknown;
      r.reason = PrefsLayoutReason::TailUnavailable;
      return r;
    }
    bool conclusive = false;
    const bool upstream = contentSaysUpstream(ev.tail, conclusive);
    if (conclusive) {
      r.layout = upstream ? PrefsLayout::Upstream : PrefsLayout::Offband;
      r.reason = PrefsLayoutReason::ContentFieldRange;
      return r;
    }
    // 3. NVS corroboration. Only ever proves Offband: absence is meaningless
    //    where the marker is unsupported (nRF52) and, even on ESP32, a stock
    //    node simply never creates it -- so we require a positive.
    if (ev.nvs_marker_supported && ev.offband_nvs_marker) {
      r.layout = PrefsLayout::Offband;
      r.reason = PrefsLayoutReason::NvsMarker;
      return r;
    }
    // 4. Fail closed.
    r.layout = PrefsLayout::Unknown;
    r.reason = PrefsLayoutReason::AmbiguousLength;
    return r;
  }

  // Unrecognised length: truncated write, corruption, or a build we do not
  // know about. Never guess -- guessing here is the corruption path.
  r.layout = PrefsLayout::Unknown;
  r.reason = PrefsLayoutReason::UnknownLength;
  return r;
}

PrefsLayoutResult detectCompanionPrefsLayout(const PrefsLayoutEvidence& ev) {
  PrefsLayoutResult r;

  if (ev.length == kOffbandLengthB) {
    r.layout = PrefsLayout::Offband;
    r.reason = PrefsLayoutReason::LengthUniqueOffband;
    return r;
  }
  if (ev.length == kUpstreamLengthB) {
    r.layout = PrefsLayout::Upstream;
    r.reason = PrefsLayoutReason::LengthUniqueUpstream;
    return r;
  }
  // Offband appended strictly after upstream's end, so anything beyond it is
  // an Offband record -- an intermediate length is a build that carried some
  // but not all of the appended fields.
  if (ev.length > kUpstreamLengthB) {
    r.layout = PrefsLayout::Offband;
    r.reason = PrefsLayoutReason::LengthUniqueOffband;
    return r;
  }

  // Shorter than upstream's end: an older record predating fields on either
  // side. Both readers short-read to EOF and keep defaults, so this is not a
  // mis-assignment hazard -- but it is still not a determination.
  r.layout = PrefsLayout::Unknown;
  r.reason = PrefsLayoutReason::UnknownLength;
  return r;
}

const char* toString(PrefsLayout v) {
  switch (v) {
    case PrefsLayout::Upstream: return "upstream";
    case PrefsLayout::Offband:  return "offband";
    default:                    return "unknown";
  }
}

const char* toString(PrefsLayoutReason v) {
  switch (v) {
    case PrefsLayoutReason::LengthUniqueOffband:  return "length-unique-offband";
    case PrefsLayoutReason::LengthUniqueUpstream: return "length-unique-upstream";
    case PrefsLayoutReason::ContentFieldRange:    return "content-field-range";
    case PrefsLayoutReason::NvsMarker:            return "nvs-marker-cw_boot";
    case PrefsLayoutReason::AmbiguousLength:      return "ambiguous-length";
    case PrefsLayoutReason::TailUnavailable:      return "tail-unavailable";
    default:                                      return "unknown-length";
  }
}

}  // namespace offband
