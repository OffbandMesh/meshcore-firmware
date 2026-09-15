#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

#if UI_HAS_CARDKB
  #include <helpers/ui/CardKbInput.h>
  #include <helpers/ui/MsgCompose.h>   // #1228
#endif

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  NodePrefs* _node_prefs;
  char _alert[80];
  unsigned long _alert_expiry;
  int _msgcount;
  unsigned long ui_started_at, next_batt_chck;
  int next_backlight_btn_check = 0;
  bool _always_on = false;   // #141: when true, never auto-blank the display
  uint8_t _disp_mode = 0;    // #542 B1: 0 auto, 1 always-on, 2 always-off (dark)
  int  _rotation = 0;            // #148: desired display rotation in degrees (0/180)
  bool _rotation_dirty = true;   // #148: rotation needs (re)applying on the next render
  bool _was_on = false;          // #148: display on/off edge, to re-apply rotation after a wake
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif

#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis = millis();
#endif
#if UI_HAS_CARDKB
  CardKbInput _kbd;   // #1205: an optional CardKB-compatible keyboard on Wire
  uint8_t _last_kbd_raw = 0;      // #1207: the keyboard's last byte, for the key test
  int  _last_btn_event = 0;       // #1207: SW1's last gesture, recorded before any handler
  bool _input_from_kbd = false;   // #1207: whether the key being dispatched is a keyboard key
#endif

  UIScreen* splash;
  UIScreen* home;
  UIScreen* msg_preview;
#ifdef QCC_BADGE_SELFTEST
  UIScreen* self_test;
#endif
#if defined(QCC_BADGE_SELFTEST) && UI_HAS_CARDKB
  UIScreen* key_test;
#endif
#if UI_HAS_CARDKB
  UIScreen* thread;     // #1230: one conversation, with compose
  UIScreen* tools;      // #1230: the device pages Home used to hold
  UIScreen* contacts;   // #1231
  UIScreen* status;     // #1231
  uint32_t _cycle_at = 0;   // #1231: when SW1 last moved along the cycle
#endif
  UIScreen* curr;

  void userLedHandler();

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

  void setCurrScreen(UIScreen* c);

public:

  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen() { setCurrScreen(home); }
#ifdef QCC_BADGE_SELFTEST
  void gotoSelfTest();
#endif
#if defined(QCC_BADGE_SELFTEST) && UI_HAS_CARDKB
  void gotoKeyTest();
#endif
#if UI_HAS_CARDKB
  // #1230: a conversation's thread; `first_key` is typed into its compose line.
  void gotoThread(int convo, char first_key = 0);
  void gotoTools();   // #1230
  // #1231: SW1's tap moves Messages -> Contacts -> Status and round; `step` is 1 or -1.
  // A breadcrumb shows where you are for 2 s after each move (design 1f).
  void cycle(int step);
  int cyclePos() const;   // 0 Messages, 1 Contacts, 2 Status
  bool breadcrumbShown() const { return _cycle_at != 0 && millis() - _cycle_at < 2000; }
  void gotoStatus();
  // The Status screen under another title, as cycle position `pos`: the inbox's empty state.
  int renderStatusAs(DisplayDriver& d, const char* title, int pos);
  bool hasKeyboard() const { return _kbd.isPresent(); }
  uint8_t lastKeyboardRaw() const { return _last_kbd_raw; }
  int lastButtonEvent() const { return _last_btn_event; }
  bool inputFromKeyboard() const { return _input_from_kbd; }
#endif
  void showAlert(const char* text, int duration_millis);
  int  getMsgCount() const { return _msgcount; }
  bool hasDisplay() const { return _display != NULL; }
  bool isButtonPressed() const;

  bool isBuzzerQuiet() { 
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    return true;
#endif
  }

  void toggleBuzzer();
  bool getGPSState();
  void toggleGPS();

  // #141: display always-on toggle (set via `display always on/off` over the _sys CLI).
  // #542 B1: reconciled onto the tristate — setAlwaysOn(on) == setDisplayMode(on ? 1 : 0).
  void setAlwaysOn(bool on);
  // #542 B1: OLED mode. 0 auto (on, blanks after timeout), 1 always-on, 2 always-off (dark).
  void setDisplayMode(uint8_t mode) override;

  // #148: request a display rotation (deg 0/180); applied at the next render cycle.
  void requestRotation(uint8_t deg);

  // #148: does the live display driver implement a verified runtime rotation?
  bool displaySupportsRotation() const;

  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void applyBuzzerMute(bool quiet) override;
  void loop() override;

  void shutdown(bool restart = false);
};
