#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <SafeBoot.h>
#include "MyMesh.h"

#ifdef CROSSWIRE_OBSERVER
  #include "helpers/wifi_observer/WifiObserver.h"
  #include "helpers/wifi_observer/CrashLog.h"
  // CW_PHASE: tracing macro for setup() crash localization. With
  // CrashLog v2's ESP_LOG hook + shutdown handler, the last phase
  // line surviving in the ring buffer pinpoints where setup() died.
  #define CW_PHASE(name) crosswire::crashLogf("[setup] phase: %s", name)
#else
  #define CW_PHASE(name) ((void)0)
#endif

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
  #if defined(EXTRAFS)
    #include <CustomLFS.h>
    CustomLFS ExtraFS(0xD4000, 0x19000, 128);
    DataStore store(InternalFS, ExtraFS, rtc_clock);
  #else
    DataStore store(InternalFS, rtc_clock);
  #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef ESP32
  #ifdef WIFI_SSID
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(RP2040_PLATFORM)
  //#ifdef WIFI_SSID
  //  #include <helpers/rp2040/SerialWifiInterface.h>
  //  SerialWifiInterface serial_interface;
  //  #ifndef TCP_PORT
  //    #define TCP_PORT 5000
  //  #endif
  // #elif defined(BLE_PIN_CODE)
  //   #include <helpers/rp2040/SerialBLEInterface.h>
  //   SerialBLEInterface serial_interface;
  #if defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(NRF52_PLATFORM)
  #ifdef BLE_PIN_CODE
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(STM32_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &serial_interface);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

void halt() {
  while (1) ;
}

void setup() {
  Serial.begin(115200);

  // SafeBoot: pre-init power guard. See src/SafeBoot.h.
  SafeBoot::checkAndMaybeSleep();
#ifdef CROSSWIRE_OBSERVER
  crosswire::wifiObserverBegin();
#endif
  CW_PHASE("post:wifiObserverBegin");

  board.begin();
  CW_PHASE("post:board.begin");

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
  }
  CW_PHASE("post:display.begin");
#endif

  if (!radio_init()) { halt(); }
  CW_PHASE("post:radio_init");

  fast_rng.begin(radio_get_rng_seed());
  CW_PHASE("post:fast_rng.begin");

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef BLE_PIN_CODE
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

  //#ifdef WIFI_SSID
  //  WiFi.begin(WIFI_SSID, WIFI_PWD);
  //  serial_interface.begin(TCP_PORT);
  // #elif defined(BLE_PIN_CODE)
  //   char dev_name[32+16];
  //   sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  //   serial_interface.begin(dev_name, the_mesh.getBLEPin());
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
    the_mesh.startInterface(serial_interface);
#elif defined(ESP32)
  CW_PHASE("ESP32:before SPIFFS.begin");
  SPIFFS.begin(true);
  CW_PHASE("ESP32:post SPIFFS.begin");
  store.begin();
  CW_PHASE("ESP32:post store.begin");
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
  CW_PHASE("ESP32:post the_mesh.begin");

#ifdef WIFI_SSID
  board.setInhibitSleep(true);   // prevent sleep when WiFi is active
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
  CW_PHASE("ESP32:post WIFI_SSID serial_interface.begin");
#elif defined(BLE_PIN_CODE)
  CW_PHASE("ESP32:before BLE serial_interface.begin (BLE_PIN_CODE)");
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  CW_PHASE("ESP32:post BLE serial_interface.begin");
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
  CW_PHASE("ESP32:post SERIAL_RX serial_interface.begin");
#else
  serial_interface.begin(Serial);
  CW_PHASE("ESP32:post fallback serial_interface.begin");
#endif
  the_mesh.startInterface(serial_interface);
  CW_PHASE("ESP32:post the_mesh.startInterface");
#else
  #error "need to define filesystem"
#endif

  sensors.begin();
  CW_PHASE("post:sensors.begin");

#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
  CW_PHASE("post:applyGpsPrefs");
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
  CW_PHASE("post:ui_task.begin");
#endif
  CW_PHASE("setup:DONE");
}

void loop() {
#ifdef CROSSWIRE_OBSERVER
  // Loop-phase tracing: track which sub-loop is currently executing.
  // Updated at each step; the periodic stats prints record the most
  // recent value. If one sub-loop hangs (deadlock, infinite retry,
  // blocking call), the phase string + iteration count being frozen
  // in the next stats sample tells us exactly which one.
  static volatile const char* s_loop_phase = "begin";
  static volatile uint32_t s_loop_iter = 0;
  crosswire::loopPhaseSet(&s_loop_phase, &s_loop_iter);
  s_loop_iter++;

  s_loop_phase = "wifiObserverLoop";
  crosswire::wifiObserverLoop();
#endif
#ifdef CROSSWIRE_OBSERVER
  s_loop_phase = "the_mesh.loop";
#endif
  the_mesh.loop();
#ifdef CROSSWIRE_OBSERVER
  s_loop_phase = "sensors.loop";
#endif
  sensors.loop();
#ifdef DISPLAY_CLASS
#ifdef CROSSWIRE_OBSERVER
  s_loop_phase = "ui_task.loop";
#endif
  ui_task.loop();
#endif
#ifdef CROSSWIRE_OBSERVER
  s_loop_phase = "end";
#endif
  rtc_clock.tick();
}
