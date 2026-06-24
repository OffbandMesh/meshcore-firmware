#include <Arduino.h>
#include <Preferences.h>

#include "MeshsmithPhotonC6Board.h"

namespace {
constexpr uint8_t RF_SWITCH_ENABLE_PIN = 3;
constexpr uint8_t RF_SWITCH_SELECT_PIN = 14;
constexpr const char* RF_SWITCH_PREFS_NAMESPACE = "photon-c6";
constexpr const char* RF_SWITCH_PREFS_KEY = "ext_ant";

bool defaultExternalAntenna() {
#ifdef USE_XIAO_ESP32C6_EXTERNAL_ANTENNA
  return true;
#else
  return false;
#endif
}

void applyWirelessAntenna(bool external) {
  pinMode(RF_SWITCH_ENABLE_PIN, OUTPUT);
  digitalWrite(RF_SWITCH_ENABLE_PIN, LOW);

  delay(100);

  pinMode(RF_SWITCH_SELECT_PIN, OUTPUT);
  digitalWrite(RF_SWITCH_SELECT_PIN, external ? HIGH : LOW);
}
}

void MeshsmithPhotonC6Board::begin() {
  ESP32Board::begin();

  Preferences preferences;
  bool external = defaultExternalAntenna();
  if (preferences.begin(RF_SWITCH_PREFS_NAMESPACE, true)) {
    external = preferences.getBool(RF_SWITCH_PREFS_KEY, external);
    preferences.end();
  }
  applyWirelessAntenna(external);

  delay(10);   // give sx1262 some time to power up
}

bool MeshsmithPhotonC6Board::getWirelessAntennaExternal(bool& external) const {
  external = digitalRead(RF_SWITCH_SELECT_PIN) == HIGH;
  return true;
}

bool MeshsmithPhotonC6Board::setWirelessAntennaExternal(bool external) {
  applyWirelessAntenna(external);

  Preferences preferences;
  if (preferences.begin(RF_SWITCH_PREFS_NAMESPACE, false)) {
    preferences.putBool(RF_SWITCH_PREFS_KEY, external);
    preferences.end();
  }

  return true;
}
