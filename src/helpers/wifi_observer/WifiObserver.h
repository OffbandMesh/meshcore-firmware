// src/helpers/wifi_observer/WifiObserver.h
//
// Top-level entry point for the Crosswire observer subsystem. Owns
// the WifiBootstrap state machine and, once STA is connected,
// transitions into MqttUplink (EastMesh vendored).
//
// Plan 1: callable from companion_radio/main.cpp under
// #ifdef CROSSWIRE_OBSERVER. Future plan adds simple_repeater wiring.

#pragma once
#include "WifiObserverConfig.h"

namespace crosswire {

// Call from setup(), after Serial.begin() but before any user-facing
// operation. Does NOT block.
void wifiObserverBegin();

// Call once per main loop iteration.
void wifiObserverLoop();

}  // namespace crosswire
