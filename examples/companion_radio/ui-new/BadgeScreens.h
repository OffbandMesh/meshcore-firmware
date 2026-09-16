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
  enum Row : uint8_t { Bluetooth, TextSize, ScreenOff, TimeZone, Gps, Battery,
                       AdvertZeroHop, AdvertFlood, Hibernate, DevicePages, kRows };
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
  char _tag[kShow][6];                  // senders' tags, five characters
  badgeui::MsgView _views[kShow];
  badgeui::Row _rows[kMaxRows];

  void send();
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
