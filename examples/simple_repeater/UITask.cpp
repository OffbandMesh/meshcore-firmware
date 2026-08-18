#include "UITask.h"
#include <helpers/ui/OffbandSplash.h>   // #822: the one shared splash
#include "target.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds

#define POWEROFF_DELAY 3000

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _started_at = millis();
  _node_prefs = node_prefs;
  _last_disp_mode = _node_prefs->ui_display_mode;   // #542 A2: seed so loop() sees no false transition
  if (_node_prefs->ui_display_mode == DISPLAY_MODE_ALWAYS_OFF) {
    _display->turnOff();          // #542 A2: boot dark
  } else {
    _display->turnOn();           // auto + always-on both start lit
  }

#if defined(PIN_USER_BTN) && defined(DISPLAY_CLASS)
  user_btn.begin();
#endif

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
  if (millis() < _started_at + BOOT_SCREEN_MILLIS) { // boot screen


    // #822: THE shared splash. Was a local MeshCore logo + website + version block;
    // five roles each had their own copy and all five shipped MeshCore artwork.
    offband::SplashInfo si( nullptr, _mc_version, _build_date, "< Repeater >" );
    offband::drawSplash(*_display, si);
  } else if (_powering_off_at > 0) {
    // #822: this screen showed the MeshCore logo + website. It keeps its branding,
    // now Offband's, rather than being left blank when that artwork was removed.
    offband::drawBrandLockup(*_display, 3);

    // Powering off
    const char* poweroff_string = "Turning OFF";
    uint16_t poffWidth = _display->getTextWidth(poweroff_string);
    _display->setCursor((_display->width() - poffWidth) / 2, 48);
    _display->drawTextCentered(_display->width()/2, 48, poweroff_string);
  } else {
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
  // #542 display modes above are Offband; the button read below is upstream's
  // MomentaryButton abstraction (1.17.0), replacing our raw digitalRead poll.
  // Both are required here: the code after this block reads `disp_mode` from
  // the preamble AND `ev` from the abstraction.
#if defined(PIN_USER_BTN) && defined(DISPLAY_CLASS)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    if (_display->isOn()) {
      // TODO: any action ?
    } else {
      _display->turnOn();
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      _display->turnOn();
      Serial.println("Powering Off");
      _powering_off_at = millis() + POWEROFF_DELAY; 
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

  if (_powering_off_at > 0) { // power off timer armed
#ifdef LED_PIN
    digitalWrite(LED_PIN, LED_STATE_ON); // switch on the led until poweroff
#endif
    if (millis() > _powering_off_at) {
      _board->powerOff();  // should not return
    }
  }
}
