#pragma once

#include <MeshCore.h>
#include <helpers/ui/BatteryGauge.h>   // #1246
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_QCC_ACTIVITY_LED
  // #1367: the dispatcher -> UI traffic seam, needed here for the destructor's detach.
  #include <helpers/ui/LedActivity.h>
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
  // #1246: the battery the screens show, averaged over readings taken on a cadence --
  // not once per redraw, which is what made the number move on its own.
  offband::BatteryAverage _batt;
  unsigned long _next_batt_read = 0;
  offband::FullPointLearner _full_learner;   // #1254: learns this cell's 100%
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

#ifdef PIN_QCC_ACTIVITY_LED
  // #1367: the activity LED -- the one beside the display, which #1366 freed by moving
  // the heartbeat to the module's own LED. One LED says one thing at a time, so the
  // classes are a priority stack, highest first:
  //
  //   traffic    a packet decoded. Lit for as long as that packet occupied the air, so
  //              the indicator reports something real rather than a chosen duration.
  //   attention  unread messages, or a send that failed and has not been seen. A slow
  //              blink, because the screen sleeps and this is the only thing that can
  //              say "the badge wants you" while it is dark.
  //   idle       dark. Never solid-on, at any traffic level.
  //
  // Airtime is clamped because raw duration fails at both ends. At the conference preset
  // (500 kHz, SF7) a packet is about 20 ms, barely perceptible, so short packets get a
  // floor. On a slow preset a packet runs hundreds of milliseconds, and with no ceiling
  // a busy mesh would read as solid-on. The forced gap keeps consecutive packets legible
  // as separate blinks rather than merging into one.
  // The gap is sized by duty cycle, not by taste. The LED draws roughly 5-8 mA lit, so
  // the worst case is what matters: a saturated mesh where a flash starts the instant the
  // gap expires. At 60 ms on and a 40 ms gap that is a 100 ms period -- a 10 Hz strobe at
  // 60% duty, about 3.6-4.8 mA continuous, which is both a real drain on a badge's cell
  // and no longer informative, since the LED has stopped tracking the packet rate and is
  // simply saturated (adversarial review, #1367). At 190 ms the period is 250 ms: still
  // an obvious four-flashes-a-second "busy", at 24% duty and roughly 1.4 mA.
  //
  // For scale, the heartbeat this project already ships runs ~10% duty and is costed at
  // 0.2-0.5 mA (NRF52Board::startHeartbeat), so the idle case here -- 60 ms every 2500 ms,
  // 2.4%, about 150 uA -- is cheaper than something already accepted. It is the saturated
  // case that needed bounding.
  // The flash is CHOPPED, not held solid. A single decoded packet held solid reads as a
  // brief steady light, not as activity -- an Ethernet LED flutters because many small
  // packets arrive back to back, which LoRa never does: its packets are rare and long, so
  // one packet is one long blink by construction. Chopping inside the window gives the
  // flutter while keeping the honest part, that total on-time tracks airtime.
  //
  // 25 Hz, not faster. At 15/15 ms (33 Hz) the eye starts fusing the flicker into a dim
  // steady light, which is the thing being avoided; 20/20 is comfortably below that and
  // still reads as rapid.
  // And jittered. A fixed 25 Hz chop reads as a metronome -- obviously a timer, not
  // traffic. Varying each half-cycle makes it look like data arriving, which is what an
  // Ethernet LED's appeal actually is: its irregularity, not its rate. The jitter is
  // bounded so the floor below stays meaningful; worst case each cycle is
  // (on+off) +/- 2*kFlickerJitterMs.
  static const uint32_t kFlickerOnMs = 20;
  static const uint32_t kFlickerOffMs = 20;
  static const uint32_t kFlickerJitterMs = 8;    // +/- per half-cycle
  static const uint32_t kFlickerMinCycles = 5;   // owner: the floor is worth 5 flickers

  uint32_t _flicker_next = 0;    // when the current half-cycle ends
  uint32_t _flicker_rng = 0;     // local PRNG state, so nothing else's stream is disturbed

  // The floor is DERIVED, so it cannot drift away from the flicker rate it is expressed
  // in. A literal here would silently stop being five flickers the moment either half of
  // the cycle changed.
  static const uint32_t kTrafficMinMs = kFlickerMinCycles * (kFlickerOnMs + kFlickerOffMs);
  static const uint32_t kTrafficMaxMs = 400;    // above this it stops reading as one event
  static const uint32_t kTrafficGapMs = 190;    // forced dark; caps the saturated duty cycle

  static_assert(kTrafficMinMs < kTrafficMaxMs,
                "the traffic floor must leave room below the ceiling");
  static_assert(kTrafficMinMs >= kFlickerMinCycles * (kFlickerOnMs + kFlickerOffMs),
                "the shortest flash must still be worth kFlickerMinCycles flickers");
  static const uint32_t kAttentionOnMs = 60;    // the "you have mail" blink
  static const uint32_t kAttentionPeriodMs = 2500;

  volatile uint32_t _traffic_pending_ms = 0;    // set from the dispatcher, read on the tick
  uint32_t _traffic_until = 0;                  // when the current traffic flash ends
  uint32_t _traffic_gap_until = 0;              // earliest the next flash may start
  uint32_t _attention_phase = 0;                // start of the current attention period
  bool _activity_lit = false;                   // what the pin is currently driven to

  void activityLedHandler();
  void setActivityLed(bool on);
  static void onTrafficDecoded(uint32_t airtime_ms);   // the registered sink
  static UITask* _activity_owner;                      // the instance the sink feeds
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
  UIScreen* nearby;     // #1234
  UIScreen* status;     // #1231
  UIScreen* settings;   // #1233
  UIScreen* zones;      // #1233: the time zone picker
  UIScreen* gps;        // #1235
  UIScreen* battery;    // #1254
  uint32_t _cycle_at = 0;   // #1231: when SW1 last moved along the cycle
#endif
  UIScreen* curr;

  void userLedHandler();

  // #1245: how long the display waits before blanking. `autoOffSecs` is the preference,
  // or the board's compiled AUTO_OFF_MILLIS where none is set; `autoOffMillis` is what
  // the timer adds. A board with no Screen off row never sets the preference and so
  // keeps exactly the timeout it was compiled with.
  unsigned long autoOffMillis() const;

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
#ifdef PIN_QCC_ACTIVITY_LED
  // #1367: detach before going away. The dispatcher holds a function pointer that reaches
  // this instance through `_activity_owner`; left dangling, the next decoded packet would
  // call through freed memory. Today this UI is a singleton that outlives everything, so
  // the destructor should never run -- which is exactly why leaving the hazard in place
  // would be the kind of thing nobody notices until a board does construct one twice.
  // Guarded on identity so a second instance's teardown cannot unhook a live first one.
  ~UITask() {
    if (_activity_owner == this) {
      offband::setLedActivitySink(nullptr);
      _activity_owner = nullptr;
    }
  }
#endif
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // #1245: the Screen off row reads this and cycles it. Setting it applies at once --
  // the timer already running is re-based on the new value, so picking a longer one
  // does not leave the screen about to blank on the old.
  uint16_t autoOffSecs() const;
  void setAutoOffSecs(uint16_t secs);

  // #1246: the battery as the screens should show it -- an average across readings
  // rather than whatever the ADC said at the instant of a redraw. `getBattMilliVolts()`
  // is still the raw read, and is what the low-battery shutdown acts on.
  uint16_t smoothedBattMilliVolts() const;

  // #1254: where 100% is on this cell -- the learned or pinned value, or the board's
  // compiled BATT_MAX_MILLIVOLTS while nothing is known. `battFullIsUserSet` says which
  // of the two ways it got there; clearing it hands the cell back to auto-learn.
  uint16_t battFullMilliVolts() const;
  bool battFullIsUserSet() const;
  // The Calibrate action. False when the reading is not a plausible full cell -- the
  // caller says so rather than the value being dropped quietly.
  bool setBattFullMilliVolts(uint16_t mv);
  void clearBattFullMilliVolts();            // Back to auto

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
  // #1231: SW1's tap moves Messages -> Contacts -> Nearby (#1234) -> Status and round;
  // `step` is 1 or -1. A breadcrumb shows where you are for 2 s after each move
  // (design 1f).
  void cycle(int step);
  int cyclePos() const;   // 0 Messages, 1 Contacts, 2 Nearby, 3 Status
  bool breadcrumbShown() const { return _cycle_at != 0 && millis() - _cycle_at < 2000; }
  void gotoStatus();
  void gotoSettings();   // #1233: from Status, or Fn+S from anywhere (design 3a)
  void gotoZones();      // #1233
  void gotoGps();        // #1235: from Settings' GPS row
  void gotoBattery();    // #1254: from Settings' Battery row
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

  // #1075: `cause` names why on the [shutdown] line, so a capture that ends
  // here says what happened: user, low-battery.
  void shutdown(bool restart = false, const char* cause = "user");
};
