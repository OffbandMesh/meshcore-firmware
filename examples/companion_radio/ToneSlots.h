#pragma once

// #1421: the one place the event-to-pref mapping lives.
//
// #1420 put a read-only version of this table inside UITask.cpp, which was fine while
// only `notify()` needed it. The picker screen has to WRITE the same slots, and a second
// copy of the table is how the two drift apart -- so there is one table, as
// pointers-to-member, usable for both reading and writing and const-correct either way.
//
// A pointer-to-member rather than a pointer-to-byte so the table itself is `constexpr`
// and lives in flash, with no dependence on any particular NodePrefs instance.

#include <helpers/ui/Tunes.h>
#include "NodePrefs.h"

namespace offband {

// The pref member holding this event's chosen tune, or nullptr for an event with no
// slot. The static_assert makes a forgotten event a COMPILE error, which is the only
// enforcement available here: `arduino_base` builds with `-w`, so a -Wswitch warning on
// a switch-based version would never be seen by anyone.
//
// The array order IS the ToneEvent order. That coupling is the cost of the shape, and
// it is what the assertion guards.
inline uint8_t NodePrefs::*toneMember(ToneEvent e) {
  static constexpr uint8_t NodePrefs::*kSlots[] = {
      &NodePrefs::ui_tone_dm,          // ContactMessage
      &NodePrefs::ui_tone_channel,     // ChannelMessage
      &NodePrefs::ui_tone_room,        // RoomMessage
      &NodePrefs::ui_tone_newcontact,  // NewContact
      &NodePrefs::ui_tone_ack,         // Ack
      &NodePrefs::ui_tone_sent,        // Sent
  };
  constexpr size_t kCount = sizeof(kSlots) / sizeof(kSlots[0]);
  static_assert(kCount == (size_t)ToneEvent::Count,
                "every ToneEvent needs a pref slot, in enum order");
  const size_t i = (size_t)e;
  return i < kCount ? kSlots[i] : nullptr;
}

// The id stored for this event, or kTuneUnset when nothing is stored -- which is also
// the answer for absent prefs or an event with no slot. Unset means "use this board's
// compiled default", so every can't-tell case lands on the behavior the firmware had
// before prefs existed rather than on silence.
inline uint8_t storedTone(const NodePrefs* p, ToneEvent e) {
  if (p == nullptr) return kTuneUnset;
  uint8_t NodePrefs::*const m = toneMember(e);
  return m == nullptr ? kTuneUnset : p->*m;
}

// The id this event will actually play: the stored choice, else the compiled default.
inline uint8_t effectiveTone(const NodePrefs* p, ToneEvent e) {
  return resolveTune(storedTone(p, e), e);
}

// Stores a choice. Returns false when there is no slot to write, so a caller cannot
// believe it saved something it did not.
inline bool setStoredTone(NodePrefs* p, ToneEvent e, uint8_t id) {
  if (p == nullptr) return false;
  uint8_t NodePrefs::*const m = toneMember(e);
  if (m == nullptr) return false;
  p->*m = id;
  return true;
}

// The six events in the order the Tones screen lists them, with the words the owner
// approved on 2026-10-14: "Tones" for the row, "DM" for a direct message.
struct ToneEventRow {
  ToneEvent event;
  const char* label;
};

constexpr ToneEventRow kToneEventRows[] = {
    {ToneEvent::ContactMessage, "DM"},
    {ToneEvent::ChannelMessage, "Channel"},
    {ToneEvent::RoomMessage, "Room"},
    {ToneEvent::NewContact, "New contact"},
    {ToneEvent::Ack, "Ack"},
    {ToneEvent::Sent, "Sent"},
};

constexpr int kToneEventRowCount = (int)(sizeof(kToneEventRows) / sizeof(kToneEventRows[0]));
static_assert(kToneEventRowCount == (int)ToneEvent::Count,
              "every ToneEvent needs a row, or the screen hides one");

}  // namespace offband
