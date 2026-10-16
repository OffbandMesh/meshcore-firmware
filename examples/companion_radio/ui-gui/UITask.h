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
#include <helpers/ui/BatteryGauge.h>
#include <helpers/ui/DisplayDriver.h>

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "StatusBar.h"

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

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
  void applyBuzzerMute(bool quiet) override;
  void loop() override;

private:
  void buildTestScreen();
  void refreshTestScreen();
  void pollButtons(uint32_t now);
  void handle(offband::gui::InputEvent ev, uint32_t now);
  void applyScreen(offband::gui::ScreenState s);
  void toggleBuzzer();
  bool buzzerQuiet();
  uint32_t idleMs(uint32_t now) const;
  StatusReadings statusReadings(uint32_t now);
  void openLoraPopover();
  bool gpsEnabled() const;

  StatusBar _status;
  offband::BatteryAverage _batt;
  uint32_t _next_batt_read = 0;
  uint32_t _next_status = 0;
  bool _gps_was_on = false;
  uint32_t _gps_on_at = 0;   // when GPS was last turned on: where seeking starts

#ifdef PIN_BUZZER
  genericBuzzer _buzzer;
#endif
  uint32_t _started_at = 0;

  DisplayDriver* _display = nullptr;
  SensorManager* _sensors = nullptr;
  NodePrefs* _prefs = nullptr;
  bool _gui_ok = false;
  bool _touch_ok = false;
  bool _always_on = false;
  bool _pocket_locked = false;  // USR long hold: off, and only USR wakes it
  uint32_t _shown_pin = 0;
  int _unread = 0;
  unsigned long _next_refresh = 0;

  offband::gui::ButtonTracker _usr;
  offband::gui::ClickCounter _usr_clicks;
  offband::gui::ButtonTracker _aux;
  uint32_t _last_input = 0;
  offband::gui::ScreenState _screen = offband::gui::ScreenState::On;
  offband::gui::Action _last_action = offband::gui::Action::None;
  offband::gui::ThemeId _theme = offband::gui::kDefaultTheme;
  offband::gui::Look _look = offband::gui::kDefaultLook;
};
