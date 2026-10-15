#pragma once

// The on-device GUI's UI task (Feature #1313). GUI envs compile this in place
// of ui-new/UITask, with the same surface main.cpp drives, so main.cpp needs
// no GUI-specific code.
//
// Epic A brings up the panel, LVGL, input, power and themes; the screens come
// in Epics D-H. Until then this shows a test screen with the BLE pairing PIN,
// which a phone needs to pair.

#include <helpers/SensorManager.h>
#include <helpers/gui/GuiTheme.h>
#include <helpers/gui/InputMap.h>
#include <helpers/gui/InputTiming.h>
#include <helpers/ui/DisplayDriver.h>

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

class UITask : public AbstractUITask {
public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial) {}

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void setAlwaysOn(bool on) { _always_on = on; }
  bool displaySupportsRotation() const { return false; }  // Epic H
  void requestRotation(uint8_t deg) { (void)deg; }

  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

private:
  void buildTestScreen();
  void refreshTestScreen();
  void pollButtons(uint32_t now);
  void handle(offband::gui::InputEvent ev, uint32_t now);
  void applyScreen(offband::gui::ScreenState s);
  uint32_t idleMs(uint32_t now) const;

  DisplayDriver* _display = nullptr;
  SensorManager* _sensors = nullptr;
  NodePrefs* _prefs = nullptr;
  bool _gui_ok = false;
  bool _touch_ok = false;
  bool _always_on = false;
  bool _manual_off = false;  // PRG long hold: stays off until the next input
  uint32_t _shown_pin = 0;
  int _unread = 0;
  unsigned long _next_refresh = 0;

  offband::gui::ButtonTracker _prg;
  offband::gui::ButtonTracker _user;
  uint32_t _last_input = 0;
  offband::gui::ScreenState _screen = offband::gui::ScreenState::On;
  offband::gui::Action _last_action = offband::gui::Action::None;
  offband::gui::ThemeId _theme = offband::gui::kDefaultTheme;
  offband::gui::Look _look = offband::gui::kDefaultLook;
};
