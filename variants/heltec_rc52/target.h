#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <RC52Board.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>

// Headless variant (#854). DISPLAY_CLASS is bound to NullDisplayDriver rather
// than left undefined, following heltec_rc32's `_without_display_` envs: that
// keeps the button/UI surface compiled in -- and the RC52 does have a real USER
// button (P1.10) -- while the panel itself is inert.
//
// The real panel is NOT here because NV3001BDisplay does not compile for nRF52
// today. Adding it is #872.
#ifdef DISPLAY_CLASS
#include <helpers/ui/MomentaryButton.h>
#include <helpers/ui/NullDisplayDriver.h>
#endif

extern RC52Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
