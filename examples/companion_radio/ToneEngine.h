#pragma once

// #1788: tunes belong to the buzzer.
//
// The tune table, the per-event choices, a channel's own tune and the eight custom slots
// used to live inside the badge's UI, so only a board running the badge's screens could
// resolve them -- though every board with a piezo drives the same genericBuzzer and the
// same RTTTL player. This is that machinery with the badge taken out of it: any UI that
// owns a buzzer holds one of these, plays through it, and hands it to the mesh layer
// through AbstractUITask::toneEngine(), which is how a client reaches the same settings.
//
// A UI that holds one plays every event through it, so the client bit that advertises
// tunes is derived from toneEngine() being non-null. A UI with a buzzer that still plays fixed
// melodies returns null and advertises nothing: a client must never be offered a control
// the board would ignore.
//
// Header-only and PIN_BUZZER-gated, so a board with no buzzer compiles none of it.

#ifdef PIN_BUZZER

#include <helpers/ui/buzzer.h>
#include <helpers/ui/BuzzerPriority.h>
#include <helpers/ui/Tunes.h>
#include <helpers/ui/TuneSlots.h>
#include <helpers/ui/SlotAssembly.h>
#include "NodePrefs.h"
#include "ToneSlots.h"
#include "TuneSlotStore.h"

namespace offband {

class ToneEngine {
  genericBuzzer* _buzzer = nullptr;
  DataStore* _store = nullptr;
  NodePrefs* _prefs = nullptr;
  // #1478: a tune pushed from the app arrives in chunks. Held here rather than in MyMesh,
  // which every companion variant compiles, so only a board with a buzzer pays for it.
  SlotAssembly<kMaxRtttl> _push;

public:
  void begin(genericBuzzer* buzzer, DataStore* store, NodePrefs* prefs) {
    _buzzer = buzzer;
    _store = store;
    _prefs = prefs;
  }

  // Plays any tune by id: a builtin, or a custom slot. The ENTIRE surface a caller needs:
  // it honors the quiet pref and the priority rule, and does nothing for an id this build
  // cannot resolve.
  TonePlay playId(uint8_t id, TonePriority priority) {
    if (_buzzer == nullptr) return TonePlay::NotFound;
    // Silent is a CHOICE: it sounds like nothing and that is success.
    if (id == kTuneSilent) return TonePlay::Played;
    // #1475: a custom slot. Asked BEFORE reading the file, because staging overwrites the
    // one buffer the player may still be walking -- if play() refused after the read, the
    // tune still sounding would be left pointing at the new tune's bytes.
    if (isSlotId(id)) {
      if (!_buzzer->wouldTake(priority)) return TonePlay::RefusedBusy;
      // Stop BEFORE staging: staging writes the shared buffer as it reads and validates
      // only afterwards, so a corrupt slot would otherwise leave a sounding tune walking
      // half-overwritten bytes.
      _buzzer->stop();
      const char* staged = stageSlotForPlayback(_store, slotIndex(id));
      // Empty, unreadable, or refused by the validator: all three resolve to nothing.
      if (staged == nullptr) return TonePlay::NotFound;
      // No loop() runs between the stage above and the play below, so the player never
      // reads a half-written buffer. That is the invariant, not the stop.
      return _buzzer->play(staged, priority);
    }
    const char* melody = tuneRtttl(id);
    // An id this build cannot resolve -- past the table after a downgrade, or a slot id
    // from a build with more slots. A remote trigger needs that told apart from played.
    if (melody == nullptr) return TonePlay::NotFound;
    return _buzzer->play(melody, priority);
  }

  // An audition. Preview priority, so it can never cut off a real notification. NotFound
  // is folded into Played: a row that makes no sound is not something to report to the
  // person choosing; the trigger is the caller that needs the distinction.
  TonePlay preview(uint8_t id) {
    const TonePlay r = playId(id, TonePriority::Preview);
    return r == TonePlay::NotFound ? TonePlay::Played : r;
  }

  // #1477: a string already staged in the playback buffer -- the editor auditioning what
  // has been typed.
  TonePlay playStaged(const char* staged) {
    if (_buzzer == nullptr) return TonePlay::NotFound;
    return _buzzer->play(staged, TonePriority::Preview);
  }

  void stop() {
    if (_buzzer != nullptr) _buzzer->stop();
  }

  // The tune an event plays: the stored choice, else this board's compiled default --
  // which, for every event, is exactly the melody the UIs used to hard-code.
  void playEvent(ToneEvent e) { playId(effectiveTone(_prefs, e), TonePriority::Event); }

  // #1524: a channel message. `stored` is the channel's tune byte as /channels2 holds it;
  // a channel that has chosen nothing inherits the event's tune.
  void playChannel(uint8_t stored) {
    playId(resolveChannelTune(stored, effectiveTone(_prefs, ToneEvent::ChannelMessage)),
           TonePriority::Event);
  }

  // Whether the picker would offer this id: a named builtin, or a slot that holds a tune.
  // Every writer of a tune choice -- the screens and the client -- answers to this.
  bool listed(uint8_t id) { return tuneIsListed(_store, id); }

  // ---- an event's tune, for the client (#1788) ----------------------------------------

  bool eventTone(uint8_t ev, uint8_t& stored, uint8_t& effective) const {
    if (ev >= (uint8_t)ToneEvent::Count) return false;
    stored = storedTone(_prefs, (ToneEvent)ev);
    effective = effectiveTone(_prefs, (ToneEvent)ev);
    return true;
  }

  // Stores a choice the picker would offer, and nothing else. The caller saves prefs:
  // only it knows how. Returns false for a refused id or event.
  bool setEventTone(uint8_t ev, uint8_t id) {
    if (ev >= (uint8_t)ToneEvent::Count) return false;
    if (!tuneIdOffered(id) || !listed(id)) return false;
    return setStoredTone(_prefs, (ToneEvent)ev, id);
  }

  // ---- the custom slots, for the client (#1478, #1788) --------------------------------

  // Returns one of the SlotAssembly results as a uint8_t, plus the bytes accepted so far.
  uint8_t feedSlot(uint8_t slot, uint8_t offset, bool more, const uint8_t* bytes, size_t len,
                   uint8_t& accepted) {
    using Res = SlotAssembly<kMaxRtttl>::Result;
    accepted = 0;
    if (slot >= kSlotCount) return (uint8_t)Res::BadSlot;
    const Res r = _push.feed(slot, offset, more, bytes, len);
    accepted = (uint8_t)_push.length();
    if (r != Res::Complete) return (uint8_t)r;
    // Committed. The SAME store call a typed tune goes through, so the same validator and
    // the same refusals apply -- an empty string clears the slot.
    const bool ok = saveSlotRtttl(_store, slot, _push.text());
    _push.reset();
    accepted = 0;
    // A store refusal is reported as a bad byte: a string the validator rejects, one over
    // the length, or a failed write all have the same remedy -- look at what was sent.
    return ok ? (uint8_t)Res::Complete : (uint8_t)Res::BadByte;
  }

  // #1478: the badge's own editor abandons a push in flight when it saves that slot, so a
  // stale commit cannot overwrite what the person just typed.
  void cancelPush() { _push.reset(); }

  // A slot's length and derived name. Through the CALLER's buffer, never the playback
  // stage: answering a client must not disturb a tune that is sounding.
  bool slotInfo(uint8_t slot, uint8_t& len, char* name, size_t name_cap) const {
    len = 0;
    if (name != nullptr && name_cap > 0) name[0] = 0;
    if (slot >= kSlotCount) return false;
    char tune[kMaxRtttl + 1];
    const size_t n = readSlotRtttl(_store, slot, tune, sizeof(tune));
    len = (uint8_t)(n > 255 ? 255 : n);
    if (n > 0 && name != nullptr) deriveSlotName(tune, name, name_cap);
    return true;
  }

  // A slot's whole tune, for a client that wants to edit rather than replace it.
  size_t slotText(uint8_t slot, char* out, size_t out_cap) const {
    if (slot >= kSlotCount) return 0;
    return readSlotRtttl(_store, slot, out, out_cap);
  }
};

}  // namespace offband

#endif  // PIN_BUZZER
