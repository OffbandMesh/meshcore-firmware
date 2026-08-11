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
// Offband dev/bench builds; 295 is the one that collides with upstream 1.17.0
// and is therefore NOT listed as Offband-unique -- it routes to the tiebreakers.

constexpr size_t kCollidingLengthA = 295;

constexpr size_t kOffbandLengthsA[]  = {292, 294, 296, 298, 364};  // incl. bench-only 296/298
constexpr size_t kUpstreamLengthsA[] = {291, 293};                 // 295 handled separately

// ---- Path B (/new_prefs): companion record --------------------------------
//
// Upstream: 1.15.0 / 1.16.0 / 1.17.0 all end at 137 (after default_scope_key
// @121, 16 bytes). Offband: <= v1.1.2 also 137 -- byte-identical, nothing
// appended yet -- then v1.2.0 -> 138 (radio_fem_rxgain @137) and v1.3.0 -> 147
// (caplog 138/139, notify_scope 140, button_actions 141..144, ui_led 145,
// ui_display_mode 146).
//
// 137 is shared, and that is harmless: at that length the two layouts are the
// same bytes, so reading it with upstream's table is correct whoever wrote it.

constexpr size_t kUpstreamLengthB   = 137;
constexpr size_t kOffbandLengthsB[] = {138, 147};

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

  if (ev.length == kUpstreamLengthB) {
    // Shared length, but the layouts coincide there, so this is correct
    // whichever build wrote it.
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
  const bool looks_companion = (ev.length == kUpstreamLengthB) ||
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
