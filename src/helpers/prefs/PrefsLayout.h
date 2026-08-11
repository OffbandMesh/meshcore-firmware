// src/helpers/prefs/PrefsLayout.h -- Offband #627
//
// Legacy-prefs layout detection for the MeshCore 1.17.0 base update.
//
// WHY THIS EXISTS
// ---------------
// MeshCore 1.17.0 replaces the flat binary prefs record with a keyed
// /prefs.json (ConfigSerializer) and migrates once, automatically, on the
// first boot after the update. That migration reads the old binary record
// with UPSTREAM's byte offsets -- which are not Offband's. Offband placed
// radio_fem_rxgain at 291 (a deliberate 1.16.0-cycle decision, #126) where
// upstream has flood_max_unscoped, so the tail fields are permuted between
// the two writers:
//
//   offset:    290        291          292          293          294
//   Offband:   rx_gain    fem_rxgain   fld_unscop   fld_advert   ui_led ...
//   Upstream:  rx_gain    fld_unscop   fld_advert   fem_rxgain   cad_enabled
//
// Reading one with the other's table silently mis-assigns values that then
// get persisted into /prefs.json on boot one. Nothing crashes; every wrong
// value is a small int that passes constrain(). So before migrating we must
// decide WHICH writer produced the file.
//
// Two upgrade paths exist and they need different readers:
//   * stock MeshCore (<1.17.0) -> Offband : read with UPSTREAM offsets
//   * Offband (<MC 1.17.0)     -> Offband : read with OFFBAND offsets
//
// THE SIGNALS, IN PRIORITY ORDER
// ------------------------------
// 1. RECORD LENGTH. Across RELEASED versions the two sets are disjoint:
//        Offband  {292, 294, 364}   upstream {291, 293, 295}
//    so length alone settles every released build. (Verified by extracting
//    the record end offset at every offband-v* and companion-v1.1[567].0
//    tag.) Unreleased Offband dev/bench builds also passed through 295, 296
//    and 298 -- 295 collides with upstream 1.17.0, hence signals 2 and 3.
//
// 2. FIELD PLAUSIBILITY on the contested bytes. At the ambiguous length the
//    two interpretations disagree about which byte is a boolean and which is
//    a flood limit:
//        byte 291 is fem_rxgain (constrain 0..1) under Offband
//                  but flood_max_unscoped (default 64) under upstream
//    so a value > 1 at 291 cannot be an Offband record. This works on EVERY
//    platform -- it is pure content, no NVS, no filesystem.
//
// 3. NVS MARKER (ESP32 only, corroboration). Offband opens NVS namespace
//    "cw_boot" from heartbeatBegin(), reached via crashLogStandardInit()
//    which every role's main.cpp calls at boot, gated only by
//    OFFBAND_CRASHLOG_ESP32 (auto-defined for all ESP32 builds, no env opts
//    out). Upstream opens NO NVS namespace anywhere. So its presence proves
//    an Offband boot. heartbeatBegin() is a no-op on nRF52, so this signal
//    is absent there -- which is why signal 2 carries the platform-neutral
//    weight and this one only corroborates.
//
// 4. FAIL CLOSED. If the layout is still undetermined we do NOT migrate and
//    we do NOT fall back to either table -- falling back to "upstream" was an
//    earlier proposal and is itself a corruption path for a truncated Offband
//    record. The caller must leave the legacy file untouched, log at error
//    level, and boot on defaults so an operator intervenes (SAFELANE 6).
//
// This header is deliberately free of Arduino/ESP-IDF dependencies so the
// decision logic is unit-testable on the host. Reading the file length, the
// tail bytes and the NVS marker is the caller's job; this only decides.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace offband {

enum class PrefsLayout : uint8_t {
  Unknown  = 0,  // undetermined -- caller MUST NOT migrate
  Upstream = 1,  // stock MeshCore wrote this record
  Offband  = 2,  // Offband wrote this record
};

// Why a decision was reached. Surfaced so the caller can log the deciding
// evidence rather than just the verdict (SAFELANE 6: no silent choices).
enum class PrefsLayoutReason : uint8_t {
  LengthUniqueOffband  = 0,  // length matches only Offband releases
  LengthUniqueUpstream = 1,  // length matches only upstream releases
  ContentFieldRange    = 2,  // ambiguous length; contested byte settled it
  NvsMarker            = 3,  // ambiguous length; cw_boot corroborated Offband
  AmbiguousLength      = 4,  // collision, and no other signal resolved it
  UnknownLength        = 5,  // matches no known release (truncated/corrupt?)
  TailUnavailable      = 6,  // record too short to hold the contested bytes
};

// Bytes 290..294 of the record -- the contested window. `valid` is false when
// the record is too short to contain them.
struct PrefsTail {
  bool    valid = false;
  uint8_t b290 = 0, b291 = 0, b292 = 0, b293 = 0, b294 = 0;
};

struct PrefsLayoutEvidence {
  size_t    length = 0;                 // record length in bytes
  PrefsTail tail;                       // contested window, if readable
  bool      offband_nvs_marker = false; // ESP32: NVS "cw_boot" exists
  bool      nvs_marker_supported = false; // false on nRF52 -- absence proves nothing
};

struct PrefsLayoutResult {
  PrefsLayout       layout = PrefsLayout::Unknown;
  PrefsLayoutReason reason = PrefsLayoutReason::UnknownLength;
};

// Path A: the CommonCLI record (/com_prefs, /node_prefs) used by repeater,
// room-server and sensor roles.
PrefsLayoutResult detectCommonPrefsLayout(const PrefsLayoutEvidence& ev);

// Path B: the companion record (/new_prefs, /node_prefs). Offband appends
// strictly after upstream's end, so a wrong read here loses the appended
// fields rather than mis-assigning -- but it is still a wrong read.
PrefsLayoutResult detectCompanionPrefsLayout(const PrefsLayoutEvidence& ev);

// Human-readable forms, for the mandatory log line.
const char* toString(PrefsLayout v);
const char* toString(PrefsLayoutReason v);

}  // namespace offband
