#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"

#ifdef CROSSWIRE_OBSERVER
  #include "helpers/wifi_observer/WifiObserver.h"
  #include "helpers/wifi_observer/CrashLog.h"
  #ifdef CROSSWIRE_OBSERVER_BLE_COMPANION
    #include "helpers/wifi_observer/WebCertStore.h"  // post-BLE cert warmup; Strycher/LoRa#276
  #endif
  #ifdef ARDUINO
    #include <esp_heap_caps.h>   // heap_caps_check_integrity_all -- post-setup probe (#281)
    #include <Preferences.h>     // post-setup NVS probe (#281)
  #endif
  // CW_PHASE: tracing macro for setup() crash localization. With
  // CrashLog v2's ESP_LOG hook + shutdown handler, the last phase
  // line surviving in the ring buffer pinpoints where setup() died.
  //
  // NOTE: heap_caps_check_integrity_all() was tried in this macro but
  // crashed at very early boot (before USB-Serial-JTAG enumerated, so
  // no output). The check is now only invoked at end of setup, in a
  // standalone block where Serial is known up.
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

#ifdef CROSSWIRE_OBSERVER
  crosswire::wifiObserverBegin();
#endif
  CW_PHASE("post:wifiObserverBegin");

  board.begin();
  CW_PHASE("post:board.begin");

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  bool display_begin_ok = display.begin();
  CW_PHASE(display_begin_ok ? "post:display.begin(OK)" : "post:display.begin(FAILED)");
#ifdef CROSSWIRE_OBSERVER
  // I2C bus scan: report ALL addresses that ACK on the bus that
  // display.begin() initialized. If OLED at expected DISPLAY_ADDRESS
  // (0x3C) doesn't appear, our pin/address assumptions are wrong for
  // this V3 sub-variant. Run AFTER display.begin() so bus is alive.
  crosswire::i2cScan(-1, -1, "board-bus-default-pins");
#endif
  if (display_begin_ok) {
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
#ifdef CROSSWIRE_OBSERVER
    // Boot counter on top-left corner. User can directly observe whether
    // it increments without any interaction = positive proof of (or
    // against) chip rebooting. Persists via RTC_NOINIT across soft
    // resets; resets only on power-on / esptool reset.
    char bootbuf[16];
    snprintf(bootbuf, sizeof(bootbuf), "B#%u", (unsigned)crosswire::bootCounterValue());
    disp->setTextSize(1);
    disp->drawTextLeftAlign(0, 0, bootbuf);
#endif
    disp->endFrame();
  }
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
  // 2026-05-27 diagnostic revert (#281): the pre-warmup webCertStoreBegin
  // call that lived here in commit 17d7dcee is now suspected of corrupting
  // the heap allocator (heap_caps_check_integrity_all returns false at
  // setup-end; every post-setup nvs_open then crashes in nvs::HashList::find).
  // Reverting the call restores the Task 11 (5ea7711c) behavior where cert
  // gen only runs from webServerStart() in the wifiObserverLoop tick.
  // The NVS probe at end of setup tells us whether removing this call
  // restores heap_ok=1.
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

#ifdef CROSSWIRE_OBSERVER
  // Plan 2 v2 Task 12: wire observer mesh context AFTER the_mesh.begin()
  // populates self_id. Strings cached as borrowed pointers in WifiObserver;
  // backing storage here must outlive the observer (static buffer +
  // macro-defined strings = effectively process-lifetime).
  //
  // CLIENT_VERSION macro was vendored from EastMesh but is no longer
  // defined post-MqttUplink retirement. Using FIRMWARE_VERSION for both
  // firmware_version and client_version fields until a project-specific
  // CLIENT_VERSION is established (filed as follow-up).
  static char observer_device_id[65];
  crosswire::bytesToHexUpper(the_mesh.self_id.pub_key, PUB_KEY_SIZE,
                             observer_device_id, sizeof(observer_device_id));
  crosswire::wifiObserverSetMeshContext(
      the_mesh.self_id,
      observer_device_id,
      the_mesh.getNodeName(),
      FIRMWARE_VERSION,            // client_version (TODO: distinguish)
      FIRMWARE_VERSION,            // firmware_version
      board.getManufacturerName());
  CW_PHASE("ESP32:post wifiObserverSetMeshContext");
#endif
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

#if defined(CROSSWIRE_OBSERVER) && defined(ARDUINO)
  // Plan 3 NVS Lock corruption probe (#281): deterministic post-setup
  // NVS round-trip on a dummy namespace. If NVS state was corrupted
  // during setup, this crashes IMMEDIATELY (rather than waiting for
  // WiFi/BLE/heartbeat to trigger the latent failure downstream).
  // Also runs heap_caps_check_integrity_all() here -- safe at setup-end
  // (boot stabilized) but unsafe in early CW_PHASE markers (crashed
  // boot pre-Serial).
  {
    bool heap_ok = heap_caps_check_integrity_all(false);
    Serial.printf("[NVS_PROBE] post-setup heap_ok=%d\n", heap_ok ? 1 : 0);

    ::Preferences nvs_probe;
    Serial.println("[NVS_PROBE] attempting post-setup nvs_open...");
    bool ok = nvs_probe.begin("__cw_probe", /*readOnly=*/false);
    if (ok) {
      nvs_probe.putUInt("seq", (uint32_t)millis());
      nvs_probe.end();
      Serial.println("[NVS_PROBE] OK -- NVS healthy at setup-end");
    } else {
      Serial.println("[NVS_PROBE] begin returned false -- NVS unusable");
    }
  }
#endif
}

void loop() {
#ifdef CROSSWIRE_OBSERVER
  // CrashLog v6: per-sub-loop visit marking + heartbeat. Each sub-loop
  // sets its bit on entry; heartbeat reads + resets every ~1s. Lets us
  // PROVE that each sub-loop actually ran in the past 1s window.
  crosswire::loopIterTick();
  crosswire::subloopMark(crosswire::SUBLOOP_WIFI);
  crosswire::wifiObserverLoop();
  crosswire::subloopMark(crosswire::SUBLOOP_MESH);
#endif
  the_mesh.loop();

#ifdef CROSSWIRE_OBSERVER
  crosswire::subloopMark(crosswire::SUBLOOP_SENSORS);
#endif
  sensors.loop();

#ifdef DISPLAY_CLASS
#ifdef CROSSWIRE_OBSERVER
  crosswire::subloopMark(crosswire::SUBLOOP_UI);
#endif
  ui_task.loop();
#endif

#ifdef CROSSWIRE_OBSERVER
  // Emit heartbeat if 1s elapsed since last. Cheap timestamp check.
  crosswire::heartbeatTick(millis());
#endif
  rtc_clock.tick();
}
