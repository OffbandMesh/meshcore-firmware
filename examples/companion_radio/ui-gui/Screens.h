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

#include <helpers/gui/GuiTheme.h>
#include <helpers/gui/GuiViews.h>
#include <helpers/gui/InputMap.h>

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

class GuiShell {
public:
  struct Hooks {
    void (*send_advert)(void* ctx);
    void (*lora_popover)(void* ctx);
    void* ctx;
  };

  void begin(const offband::gui::ThemeTokens& t, offband::gui::Look look, const Hooks& hooks);
  void setTheme(const offband::gui::ThemeTokens& t, offband::gui::Look look);

  void showLock();
  void showTab(Tab tab);
  bool locked() const { return _locked; }
  Tab tab() const { return _tab; }

  // Handles an action; false if the screen had no use for it.
  bool action(offband::gui::Action a);

  void updateHome(const HomeData& d);
  void updateLock(const LockData& d);
  void setAdvertState(offband::gui::AdvertButton::State s);

  StatusBar& status() { return _status; }

private:
  void rebuild();
  void buildHome(lv_obj_t* content);
  void buildPlaceholder(lv_obj_t* content);
  void buildTabs(lv_obj_t* parent);
  void buildLock(lv_obj_t* scr);
  void focusable(lv_obj_t* o);

  offband::gui::ThemeTokens _t{};
  offband::gui::LookTokens _look{};
  Hooks _hooks{};
  StatusBar _status;
  lv_group_t* _group = nullptr;
  bool _locked = false;
  Tab _tab = Tab::Home;
  Tab _pending_tab = Tab::Home;   // a switch asked for from inside an event
  bool _gesture_hooked = false;

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
