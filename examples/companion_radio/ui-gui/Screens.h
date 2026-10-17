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
#include <helpers/gui/RadarViews.h>
#include <helpers/gui/SettingsViews.h>

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
  char time[12];   // "12:05 PM"
  char text[96];
};

struct LockData {
  char clock[12];   // "21:07", or "9:07 PM" (12-hour)
  char sub[64];
  uint8_t count;   // items, 0..2
  LockItem items[2];
  uint32_t more;   // unread in threads not shown
};

// A node as its detail screen shows it (E4).
struct NodeDetail {
  offband::gui::NodeRow row;   // name, kind, path, heard, SNR, unread (row.key -> key below)
  uint8_t key[32];
  int16_t rssi;                // last heard, when row.snr_known
  char path[80];               // "path: You › Hilltop-2 › Wren"; empty with no route
  bool can_trace;              // a route is known
};

// The detail's actions (mockups 2-6) and how each stands.
enum class NodeAct : uint8_t { Ping, Trace, Login, Share, Count };
struct ActView {
  enum State : uint8_t { Idle, Busy, Ok, Failed } state;
  uint32_t rtt_ms;           // Ping, Trace: round trip when Ok
  bool login_refused;        // Login Failed: the node answered no (else: no reply)
  uint8_t body[176];         // Ping: the status reply; Trace: the trace reply
  uint8_t len;
  uint32_t done_ms;          // when it finished (Share's "Shared ✓" lasts 2 s)
};

// Setup's pages, in the drawn list's order (Blocked is Contacts, owner pick
// a, 2026-10-17).
enum class SetPage : uint8_t {
  Node, Radio, Stats, Privacy, Contacts, Msg, Display, Conn, Power, Actions, Debug, About, Count,
  Theme = 0x40,   // Display › Theme (not in the list)
  List = 0xFF
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
  // Node detail (E4).
  virtual bool nodeDetail(const uint8_t* key, NodeDetail* out) = 0;    // false if gone
  // A detail opened: the actions start Idle (results last until you leave).
  virtual void detailOpened(const uint8_t* key) = 0;
  virtual bool startNodeAct(const uint8_t* key, NodeAct a, const char* password) = 0;
  virtual void nodeActState(NodeAct a, ActView* out) = 0;
  virtual const char* savedPassword(const uint8_t* key) = 0;          // null if none
  virtual int openDm(const uint8_t* key) = 0;                         // its thread, -1 if none
  // Map (Epic F): this device's position, false if it has none.
  virtual bool ownPos(offband::gui::GeoPos* out) = 0;
  // Setup (Epic G): the radio as set, this board's TX ceiling, and a save
  // (false if refused); the list's value column for a page.
  virtual offband::gui::RadioValues radioValues() = 0;
  virtual int8_t txMax() = 0;
  virtual bool saveRadio(const offband::gui::RadioValues& v) = 0;
  virtual void setupSummary(SetPage page, char* out, size_t n) = 0;
  // The GUI's own settings, read and written in place; prefsChanged() saves
  // them when the device is idle and applies the Display ones at once.
  virtual offband::gui::GuiPrefs& guiPrefs() = 0;
  virtual void prefsChanged() = 0;
  // Leaving a Settings page: write pending changes now (owner pick a,
  // 2026-10-17), not only when the device is next idle.
  virtual void savePrefsNow() = 0;
  virtual uint16_t screenOffSecs() = 0;   // as it takes effect
  virtual void setScreenOffSecs(uint16_t secs) = 0;
  virtual bool buzzerOn() = 0;            // Alert with: Buzzer, or Silent
  virtual void setBuzzerOn(bool on) = 0;
  // Node, Privacy, Contacts (G4): applied at once, saved on leaving the page.
  virtual offband::gui::NodeSettings nodeSettings() = 0;
  virtual void setNodeSettings(const offband::gui::NodeSettings& s) = 0;
  virtual void radioStats(offband::gui::RadioStats* out) = 0;
  virtual const uint8_t* selfKey() = 0;   // 32 bytes
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
    if (_composing || _login) return true;
    if (_searching && _tab == Tab::Nodes) return _search_wheel || _search_kb;
    if (_tab == Tab::Map && !_detail) return true;   // USR selects the next node
    if (_tab == Tab::Setup && _ss != SetSheet::None) return true;   // a sheet's cursor or stepper
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
  // Node detail (E4)
  void openDetail(const uint8_t* key);
  void closeDetail();
  void buildDetail(lv_obj_t* content);
  void buildLogin(lv_obj_t* scr);
  void openLogin();
  void closeLogin();
  void actPressed(NodeAct a);
  void refreshLoginField();     // the masked password, one dot a character
  void refreshActs(uint32_t now_ms);   // the action buttons' text and look, from their state
  uint32_t detailSignature();   // what the detail shows; it redraws when this moves

  // Map (Epic F): the radar.
  void buildRadar(lv_obj_t* content);
  void radarStep(int dir);       // USR/AUX: the next or previous node, nearest first
  void radarOpen();              // the selected node's detail
  uint32_t radarSignature();     // what the radar shows; it redraws when this moves
  bool detailTab() const { return _tab == Tab::Nodes || _tab == Tab::Map; }

public:
  // Node detail: redraws as its actions progress; the login sheet closes
  // itself after a success.
  void updateDetail(uint32_t now_ms);
  // Map: redrawn when a position, the selection or the scale changes.
  void updateRadar(uint32_t now_ms);
  // Setup: timed notes ("Not saved") clear themselves.
  void updateSetup(uint32_t now_ms);
  bool setupSheetOpen() const { return _ss != SetSheet::None; }

private:
  // Setup (Epic G, SetupScreens.cpp): the list, the pages, and the edit
  // sheets of mockups 3-6.
  enum class SetSheet : uint8_t { None, Pick, Step, Pad, Confirm };
  void resetSetup();
  void buildSetup(lv_obj_t* content);
  void buildSetupList(lv_obj_t* content);
  void buildRadioPage(lv_obj_t* content);
  void buildMsgPage(lv_obj_t* content);
  void buildNodePage(lv_obj_t* content);
  void buildStatsPage(lv_obj_t* content);
  void buildPrivacyPage(lv_obj_t* content);
  void buildContactsPage(lv_obj_t* content);
  // Node › Name: the keyboard under a one-line field (as Admin login).
  void openSetText();
  void buildSetText(lv_obj_t* scr);
  void refreshSetText();
  void closeSetText(bool keep);
  void buildDisplayPage(lv_obj_t* content);
  void buildThemePage(lv_obj_t* content);
  void buildSetupLater(lv_obj_t* content);
  // The open picker or stepper, for whichever page and row opened it.
  int pickCount();
  int pickCurrent();
  void pickText(int i, char* out, size_t n);
  const char* pickTitle(const char** sub);
  void pickApply(int i);
  void stepApply(int v);
  void stepText(int v, char* out, size_t n);
  lv_obj_t* setHeader(lv_obj_t* content, const char* title);
  lv_obj_t* setSection(lv_obj_t* parent, const char* h);
  lv_obj_t* setRow(lv_obj_t* parent, uint8_t id, const char* k, const char* hint, const char* v, uint8_t kind,
                   bool on, bool changed, bool off);
  void setOpen(SetPage p);
  void setBack();
  void setRowPressed(uint8_t id);
  bool setAction(offband::gui::Action a);
  bool fullScreenSetup() const { return _tab == Tab::Setup && _set_page == SetPage::Radio; }
  void setFlag(uint8_t& flag);   // a toggle row: flip, save, redraw
  void openSetSheet(SetSheet kind, uint8_t row);
  void closeSetSheet();
  void setSheetCursor(int i);
  void setSheetStep(int dir);
  void setSheetPress(int i);   // a row, a key, or a button
  void setSheetCommit();
  void setNote(const char* text);

  bool _set_text = false;                   // Node › Name is being typed
  offband::gui::ComposeBuffer _set_buf;
  lv_obj_t* _set_text_label = nullptr;
  uint8_t _pad_stage = 0;                   // Position: 0 latitude, 1 longitude
  int32_t _pad_lat = 0;
  uint32_t _stats_at = 0;                   // Radio stats: next refresh
  lv_coord_t _stats_scroll = 0;             // Radio stats: where the reader is, across refreshes
  lv_obj_t* _set_body = nullptr;            // the page's scrolling body (Radio stats)
  SetPage _set_page = SetPage::List;
  SetPage _set_focus = SetPage::Node;    // the list row to refocus
  uint8_t _set_focus_row = 0xFF;         // a page's row to refocus
  offband::gui::RadioValues _radio_was{};
  offband::gui::RadioValues _radio_now{};
  int _preset_hint = -1;                 // the preset last picked, among twins
  char _set_note[24] = {};               // shown on Save for 2 s
  uint32_t _set_note_at = 0;
  lv_obj_t* _set_save = nullptr;
  SetSheet _ss = SetSheet::None;
  uint8_t _ss_row = 0;                   // the row the sheet edits
  int _ss_cur = 0;                       // the cursor: an option, a key, a button
  int _ss_count = 0;
  int _ss_val = 0;                       // the stepper's value
  offband::gui::StepSpec _ss_spec{0, 0, 1};
  offband::gui::NumberEntry _ss_pad;
  lv_obj_t* _ss_obj = nullptr;           // on the top layer
  lv_obj_t* _ss_list = nullptr;          // the picker's scrolling list
  lv_obj_t* _ss_value = nullptr;         // the stepper's or pad's value
  lv_obj_t* _ss_limit = nullptr;         // the range line under it
  static constexpr size_t kSheetItems = 16;   // a picker builds only its window
  static constexpr int kPickRows = 6;
  lv_obj_t* _ss_item[kSheetItems] = {};
  int _ss_top = 0;                       // the picker's first option on screen
  int _ss_picked = -1;                   // the option the value has now
  int16_t _ss_drag = 0;                  // a drag's travel since the last row step
  bool _ss_dragged = false;
  void setPickTop(int top);

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
  void resetNodes();             // Nodes opens on its list
  void resetMap();               // Map opens on the radar, nothing selected
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
  size_t _jump_unread = 0;        // on opening: unread messages to start above (Jump to oldest unread)

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

  // Node detail (E4): which node, and the login sheet over it.
  bool _detail = false;
  uint8_t _detail_key[32] = {};
  bool _login = false;
  offband::gui::ComposeBuffer _login_pw;
  uint32_t _detail_sig = 0;
  uint32_t _detail_at = 0;     // next check of what the detail shows
  uint32_t _login_ok_at = 0;   // when a login succeeded: the sheet closes 1 s later
  bool _login_sent = false;    // a login is in flight from this sheet
  lv_obj_t* _login_field = nullptr;
  lv_obj_t* _login_status = nullptr;
  uint32_t _login_at = 0;      // lv tick when the login went
  uint32_t _detail_now = 0;    // the loop's clock, for the actions' timed text
  lv_obj_t* _act_btn[3] = {};  // the detail's action buttons, primary first
  NodeAct _act_kind[3] = {};   // NodeAct::Count: DM
  bool _act_off[3] = {};       // Trace with no route
  uint8_t _act_count = 0;
  uint8_t _act_focus = 0;      // the focused button, kept across redraws
  uint8_t _pending_act = 0;
  offband::gui::NodeKind _detail_kind = offband::gui::NodeKind::Other;
  uint8_t _row_keys[kNodeRowsShown][32] = {};   // each built row's node, for opening it
  void rowOpens(lv_obj_t* row, size_t i, const uint8_t* key);

  // Map (Epic F): the scale, the selection (by key, so it survives a
  // redraw), and the pins drawn.
  static constexpr size_t kRadarPins = 20;   // the nearest shown (owner default, 2026-10-17)
  size_t _radar_step = offband::gui::kRadarStartStep;
  uint8_t _radar_sel[32] = {};   // the whole key: two nodes can share a prefix
  bool _radar_sel_set = false;
  uint8_t _radar_keys[kRadarPins][32] = {};   // each drawn pin's node, for a tap
  uint32_t _radar_sig = 0;
  uint32_t _radar_at = 0;

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
  lv_obj_t* _lock_ampm = nullptr;   // 12-hour: the clock font has digits and a colon only
  lv_obj_t* _lock_sub = nullptr;
  lv_obj_t* _lock_list = nullptr;
  uint8_t _lock_count = 0xFF;
  uint32_t _lock_more = 0xFFFFFFFF;
};
