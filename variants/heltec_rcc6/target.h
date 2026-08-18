#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <HeltecRCC6Board.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
// Plain SensorManager, not EnvironmentSensorManager. This board has no I2C bus
// (PIN_BOARD_SDA/SCL are -1) so there are no environment sensors to manage, and
// pulling in the environment manager would drag the I2C sensor discovery path
// onto a C6 -- where a blind bus scan hangs at 0x0d and never returns (#294).
#include <helpers/SensorManager.h>

extern HeltecRCC6Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

#ifdef DISPLAY_CLASS
  #include <helpers/ui/MomentaryButton.h>
  // The driver header must be included, not merely named by DISPLAY_CLASS --
  // otherwise `extern DISPLAY_CLASS display;` declares an incomplete type and
  // every consumer fails with "'display' was not declared in this scope".
  #ifdef HELTEC_RCC6_WITH_DISPLAY
    #include <helpers/ui/NV3001BDisplay.h>
  #else
    #include <helpers/ui/NullDisplayDriver.h>
  #endif
  extern DISPLAY_CLASS display;
  // examples/simple_repeater/UITask.cpp uses user_btn under
  // `#if defined(PIN_USER_BTN) && defined(DISPLAY_CLASS)`, so a display build
  // must provide it or it will not link.
  extern MomentaryButton user_btn;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
