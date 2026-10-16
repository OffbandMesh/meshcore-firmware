#pragma once

// The on-device GUI's UI task (Feature #1313). GUI envs compile this in place
// of ui-new/UITask, with the same surface main.cpp drives, so main.cpp needs
// no GUI-specific code.
//
// Epic A brought up the panel, LVGL, input, power and themes; Epic D adds the
// screens (GuiShell): the status bar, Home, Lock, and the chat screens.

#include <helpers/SensorManager.h>
#include <helpers/gui/GuiTheme.h>
#include <helpers/gui/GuiViews.h>
#include <helpers/gui/InputMap.h>
#include <helpers/gui/InputTiming.h>
#include <helpers/ui/BatteryGauge.h>
#include <helpers/ui/DisplayDriver.h>

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "Screens.h"

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

class UITask : public AbstractUITask, public ShellModel {
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

  // ShellModel: what the screens read and do.
  const offband::gui::MsgStore& store() override;
  offband::gui::NotifyMode threadMode(int thread) override;
  void threadOpened(int thread) override;
  void routeMeta(int thread, char* out, size_t n) override;
  int zone() override { return _prefs ? _prefs->ui_tz : 0; }
  void sendAdvert() override;
  void loraPopover() override { openLoraPopover(); }
  size_t composeBudget(int thread) override;
  const char* cannedText(int i) override;
  bool sendText(int thread, const char* text, size_t len) override;
  uint8_t dmAttempt(uint32_t seq) override;
  void resend(uint32_t seq) override;

private:
  void pollButtons(uint32_t now);
  void handle(offband::gui::InputEvent ev, uint32_t now);
  void applyScreen(offband::gui::ScreenState s);
  void toggleBuzzer();
  bool buzzerQuiet();
  uint32_t idleMs(uint32_t now) const;
  StatusReadings statusReadings(uint32_t now);
  void homeData(HomeData* d);
  void lockData(LockData* d);
  void openLoraPopover();
  int channelSlot(int thread);   // the channel slot behind a channel thread, or -1
  bool sendDirect(int thread, const char* text, uint32_t timestamp, uint8_t attempt, uint32_t* timeout_ms);
  void pollDmRetries(uint32_t now);
  bool gpsEnabled() const;
  uint8_t battPercent() const;

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
  int _unread = 0;

  GuiShell _shell;
  offband::gui::DmRetry _dm[8];   // DMs sent from the device, awaiting their ACK
  offband::gui::AdvertButton _advert;
  offband::BatteryAverage _batt;
  uint32_t _next_batt_read = 0;
  uint32_t _next_status = 0;
  uint32_t _next_screen = 0;
  bool _gps_was_on = false;
  uint32_t _gps_on_at = 0;   // when GPS was last turned on: where seeking starts

  offband::gui::ButtonTracker _usr;
  offband::gui::ClickCounter _usr_clicks;
  offband::gui::ButtonTracker _aux;
  uint32_t _last_input = 0;
  offband::gui::ScreenState _screen = offband::gui::ScreenState::On;
  offband::gui::ThemeId _theme = offband::gui::kDefaultTheme;
  offband::gui::Look _look = offband::gui::kDefaultLook;
};
