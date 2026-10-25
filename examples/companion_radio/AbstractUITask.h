#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#include "NodePrefs.h"

namespace offband { class ToneEngine; }   // ToneEngine.h, PIN_BUZZER builds only

enum class UIEventType {
    none,
    contactMessage,
    channelMessage,
    roomMessage,
    newContactMessage,
    ack,
    // #1419: the badge sent a message from its own keyboard. Appended, so the three
    // UI implementations' switches ignore it through their existing `default` until
    // one handles it. Only the Offband screen set can fire it -- the screen that sends is
    // inside `#if OFFBAND_SCREEN_UI` (#1788).
    sent
};

class AbstractUITask {
protected:
  mesh::MainBoard* _board;
  MultiSerialInterface* _interfaceManager;
  bool _connected;

  AbstractUITask(mesh::MainBoard* board, MultiSerialInterface* interfaceManager) : _board(board), _interfaceManager(interfaceManager) {
    _connected = false;
  }

public:
  void setHasConnection(bool connected) { _connected = connected; }
  bool hasConnection() const { return _connected; }
  uint16_t getBattMilliVolts() const { return _board->getBattMilliVolts(); }
  bool isBluetoothEnabled() const { return _interfaceManager->isBluetoothEnabled(); }
  void enableBluetooth() { _interfaceManager->enableBluetooth(); }
  void disableBluetooth() { _interfaceManager->disableBluetooth(); }
  virtual void msgRead(int msgcount) = 0;
  virtual void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) = 0;
  virtual void notify(UIEventType t = UIEventType::none) = 0;
  // #510: push the low-level buzzer mute to the hardware driver.
  //
  // The buzzer is a MEMBER of each UITask implementation, not a global, so the mesh
  // layer cannot reach it. Setting the notification scope over 0xC5 updated only the
  // pref and left the driver in whatever state boot put it in -- play() then returned
  // early and the device stayed silent while reporting success. This exists so a
  // scope change applies to the hardware immediately instead of at the next reboot.
  //
  // Non-pure with a no-op default: implementations without a buzzer inherit the
  // default and are unaffected.
  virtual void applyBuzzerMute(bool quiet) { (void)quiet; }

  // #1524: a channel message, and which channel it came from.
  //
  // notify() takes an event type and nothing else, deliberately: the policy decision
  // stays in the mesh layer and the UI stays dumb. A tune set on one channel needs the
  // channel, and UIEventType is consumed by four UI families -- so this is its own
  // virtual rather than a wider notify(). Third time this shape answers exactly this
  // problem, after applyBuzzerMute (#510) and the slot hooks (#1478, now toneEngine()).
  //
  // The default IS the current behaviour, so ui-orig, ui-tiny and ui-gui inherit it and
  // sound exactly as they do now. Only the badge overrides it.
  virtual void notifyChannel(uint8_t channel_idx) {
    (void)channel_idx;
    notify(UIEventType::channelMessage);
  }

  // #1788: the tune engine this UI plays through, or nullptr when it plays fixed melodies
  // (or has no buzzer). The mesh layer reaches event tones, channel tones, the custom
  // slots and auditions through it, and advertises tunes to a client only when it is
  // non-null -- so a board is offered exactly what it will act on. Replaces the per-call
  // slot virtuals #1478 added: one hook, and the engine carries the rest. (Not `tones()`:
  // ui-new already has a screen member by that name.)
  virtual offband::ToneEngine* toneEngine() { return nullptr; }

  // #1788: the screen settings a client can reach -- text sizes, Screen off, time zone,
  // and the battery's full point -- for a UI whose screens honor them. The default says
  // "not here"; a UI that overrides clientScreenSettings() to true overrides the rest.
  // Prefixed `client` because the UIs already have their own setters by the plain names
  // (ui-gui's setScreenOffSecs), and these are the client's door, not a second setter.
  struct ScreenSettings {
    uint8_t nav_text;        // badgeui::TextSize
    uint8_t msg_text;
    uint16_t screen_stored;  // seconds; 0 = the board's compiled timeout
    uint16_t screen_secs;    // what the display actually waits
    uint8_t tz;              // a TimeZones.h index; 0 = not set
    uint16_t batt_stored;    // mV; 0 = nothing learned or pinned
    uint16_t batt_full;      // mV the bar treats as 100%
    uint8_t batt_pinned;     // 1 = set by hand, which auto-learn leaves alone
  };
  virtual bool clientScreenSettings(ScreenSettings& out) { (void)out; return false; }
  // Each returns false for a value the matching screen would not offer. They apply and
  // persist, as the screen's own write does.
  virtual bool clientSetTextSize(bool message, uint8_t size) { (void)message; (void)size; return false; }
  virtual bool clientSetScreenOff(uint16_t secs) { (void)secs; return false; }
  virtual bool clientSetTimeZone(uint8_t tz) { (void)tz; return false; }
  // The Battery screen's two actions. Pin answers 0 pinned, 1 the reading is too high to
  // be a full cell, 2 not charged yet -- the same two refusals the screen shows.
  virtual uint8_t clientPinBattFull() { return 0xFF; }
  virtual bool clientClearBattFull() { return false; }

  // Sound the un-listed tune, for a message the mesh layer has decided asks for it.
  //
  // Another use of this shape, after applyBuzzerMute (#510), the slot hooks (#1478, now
  // toneEngine()) and notifyChannel (#1524), and for the same reason: the tune table and the buzzer are
  // members of a UITask implementation, MyMesh.cpp is compiled by every companion
  // variant, and the decision of WHETHER to sound belongs in the mesh layer. So the mesh
  // layer asks and does not reach.
  //
  // Returns whether the buzzer ACCEPTED the tune -- not whether anybody heard it, and the
  // difference is not pedantry. It can decline for things software knows about: the quiet
  // pref, or an id this build cannot resolve. It CANNOT know about a mute switch wired in
  // series with the sounder, which is exactly how the QCC badge is built (BZ1 behind
  // MOSFET Q3 on P0.06 with a MUTE slide switch in series). With that switch open the
  // firmware drives the pin, this returns true, and the badge is silent.
  //
  // So true means "the request took the buzzer". An earlier version of this comment said
  // it meant the badge made a noise, and that wording cost a bench session on 2026-10-18:
  // the trigger fired twice, logged success, and the owner heard nothing.
  //
  // The default is false and silent, so every UI without this inherits no behaviour at
  // all. Only the badge overrides it.
  virtual bool playTriggerTune() { return false; }

  // #542 B1: OLED mode (0 auto, 1 always-on, 2 always-off). No-op default: UIs
  // without a controllable display inherit it and are unaffected. Applied live from
  // the 0xC5 display SET sub-code and at boot.
  virtual void setDisplayMode(uint8_t mode) { (void)mode; }
  virtual void loop() = 0;
};
