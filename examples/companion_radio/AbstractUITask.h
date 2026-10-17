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

enum class UIEventType {
    none,
    contactMessage,
    channelMessage,
    roomMessage,
    newContactMessage,
    ack,
    // #1419: the badge sent a message from its own keyboard. Appended, so the three
    // UI implementations' switches ignore it through their existing `default` until
    // one handles it. Only the badge can fire it -- the screen that sends is inside
    // `#if UI_HAS_CARDKB`, which no other variant sets.
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
  // problem, after applyBuzzerMute (#510) and feedTuneSlot (#1478).
  //
  // The default IS the current behaviour, so ui-orig, ui-tiny and ui-gui inherit it and
  // sound exactly as they do now. Only the badge overrides it.
  virtual void notifyChannel(uint8_t channel_idx) {
    (void)channel_idx;
    notify(UIEventType::channelMessage);
  }

  // #1478: a custom tune slot, pushed from the app. Routed through this interface for
  // the same reason applyBuzzerMute is: the slot store lives in the badge's UI and
  // MyMesh.cpp is compiled by every companion variant, so the mesh layer asks rather
  // than knowing. A UI without slots keeps the defaults and the handler reports
  // unsupported.
  //
  // feedTuneSlot returns one of the SlotAssembly results as a uint8_t, plus the bytes
  // accepted so far in `accepted`, so the handler can answer without knowing the store.
  virtual uint8_t feedTuneSlot(uint8_t slot, uint8_t offset, bool more,
                               const uint8_t* bytes, size_t len, uint8_t& accepted) {
    (void)slot; (void)offset; (void)more; (void)bytes; (void)len;
    accepted = 0;
    return 0xFF;   // unsupported on this UI
  }

  // The stored tune's length and derived name, for SLOT_GET. False when this UI has no
  // slots. `name` is written as a NUL-terminated string of at most 12 characters.
  virtual bool readTuneSlot(uint8_t slot, uint8_t& len, char* name, size_t name_cap) {
    (void)slot; (void)name; (void)name_cap;
    len = 0;
    return false;
  }
  // #542 B1: OLED mode (0 auto, 1 always-on, 2 always-off). No-op default: UIs
  // without a controllable display inherit it and are unaffected. Applied live from
  // the 0xC5 display SET sub-code and at boot.
  virtual void setDisplayMode(uint8_t mode) { (void)mode; }
  virtual void loop() = 0;
};
