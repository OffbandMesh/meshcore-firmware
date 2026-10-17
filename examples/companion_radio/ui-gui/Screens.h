#pragma once

// The GUI's screens (#1497, PLAN.md §3 D3): the shell (status bar, content,
// tab bar), Home and Lock. Design of record: ObScreen.dc.html (240x320) and
// the owner-approved gap mockups.
//
// Input arrives as Actions (InputMap): Next and Prev move the focus, Open
// presses the focused item, Back returns toward Home. Touch presses items
// directly. On Lock, USR or a swipe up unlocks.

#include <lvgl.h>
#include <stdint.h>

#include <helpers/gui/GuiPrefs.h>
#include <helpers/gui/GuiTheme.h>
#include <helpers/gui/GuiViews.h>
#include <helpers/gui/InputMap.h>
#include <helpers/gui/NodeViews.h>

#include "StatusBar.h"

enum class Tab : uint8_t { Home, Chat, Nodes, Map, Setup, Count };

struct HomeCell {
  char value[16];
  char sub[48];
};

struct HomeData {
  char name[32];
  char key[24];
  HomeCell cells[6];   // Radio, Heard, Unread, GPS, Battery, Airtime
};

struct LockItem {
  char label[48];
  char time[8];
  char text[96];
};

struct LockData {
  char clock[8];
  char sub[64];
  uint8_t count;   // items, 0..2
  LockItem items[2];
  uint32_t more;   // unread in threads not shown
};

// What the screens need from the rest of the firmware. UITask implements it,
// so the screens stay free of the mesh.
class ShellModel {
public:
  virtual const offband::gui::MsgStore& store() = 0;
  virtual offband::gui::NotifyMode threadMode(int thread) = 0;   // All for DMs and rooms
  virtual void threadOpened(int thread) = 0;                     // mark it read
  virtual void routeMeta(int thread, char* out, size_t n) = 0;   // the header's route line
  virtual int zone() = 0;                                        // TimeZones.h index
  virtual void sendAdvert() = 0;
  virtual void loraPopover() = 0;
  // Compose (D5)
  virtual size_t composeBudget(int thread) = 0;              // bytes a message may carry
  virtual const char* cannedText(int i) = 0;                 // chip i (0..3), "" if none
  virtual bool sendText(int thread, const char* text, size_t len) = 0;
  virtual uint8_t dmAttempt(uint32_t seq) = 0;               // the try in flight, 0 if none
  virtual void resend(uint32_t seq) = 0;                     // a failed DM, again
  // Notifications (D6)
  virtual void setThreadMode(int thread, offband::gui::NotifyMode m) = 0;   // channels only
  virtual const char* nodeName() = 0;
  // Nodes (Epic E): every contact as a list row, rebuilt on each call; the
  // array stays valid until the next call. *total = contacts held.
  virtual const offband::gui::NodeRow* nodeRows(size_t* total) = 0;
  virtual uint8_t radioSf() = 0;   // for signal bars: the margin over this SF's floor
  virtual ~ShellModel() {}
};

class GuiShell {
public:
  void begin(const offband::gui::ThemeTokens& t, offband::gui::Look look, ShellModel* model);
  void setTheme(const offband::gui::ThemeTokens& t, offband::gui::Look look);

  void showLock();
  void showTab(Tab tab);
  bool locked() const { return _locked; }
  Tab tab() const { return _tab; }
  bool composing() const { return _composing; }
  // USR steps through something here (the letter wheel, a keyboard's key
  // cursor, a conversation's scroll): each click is one step, at once.
  bool stepping() const {
    if (_locked || _sheet != Sheet::None) return false;
    if (_composing) return true;
    if (_searching && _tab == Tab::Nodes) return _search_wheel || _search_kb;
    return _tab == Tab::Chat && _thread >= 0;
  }
  // Compose for a thread (from a conversation); leaving returns to it.
  void openCompose(int thread);

  // Handles an action; false if the screen had no use for it.
  bool action(offband::gui::Action a);

  void updateHome(const HomeData& d);
  void updateLock(const LockData& d);
  void setAdvertState(offband::gui::AdvertButton::State s);
  // Redraws Chats or the open conversation if the store changed.
  void updateChats();
  // Nodes: row text refreshed in place; the list rebuilt when the order
  // changes, at most every few seconds.
  void updateNodes(uint32_t now_ms);

  StatusBar& status() { return _status; }

private:
  void rebuild();
  void buildHome(lv_obj_t* content);
  void buildChats(lv_obj_t* content);
  void buildConversation(lv_obj_t* content);
  void buildPlaceholder(lv_obj_t* content);
  void buildNodes(lv_obj_t* content);
  lv_obj_t* nodeRow(lv_obj_t* list, const offband::gui::NodeRow& r);
  void buildKeyboard(lv_obj_t* scr);
  void openSearch(bool wheel);
  void closeSearch();
  void buildSearch(lv_obj_t* scr);
  void refreshSearch();
  void buildWheel(lv_obj_t* scr);
  void wheelPick();
  void buildCompose(lv_obj_t* scr);
  void buildKeys();
  void refreshDraft();
  void setHotKey(int i, bool preview);
  void pressKey(int i);
  void insertText(const char* text);
  void leaveCompose();
  // Bottom sheets (D6): the channel notification sheet, and the menu USR
  // hold opens in a channel (owner pick a, 2026-10-16).
  enum class Sheet : uint8_t { None, Notify, Menu };
  void openSheet(Sheet kind);
  void closeSheet();
  void sheetHot(int i);
  void sheetPick(int i);
  void buildTabs(lv_obj_t* parent);
  void buildLock(lv_obj_t* scr);
  void focusable(lv_obj_t* o);
  void openThread(int thread);   // -1: back to the list
  void later(void (*fn)(GuiShell*));

  offband::gui::ThemeTokens _t{};
  offband::gui::LookTokens _look{};
  ShellModel* _model = nullptr;
  StatusBar _status;
  lv_group_t* _group = nullptr;
  bool _locked = false;
  Tab _tab = Tab::Home;
  Tab _pending_tab = Tab::Home;   // a switch asked for from inside an event
  int _pending_thread = -1;
  void (*_pending_fn)(GuiShell*) = nullptr;

  // Chats and Conversation
  int _thread = -1;               // the open conversation; -1 = the list
  int _focus_thread = -1;         // the list row to refocus after a redraw
  uint32_t _shown_rev = 0xFFFFFFFF;
  lv_obj_t* _msg_list = nullptr;  // the conversation's scrolling column
  lv_obj_t* _focus_row = nullptr; // the list row for _focus_thread, if shown
  lv_coord_t _keep_scroll = -1;   // a conversation's scroll to restore on redraw; -1 = bottom

  // Compose (D5)
  bool _composing = false;
  offband::gui::ComposeBuffer _draft;
  offband::gui::ShiftState _shift;
  offband::gui::PageState _page;
  offband::gui::KeyRect _keys[40];
  size_t _nkeys = 0;
  lv_obj_t* _key_obj[40] = {};
  int _hot = -1;                    // the highlighted key: pressed, or the button cursor
  lv_obj_t* _kb = nullptr;          // the keyboard area, which takes the touches
  lv_obj_t* _draft_box = nullptr;
  lv_obj_t* _draft_label = nullptr;
  lv_obj_t* _caret = nullptr;
  lv_obj_t* _counter = nullptr;
  lv_obj_t* _key_preview = nullptr; // on the top layer while a finger is down
  uint32_t _not_sent_at = 0;        // "Not sent" shows in the counter for 2 s

  // Bottom sheet
  Sheet _sheet = Sheet::None;
  lv_obj_t* _sheet_obj = nullptr;   // the backdrop on the top layer; the sheet is its child
  lv_obj_t* _sheet_rows[3] = {};
  uint8_t _sheet_count = 0;
  int _sheet_hot = 0;

  // Nodes (Epic E)
  static constexpr size_t kNodeRowsShown = 50;   // the newest; search reaches the rest
  // Built 12 at a time: 50 rows cost ~515 ms to build and ~380 ms to draw
  // (bench, #1534). More are added on scrolling near the end, or when USR
  // steps past the last row.
  static constexpr size_t kNodeBatch = 12;
  size_t _node_cap = kNodeBatch;
  lv_obj_t* _node_list = nullptr;
  lv_obj_t* _node_clear = nullptr;   // the unread view's footer, in the focus order
  lv_obj_t* _tab_obj[5] = {};
  void appendNodeRows();
  offband::gui::TypeFilter _node_filter = offband::gui::TypeFilter::All;
  bool _node_unread = false;
  char _node_query[33] = {};
  uint16_t _node_order[kNodeRowsShown] = {};
  size_t _node_shown = 0;
  lv_obj_t* _node_row_obj[kNodeRowsShown] = {};
  lv_obj_t* _node_count = nullptr;
  uint32_t _node_refresh_at = 0;
  uint32_t _node_rebuild_at = 0;
  uint8_t _focus_key[6] = {};   // the focused row's node, kept across rebuilds
  bool _focus_key_set = false;

  // Nodes search (E3): the field takes the header; the keyboard (touch) or
  // the letter wheel (buttons) sits under the results, or neither once Done.
  bool _searching = false;
  bool _search_kb = false;
  bool _search_wheel = false;
  offband::gui::ComposeBuffer _query;
  offband::gui::LetterWheel _wheel;
  lv_obj_t* _query_label = nullptr;
  lv_obj_t* _match_label = nullptr;
  lv_obj_t* _search_list = nullptr;
  lv_obj_t* _wheel_strip = nullptr;
  // Performance (bench, #1534: 889 ms per keystroke with a full contact
  // list): the rows are read once per search, the results redraw 250 ms
  // after typing stops, and only what fits is built while typing.
  const offband::gui::NodeRow* _search_rows = nullptr;
  size_t _search_total = 0;
  uint32_t _search_due = 0;   // when the results next redraw; 0 = not owed
  void querySoon();           // the field now, the results shortly

  // Home
  lv_obj_t* _name = nullptr;
  lv_obj_t* _key = nullptr;
  lv_obj_t* _value[6] = {};
  lv_obj_t* _sub[6] = {};
  lv_obj_t* _advert = nullptr;
  lv_obj_t* _advert_label = nullptr;
  offband::gui::AdvertButton::State _advert_shown = offband::gui::AdvertButton::State::Idle;

  // Lock
  lv_obj_t* _lock_clock = nullptr;
  lv_obj_t* _lock_sub = nullptr;
  lv_obj_t* _lock_list = nullptr;
  uint8_t _lock_count = 0xFF;
  uint32_t _lock_more = 0xFFFFFFFF;
};
