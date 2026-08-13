// src/helpers/prefs/PrefsLayout.cpp -- Offband #627. See PrefsLayout.h for rationale.

#include "PrefsLayout.h"

namespace offband {

namespace {

// ---- HOW THESE NUMBERS WERE OBTAINED -- read before editing them ----------
//
// COMPUTE the lengths. Do NOT read them off the `// <offset>` comments in
// CommonCLI.cpp / DataStore.cpp, and do not derive them by hand.
//
// The length of a record is the sum of the byte counts its save/load sequence
// actually moves -- i.e. every `sizeof(_prefs.field)` resolved against the
// field's DECLARED TYPE. #665 shipped a table built by reading the offset
// comments instead: DataStore.cpp's comments treat `uint32_t gps_interval` as
// one byte, so every Path B offset after it is wrong by 3, and every Path B
// entry in this file was a length no firmware has ever written. Path A's
// comments happen to be correct, which is exactly why the error went unnoticed
// -- half the evidence agreed.
//
// The generator lives in the #665 issue thread; it parses the read sequence at
// a given tag or commit and sums it. Re-run it rather than hand-editing.

// ---- Path A (/com_prefs): computed at every release tag -------------------
//
//   upstream repeater-v1.10.0            -> 166
//            repeater-v1.11.0            -> 170
//            repeater-v1.12.0 .. v1.14.1 -> 290
//            repeater-v1.15.0            -> 291
//            repeater-v1.16.0            -> 293
//            repeater-v1.17.0            -> 295
//   Offband  v0.17.0 .. v0.19.0-rc1      -> 292
//            v1.0.0  .. v1.2.0           -> 294
//            v1.3.0                      -> 364
//
// Disjoint across releases. 296/298 additionally existed in unreleased Offband
// dev/bench builds; 295 collides with upstream 1.17.0 and is therefore NOT
// listed as Offband-unique -- it routes to the tiebreakers.
//
// SAFETY OF THE OLDER UPSTREAM LENGTHS -- verified by comparing the field ->
// offset MAP at each tag against 1.17.0's (comparing read INDEX instead is
// misleading: pad merges/splits shift the index without moving any field).
//
//   1.10.0 / 1.11.0 / 1.12.0 / 1.13.0 -- zero fields moved. 1.14.0 replaced a
//     3-byte zero pad at 121 with path_hash_mode@121 + loop_detect@122 + a
//     1-byte pad, so byte 124 onward realigns exactly. An older record reads
//     those two as 0 (the pad was memset to 0), which is their default.
//   1.14.1 -- ONE real move: rx_boosted_gain sat at byte 79 (a pad slot in
//     every other version) and 1.15.0 relocated it to 290. A 1.14.1 record read
//     with this table therefore LOSES that one setting -- byte 79 is skipped as
//     pad and offset 290 is past EOF, so it short-reads to the default. Nothing
//     is mis-assigned. This is exactly what upstream 1.17.0 does with the same
//     file: upstream has no length check at all, it just reads. We match it.
//
// That is the standard for this table: accepting a length must produce the same
// result upstream's own migration would. Refusing it does not protect the user,
// it factory-resets them.

constexpr size_t kCollidingLengthA = 295;

constexpr size_t kOffbandLengthsA[]  = {292, 294, 296, 298, 364};  // incl. bench-only 296/298
constexpr size_t kUpstreamLengthsA[] = {166, 170, 290, 291, 293};  // 295 handled separately

// ---- Path B (/new_prefs): companion record --------------------------------
//
// Path B is PURE APPEND from upstream 1.10.0 all the way through Offband
// v1.3.0 -- verified by comparing the field -> offset map at every tag: ZERO
// fields move. Every historical length is therefore a strict prefix of the
// current layout, and reading one with this table short-reads the missing
// fields to their caller-set defaults. That is precisely what upstream 1.17.0
// does, since upstream applies no length check at all.
//
// Upstream lengths (computed):
//   84   1.7.0 .. 1.10.0        91   1.12.0 / 1.13.0      93   1.14.1
//   85   1.11.0                 92   1.14.0              140   1.15.0 .. 1.17.0
//
// 140 is ALSO written by Offband v0.17.0 .. v1.2.0-beta1 -- Offband had appended
// nothing yet, so at that length the layouts are the same bytes and "upstream"
// is the correct table whichever wrote it.
//
// Offband append boundaries (computed at the commit that introduced each):
//   141  + radio_fem_rxgain @140                    (#298, bfa3fb74) [v1.2.0]
//   143  + caplog_enabled/caplog_level @141,142      (#435, 44dc242a)
//   148  + notify_scope @143 + button_actions @144..147 (#510/#509, 9de49edd)
//   150  + ui_led_enabled/ui_display_mode @148,149   (#542, dd20fabc) [v1.3.0]
//
// Note 144 is NOT a boundary: notify_scope and button_actions landed in ONE
// commit. Deriving the ladder by hand invents it; computing it does not.
//
// THE LENGTH ON DISK IS WRITTEN BY THE FIRMWARE THAT LAST CALLED savePrefs(),
// NOT BY THE FIRMWARE THAT IS RUNNING. A device that upgraded across several
// appends without the user ever changing a setting still holds the OLD, shorter
// record -- which is why every boundary above must be listed, not just the ones
// that coincide with a release tag.
//
// Do NOT relax this to `>= 141`. #627 removed exactly that rule because it
// swallowed a truncated Path A record (292 bytes) or a file with garbage
// appended and read it with the companion table.
//
// APPENDING A NEW FIELD? Add its new end-length here in the SAME PR. Miss it
// and every device that upgrades without re-saving factory-resets itself.

constexpr size_t kUpstreamLengthsB[] = {84, 85, 91, 92, 93, 140};
constexpr size_t kOffbandLengthsB[]  = {141, 143, 148, 150};

template <size_t N>
bool contains(const size_t (&arr)[N], size_t v) {
  for (size_t i = 0; i < N; ++i) {
    if (arr[i] == v) return true;
  }
  return false;
}

// Field-plausibility tiebreak for the ambiguous Path-A length.
//
//   byte 291 : fem_rxgain         (constrain 0..1) under Offband
//              flood_max_unscoped (default 64)     under upstream
//   byte 293 : flood_max_advert   (default 8)      under Offband
//              fem_rxgain         (constrain 0..1) under upstream
//
// Both tests are of the same shape and are the ONLY valid shape here: a value
// that exceeds a field's constrained boolean range proves the layout that
// would put a boolean there is impossible.
//
//   b291 > 1 -> cannot be Offband (fem_rxgain is clamped 0..1) -> Upstream
//   b293 > 1 -> cannot be Upstream (fem_rxgain is clamped 0..1) -> Offband
//
// An earlier version tested `b292 > 1 -> Offband`. That was UNSOUND and would
// have corrupted devices: under upstream b292 is flood_max_advert (default 8),
// so an upstream record with flood_max_unscoped set to 0 or 1 and the default
// advert limit gives b291<=1, b292=8 and was confidently mis-read as Offband.
// b292 carries a flood limit under BOTH layouts and discriminates nothing.
//
// If both tests fire the record fits neither layout -- corruption, not a tie.
bool contentDecides(const PrefsTail& t, PrefsLayout& out, bool& contradiction) {
  contradiction = false;
  out = PrefsLayout::Unknown;
  if (!t.valid) return false;

  const bool cannot_be_offband  = (t.b291 > 1);
  const bool cannot_be_upstream = (t.b293 > 1);

  if (cannot_be_offband && cannot_be_upstream) { contradiction = true; return false; }
  if (cannot_be_offband)  { out = PrefsLayout::Upstream; return true; }
  if (cannot_be_upstream) { out = PrefsLayout::Offband;  return true; }
  return false;  // both plausible: flood limits deliberately set to 0 or 1
}

}  // namespace

PrefsLayoutResult detectCommonPrefsLayout(const PrefsLayoutEvidence& ev) {
  PrefsLayoutResult r;

  // Guard first: a length claimed by both tables is a bug in the tables, and
  // silently preferring whichever branch is written first is how that bug
  // would reach a device. Refuse instead.
  if (contains(kOffbandLengthsA, ev.length) && contains(kUpstreamLengthsA, ev.length)) {
    r.layout = PrefsLayout::Unknown;
    r.reason = PrefsLayoutReason::LengthTableOverlap;
    return r;
  }

  // The known collision, handled BEFORE the unique-length tables so it can
  // never be short-circuited by a future table edit.
  if (ev.length == kCollidingLengthA) {
    if (!ev.tail.valid) {
      r.layout = PrefsLayout::Unknown;
      r.reason = PrefsLayoutReason::TailUnavailable;
      return r;
    }
    PrefsLayout decided = PrefsLayout::Unknown;
    bool contradiction = false;
    if (contentDecides(ev.tail, decided, contradiction)) {
      r.layout = decided;
      r.reason = PrefsLayoutReason::ContentFieldRange;
      return r;
    }
    if (contradiction) {
      r.layout = PrefsLayout::Unknown;
      r.reason = PrefsLayoutReason::ContentContradiction;
      return r;
    }
    // NVS corroboration. Only ever proves Offband: absence is meaningless
    // where the marker is unsupported (nRF52) and, even on ESP32, a stock node
    // simply never creates it -- so we require a positive.
    if (ev.nvs_marker_supported && ev.offband_nvs_marker) {
      r.layout = PrefsLayout::Offband;
      r.reason = PrefsLayoutReason::NvsMarker;
      return r;
    }
    r.layout = PrefsLayout::Unknown;
    r.reason = PrefsLayoutReason::AmbiguousLength;
    return r;
  }

  if (contains(kOffbandLengthsA, ev.length)) {
    r.layout = PrefsLayout::Offband;
    r.reason = PrefsLayoutReason::LengthUniqueOffband;
    return r;
  }
  if (contains(kUpstreamLengthsA, ev.length)) {
    r.layout = PrefsLayout::Upstream;
    r.reason = PrefsLayoutReason::LengthUniqueUpstream;
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

  if (contains(kUpstreamLengthsB, ev.length)) {
    // Includes 140, which Offband <= v1.2.0-beta1 also wrote. Offband had
    // appended nothing by then, so the layouts coincide and this is the correct
    // table whichever build wrote it.
    r.layout = PrefsLayout::Upstream;
    r.reason = PrefsLayoutReason::LengthUniqueUpstream;
    return r;
  }
  if (contains(kOffbandLengthsB, ev.length)) {
    r.layout = PrefsLayout::Offband;
    r.reason = PrefsLayoutReason::LengthUniqueOffband;
    return r;
  }

  // Every other length -- including anything merely LONGER than upstream's
  // end. An earlier version treated `> 137` as Offband, which would have
  // accepted a truncated Path-A record (e.g. 292 bytes) or a file with
  // garbage appended and read it with the companion table.
  r.layout = PrefsLayout::Unknown;
  r.reason = PrefsLayoutReason::UnknownLength;
  return r;
}

PrefsIdentity identifyLegacyPrefs(const PrefsLayoutEvidence& ev) {
  PrefsIdentity id;

  const bool looks_common = (ev.length == kCollidingLengthA) ||
                            contains(kOffbandLengthsA, ev.length) ||
                            contains(kUpstreamLengthsA, ev.length);
  const bool looks_companion = contains(kUpstreamLengthsB, ev.length) ||
                               contains(kOffbandLengthsB, ev.length);

  // The two families' length sets are disjoint by construction; if that ever
  // stops being true, refuse rather than pick.
  if (looks_common && looks_companion) {
    id.family = PrefsFamily::Unknown;
    id.layout = PrefsLayout::Unknown;
    id.reason = PrefsLayoutReason::LengthTableOverlap;
    return id;
  }

  if (looks_common) {
    id.family = PrefsFamily::Common;
    const PrefsLayoutResult r = detectCommonPrefsLayout(ev);
    id.layout = r.layout;
    id.reason = r.reason;
    return id;
  }
  if (looks_companion) {
    id.family = PrefsFamily::Companion;
    const PrefsLayoutResult r = detectCompanionPrefsLayout(ev);
    id.layout = r.layout;
    id.reason = r.reason;
    return id;
  }

  id.family = PrefsFamily::Unknown;
  id.layout = PrefsLayout::Unknown;
  id.reason = PrefsLayoutReason::UnknownLength;
  return id;
}

const char* toString(PrefsLayout v) {
  switch (v) {
    case PrefsLayout::Unknown:  return "unknown";
    case PrefsLayout::Upstream: return "upstream";
    case PrefsLayout::Offband:  return "offband";
  }
  return "invalid-layout";
}

const char* toString(PrefsFamily v) {
  switch (v) {
    case PrefsFamily::Unknown:   return "unknown";
    case PrefsFamily::Common:    return "common";
    case PrefsFamily::Companion: return "companion";
  }
  return "invalid-family";
}

const char* toString(PrefsLayoutReason v) {
  switch (v) {
    case PrefsLayoutReason::LengthUniqueOffband:  return "length-unique-offband";
    case PrefsLayoutReason::LengthUniqueUpstream: return "length-unique-upstream";
    case PrefsLayoutReason::ContentFieldRange:    return "content-field-range";
    case PrefsLayoutReason::NvsMarker:            return "nvs-marker-cw_boot";
    case PrefsLayoutReason::AmbiguousLength:      return "ambiguous-length";
    case PrefsLayoutReason::UnknownLength:        return "unknown-length";
    case PrefsLayoutReason::TailUnavailable:      return "tail-unavailable";
    case PrefsLayoutReason::ContentContradiction: return "content-contradiction";
    case PrefsLayoutReason::LengthTableOverlap:   return "length-table-overlap";
  }
  return "invalid-reason";
}

}  // namespace offband
