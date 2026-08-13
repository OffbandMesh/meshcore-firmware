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
    ack
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
  // #542 B1: OLED mode (0 auto, 1 always-on, 2 always-off). No-op default: UIs
  // without a controllable display inherit it and are unaffected. Applied live from
  // the 0xC5 display SET sub-code and at boot.
  virtual void setDisplayMode(uint8_t mode) { (void)mode; }
  virtual void loop() = 0;
};
