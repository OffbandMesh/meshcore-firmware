#include "UITask.h"
#ifdef PIN_BUZZER
  #include "TuneSlotStore.h"   // #1475: custom slots resolve through the store
#endif
#ifdef PIN_QCC_ACTIVITY_LED
  // #1367: the dispatcher -> UI traffic seam. Up here, not beside the scheduler further
  // down: the sink is registered in begin(), which comes first in this file, so an
  // include placed with the implementation would arrive after its own use.
  #include "helpers/ui/LedActivity.h"
#endif
#include "helpers/diagnostics/CrashLog.h"   // #1075: the [shutdown] line
#include "helpers/ui/OffbandSplash.h"
#include <helpers/ui/KeyNav.h>
#include <helpers/ui/ScreenName.h>   // #973: null-first screen labels for the trace
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#ifdef WIFI_SSID
  #include <WiFi.h>
#endif
#ifdef OFFBAND_OBSERVER
  #include <helpers/diagnostics/CrashLog.h>
#endif
#ifdef QCC_BADGE_SELFTEST
  #include <SafeBoot.h>
  #include "QccSelfTest.h"
#endif
#if defined(QCC_BADGE_SELFTEST) && UI_HAS_CARDKB
  #include <helpers/ui/LineEdit.h>
#endif
#if UI_HAS_CARDKB
  #include "BadgeScreens.h"   // #1230: the inbox and thread screens
#endif
#include <helpers/ui/ScreenOff.h>   // #1245: the auto-off arithmetic, for every board

// #1245: the compiled fallback, for a board with no screen-off preference set. The
// badge has one; every other board keeps this, and keeps the behavior it has now.
#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"

// #153 added offbandIsDevBuild() to gate debug-only UI (the heap readout) to dev
// builds. #761 replaced that with an explicit -D OFFBAND_SHOW_HEAP opt-in, and it
// had no other callers, so it is removed rather than left to rot as dead code
// whose comment describes a policy the tree no longer follows.

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;

public:
  SplashScreen(UITask* task) : _task(task) {
    // #822: the version derivation and the commit-hash trim both moved into
    // offband::drawSplash, so every role renders the same identity.
    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
#ifdef OFFBAND_OBSERVER
    offband::crashLogf("[ui] SplashScreen.render() at %lu", (unsigned long)millis());
#endif
    offband::SplashInfo si( nullptr, FIRMWARE_VERSION, FIRMWARE_BUILD_DATE );
    offband::drawSplash(display, si);

    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
#ifdef QCC_BADGE_SELFTEST
      _task->gotoSelfTest();
#else
      _task->gotoHomeScreen();
#endif
    }
  }
};

#ifdef QCC_BADGE_SELFTEST
// Bring-up legend (#1173): names the badge's physical outputs so they can be found
// without opening the badge, and shows the battery reading SafeBoot let this boot
// through on (#1185). Shown once after the splash, on diag builds only.
class SelfTestScreen : public UIScreen {
  UITask* _task;
  unsigned long _dismiss_after = 0;

public:
  explicit SelfTestScreen(UITask* task) : _task(task) {}
  void arm() { _dismiss_after = millis() + 6000; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);
    display.drawTextLeftAlign(0, 0, "SELF-TEST (diag)");
    display.drawTextLeftAlign(0, 16, "P0.08 LED: heartbeat");
    display.drawTextLeftAlign(0, 28, "P0.06 buzz: boot tune");
    char safeboot[qcc::kSelfTestLineChars + 1];
    qcc::formatSafeBootLine(safeboot, sizeof(safeboot), SafeBoot::bootBattMilliVolts());
    display.drawTextLeftAlign(0, 40, safeboot);
#if UI_HAS_CARDKB
    char kbd[qcc::kSelfTestLineChars + 1];
    qcc::formatKeyboardLine(kbd, sizeof(kbd), _task->hasKeyboard());
    display.drawTextLeftAlign(0, 52, kbd);
#endif
    return 500;
  }

  void poll() override {
    if (millis() >= _dismiss_after) _task->gotoHomeScreen();
  }
};
#endif

#if defined(QCC_BADGE_SELFTEST) && UI_HAS_CARDKB
// Diag key test (#1207): the bench instrument for the keyboard and SW1. It shows the
// last key as its name and raw code (Fn-layer keys included, which the UI otherwise
// ignores), SW1's last gesture, and a typed line. Esc leaves, and so does a long SW1
// press, so a keyboard that fails mid-test cannot strand the screen. TAB on Home opens it.
class KeyTestScreen : public UIScreen {
  UITask* _task;
  LineEdit<40> _line;

public:
  explicit KeyTestScreen(UITask* task) : _task(task) {}
  void reset() { _line.clear(); }

  int render(DisplayDriver& display) override {
    char name[12], row[qcc::kSelfTestLineChars + 1];
    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);
    display.drawTextLeftAlign(0, 0, "KEY TEST  ESC=exit");
    const uint8_t raw = _task->lastKeyboardRaw();
    if (raw == 0) {
      snprintf(row, sizeof(row), "KB  -");
    } else {
      cardkb::keyName(raw, name, sizeof(name));
      snprintf(row, sizeof(row), "KB  %s 0x%02X", name, (unsigned)raw);
    }
    display.drawTextLeftAlign(0, 16, row);
    snprintf(row, sizeof(row), "SW1 %s", buttonEventName(_task->lastButtonEvent()));
    display.drawTextLeftAlign(0, 28, row);
    snprintf(row, sizeof(row), ">%s_", _line.tail(qcc::kSelfTestLineChars - 2));
    display.drawTextLeftAlign(0, 44, row);
    return 200;   // Fn keys and triple-clicks change the screen without a dispatch
  }

  bool handleInput(char c) override {
    const uint8_t key = (uint8_t)c;
    if (!_task->inputFromKeyboard()) {
      if (key == KEY_ENTER) _task->gotoHomeScreen();   // SW1 long press leaves
      return true;                                     // other gestures are only shown
    }
    if (key == KEY_CANCEL) return false;   // Esc: UITask backs out to Home
    if (key == KEY_ENTER) {
      _line.clear();
      return true;
    }
    _line.apply(key);
    return true;   // arrows and the rest are shown on the KB line and do nothing else
  }
};
#endif

class HomeScreen : public UIScreen {
  enum HomePage {
    FIRST,
    RECENT,
    RADIO,
    BLUETOOTH,
    ADVERT,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
    SHUTDOWN,
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  AdvertPath recent[UI_RECENT_LIST_SIZE];


  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
    // Convert millivolts to percentage
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
    const int minMilliVolts = BATT_MIN_MILLIVOLTS;
    const int maxMilliVolts = BATT_MAX_MILLIVOLTS;
    int batteryPercentage = ((batteryMilliVolts - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
    if (batteryPercentage < 0) batteryPercentage = 0; // Clamp to 0%
    if (batteryPercentage > 100) batteryPercentage = 100; // Clamp to 100%

    // battery icon
    int iconWidth = 24;
    int iconHeight = 10;
    int iconX = display.width() - iconWidth - 5; // Position the icon near the top-right corner
    int iconY = 0;
    display.setColor(UIColor::title_txt);

    // battery outline
    display.drawRect(iconX, iconY, iconWidth, iconHeight);

    // battery "cap"
    display.fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 3, iconHeight / 2);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
    display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);

    // show muted icon if buzzer is muted
#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(UIColor::warning_txt);
      display.drawXbm(iconX - 9, iconY + 1, muted_icon, 8, 8);
    }
#endif
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;

  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0),
       _shutdown_init(false), sensors_lpp(200) {  }

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    display.setColor(UIColor::title_bkg);
    display.fillRect(0, 0, display.width(), 12);
    char tmp[80];
    // node name
    display.setTextSize(1);
    display.setColor(UIColor::title_txt);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    display.setCursor(0, 2);
    display.print(filtered_name);

    // battery voltage
    renderBatteryIndicator(display, _task->smoothedBattMilliVolts());   // #1246

    // curr page indicator
    if (UIColor::title_bkg == UIColor::window_bkg) {
      display.setColor(UIColor::title_txt);
    } else {
      display.setColor(UIColor::title_bkg);
    }
    int y = 14;
    int x = display.width() / 2 - 5 * (HomePage::Count-1);
    for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
      if (i == _page) {
        display.fillRect(x-1, y-1, 4, 4);
      } else {
        display.fillRect(x, y, 2, 2);
      }
    }

    if (_page == HomePage::FIRST) {
#if UI_HAS_CARDKB
      // #1231: on the badge these pages are tools, opened from Status, which now holds
      // the message count and the pairing PIN.
      display.setColor(UIColor::primary_txt);
      display.setTextSize(2);
      display.drawTextCentered(display.width() / 2, 22, "Tools");
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 44, "Enter: back");
      return 5000;
#endif
      display.setColor(UIColor::primary_txt);
      display.setTextSize(2);
      sprintf(tmp, "MSG: %d", _task->getMsgCount());
      display.drawTextCentered(display.width() / 2, 22, tmp);

      #ifdef WIFI_SSID
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 54, tmp);
      #endif
      if (_task->hasConnection()) {
        display.setColor(UIColor::warning_txt);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 43, "< Connected >");

      } else if (the_mesh.getBLEPin() != 0) { // BT pin
        display.setColor(UIColor::warning_txt);
        display.setTextSize(2);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, 43, tmp);
      }
    } else if (_page == HomePage::RECENT) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      display.setColor(UIColor::primary_txt);
      int y = 20;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++, y += 11) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(tmp, "%ds", secs);
        } else if (secs < 60*60) {
          sprintf(tmp, "%dm", secs / 60);
        } else {
          sprintf(tmp, "%dh", secs / (60*60));
        }

        int timestamp_width = display.getTextWidth(tmp);
        int max_name_width = display.width() - timestamp_width - 1;

        char filtered_recent_name[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_recent_name, a->name, sizeof(filtered_recent_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_recent_name);
        display.setCursor(display.width() - timestamp_width - 1, y);
        display.print(tmp);
      }
    } else if (_page == HomePage::RADIO) {
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);
      // freq / sf
      display.setCursor(0, 20);
      sprintf(tmp, "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
      display.print(tmp);

      display.setCursor(0, 31);
      sprintf(tmp, "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
      display.print(tmp);

      // tx power,  noise floor
      display.setCursor(0, 42);
      sprintf(tmp, "TX: %ddBm", _node_prefs->tx_power_dbm);
      display.print(tmp);
      display.setCursor(0, 53);
      sprintf(tmp, "Noise floor: %d", radio_driver.getNoiseFloor());
      display.print(tmp);
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(UIColor::corp_blue);
      display.drawXbm((display.width() - 32) / 2, 18,
          _task->isBluetoothEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      display.setColor(UIColor::secondary_txt);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 64 - 11, "toggle: " PRESS_LABEL);
    } else if (_page == HomePage::ADVERT) {
      display.setColor(UIColor::corp_blue);
      display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(display.width() / 2, 64 - 11, "advert: " PRESS_LABEL);
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = 18;
      bool gps_state = _task->getGPSState();
#ifdef PIN_GPS_SWITCH
      bool hw_gps_state = digitalRead(PIN_GPS_SWITCH);
      if (gps_state != hw_gps_state) {
        strcpy(buf, gps_state ? "gps off(hw)" : "gps off(sw)");
      } else {
        strcpy(buf, gps_state ? "gps on" : "gps off");
      }
#else
      strcpy(buf, gps_state ? "gps on" : "gps off");
#endif
      display.setColor(UIColor::primary_txt);
      display.drawTextLeftAlign(0, y, buf);
      if (nmea == NULL) {
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "Can't access GPS");
      } else {
        display.setColor(UIColor::primary_txt);
        strcpy(buf, nmea->isValid()?"fix":"no fix");
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "sat");
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "pos");
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%.4f %.4f",
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "alt");
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%.2f", nmea->getAltitude()/1000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
      }
#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      int y = 18;
      refresh_sensors();
      char buf[30];
      char name[30];
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

      for (int i = 0; i < sensors_scroll_offset; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      for (int i = 0; i < (sensors_scroll?UI_RECENT_LIST_SIZE:sensors_nb); i++) {
        uint8_t channel, type;
        if (!r.readHeader(channel, type)) { // reached end, reset
          r.reset();
          r.readHeader(channel, type);
        }

        display.setCursor(0, y);
        float v;
        switch (type) {
          case LPP_GPS: // GPS
            float lat, lon, alt;
            r.readGPS(lat, lon, alt);
            strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
            break;
          case LPP_VOLTAGE:
            r.readVoltage(v);
            strcpy(name, "voltage"); sprintf(buf, "%6.2f", v);
            break;
          case LPP_CURRENT:
            r.readCurrent(v);
            strcpy(name, "current"); sprintf(buf, "%.3f", v);
            break;
          case LPP_TEMPERATURE:
            r.readTemperature(v);
            strcpy(name, "temperature"); sprintf(buf, "%.2f", v);
            break;
          case LPP_RELATIVE_HUMIDITY:
            r.readRelativeHumidity(v);
            strcpy(name, "humidity"); sprintf(buf, "%.2f", v);
            break;
          case LPP_BAROMETRIC_PRESSURE:
            r.readPressure(v);
            strcpy(name, "pressure"); sprintf(buf, "%.2f", v);
            break;
          case LPP_ALTITUDE:
            r.readAltitude(v);
            strcpy(name, "altitude"); sprintf(buf, "%.0f", v);
            break;
          case LPP_POWER:
            r.readPower(v);
            strcpy(name, "power"); sprintf(buf, "%6.2f", v);
            break;
          default:
            r.skipData(type);
            strcpy(name, "unk"); sprintf(buf, "");
        }
        display.setCursor(0, y);
        display.setColor(UIColor::secondary_txt);
        display.print(name);
        display.setColor(UIColor::primary_txt);
        display.setCursor(
          display.width()-display.getTextWidth(buf)-1, y
        );
        display.print(buf);
        y = y + 12;
      }
      if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset+1)%sensors_nb;
      else sensors_scroll_offset = 0;
#endif
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(UIColor::corp_blue);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.setColor(UIColor::warning_txt);
        display.drawTextCentered(display.width() / 2, 34, "hibernating...");
      } else {
        display.setColor(UIColor::secondary_txt);
        display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate:" PRESS_LABEL);
      }
    }

    // Diagnostic: free_heap on every home page so it is visible without
    // serial monitor (CP2102 monitor-connect DTR/RTS toggle resets the
    // chip on V3 hardware, which can throw the device into a crash-cycle
    // when heap is already tight). Same value as the [hb] heartbeat log
    // line in CrashLog.cpp (ESP.getFreeHeap()), refreshed at the screen's
    // native 5-second cadence (return 5000 below).
#if (defined(ESP32) || defined(ESP_PLATFORM)) && defined(OFFBAND_SHOW_HEAP)
    // Heap readout was added for the ESP32 V3/V4 heap crisis; ESP.* is
    // ESP32-only, so guard it -- nRF52 companions otherwise fail to compile
    // ('ESP' not declared). #8.
    //
    // #761: now OPT-IN via -D OFFBAND_SHOW_HEAP, and off by default everywhere.
    // It was previously gated on offbandIsDevBuild(), which meant it was absent
    // from tagged releases but unavoidable on every bench build -- i.e. present
    // exactly when someone is doing UI work and does not want a debug readout
    // occupying the bottom line, with no way to switch it off.
    //
    // Deliberately kept rather than deleted: #624 (RCC6/C6) and #625 (RC52/nRF52)
    // are boards where heap is the open question, and on this board class
    // attaching a serial console perturbs what is being measured (#756), so an
    // on-screen readout with no host attached is the right instrument. One build
    // flag away when a heap investigation needs it.
    char heap_tmp[24];
    snprintf(heap_tmp, sizeof(heap_tmp), "Heap:%u", (unsigned)ESP.getFreeHeap());
    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);
    display.drawTextCentered(display.width() / 2, 56, heap_tmp);
#endif

    return 5000;   // next render after 5000 ms
  }

  bool handleInput(char c) override {
    // #1205: the arrows move pages too, Up and Down included, the same way the
    // button's click and double-click do.
    const keynav::Step step = keynav::pageStep((uint8_t)c);
    if (step != keynav::Step::None) {
      _page = keynav::stepPage(_page, HomePage::Count, step);
      if (step == keynav::Step::Next && _page == HomePage::RECENT) {
        _task->showAlert("Recent adverts", 800);
      }
      return true;
    }
#if defined(QCC_BADGE_SELFTEST) && UI_HAS_CARDKB
    if ((uint8_t)c == KEY_TAB && _task->hasKeyboard()) {   // #1207: diag key test
      _task->gotoKeyTest();
      return true;
    }
#endif
#if UI_HAS_CARDKB
    if (c == KEY_ENTER && _page == HomePage::FIRST) {   // #1233: back to Settings, which opened them
      _task->gotoSettings();
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isBluetoothEnabled()) {  // toggle Bluetooth on/off
        _task->disableBluetooth();
      } else {
        _task->enableBluetooth();
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert("Advert sent!", 1000);
      } else {
        _task->showAlert("Advert failed..", 1000);
      }
      return true;
    }
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && _page == HomePage::GPS) {
      _task->toggleGPS();
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::SHUTDOWN) {
      _shutdown_init = true;  // need to wait for button to be released
      return true;
    }
    return false;
  }
};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[78];
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  int head = MAX_UNREAD_MSGS - 1; // index of latest unread message
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    head = (head + 1) % MAX_UNREAD_MSGS;
    if (num_unread < MAX_UNREAD_MSGS) num_unread++;

    auto p = &unread[head];
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    char tmp[16];
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(UIColor::corp_blue);
    sprintf(tmp, "Unread: %d", num_unread);
    display.print(tmp);

    auto p = &unread[head];

    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(tmp, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(tmp, "%dm", secs / 60);
    } else {
      sprintf(tmp, "%dh", secs / (60*60));
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);  // horiz line

    display.setCursor(0, 14);
    display.setColor(UIColor::secondary_txt);
    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));
    display.print(filtered_origin);

    display.setCursor(0, 25);
    display.setColor(UIColor::primary_txt);
    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    display.printWordWrap(filtered_msg, display.width());

#if AUTO_OFF_MILLIS==0 // probably e-ink
    return 10000; // 10 s
#else
    return 1000;  // next render after 1000 ms
#endif
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
      num_unread--;
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      }
      return true;
    }
    if (c == KEY_ENTER) {
      num_unread = 0;  // clear unread queue
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + autoOffMillis();

#ifdef PIN_QCC_ACTIVITY_LED
  // #1367: own the activity LED and start it dark, then let the dispatcher tell us about
  // decoded traffic. Registered here rather than in a constructor so the sink only goes
  // live once there is a UI to receive it -- the dispatcher starts receiving early.
  pinMode(PIN_QCC_ACTIVITY_LED, OUTPUT);
  _activity_lit = true;          // force setActivityLed() past its no-change guard
  setActivityLed(false);
  _attention_phase = millis();
  _activity_owner = this;
  offband::setLedActivitySink(&UITask::onTrafficDecoded);
#endif

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif
#if UI_HAS_CARDKB
  // #1205: optional. Without it the badge stays button-driven. Wire is already up
  // from board.begin().
  const bool kbd_found = _kbd.begin(Wire);
  MESH_DEBUG_PRINTLN("UITask: CardKB keyboard %s", kbd_found ? "found" : "not found");
#endif

  _node_prefs = node_prefs;

  // #542 B1: seed the display mode from prefs and set the boot state accordingly.
  _disp_mode = (_node_prefs != NULL) ? _node_prefs->ui_display_mode : 0;
  _always_on = (_disp_mode == 1);
  if (_display != NULL) {
    if (_disp_mode == 2) {
      _display->turnOff();   // always-off: boot dark
    } else {
      _display->turnOn();
    }
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
  buzzer.startup();
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;

  // #973: every screen below used to be an unchecked `new`. A null screen is
  // COMPLETELY silent: setCurrScreen() stored it, the render guard further down
  // (`millis() >= _next_refresh && curr`) stopped firing, and the panel held its last
  // blitted frame forever -- on a board whose radio, mesh, BLE and heartbeat were all
  // healthy. No error, no log line, no degraded mode. On RC52 that cost a flash cycle,
  // a purpose-built driver instrument and a UART sniffer to find, for a failure a null
  // check names instantly.
  //
  // MLOG_BOOT via mesh_log_line(), NOT crashLogf(): every crashLogf site in this file
  // is gated on OFFBAND_OBSERVER, which implies WiFi and so compiles out on every
  // nRF52 board (#972) -- precisely the boards tight enough on RAM to hit this. BOOT
  // level survives whatever caplog level is configured and reaches the UART0 mirror.
  //
  // Allocation is NOT retried and the board is NOT halted: #973 is strictly that the
  // failure was silent. The memory constraint itself is #856.
#define UI_NEW_SCREEN(member, expr)                                                   \
  do {                                                                                \
    (member) = (expr);                                                                \
    if (!(member)) {                                                                  \
      _screen_alloc_failed = true;                                                    \
      mesh_log_line(MLOG_BOOT, "[ui] SCREEN ALLOC FAILED: %s\r\n", #member);          \
    }                                                                                 \
  } while (0)

  UI_NEW_SCREEN(splash, new SplashScreen(this));
#if UI_HAS_CARDKB
  // #1230: the Messages inbox is Home (owner: "I want the Unread/messages screen to
  // become the Home screen instead of the PIN screen"). The pages Home held stay
  // reachable as tools. A message arriving flashes its row in the inbox instead of
  // opening the preview popup, so the popup's queue isn't allocated.
  UI_NEW_SCREEN(home, new InboxScreen(this));
  UI_NEW_SCREEN(thread, new ThreadScreen(this));
  UI_NEW_SCREEN(tools, new HomeScreen(this, &rtc_clock, sensors, node_prefs));
  UI_NEW_SCREEN(contacts, new ContactsScreen(this));   // #1231
  UI_NEW_SCREEN(nearby, new NearbyScreen(this));       // #1234
  UI_NEW_SCREEN(status, new StatusScreen(this));       // #1231
  UI_NEW_SCREEN(settings, new SettingsScreen(this));   // #1233
  UI_NEW_SCREEN(zones, new ZonePickerScreen(this));    // #1233
  UI_NEW_SCREEN(gps, new GpsScreen(this));             // #1235
  UI_NEW_SCREEN(battery, new BatteryScreen(this));     // #1254
#ifdef PIN_BUZZER
  UI_NEW_SCREEN(tones, new TonesScreen(this));               // #1421
  UI_NEW_SCREEN(tone_picker, new TonePickerScreen(this));    // #1421
  UI_NEW_SCREEN(custom_tunes, new CustomTunesScreen(this));  // #1476
  UI_NEW_SCREEN(rtttl_edit, new RtttlEditScreen(this));      // #1477
#endif
  // DELIBERATELY null on this build, not a failure: a message arriving flashes its
  // inbox row instead of opening the preview popup, so the popup is never allocated.
  // Left as a plain assignment so UI_NEW_SCREEN never reports it.
  msg_preview = NULL;
#else
  UI_NEW_SCREEN(home, new HomeScreen(this, &rtc_clock, sensors, node_prefs));
  UI_NEW_SCREEN(msg_preview, new MsgPreviewScreen(this, &rtc_clock));
#endif
#ifdef QCC_BADGE_SELFTEST
  UI_NEW_SCREEN(self_test, new SelfTestScreen(this));
#endif
#if defined(QCC_BADGE_SELFTEST) && UI_HAS_CARDKB
  UI_NEW_SCREEN(key_test, new KeyTestScreen(this));
#endif
#undef UI_NEW_SCREEN

  // #973: one summary line, so a capture that caught only the tail of boot still says
  // the UI came up degraded. The per-screen lines above name which ones.
  if (_screen_alloc_failed) {
    mesh_log_line(MLOG_BOOT,
                  "[ui] DEGRADED: one or more screens failed to allocate; "
                  "navigation to them will be refused (#973, memory: #856)\r\n");
  }

  setCurrScreen(splash);
}

// #141: display always-on toggle. Persisted in NVS (offband_ui); applied here
// live by the observer CLI (and at boot) via the applier registered in main.cpp.
// #542 B1: reconciled onto the tristate — always-on is mode 1, normal is mode 0 (auto).
void UITask::setAlwaysOn(bool on) {
  // #542 B1: the legacy bool cannot express always-off (mode 2). If the user set
  // always-off explicitly (via the 0xC5 command), a legacy setAlwaysOn(false) must
  // NOT silently downgrade it to auto -- the newer, more specific control wins.
  if (_disp_mode == 2) return;
  setDisplayMode(on ? 1 : 0);
}

#ifdef QCC_BADGE_SELFTEST
void UITask::gotoSelfTest() {
  ((SelfTestScreen*)self_test)->arm();
  setCurrScreen(self_test);
}
#endif

#if defined(QCC_BADGE_SELFTEST) && UI_HAS_CARDKB
void UITask::gotoKeyTest() {
  ((KeyTestScreen*)key_test)->reset();
  setCurrScreen(key_test);
}
#endif

#if UI_HAS_CARDKB
void UITask::gotoThread(int convo, char first_key) {
  ((ThreadScreen*)thread)->begin(convo);
  setCurrScreen(thread);
  the_mesh.badgeStore().setOpen(convo);   // setCurrScreen closed it
  if (first_key != 0) thread->handleInput(first_key);
}

void UITask::gotoTools() {
  setCurrScreen(tools);
}

void UITask::gotoStatus() {
  setCurrScreen(status);
}

void UITask::gotoSettings() {
  ((SettingsScreen*)settings)->begin();
  setCurrScreen(settings);
}

void UITask::gotoZones() {
  ((ZonePickerScreen*)zones)->begin();
  setCurrScreen(zones);
}

void UITask::gotoGps() {
  ((GpsScreen*)gps)->begin();
  setCurrScreen(gps);
}

void UITask::gotoBattery() { setCurrScreen(battery); }   // #1254

#if UI_HAS_CARDKB && defined(PIN_BUZZER)
// #1421. #1476: re-read the slot names on entry, because the Custom row shows a count
// that comes from the cache -- same reason the slot list itself re-reads. Eight reads
// per visit to this screen, not per render, which bounds staleness to one visit rather
// than resting on "nothing else ever writes those files".
void UITask::gotoTones() {
  offband::invalidateSlotNames();
  setCurrScreen(tones);
}

// #1478: both guards, matching the declaration -- see UITask.h.
#if UI_HAS_CARDKB && defined(PIN_BUZZER)
uint8_t UITask::feedTuneSlot(uint8_t slot, uint8_t offset, bool more, const uint8_t* bytes,
                            size_t len, uint8_t& accepted) {
  using Res = offband::SlotAssembly<offband::kMaxRtttl>::Result;
  accepted = 0;
  if (slot >= offband::kSlotCount) return (uint8_t)Res::BadSlot;

  const Res r = _slot_push.feed(slot, offset, more, bytes, len);
  accepted = (uint8_t)_slot_push.length();
  if (r != Res::Complete) return (uint8_t)r;

  // Committed. The SAME store call a typed tune goes through, so the same validator and
  // the same refusals apply -- an empty string clears the slot, which is how a client
  // deletes one without a third sub-code.
  const bool ok = offband::saveSlotRtttl(the_mesh.dataStore(), slot, _slot_push.text());
  _slot_push.reset();
  accepted = 0;
  // A store refusal is reported as a bad byte rather than invented as a new code: the
  // only ways saveSlotRtttl says no are a string the validator rejects, one over the
  // length, or a write that failed -- and the client's remedy for all three is the same,
  // which is to look at what it sent.
  return ok ? (uint8_t)Res::Complete : (uint8_t)Res::BadByte;
}

bool UITask::readTuneSlot(uint8_t slot, uint8_t& len, char* name, size_t name_cap) {
  len = 0;
  if (name != nullptr && name_cap > 0) name[0] = 0;
  if (slot >= offband::kSlotCount) return false;
  // Through the CALLER's buffer, never the playback stage: answering a client query must
  // not disturb a tune that is sounding.
  char tune[offband::kMaxRtttl + 1];
  const size_t n = offband::readSlotRtttl(the_mesh.dataStore(), slot, tune, sizeof(tune));
  len = (uint8_t)(n > 255 ? 255 : n);
  if (n > 0 && name != nullptr) offband::deriveSlotName(tune, name, name_cap);
  return true;
}
#endif

void UITask::gotoRtttlEdit(uint8_t slot_index) {
  ((RtttlEditScreen*)rtttl_edit)->begin(slot_index);
  setCurrScreen(rtttl_edit);
}

void UITask::gotoCustomTunes() {
  ((CustomTunesScreen*)custom_tunes)->begin();
  setCurrScreen(custom_tunes);
}

void UITask::gotoTonePicker(offband::ToneEvent e) {
  _tone_picking = e;
  _tone_picking_channel = -1;   // #1525: event mode, so no Default row
  ((TonePickerScreen*)tone_picker)->begin();
  setCurrScreen(tone_picker);
}

// #1525: the same picker, for one conversation.
//
// _tone_picking stays at ChannelMessage because that is what the conversation INHERITS
// when it has chosen nothing -- the Default row shows that tune's name, and the picker
// resolves against it. So the event the picker was built around is still the right
// fallback here rather than something this mode has to carry separately.
void UITask::gotoTonePickerForChannel(uint8_t channel_idx, int convo) {
  _tone_picking = offband::ToneEvent::ChannelMessage;
  _tone_picking_channel = (int)channel_idx;
  _tone_picking_convo = convo;
  ((TonePickerScreen*)tone_picker)->begin();
  setCurrScreen(tone_picker);
}

void UITask::leaveTonePicker() {
  const int convo = _tone_picking_channel >= 0 ? _tone_picking_convo : -1;
  // Cleared on the way out, so the mode cannot outlive the visit. Review's point: a
  // future caller that opens the picker without clearing this would inherit a stale
  // channel and be sent back to a conversation it was never in. Clearing here means only
  // the call that SETS the mode can turn it on.
  _tone_picking_channel = -1;
  _tone_picking_convo = -1;
  if (convo >= 0) {
    gotoThread(convo);
    return;
  }
  gotoTones();
}

bool UITask::playTriggerTune() {
  // One call into the resolver that already knows how to find the id, honour quiet and
  // MUTE, and refuse politely. Nothing here duplicates any of that.
  const offband::TonePlay r =
      playTuneById(offband::kTuneTrigger, offband::TonePriority::Trigger);
  return r == offband::TonePlay::Played;
}

offband::TonePlay UITask::playTuneById(uint8_t id, offband::TonePriority priority) {
  // Silent is a CHOICE: it sounds like nothing and that is success.
  if (id == offband::kTuneSilent) return offband::TonePlay::Played;
  // #1475: a custom slot. Asked BEFORE reading the file, because staging overwrites the
  // one buffer the player may still be walking -- if play() refused after the read, the
  // tune still sounding would be left pointing at the new tune's bytes. The pre-check is
  // not a second copy of the priority rule; it is the same rule, queried.
  if (offband::isSlotId(id)) {
    if (!buzzer.wouldTake(priority)) return offband::TonePlay::RefusedBusy;
    // Stop BEFORE staging. wouldTake() has already said this request takes the buzzer,
    // so stopping costs nothing -- and it closes the one window review found that is
    // actually reachable: stageSlotForPlayback writes into the shared buffer as it reads
    // the file and only validates afterwards, so a corrupt slot overwrites the buffer and
    // then returns nullptr with no play() call. Without this, a tune already sounding
    // would be left walking those bytes.
    buzzer.stop();
    const char* staged =
        offband::stageSlotForPlayback(the_mesh.dataStore(), offband::slotIndex(id));
    // An empty slot, a slot whose file will not read, or one holding a string the
    // validator refuses. All three are "this id resolves to nothing", which is what
    // NotFound means; the picker folds it back to Played and a trigger does not.
    if (staged == nullptr) return offband::TonePlay::NotFound;
    // No loop() runs between the stage above and the play below, so the player never
    // reads a half-written buffer. That is the invariant, not the stop.
    return buzzer.play(staged, priority);
  }
  const char* melody = offband::tuneRtttl(id);
  // Anything else with no melody is an id this build cannot resolve -- past the table
  // after a downgrade, or a slot id from a build with more slots. A remote trigger needs
  // to tell that apart from "played", because for it the badge making no noise is a
  // failure it may want to report. The picker does not care, so previewTune() folds it.
  if (melody == nullptr) return offband::TonePlay::NotFound;
  return buzzer.play(melody, priority);
}

void UITask::stopTune() { buzzer.stop(); }
#endif

int UITask::renderStatusAs(DisplayDriver& d, const char* title, int pos) {
  return ((StatusScreen*)status)->drawAs(d, title, pos);
}

int UITask::cyclePos() const {
  if (curr == contacts) return 1;
  if (curr == nearby) return 2;
  if (curr == status) return 3;
  return 0;   // Messages, and anything reached from it
}

void UITask::cycle(int step) {
  const int pos = (cyclePos() + step + badgeui::kCycleStops) % badgeui::kCycleStops;
  if (pos == 1) {
    ((ContactsScreen*)contacts)->reload();
    setCurrScreen(contacts);
  } else if (pos == 2) {
    ((NearbyScreen*)nearby)->reload();
    setCurrScreen(nearby);
  } else if (pos == 3) {
    setCurrScreen(status);
  } else {
    setCurrScreen(home);
  }
  _cycle_at = millis();
}
#endif

// #542 B1: OLED mode. 0 auto (on, blanks after timeout), 1 always-on, 2 always-off (dark).
// Applied live; the loop's blank decision honours _disp_mode. Reused by the observer
// #141 applier (via setAlwaysOn above) and the 0xC5 display SET sub-code.
void UITask::setDisplayMode(uint8_t mode) {
  _disp_mode = mode;
  _always_on = (mode == 1);
  if (_display == NULL) return;
  if (mode == 2) {
    if (_display->isOn()) _display->turnOff();     // always-off: dark now, stays dark
  } else {
    if (!_display->isOn()) _display->turnOn();      // auto / always-on: light it now
    if (mode == 0) _auto_off = millis() + autoOffMillis();  // fresh timeout only for auto (always-on never blanks)
  }
}

// #148: request a rotation; the render loop applies it at the frame boundary.
void UITask::requestRotation(uint8_t deg) {
  _rotation = (int)deg;
  _rotation_dirty = true;
}

// #148: report whether the live display driver implements runtime rotation.
bool UITask::displaySupportsRotation() const {
  return _display && _display->supportsRotation();
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

#if defined(PIN_BUZZER)
// #1419: the UI's event enum, in the tune table's terms. `none` and anything unmapped
// has no tone event, so it stays silent rather than falling onto some other event's
// tune.
static bool toneEventFor(UIEventType t, offband::ToneEvent& out) {
  switch (t) {
    case UIEventType::contactMessage:    out = offband::ToneEvent::ContactMessage; return true;
    case UIEventType::channelMessage:    out = offband::ToneEvent::ChannelMessage; return true;
    case UIEventType::roomMessage:       out = offband::ToneEvent::RoomMessage;    return true;
    case UIEventType::newContactMessage: out = offband::ToneEvent::NewContact;     return true;
    case UIEventType::ack:               out = offband::ToneEvent::Ack;            return true;
    case UIEventType::sent:              out = offband::ToneEvent::Sent;           return true;
    case UIEventType::none:
    default:
      return false;
  }
}

// #1420: the id stored for this event, or `kTuneUnset` when nothing is stored -- which
// is also the answer when prefs are not available yet. Unset means "use this board's
// compiled default", so a missing pref and an un-loaded prefs object both land on the
// same safe behavior rather than on silence.
#endif

#if UI_HAS_CARDKB && defined(PIN_BUZZER)
// #1524: which tune a message on `channel_idx` plays -- the channel's own choice, else
// the global Channel tune that every channel message has always used. A channel that has
// chosen nothing, and a channel_idx that does not resolve, both land on exactly what this
// board played before the feature existed.
static uint8_t channelTuneIdFor(NodePrefs* prefs, uint8_t channel_idx) {
  const uint8_t inherited =
      offband::effectiveTone(prefs, offband::ToneEvent::ChannelMessage);
  ChannelDetails ch;
  if (!the_mesh.getChannel(channel_idx, ch)) return inherited;
  return offband::resolveChannelTune(ch.tune, inherited);
}
#endif

// Trigger vibration for all UI events except none.
//
// #1419: `sent` is excluded. Vibration is feedback for something that ARRIVED; a send
// is the user's own keypress and they already know they made it. No board today both
// has a motor and can fire `sent` -- the screen that sends is badge-only and the badge
// has no motor -- so this changes nothing now. It is here because "no board does both
// yet" is a latent bug, not a safety argument: a later board with a keyboard and a
// motor would have buzzed on every send without anyone choosing that.
//
// Lifted out of notify() in #1524 so the channel path gets the motor too without also
// getting notify()'s tune lookup -- calling notify() for the vibration would have played
// the global tune on top of the channel's own.
static void vibrateFor(UIEventType t) {
#ifdef PIN_VIBRATION
  if (t != UIEventType::none && t != UIEventType::sent) {
    vibration.trigger();
  }
#else
  (void)t;
#endif
}

#if defined(PIN_BUZZER)
// #1549: one way to turn a tune id into sound, for every event.
//
// This replaces a direct offband::tuneRtttl() lookup that could only play the builtin
// table. Once a custom slot became selectable (#1413), picking one for an event stored an
// id past that table, the lookup returned nullptr, and the event went SILENT -- while the
// picker's own audition played it, because the audition had always gone through
// playTuneById. One resolver for both paths is the fix and the point: a third kind of
// tune cannot now reach one path and not the other.
void UITask::playEventTune(uint8_t id) {
#if UI_HAS_CARDKB
  // Slots exist on this board, so the id may name a file rather than a table entry.
  // playTuneById is the only resolver that knows how, and it carries the
  // stop-before-staging ordering that makes slot playback safe (#1437).
  playTuneById(id, offband::TonePriority::Event);
#else
  // No keyboard means no slot store and no picker, so nothing can have stored an id past
  // the table. The table lookup is the whole answer, and playTuneById is not compiled
  // here (#1478's guard).
  if (const char* melody = offband::tuneRtttl(id)) {
    buzzer.play(melody, offband::TonePriority::Event);
  }
#endif
}
#endif

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
  // #1418/#1419/#1420: the tone is looked up, not written here. The id comes from the
  // stored pref, falling back to this board's compiled default when nothing has been
  // chosen -- so an un-configured badge plays what it always did, and a deliberately
  // silenced event stays silent.
  offband::ToneEvent ev;
  if (toneEventFor(t, ev)) playEventTune(offband::effectiveTone(_node_prefs, ev));
#endif
  vibrateFor(t);
}

// #1524: a channel message, resolved against the channel it arrived on.
void UITask::notifyChannel(uint8_t channel_idx) {
#if UI_HAS_CARDKB && defined(PIN_BUZZER)
  playEventTune(channelTuneIdFor(_node_prefs, channel_idx));
  vibrateFor(UIEventType::channelMessage);
#else
  // Nothing on this board can set a per-channel tune -- the screen that does is the
  // badge's -- so it keeps the base class's behaviour exactly, by calling it.
  (void)channel_idx;
  notify(UIEventType::channelMessage);
#endif
}


void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
#if !UI_HAS_CARDKB
  // #1230: on the badge the phone catching up is no reason to leave what's on screen.
  if (msgcount == 0) {
    gotoHomeScreen();
  }
#endif
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;

#if UI_HAS_CARDKB
  // #1230: the message is already in the badge store. Its row jumps to the top of the
  // inbox and flashes; nothing takes the screen away (design 1a, "no lost keystrokes").
  ((InboxScreen*)home)->flash();
#else
  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from_name, text);
  setCurrScreen(msg_preview);
#endif

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection() && _disp_mode != 2) {  // #542 B1: not in always-off
#ifdef OFFBAND_OBSERVER
      offband::crashLogf("[ui] newMsg: display off + no conn -> turnOn");
#endif
      _display->turnOn();
    }
    if (_display->isOn()) {
    _auto_off = millis() + autoOffMillis();  // extend the auto-off timer
    _next_refresh = 100;  // trigger refresh
    }
  }
}

#ifdef PIN_QCC_ACTIVITY_LED

UITask* UITask::_activity_owner = nullptr;

// Wrap-safe "is a before b". millis() wraps every 49.7 days and a badge is meant to run
// for a conference, so a plain `<` would park the LED on or off for the rest of the
// event at the wrap. The subtraction is the usual idiom: the difference stays correct
// across the wrap, and the sign is what is being asked about. Named locally because the
// only `before` in the tree is a BadgeStore private taking ints, which is both
// unreachable here and narrower than millis().
static inline bool activityBefore(uint32_t a, uint32_t b) {
  return (int32_t)(a - b) < 0;
}

// Called from the dispatcher's receive path (#1367). Does nothing but record the airtime
// of the packet that just decoded: the receive path is not the place to reason about
// patterns, and the UI tick will pick this up within a few milliseconds. A second packet
// arriving before the tick overwrites the first rather than queueing -- the LED can only
// show one flash at a time, so a queue would only build latency between what the radio
// is doing and what the badge is showing.
void UITask::onTrafficDecoded(uint32_t airtime_ms) {
  UITask* self = _activity_owner;
  if (self != nullptr) self->_traffic_pending_ms = airtime_ms;
}

void UITask::setActivityLed(bool on) {
  if (on == _activity_lit) return;   // only touch the pin on a change
  _activity_lit = on;
  // Active HIGH, and that is proven rather than assumed: P0.08 drives a 2N7000 gate with
  // the LED's anode at VCC through R8, so a high gate sinks the cathode and lights it
  // (badge schematic). This is deliberately NOT LED_STATE_ON, which describes the
  // module's LED on P0.15 -- a different part whose polarity is a separate question.
  digitalWrite(PIN_QCC_ACTIVITY_LED, on ? HIGH : LOW);
}

void UITask::activityLedHandler() {
  const uint32_t now = millis();

  // #542: the existing LED preference gates the whole indicator. No new setting was
  // added for this -- `ui_led_enabled` already exists and already persists; until now
  // nothing read it.
  if (_node_prefs != nullptr && _node_prefs->ui_led_enabled == 0) {
    setActivityLed(false);
    return;
  }

  // A packet decoded since the last tick: start a flash, unless the forced gap after the
  // previous one has not elapsed. Dropping the flash rather than deferring it is
  // deliberate -- under load, deferring would chain flashes end to end and the LED would
  // read as solid, which is the one thing it must never do.
  const uint32_t pending = _traffic_pending_ms;
  if (pending != 0) {
    _traffic_pending_ms = 0;
    if (!activityBefore(now, _traffic_gap_until)) {
      uint32_t on_ms = pending;
      if (on_ms < kTrafficMinMs) on_ms = kTrafficMinMs;
      if (on_ms > kTrafficMaxMs) on_ms = kTrafficMaxMs;
      _traffic_until = now + on_ms;
      _traffic_gap_until = _traffic_until + kTrafficGapMs;
      // Start the burst lit, on its own half-cycle, so the first flicker is immediate
      // rather than waiting out a dark period the viewer never asked for.
      _flicker_next = now;
      if (_flicker_rng == 0) _flicker_rng = now | 1;   // xorshift must never be seeded 0
    }
  }

  // Priority: traffic outranks attention, so a badge that is both busy and holding
  // unread shows the busier signal. Attention reasserts itself the moment traffic stops.
  if (activityBefore(now, _traffic_until)) {
    // Chopped, not solid, and jittered. Held solid a single packet reads as a brief
    // steady light; an Ethernet LED's appeal is its irregularity, so each half-cycle is
    // varied rather than run at a fixed rate.
    if (!activityBefore(now, _flicker_next)) {
      // xorshift32, local: the Arduino RNG is shared, and perturbing its stream from an
      // indicator would be an unpleasant thing to debug somewhere else.
      _flicker_rng ^= _flicker_rng << 13;
      _flicker_rng ^= _flicker_rng >> 17;
      _flicker_rng ^= _flicker_rng << 5;
      const uint32_t span = 2 * kFlickerJitterMs + 1;
      const int32_t jitter = (int32_t)(_flicker_rng % span) - (int32_t)kFlickerJitterMs;
      const uint32_t base = _activity_lit ? kFlickerOffMs : kFlickerOnMs;  // next half
      _flicker_next = now + (uint32_t)((int32_t)base + jitter);
      setActivityLed(!_activity_lit);
    }
    return;
  }

  // Attention: a short blink at the top of each period while something wants looking at.
  //
  // The source is the BADGE's own unread, not `_msgcount`. That distinction is the whole
  // of bug #1367-a, found on hardware: `_msgcount` is `offline_queue_len`, the queue of
  // messages waiting for a PHONE to collect over BLE, and it is cleared by
  // `UITask::msgRead()` when the client drains that queue. Reading a conversation on the
  // badge does not touch it, and on a badge with no phone paired nothing ever will -- so
  // the LED blinked forever and going to the conversation could not stop it.
  //
  // `BadgeMsgStore::totalUnread()` is the count the Inbox title already shows as "N new".
  // An arriving message counts as unread only if its conversation is NOT open, and
  // opening a conversation zeroes it, which is exactly the behaviour the LED should
  // follow.
  const bool wants_attention = (the_mesh.badgeStore().totalUnread() > 0);
  if (!wants_attention) {
    _attention_phase = now;   // keep the phase fresh so the next blink is immediate
    setActivityLed(false);
    return;
  }
  if (!activityBefore(now, _attention_phase + kAttentionPeriodMs)) _attention_phase = now;
  setActivityLed(activityBefore(now, _attention_phase + kAttentionOnMs));
}

#endif   // PIN_QCC_ACTIVITY_LED

void UITask::userLedHandler() {
// #275 (P0): on nRF52 the green LED is the ungated heartbeat, driven from the main
// loop by NRF52Board::heartbeatTick(). UITask must NOT also write PIN_STATUS_LED or
// it would contend with (and, when the UI/loop stalls, silently kill) the heartbeat.
// ESP32 keeps its UI-driven status LED unchanged.
#if defined(PIN_STATUS_LED) && !defined(NRF52_PLATFORM)
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  // #973: a null screen used to be stored here without comment, after which the render
  // guard (`millis() >= _next_refresh && curr`) simply stopped firing and the panel
  // froze on its last frame. No call site passes null on purpose -- verified by grep
  // across examples/companion_radio -- so a null arriving here is always a screen whose
  // `new` failed. Say so, and keep what is on screen: a stale frame the user can see and
  // report beats a dark panel that reads as dead hardware.
  if (!c) {
    mesh_log_line(MLOG_BOOT,
                  "[ui] setCurrScreen(NULL) REFUSED -- screen alloc failed; "
                  "holding current (#973)\r\n");
    return;
  }

#ifdef OFFBAND_OBSERVER
  // Screen transition tracing: log every change so we can see if the
  // SplashScreen <-> HomeScreen oscillation observed on hv3-bench is
  // a real state-machine bug.
  //
  // #973: resolved through offband::screenName(), which tests null FIRST. The inline
  // chain this replaces tested `c == home` before `c == nullptr`, so when `home` was
  // null -- or `msg_preview` deliberately null on the CardKB build -- a transition to
  // NOTHING printed that member's name. The trace read as a clean, healthy handoff
  // while the UI went dark, and that is what sent the first round of diagnosis at the
  // display driver. The ordering is covered by test/test_screen_name, positively and
  // negatively. Do not inline this back.
  const offband::ScreenLabel kScreens[] = {
    { splash,       "SPLASH"       },
    { home,         "HOME"         },
    { msg_preview,  "MSG_PREVIEW"  },
#if UI_HAS_CARDKB
    { thread,       "THREAD"       },
    { tools,        "TOOLS"        },
    { contacts,     "CONTACTS"     },
    { nearby,       "NEARBY"       },
    { status,       "STATUS"       },
    { settings,     "SETTINGS"     },
    { zones,        "ZONES"        },
    { gps,          "GPS"          },
    { battery,      "BATTERY"      },
#endif
#if UI_HAS_CARDKB && defined(PIN_BUZZER)
    { tones,        "TONES"        },
    { tone_picker,  "TONE_PICKER"  },
    { custom_tunes, "CUSTOM_TUNES" },
    { rtttl_edit,   "RTTTL_EDIT"   },
#endif
#ifdef QCC_BADGE_SELFTEST
    { self_test,    "SELF_TEST"    },
#endif
#if defined(QCC_BADGE_SELFTEST) && UI_HAS_CARDKB
    { key_test,     "KEY_TEST"     },
#endif
  };
  const size_t kScreenCount = sizeof(kScreens) / sizeof(kScreens[0]);
  offband::crashLogf("[ui] setCurrScreen %s -> %s",
                     offband::screenName(curr, kScreens, kScreenCount),
                     offband::screenName(c, kScreens, kScreenCount));
#endif
#if UI_HAS_CARDKB
  // #1230: a thread off screen isn't being read. It marks itself open when it draws.
  the_mesh.badgeStore().setOpen(-1);
#endif
  curr = c;
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart, const char* cause){
  // #1075: say why, before the display message and the teardown. A capture
  // used to end here with nothing, and the reason only ever reached a screen.
  if (!restart) offband::crashLogShutdown(cause, board.getBattMilliVolts());

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    // Power off board including radio, display, GPS and components
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);  // REVISIT: could be mapped to different key code
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
#if UI_HAS_CARDKB
  if (ev != BUTTON_EVENT_NONE) _last_btn_event = ev;   // #1207: before a handler consumes it
#endif
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#endif
#if UI_HAS_CARDKB
  // #1205: one key per poll. Like a button event it wakes a dark display first, but it
  // never goes through handleLongPress(), so typing can never enter CLI rescue. A
  // button event in the same pass wins; the key is the rarer of the two.
  _input_from_kbd = false;
  const uint8_t kbd_raw = _kbd.poll(millis());
  if (kbd_raw != 0) {
    _last_kbd_raw = kbd_raw;   // #1207: shown by the key test, Fn-layer keys included
    bool key_test_up = false;
#if defined(QCC_BADGE_SELFTEST)
    key_test_up = (curr == key_test);
#endif
    // #1239: every key wakes a dark display and holds the auto-off off, not only the ones
    // the UI maps. checkDisplayOn() is both the wake and the timer, and it used to be
    // reached on the way to dispatching a key, so the Fn layer never got there.
    //
    // It answers about the display rather than about the key: 0 when it woke a dark one,
    // and the character it was handed when the screen was already lit. So ask it with a
    // stand-in. The stand-in is never dispatched -- cardkb::actionFor() reads the answer
    // as a yes or no and says what the real key does.
    const char kWakeProbe = 1;
    const cardkb::Action action =
        cardkb::actionFor(kbd_raw, checkDisplayOn(kWakeProbe) == 0, c != 0, key_test_up);
    if (action.settings) gotoSettings();
    if (action.ui_key != 0) {
      _input_from_kbd = true;
      c = (char)action.ui_key;
    }
  }
#endif
#if defined(UI_HAS_ROTARY_INPUT)
  RotaryInputEvent rotaryEv = rotary_input.poll();
  if (c == 0 && _display != NULL && _display->isOn()) {
    if (rotaryEv == RotaryInputEvent::Next) {
      c = KEY_NEXT;
    } else if (rotaryEv == RotaryInputEvent::Prev) {
      c = KEY_PREV;
    }
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    int ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
#ifdef OFFBAND_OBSERVER
    offband::crashLogf("[ui] button event c=0x%x dispatched to curr screen", (int)c);
#endif
    // #1205: Esc backs out to Home from any screen that does not take it. On the badge
    // it goes up one level instead: the device pages, the zone picker (#1233) and the
    // GPS (#1235) to Settings, and Settings to Status, where it was opened.
    if (!curr->handleInput(c) && keynav::backsOut((uint8_t)c) && curr != home) {
#if UI_HAS_CARDKB
#ifdef PIN_BUZZER
      // #1421: the picker is one level deeper than the rest, so Esc from it goes to
      // Tones rather than all the way out. Without this it would land on Home, because
      // the fallback below is the default for any screen not named here.
      if (curr == rtttl_edit) gotoCustomTunes();
      // #1525: the picker now serves conversations too, and one opened from a
      // conversation has to go back to it -- landing in Settings would strand someone
      // three levels from what they were reading.
      else if (curr == tone_picker) leaveTonePicker();
      else if (curr == custom_tunes) gotoTones();
      else
#endif
      if (curr == tools || curr == zones || curr == gps || curr == battery
#ifdef PIN_BUZZER
          || curr == tones
#endif
         ) gotoSettings();
      else if (curr == settings) gotoStatus();
      else
#endif
      gotoHomeScreen();
    }
    _auto_off = millis() + autoOffMillis();   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();
#ifdef PIN_QCC_ACTIVITY_LED
  activityLedHandler();   // #1367: traffic over attention over dark, on the same tick
#endif

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    // #148: (re)apply rotation at the frame boundary -- on a pending change and
    // again after a wake (some drivers reset orientation in turnOn()'s re-init).
    if (!_was_on) { _was_on = true; _rotation_dirty = true; }
    if (_rotation_dirty) {
      _display->setRotation((uint8_t)_rotation);
      _rotation_dirty = false;
      _display->clear();
      _next_refresh = 0;            // force an immediate full repaint
    }
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(UIColor::popup_bkg);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(UIColor::popup_txt);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
#ifdef KEEP_DISPLAY_ON_USB
    // Opt-in: refresh the auto-off deadline while externally powered, so the
    // timer counts from the moment external power is removed. Off by default
    // because OLED panels burn in quickly; only enable for LCD targets or
    // where the display is replaceable.
    if (board.isExternalPowered()) {
      _auto_off = millis() + autoOffMillis();
    }
#endif
    // #542 B1: mode 2 (always-off) enforces dark every loop — self-corrects any relight.
    // #141: always-on (mode 1) skips the auto-blank; auto (mode 0) blanks on timeout.
    // #1245: the deadline is compared by difference, as this file does elsewhere (the GPS
    // update above). `millis() > _auto_off` reads false for the whole of the run after
    // millis() wraps, which would have left the display lit for another seven weeks.
    if (_disp_mode == 2 || (!_always_on && (long)(millis() - _auto_off) > 0)) {
      _display->turnOff();
#if UI_HAS_CARDKB
      the_mesh.badgeStore().setOpen(-1);   // #1230: a dark thread isn't being read
#endif
    }
#endif
  } else if (_display != NULL) {
    _was_on = false;   // #148: display off -> re-apply rotation on the next wake
  }

  // #1246: one reading a second into the average, whatever the screens are doing. The
  // badge used to take a fresh eight-sample ADC read on every redraw and show it, which
  // is why a transmit or a noisy sample moved the number on its own.
  if ((long)(millis() - _next_batt_read) >= 0) {
    _batt.feed(getBattMilliVolts());
    _next_batt_read = millis() + 1000;
    // #1254: the same reading teaches the learner where this cell's 100% is. A value
    // the user pinned is theirs -- auto-learn does not touch it until they clear it.
    if (_node_prefs != NULL) {
      // Fed even when the user has pinned a value, so the log keeps explaining itself; a
      // pinned full point is protected by skipping the save below, not by going quiet.
      const bool pinned = _node_prefs->batt_full_user != 0;
      const uint16_t learned =
          _full_learner.feed(millis(), _batt.value(), board.isExternalPowered());
      // Every decision goes to the log, not only a success. On the bench a learn failed
      // with nothing to say whether USB was never seen, the charge fell short, or the
      // window refused; each of those now names itself with its number.
      using L = offband::FullPointLearner;
      switch (_full_learner.event()) {
        case L::kPluggedIn:
          MESH_DEBUG_PRINTLN("BATT: external power on, reading %u mV%s",
                             (unsigned)_full_learner.chargeMv(),
                             pinned ? ", full point pinned by user" : "");
          break;
        case L::kStillCharging:
          MESH_DEBUG_PRINTLN("BATT: on USB, %d mV over the last %u min -- still charging",
                             (int)_full_learner.riseMv(), (unsigned)(L::kHistorySlots - 1));
          break;
        case L::kChargerDone:
          MESH_DEBUG_PRINTLN("BATT: on USB, %d mV over the last %u min -- charger done at %u mV",
                             (int)_full_learner.riseMv(), (unsigned)(L::kHistorySlots - 1),
                             (unsigned)_full_learner.chargeMv());
          break;
        case L::kWatching:
          MESH_DEBUG_PRINTLN("BATT: unplugged at %u mV after a settled charge, watching %lu s",
                             (unsigned)_full_learner.chargeMv(),
                             (unsigned long)(L::kWatchMs / 1000UL));
          break;
        case L::kNotLearning:
          MESH_DEBUG_PRINTLN("BATT: unplugged at %u mV, charger never settled (%u flat min "
                             "needed), nothing to learn",
                             (unsigned)_full_learner.chargeMv(),
                             (unsigned)(L::kHistorySlots - 1));
          break;
        case L::kLearned:
          MESH_DEBUG_PRINTLN("BATT: window closed, full point %u mV%s", (unsigned)learned,
                             pinned ? " -- not saved, the user's pinned value stands" : "");
          break;
        case L::kImplausible:
          MESH_DEBUG_PRINTLN("BATT: window closed at %u mV, outside %u..%u, nothing learned",
                             (unsigned)_full_learner.bestMv(), (unsigned)L::kFloorMv,
                             (unsigned)L::kCeilMv);
          break;
        default:
          break;
      }
      if (!pinned && learned != 0 && learned != _node_prefs->batt_full_mv) {
        _node_prefs->batt_full_mv = learned;
        the_mesh.savePrefs();
        MESH_DEBUG_PRINTLN("BATT: saved full point %u mV", (unsigned)learned);
      }
    }
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    // #1667: the cutoff acts on a DEBOUNCED reading, not a single raw sample. It used to
    // be `if (getBattMilliVolts() < AUTO_SHUTDOWN_MILLIVOLTS) shutdown()`, which took the
    // owner's badge down with its gauge reading ~80% -- see helpers/ui/LowBattCutoff.h for
    // why one raw sample was never enough evidence to power a board off.
    const uint32_t now_ms = millis();
    const uint16_t raw_mv = getBattMilliVolts();
    const uint16_t smoothed_mv = smoothedBattMilliVolts();
    const bool external = board.isExternalPowered();
    const bool fire = offband::lowBattShouldShutdown(
        _low_batt, now_ms, raw_mv, smoothed_mv, (uint16_t)AUTO_SHUTDOWN_MILLIVOLTS,
        OFFBAND_LOW_BATT_SUSTAIN_MS, external);

    // #1668: the badge's own view, on the wire a bench rig reads, so a discharge run is
    // measured rather than argued about -- and so an outlier read is caught in the act as
    // `raw` diverging from `sm` at one instant. The rig logs its MAX17048 and INA228
    // independently, so a capture carries both views timestamped against each other.
    mesh_log_line(MLOG_DEBUG, "[batt] raw=%u sm=%u thr=%u ext=%d low_ms=%lu fire=%d\r\n",
                  (unsigned)raw_mv, (unsigned)smoothed_mv,
                  (unsigned)AUTO_SHUTDOWN_MILLIVOLTS, external ? 1 : 0,
                  (unsigned long)offband::lowBattHeldMs(_low_batt, now_ms), fire ? 1 : 0);

    if (fire) {
      if (_display != NULL) {
        _display->startFrame();
        _display->setTextSize(2);
        _display->setColor(UIColor::warning_txt);
        _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
        _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
        _display->endFrame();
        if (_display->isEink() == false) { delay(3000); }
      }
      shutdown(false, "low-battery");
    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

// #1245: the preference, or the board's compiled fallback where none is set. A board
// compiled with AUTO_OFF_MILLIS 0 never blanks -- that is an e-ink decision made at
// compile time (the `#if AUTO_OFF_MILLIS > 0` guards above), and a preference does not
// get to undo it.
// #1246: the average, or the raw read until there is one. Never 0 from the average's
// unseeded state, which would have drawn an empty battery for the first frame.
uint16_t UITask::smoothedBattMilliVolts() const {
  return _batt.seeded() ? _batt.value() : getBattMilliVolts();
}

// #1254: the learned or pinned full point, or the board's compiled one while this cell
// has taught the badge nothing.
uint16_t UITask::battFullMilliVolts() const {
  const uint16_t pref = (_node_prefs != NULL) ? _node_prefs->batt_full_mv : 0;
  return pref != 0 ? pref : (uint16_t)BATT_MAX_MILLIVOLTS;
}

bool UITask::battFullIsUserSet() const {
  return _node_prefs != NULL && _node_prefs->batt_full_user != 0;
}

bool UITask::setBattFullMilliVolts(uint16_t mv) {
  if (_node_prefs == NULL) return false;
  // A press at the wrong moment must not pin a number that makes the bar meaningless --
  // below the empty end it would read 0% everywhere. Same band the learner uses. The
  // answer goes back to the caller so the screen can say which way it was wrong.
  if (!offband::FullPointLearner::plausibleFullMv(mv)) return false;
  _node_prefs->batt_full_mv = mv;
  _node_prefs->batt_full_user = 1;
  _full_learner.reset();          // a pending window would otherwise overwrite this
  the_mesh.savePrefs();
  return true;
}

void UITask::clearBattFullMilliVolts() {
  if (_node_prefs == NULL) return;
  _node_prefs->batt_full_mv = 0;
  _node_prefs->batt_full_user = 0;
  _full_learner.reset();          // start watching again from the next charge
  the_mesh.savePrefs();
}

uint16_t UITask::autoOffSecs() const {
  const uint16_t pref = (_node_prefs != NULL) ? _node_prefs->ui_screen_secs : 0;
  return offband::screenOffSecsShown(pref, (uint32_t)AUTO_OFF_MILLIS);
}

unsigned long UITask::autoOffMillis() const {
  const uint16_t pref = (_node_prefs != NULL) ? _node_prefs->ui_screen_secs : 0;
  return (unsigned long)offband::screenOffMillis(pref, (uint32_t)AUTO_OFF_MILLIS);
}

void UITask::setAutoOffSecs(uint16_t secs) {
  if (_node_prefs == NULL) return;
  _node_prefs->ui_screen_secs = secs;
  _auto_off = millis() + autoOffMillis();   // the running timer follows the new value
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn() && _disp_mode != 2) {   // #542 B1: button does not wake a deliberately-off screen
      _display->turnOn();   // turn display on and consume event
      c = 0;
    }
    _auto_off = millis() + autoOffMillis();   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
    c = 0;   // consume event
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double-click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  // #1590: away from Home this is the way OUT. A badge with no keyboard fitted has no
  // Esc, and KEY_CANCEL is the only key backsOut() accepts, so without this there is no
  // way to leave Settings at all on such a unit. At Home there is nowhere to go, so the
  // gesture keeps its original job there. The rule is in keynav so it is host-tested.
  const uint8_t k = keynav::tripleClickKey(curr == home);
  if (k != 0) return (char)k;
  toggleBuzzer();
  return 0;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  }
  return false;
}

void UITask::toggleGPS() {
    if (_sensors != NULL) {
    // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          notify(UIEventType::ack);
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          notify(UIEventType::ack);
        }
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}

// #510: apply the mute to the buzzer DRIVER, not just the pref. Without this a scope
// set over 0xC5 records the value and leaves the hardware muted until the next reboot.
void UITask::applyBuzzerMute(bool quiet) {
#ifdef PIN_BUZZER
  buzzer.quiet(quiet);
#else
  (void)quiet;
#endif
}
