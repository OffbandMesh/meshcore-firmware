#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <RC52Board.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>

// DISPLAY_CLASS is always bound to something -- NullDisplayDriver on the
// headless roles, RC52Display on the with-display ones -- rather than left
// undefined, following heltec_rc32. That keeps the button/UI surface compiled
// in (the RC52 does have a real USER button, P1.10) while an inert panel simply
// draws nowhere.
//
// The with-display arm points at the VARIANT-LOCAL driver in this directory, not
// at src/helpers/ui/NV3001BDisplay.h. The shared driver still does not compile
// for nRF52; #948 chose duplication inside this variant over a fleet-wide port
// so that RC32 and RCC6 cannot regress from RC52 work. Same shape as RC32's
// switch below -- deliberately, so the two read alike.
#ifdef DISPLAY_CLASS
#include <helpers/ui/MomentaryButton.h>
#ifdef HELTEC_RC52_WITH_DISPLAY
#include "RC52Display.h"
#else
#include <helpers/ui/NullDisplayDriver.h>
#endif
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
