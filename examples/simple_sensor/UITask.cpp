#include "UITask.h"
#include <helpers/ui/OffbandSplash.h>   // #822: the one shared splash
#include <Arduino.h>
#include <helpers/CommonCLI.h>

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  _last_disp_mode = _node_prefs->ui_display_mode;   // #542 A2: seed so loop() sees no false transition
  if (_node_prefs->ui_display_mode == DISPLAY_MODE_ALWAYS_OFF) {
    _display->turnOff();          // #542 A2: boot dark
  } else {
    _display->turnOn();           // auto + always-on both start lit
  }

  // strip off dash and commit hash by changing dash to null terminator
  // e.g: v1.2.3-abcdef -> v1.2.3
  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if(dash){
    *dash = 0;
  }

  snprintf(_mc_version, sizeof(_mc_version), "%s", version);
  free(version);
  _build_date = build_date;   // FIRMWARE_BUILD_DATE literal from main.cpp -- static lifetime
}

void UITask::renderCurrScreen() {
  char tmp[80];
  if (millis() < BOOT_SCREEN_MILLIS) { // boot screen

    // #822: THE shared splash. Was a local MeshCore logo + website + version block;
    // five roles each carried their own copy and all five shipped MeshCore artwork.
    offband::SplashInfo si( nullptr, _mc_version, _build_date, "< Sensor >" );
    offband::drawSplash(*_display, si);
  } else {  // home screen
    // node name
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(UIColor::primary_txt);
    _display->print(_node_prefs->node_name);

    // freq / sf
    _display->setCursor(0, 20);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    // bw / cr
    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);
  }
}

void UITask::loop() {
  uint8_t disp_mode = _node_prefs->ui_display_mode;   // #542 A2
  if (disp_mode != _last_disp_mode) {                 // mode changed via CLI -> apply cleanly
    _last_disp_mode = disp_mode;
    if (disp_mode == DISPLAY_MODE_ALWAYS_OFF) {
      _display->turnOff();
    } else {                                          // auto / always-on: relight + fresh timer
      _display->turnOn();
      _auto_off = millis() + AUTO_OFF_MILLIS;
    }
  }
  if (disp_mode == DISPLAY_MODE_ALWAYS_OFF) {
    return;                        // stay dark; a button does not wake a deliberately-off screen
  }
#ifdef PIN_USER_BTN
  if (millis() >= _next_read) {
    int btnState = digitalRead(PIN_USER_BTN);
    if (btnState != _prevBtnState) {
      if (btnState == USER_BTN_PRESSED) {  // pressed?
        if (_display->isOn()) {
          // TODO: any action ?
        } else {
          _display->turnOn();
        }
        _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
      }
      _prevBtnState = btnState;
    }
    _next_read = millis() + 200;  // 5 reads per second
  }
#endif

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

      _next_refresh = millis() + 1000;   // refresh every second
    }
    if (disp_mode != DISPLAY_MODE_ALWAYS_ON && millis() > _auto_off) {
      _display->turnOff();
    }
  }
}
