#pragma once

// Per-channel notification mode, as stored on the channel record (Offband #606).
//
// One pref per channel, on the board. The client (0xC5 CHNOTIFY) and the device's own
// UI both write THIS byte -- there is no second store. That is the owner's ruling,
// 2026-11-02: "the variant/board has a stored channel pref ... exposing a way for the
// GUI or for client to set it shouldn't result in 2 separate prefs for the same thing."
//
// It lives in the /channels2 record's spare field, beside the tune (#1523), at index 1.
// Taking a second spare byte changes no offset: the field has always been 4 bytes on the
// wire, the stride stays 68, DataStore::hasChannels's hardcoded 4 + 32 + 32 stays right,
// and an older build reads the file exactly as it always did.
//
// This header is deliberately free of Arduino and of NodePrefs.h, so the whole decision
// is unit-testable on the host.

#include <stdint.h>
#include <stddef.h>

#include <helpers/ui/TuneSlots.h>   // the spare-field layout and the tune's writer

namespace offband {

// ---- the stored byte -------------------------------------------------------------
//
// 1-BASED, AND 0 IS NOT "ALL". The spare field is zero on every record written before
// this feature and on every fresh channel, so 0 has to mean "no choice made" -- an
// encoding where 0 meant All would silently set every channel in the field to All. The
// tune byte in the same field is 1-based for exactly this reason.
//
// Keep these equal to OFFBAND_CHNOTIFY_* in OffbandConfigProtocol.h, which states the
// same values as the wire contract. MyMesh.cpp static_asserts that the two agree.
constexpr uint8_t kChanNotifyUnset    = 0;   // inherit the device-wide scope
constexpr uint8_t kChanNotifyAll      = 1;
constexpr uint8_t kChanNotifyMentions = 2;
constexpr uint8_t kChanNotifyOff      = 3;
constexpr uint8_t kChanNotifyCount    = 4;   // a valid stored value is < this

inline bool channelNotifySettable(uint8_t stored) { return stored < kChanNotifyCount; }

// ---- the device-wide scope, mirrored ---------------------------------------------
//
// Mirrored rather than included: the scope lives in NodePrefs.h, which is companion-only
// and pulls Arduino, and this header stays host-testable. MyMesh.cpp carries
// static_asserts that these equal NOTIFY_SCOPE_*; if one side moves, the build breaks
// rather than the behaviour drifting.
//
// #606 adds NO scope value. "Inherit" is a CLIENT concept -- a channel row in the app
// that follows the device setting rather than carrying its own. The firmware is the
// authority, so there is nothing for it to inherit from; the device expresses the same
// idea by a per-channel byte that is not set, and the client's Inherit row writes that.
constexpr uint8_t kScopeAll  = 0;
constexpr uint8_t kScopeSelf = 1;
constexpr uint8_t kScopeNone = 2;

// ---- where it sits in the record -------------------------------------------------
constexpr size_t kChannelNotifySpareIndex = 1;
static_assert(kChannelNotifySpareIndex < kChannelSpareBytes,
              "the notify mode must fit the spare field");
static_assert(kChannelNotifySpareIndex != kChannelTuneSpareIndex,
              "the notify mode must not land on the tune's byte");

// The stored mode out of a record's spare field.
inline uint8_t channelNotifyFromSpare(const uint8_t* spare) {
  if (spare == nullptr) return kChanNotifyUnset;
  const uint8_t stored = spare[kChannelNotifySpareIndex];
  // An unreadable value reads as unset rather than being trusted. Unlike the tune --
  // where an unresolvable id is preserved byte-for-byte because a later build may know
  // it -- the mode space is closed at four values and will not grow a meaning later, so
  // there is nothing to preserve and a wrong byte must not decide whether a radio sounds.
  return channelNotifySettable(stored) ? stored : kChanNotifyUnset;
}

// The spare field as it goes back to the file, carrying BOTH residents.
//
// One writer for the whole field, deliberately. `channelTuneToSpare` zeroes all four
// bytes before writing the tune -- correct on its own, and a second writer called before
// it would be silently erased. Routing both through here means the order cannot be got
// wrong at a call site.
inline void channelSpareToFile(uint8_t tune_stored, uint8_t notify_stored, uint8_t* spare) {
  channelTuneToSpare(tune_stored, spare);   // zeroes the field, then writes the tune
  if (spare == nullptr) return;
  spare[kChannelNotifySpareIndex] = notify_stored;
}

// ---- what the radio should do -----------------------------------------------------
//
// Returns the effective mode for a channel message: kChanNotifyAll, kChanNotifyMentions
// or kChanNotifyOff. Never returns Unset -- the caller gets a decision, not a lookup.
//
// A CHANNEL THAT HAS BEEN SET DECIDES FOR ITSELF. There is no second switch to find and
// flip: a client writes a channel's mode and the radio obeys it. An earlier draft made
// the scope a master switch that ignored these unless a fourth "Inherit" scope was
// selected, which meant a client could set a channel to Off and nothing happened until
// the user discovered and changed a separate device setting -- the same "why will this
// channel not shut up" complaint the feature exists to end.
//
// A channel that has NOT been set falls back to the device scope, which is what the
// scope is for: the default for everything nobody has touched.
//
// NONE IS ABSOLUTE, and is the one exception. A user who sets the device to Off is
// asking for silence, not for a negotiation, and a channel still sounding through it is
// the worst failure this feature has. ALL and SELF are defaults; NONE is a mute.
inline uint8_t resolveChannelNotify(uint8_t scope, uint8_t stored) {
  if (scope == kScopeNone) return kChanNotifyOff;        // absolute, overrides a channel
  if (channelNotifySettable(stored) && stored != kChanNotifyUnset) return stored;
  return (scope == kScopeSelf) ? kChanNotifyMentions : kChanNotifyAll;
}

// Should a channel message sound, given the resolved mode and whether it @[mentions] us?
inline bool channelNotifyAlerts(uint8_t resolved, bool mentions_self) {
  switch (resolved) {
    case kChanNotifyOff:      return false;
    case kChanNotifyMentions: return mentions_self;
    case kChanNotifyAll:
    default:                  return true;
  }
}

// What a channel's mode becomes when the app overwrites the slot.
//
// CMD_SET_CHANNEL carries a name and a 16-byte secret and nothing else, so the handler
// builds a fresh record and an edit from the phone keeps only what is carried over
// deliberately. Without this, renaming a channel from the phone would silently clear the
// mode set on the device.
//
// Carried only when the slot still holds the SAME CHANNEL, judged by the secret rather
// than by the slot being occupied: slots are a fixed table the app reuses, so "the slot
// was not empty" is also true when channel A has been replaced by channel B -- and B
// would then inherit a mode chosen for a channel that is gone. A re-key counts as a
// different channel; a rename, which is what people actually do, keeps its mode.
//
// This matches the tune's rule (channelTuneAfterEdit) and is DELIBERATELY stricter than
// the GUI's retired per-channel array (#1457), which keyed off occupancy alone and so
// did carry a mode across a replacement. Behaviour change for GUI boards, recorded on
// #606: a replacement now drops the mode instead of handing it to a different channel.
inline uint8_t channelNotifyAfterEdit(bool was_empty, bool same_channel, uint8_t before) {
  if (was_empty) return kChanNotifyUnset;       // a fresh channel inherits the scope
  return same_channel ? before : kChanNotifyUnset;
}

// What a rebuilt Public record carries in from the slot Public came from.
//
// placePublicInSlot0 does not MOVE Public's record into slot 0, it builds a fresh one
// from the known key and then copies forward what belongs to the channel rather than to
// the slot. Public is the one channel a user cannot delete, so the mode and the tune they
// chose for it have to survive a shuffle they never asked for and cannot see.
//
// BOTH FIELDS MOVE TOGETHER, and the signature is what enforces it.
//
// They already failed to once: #1625 wrote the tune's carry as a bare line at the call
// site, #606 added a second resident to the same spare field, and the notify byte was
// simply not copied -- so a user who silenced Public had it sound again at the next boot
// that re-placed the slot.
//
// WRITTEN THROUGH OUT-PARAMETERS, not returned, which is the point. A third resident in
// that field becomes a third required argument, so a call site that has not been taught
// about it FAILS TO COMPILE. Returning a struct -- or a tuple -- would leave the
// assignment at the call site optional, and an optional assignment is exactly the thing
// that was missed the first time. This also matches channelSpareToFile above, which takes
// the field it writes for the same reason.
inline void carryChannelPrefs(bool had_prior_public,
                              uint8_t prior_tune, uint8_t prior_notify,
                              uint8_t* out_tune, uint8_t* out_notify) {
  if (out_tune == nullptr || out_notify == nullptr) return;
  // No prior Public means there is nothing to carry. Unset for both, so the channel takes
  // the device scope rather than an invented All.
  *out_tune   = had_prior_public ? prior_tune   : kChannelTuneUnset;
  *out_notify = had_prior_public ? prior_notify : kChanNotifyUnset;
}

}  // namespace offband
