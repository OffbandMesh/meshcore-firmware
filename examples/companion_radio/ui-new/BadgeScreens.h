#pragma once

// #1230: the badge's screens from the owner's design (the QCC mockups, sections 1a,
// 1c, 1e and 2a). The Messages inbox is Home on badge builds; a thread is one
// conversation, with compose inline under it. Badge builds only (UI_HAS_CARDKB).

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/MsgCompose.h>
#include <helpers/ui/BadgeLayout.h>
#include <helpers/ui/NearbyList.h>
#include "../MyMesh.h"

class UITask;

// Every conversation, pinned first, then unread, then the most recent; named channels
// with nothing in them yet come last, so there is somewhere to write before the first
// message.
class InboxScreen : public UIScreen {
public:
  explicit InboxScreen(UITask* task) : _task(task) {}
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

  // A message arrived: its conversation's row inverts for a second.
  void flash();

private:
  // A row: a conversation in the badge store, or a named channel the store hasn't
  // seen yet (by its slot).
  struct Item {
    int16_t convo;
    int16_t slot;
  };
  static constexpr int kMaxItems = MyMesh::kBadgeConvos + MAX_GROUP_CHANNELS;

  UITask* _task;
  int _sel = 0;
  // The selection follows its conversation when rows reorder, by kind and key: an
  // index can be reused by another conversation after an eviction.
  uint8_t _sel_kind = 0;
  uint8_t _sel_key[PUB_KEY_SIZE] = {0};
  uint32_t _marquee_from = 0;
  int _flash_convo = -1;
  uint32_t _flash_until = 0;

  int items(Item* out, int max) const;
  static bool identity(const Item& item, uint8_t& kind, uint8_t* key);
  void follow(const Item* list, int count);
  void select(const Item* list, int count, int sel);
  void open(const Item& item, char first_key);
  bool drawItem(DisplayDriver& d, int row, const Item& item, bool selected, uint32_t now_ms);
};

// #1231: chat contacts, most recently heard first, with their hops and when they were
// last heard (design 1a). Contacts gone quiet read "stale" rather than being dropped.
// Enter opens the DM thread.
class ContactsScreen : public UIScreen {
public:
  explicit ContactsScreen(UITask* task) : _task(task) {}
  void reload();   // on the way in, and every few seconds while shown
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

  static constexpr int kMax = 64;              // the most recently heard
  static constexpr uint32_t kStaleSecs = 3 * 3600;

private:
  struct Entry {
    uint32_t heard;              // lastmod, by our clock
    uint8_t key[PUB_KEY_SIZE];   // the whole key: slots move when the phone edits
  };
  UITask* _task;
  Entry _list[kMax];
  int _count = 0;
  int _total = 0;
  int _sel = 0;
  uint8_t _sel_key[PUB_KEY_SIZE] = {0};
  uint32_t _loaded_at = 0;

  void select(int sel);
  void open(char first_key);
};

// #1234: who's around. Every node heard in the last hour, newest first: `@` a person,
// `^` a repeater, `&` a room, `*` a node that isn't a contact. Enter on a person opens
// the DM thread. The third stop on the SW1 cycle.
class NearbyScreen : public UIScreen {
public:
  explicit NearbyScreen(UITask* task) : _task(task) {}
  void reload();   // on the way in, and every few seconds while shown
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

  static constexpr int kMax = 32;
  static constexpr uint32_t kWindowSecs = 3600;

private:
  UITask* _task;
  badgeui::NearbyList<kMax> _list;
  int _sel = 0;
  uint8_t _sel_key[PUB_KEY_SIZE] = {0};   // the selection follows its node
  uint8_t _sel_key_len = 0;
  uint32_t _loaded_at = 0;

  void select(int sel);
  void open(char first_key);
};

// #1231: what the radio is doing (design 1a, "the empty state is the status screen").
// The BLE pairing PIN moved here from Home. Enter opens the device pages.
class StatusScreen : public UIScreen {
public:
  explicit StatusScreen(UITask* task) : _task(task) {}
  int render(DisplayDriver& display) override { return drawAs(display, " Status", 3); }
  bool handleInput(char c) override;

  // Draws this screen under `title`, as cycle position `pos`: the inbox's empty state
  // is this screen under the Messages title.
  int drawAs(DisplayDriver& display, const char* title, int pos);

private:
  UITask* _task;
  uint32_t _stats_at = 0;   // the nodes-heard count is refreshed every 10 s
  int _heard = 0;
  int _farthest = 0;
};

// #1233: Settings in the design's grammar (3a): one list, label left and value right.
// The owner's list: Bluetooth, time zone, advert (zero-hop and flood) and hibernate,
// with the GPS beside them, whose row opens its screen (#1235). Hibernate asks first,
// as the design's gate does.
class SettingsScreen : public UIScreen {
public:
  explicit SettingsScreen(UITask* task) : _task(task) {}
  void begin();
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;
  void poll() override;

private:
  // #1362: the one Text size row became two, navigation and messages, kept adjacent so
  // they read as a pair. Both cycle in place, like the single row did. The adjacency is
  // asserted rather than asked for: the enum order IS the on-screen order, so a later
  // insertion would separate the pair silently.
  // #1421: Tones sits after Screen off, so the two rows about how the badge behaves AT
  // you -- when the screen blanks, what it sounds like -- read together. It is hidden by
  // shown() on a board with no buzzer rather than compiled out, so the enum stays one
  // shape everywhere.
  enum Row : uint8_t { Bluetooth, NavTextSize, MsgTextSize, ScreenOff, Tones, TimeZone,
                       Gps, Battery, AdvertZeroHop, AdvertFlood, Hibernate, DevicePages,
                       kRows };
  static_assert(MsgTextSize == NavTextSize + 1,
                "the two text-size rows must stay adjacent -- the enum order is the row order");
  UITask* _task;
  int _sel = 0;
  bool _gate = false;              // "Hibernate?" is up
  bool _shutdown_pending = false;  // after SW1's hold is let go
  int _sent_row = -1;              // an advert row saying "sent" for a moment
  uint32_t _sent_until = 0;
  // #1238, #1245: a row that cycles through values takes effect at once and is saved
  // once the cycling settles, so walking all the way round is one write and not five.
  static constexpr uint32_t kCycleSettleMs = 3000;
  uint32_t _cycled_at = 0;

  static bool shown(int row);
  static bool cycles(int row);   // #1245: a row whose press steps a value
  void step(int dir);
  void act();
  void saveCycledIfPending();   // #1238: before the screen goes, or another row acts
  int drawGate(DisplayDriver& d);
};

// #1254: the battery, in the shape the GPS screen uses (#1235) -- what it reads now,
// where 100% is and how the badge got that, then the two things you can do about it.
// Auto-learn sets the full point after a charge; this is for pinning it by hand, and for
// handing it back to auto when a press was mistimed.
class BatteryScreen : public UIScreen {
public:
  explicit BatteryScreen(UITask* task) : _task(task) {}
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

  // "98%": the title's right slot, and Settings' Battery row.
  static void summary(UITask* task, char* out, size_t n);

private:
  enum Row : uint8_t { SetHere, BackToAuto };
  UITask* _task;
  int _sel = SetHere;

  bool canClear() const;   // whether there is a learned or pinned value to hand back
  void act();
};

// #1233: the time zone, as the design's option list (3a): each zone with its offset
// now, "now" beside the one in use and "gps" beside the one a GPS fix suggests.
class ZonePickerScreen : public UIScreen {
public:
  explicit ZonePickerScreen(UITask* task) : _task(task) {}
  void begin();   // on the zone in use, else on the GPS suggestion
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

private:
  UITask* _task;
  int _sel = 0;
  int _suggested = 0;   // 0: no GPS fix to go on
};

// #1235: the GPS, opened from its row in Settings (owner: "GPS opened from Settings is
// fine"). The fix, the position, the altitude and the GPS's own UTC time, then rows in
// the Settings grammar: "Use zone <the fix's zone>" while that differs from the zone in
// use, and GPS on/off.
class GpsScreen : public UIScreen {
public:
  explicit GpsScreen(UITask* task) : _task(task) {}
  void begin();   // on "Use zone" when there's a zone to use
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

  // "fix 9", "no fix", "No GPS Module" or "off": the title's right slot, and Settings'
  // GPS row.
  static void summary(UITask* task, char* out, size_t n);
  // The zone a current fix suggests; 0 (not set) with the GPS off or no fix.
  static int suggestedZone(UITask* task);

private:
  enum Row : uint8_t { UseZone, Power };
  UITask* _task;
  int _sel = Power;
  bool _use_shown = false;   // whether the last render drew "Use zone"

  int pending() const;   // the suggested zone while it differs from the one in use, else 0
  void act();
};

// One conversation, newest at the bottom, with the compose line under a dotted rule
// (design 1a). The name shows in a bar for 2 s on entry. Past one line the editor
// takes the screen, keeping the destination and the room left in its footer.
#ifdef PIN_BUZZER
// #1477: for kMaxRtttl, which sizes the editor's LineEdit. The pure half only -- the
// store needs DataStore and stays in the .cpp, so this header learns the slot sizes
// without learning the filesystem.
#include <helpers/ui/TuneSlots.h>

// #1421: Settings -> Tones. One row per sound event, showing the tune it will play.
// Pressing a row opens the picker for that event.
class TonesScreen : public UIScreen {
public:
  explicit TonesScreen(UITask* task) : _task(task) {}
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

  // What Settings' own Tones row shows: the DM tune's name, which the owner chose over
  // a count because a count says nothing you would act on.
  static void summary(UITask* task, char* out, size_t n);

private:
  // #1468: row 0 is the notification scope -- the gate ABOVE the six event rows, and on
  // this UI family the only way to reach it at all.
  //
  // #1476: Custom goes last, below the six rows it supplies tunes to. That makes eight
  // rows, so Tones scrolls by one at the large face. The owner chose a row here over a
  // second Settings entry: it is about tunes, and one scroll is cheaper than a second
  // place to look for them.
  //
  // The row count itself lives in the .cpp -- it needs ToneSlots.h, and this header is
  // compiled by every variant that builds these screens, buzzer or not.
  static constexpr int kScopeRow = 0;

  void cycleScope();

  UITask* _task;
  int _sel = 0;
};

// #1421: the tunes, for one event. The owner's design calls this archetype a fixed
// option list, and ZonePickerScreen is the same shape -- this is that screen with a
// different source of rows, plus a preview as the selection moves.
//
// Seven builtins is exactly the seven content rows under the title bar at the large
// face, so at that size the whole list is visible with no scrolling. It still scrolls,
// because a smaller face has more rows and a larger one fewer.
class TonePickerScreen : public UIScreen {
public:
  explicit TonePickerScreen(UITask* task) : _task(task) {}
  void begin();   // on the tune in use for the event being edited, and auditions it
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

private:
  // Audition whatever is highlighted, and give any refusal words. Both the arrival and
  // a move go through here, because arriving silently was the #1467 bug: the picker
  // opens on the tune in USE, which is the one you are most likely to want to hear and
  // least likely to navigate onto, so auditioning only on movement meant the common
  // path -- open, press Enter -- never made a sound.
  void audition();

  // #1475: move the highlight onto a row that is actually drawn. render() skips an id
  // with no label and handleInput() steps over one, but nothing re-anchored a selection
  // that STOPPED being listed while the picker was open -- a slot cleared from the app,
  // say -- which left the highlight on a row no longer drawn and nothing looking
  // selected. Silent is always listed, so this always terminates.
  void normalizeSel();

  // #1525: the picker serves an event and a conversation. A conversation gets one extra
  // row at the top -- Default -- so the selection is a ROW, mapped to a tune id through
  // offband::tonePickerIdAt(). Never an id directly: a field meaning a row in one mode
  // and an id in the other is the trap that got pickerRowFor renamed in #1475.
  bool withDefault() const;      // is the Default row shown -- conversation mode
  // #1525: which slot the conversation being edited is in NOW, re-resolved from its
  // secret rather than trusting the index the picker was opened with. Channels can be
  // edited or deleted from the phone while a screen is open -- normalizeSel() exists
  // because a slot's TUNE can change underneath it -- and committing to an index whose
  // channel has been replaced would set a tune on a conversation nobody was looking at.
  // -1 once the channel is gone. Distinct from withDefault(), which is the mode and
  // must not change while the picker is open or the row mapping would shift under the
  // selection.
  int currentSlot() const;
  uint8_t selectedId() const;    // the tune the highlighted row offers
  uint8_t inheritedId() const;   // what Default resolves to, and the audition for it
  // The conversation's effective tune, or the event's. What "now" marks.
  uint8_t effectiveId() const;
  void commit();                 // write the choice: a channel record, or a pref

  UITask* _task;
  int _row = 0;
  uint8_t _subject[32] = {0};   // the edited channel's secret, captured at begin()
  const char* _note = "";      // "Buzzer: OFF" when a preview could not be heard
  uint32_t _note_until = 0;
};

// #1476: the eight custom slots -- what is in them, and the way in to editing one.
//
// Numbered rather than named-only, so an empty slot still has an identity to refer to
// ("slot 4") -- which is what a person needs when a tune is pushed to one from the app
// (#1478). Highlighting a filled slot auditions it, the same as the tune picker: a list
// of tune names you cannot hear is a list you have to guess at.
class CustomTunesScreen : public UIScreen {
public:
  explicit CustomTunesScreen(UITask* task) : _task(task) {}
  void begin();
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

  // What the Tones row shows: how many slots hold something, not how many exist.
  static void summary(UITask* task, char* out, size_t n);

private:
  void audition();

  UITask* _task;
  int _sel = 0;
  const char* _note = "";
  uint32_t _note_until = 0;
};

// #1477: typing an RTTTL string for one slot.
//
// The key budget is fixed by the hardware, not chosen: CardKbKeys.h maps the CardKB to
// arrows, Backspace, Tab, Enter, Esc and printable ASCII, and nothing else. KEY_SELECT,
// KEY_HOME and KEY_CONTEXT_MENU exist in UIScreen.h but reach the badge only from the
// button, so an editor key bound to one of them would be unreachable while typing.
//
// That leaves: printables, Backspace and Tab for the text, Enter to save, Esc to leave,
// and the four arrows. Up and Down have to scroll -- 218 characters at 21 columns is
// eleven rows and only a few are visible -- so Left and Right are the whole of the
// remaining budget, and they take Clear and Play.
//
// No cursor movement, per the owner: a typo early in a long string is restarted with
// Clear rather than un-typed, and the app push (#1478) is the route for anything long.
class RtttlEditScreen : public UIScreen {
public:
  explicit RtttlEditScreen(UITask* task) : _task(task) {}
  void begin(uint8_t slot_index);
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

private:
  void note(const char* text);
  void follow();          // keep the end of the text in view, where typing happens

  UITask* _task;
  LineEdit<offband::kMaxRtttl> _line;
  uint8_t _slot = 0;
  int _top_row = 0;
  bool _confirm_delete = false;   // a second Enter on an empty line, never the first
  const char* _note = "";
  uint32_t _note_until = 0;
};
#endif

class ThreadScreen : public UIScreen {
public:
  explicit ThreadScreen(UITask* task) : _task(task) {}

  // Before showing it. Reopening the conversation whose draft this holds keeps it.
  void begin(int convo);
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

  static constexpr int kShow = 8;          // the newest messages laid out
  static constexpr int kMaxRows = 100;     // their rows, a selection's meta row included
  static constexpr int kInlineChars = 14;  // past this, the full-screen editor

private:
  UITask* _task;
  int _convo = -1;
  uint8_t _kind = 0;
  uint8_t _key[PUB_KEY_SIZE] = {0};   // whose draft _line is
  compose::Composer<MAX_TEXT_LEN> _line;
  uint32_t _entered_at = 0;
  uint16_t _unread_at_entry = 0;
  uint32_t _sel_seq = 0;              // the selected message, 0 for none
  const char* _note = "";
  uint32_t _note_until = 0;

  // Laid out on each render. Kept here rather than on the 4 KB loop stack.
  uint32_t _seqs[kShow];
  char _txt[kShow][MAX_TEXT_LEN + 1];   // display-ready text
  // #1669: senders' tags. Sized from badgeui::kTagChars so the buffer cannot be the
  // thing that limits the name -- the character cap and the kTagPx pixel cap are.
  char _tag[kShow][badgeui::kTagChars + 1];
  badgeui::MsgView _views[kShow];
  badgeui::Row _rows[kMaxRows];

  void send();
#ifdef PIN_BUZZER
  // #1525: which channel slot this conversation is, or -1 when it is not a channel.
  // Contacts are out of scope for this epic, so a DM thread has no tune of its own and
  // Right does nothing there.
  int channelSlot() const;
  void openTunePicker();
  // What the entry bar shows: the conversation's tune, or "Default".
  void tuneHint(char* out, size_t cap) const;
#endif
  bool failedSelected() const;
  void resend();
  void leave();
  void selectOlder();
  void selectNewer();
  void note(const char* text);
  void drawRow(DisplayDriver& d, int screen_row, const badgeui::Row& r, bool channel);
  void drawCompose(DisplayDriver& d, int row);
  void drawEditor(DisplayDriver& d, const char* name);
};
