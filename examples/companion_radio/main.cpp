#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <SafeBoot.h>
#include "MyMesh.h"

#ifdef OFFBAND_OBSERVER
  #include "helpers/wifi_observer/WifiObserver.h"
  #include "helpers/diagnostics/CrashLog.h"
  #include "helpers/wifi_observer/ConfigSchema.h"   // #141: getDisplayAlwaysOn()
  #include "helpers/wifi_observer/ObserverCli.h"     // #141: setDisplayAlwaysOnApplier()
  // CW_PHASE: tracing macro for setup() crash localization. With
  // CrashLog v2's ESP_LOG hook + shutdown handler, the last phase
  // line surviving in the ring buffer pinpoints where setup() died.
  #define CW_PHASE(name) offband::crashLogf("[setup] phase: %s", name)
#else
  #define CW_PHASE(name) ((void)0)
#endif

// #377 (Epic #350): boot-survival CrashLog on the nRF52 companion. The observer
// path (ESP32) already wires CrashLog above; nRF52 companions are non-observer,
// so include it here. Additive + nRF52-scoped -- does not touch the observer
// blocks or the runtime-WiFi path.
#if defined(NRF52_PLATFORM) && !defined(OFFBAND_OBSERVER)
  #include "helpers/diagnostics/CrashLog.h"
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
    // SECURITY (Offband #167/#168): SerialWifiInterface is a TCP server on
    // TCP_PORT (5000) with NO connection auth -- any host on the LAN that reaches
    // it becomes the companion peer (full companion-API control). Do NOT pair
    // WIFI_SSID with OFFBAND_OBSERVER: the config command would land on this
    // unauthenticated socket (a compile-time #error in OffbandConfigProtocol.h
    // enforces that). Authenticating this transport is tracked in #167 (P1).
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

#if defined(OFFBAND_OBSERVER) && defined(DISPLAY_CLASS)
// #141: applier the observer CLI invokes so `display always on/off` reaches the
// live UITask immediately. Registered in setup() after ui_task.begin().
static void applyDisplayAlwaysOn(bool on) { ui_task.setAlwaysOn(on); }
// #148: applier for `display rotate`/`display flip`.
static void applyDisplayRotation(uint8_t deg) { ui_task.requestRotation(deg); }
// #148: capability query so the observer CLI refuses rotation on displays
// without a verified runtime-rotation override.
static bool displayRotationSupported() { return ui_task.displaySupportsRotation(); }
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

/* WIFI RECONNECT TRACKERS */
#if defined(ESP32) && defined(WIFI_SSID)
  bool wifi_needs_reconnect = false;
  unsigned long last_wifi_reconnect_attempt = 0;
#endif

void setup() {
  Serial.begin(115200);
  // #149: never let serial logging block the main loop. On the S3's USB-Serial-JTAG
  // (HWCDC) a write stalls when the port is plugged in but not being drained fast
  // enough; under BLE_DEBUG_LOGGING's per-frame flood (e.g. an app reconnect + full
  // sync) that stall starves BLE servicing until the send/recv queues overflow and
  // BLE jams. Timeout 0 = drop log bytes instead of blocking; output still flows
  // normally whenever a serial monitor is attached and draining the port.
  // #216/CI: setTxTimeoutMs is a USB-CDC (HWCDC) method, present only when Serial is
  // the native USB CDC (ARDUINO_USB_CDC_ON_BOOT=1, e.g. Heltec V4). On boards where
  // Serial is a UART HardwareSerial (e.g. Heltec V3 = esp32-s3-devkitc-1) it doesn't
  // exist -- and isn't needed there (no HWCDC head-of-line blocking). Guarding on plain
  // ESP32 broke the V3 observer build.
#if defined(ESP32) && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif

  // #149: stamp the running build on the serial console at boot. Every test build
  // looks identical otherwise, so there's no way to confirm what's actually flashed.
#ifdef OFFBAND_VERSION
  Serial.print("\n=== Offband build: "); Serial.print(OFFBAND_VERSION);
  #ifdef OFFBAND_GIT_SHA
  Serial.print(" sha "); Serial.print(OFFBAND_GIT_SHA);
  #endif
  Serial.println(" ===");
#endif

  // SafeBoot: pre-init power guard. See src/SafeBoot.h.
  SafeBoot::checkAndMaybeSleep();
#ifdef OFFBAND_OBSERVER
  offband::wifiObserverBegin();
#endif
  CW_PHASE("post:wifiObserverBegin");

  board.begin();
  CW_PHASE("post:board.begin");

#if defined(NRF52_PLATFORM) && defined(NRF52_POWER_MANAGEMENT)
  // #257: surface the last reset cause on the boot banner (unconditional, NOT
  // MESH_DEBUG-gated) so a Watchdog / CPU-Lockup recovery is visible the moment
  // the unit is pulled to USB. RESETREAS was captured pre-SystemInit in initPowerMgr().
  {
    char rr[64];
    snprintf(rr, sizeof(rr), "[boot] last reset: %s (RESETREAS=0x%lX)",
             board.getResetReasonString(board.getResetReason()),
             (unsigned long)board.getResetReason());
    Serial.println(rr);
  }
#endif

#if defined(NRF52_PLATFORM) && !defined(OFFBAND_OBSERVER)
  // #377 (Epic #350): start the boot-survival breadcrumb on the nRF52 companion.
  // Delegate the reset reason to the board (#376 decision 2) via a non-capturing
  // lambda -- it references only the global `board`, so it converts to the
  // ResetReasonHook function pointer. crashLogBegin() dumps a ring that survived
  // the previous reset (retained .noinit, #361); the boot line records the reason
  // AND keeps the ring referenced so ld-2.29 GC can't drop it (#363).
  offband::crashLogSetResetReasonHook([]() -> const char* {
    return board.getResetReasonString(board.getResetReason());
  });
  offband::crashLogBegin();
  offband::crashLogf("[boot] nRF52 companion up; reset=%s",
                     board.getResetReasonString(board.getResetReason()));
#endif

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  bool display_begin_ok = display.begin();
  CW_PHASE(display_begin_ok ? "post:display.begin(OK)" : "post:display.begin(FAILED)");
#ifdef OFFBAND_OBSERVER
  // I2C bus scan: report ALL addresses that ACK on the bus that
  // display.begin() initialized. If OLED at expected DISPLAY_ADDRESS
  // (0x3C) doesn't appear, our pin/address assumptions are wrong for
  // this V3 sub-variant. Run AFTER display.begin() so bus is alive.
  offband::i2cScan(-1, -1, "board-bus-default-pins");
#endif
  if (display_begin_ok) {
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
#ifdef OFFBAND_OBSERVER
    // Boot counter on top-left corner. User can directly observe whether
    // it increments without any interaction = positive proof of (or
    // against) chip rebooting. Persists via RTC_NOINIT across soft
    // resets; resets only on power-on / esptool reset.
    char bootbuf[16];
    snprintf(bootbuf, sizeof(bootbuf), "B#%u", (unsigned)offband::bootCounterValue());
    disp->setTextSize(1);
    disp->drawTextLeftAlign(0, 0, bootbuf);
#endif
    disp->endFrame();
  }
#endif

  if (!radio_init()) { halt(); }
  CW_PHASE("post:radio_init");

  fast_rng.begin(radio_driver.getRngSeed());
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
  // #411: mirror captured serial-log lines to the live console EXCEPT where the
  // framed protocol runs on Serial itself (USB-serial companion) -- there it stays
  // capture-only so nothing raw corrupts the protocol line.
  meshLogSetMirror(!serial_interface.isConsoleSharedWithProtocol());
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
  // #411: mirror captured serial-log lines to the live console EXCEPT where the
  // framed protocol runs on Serial itself (USB-serial companion) -- there it stays
  // capture-only so nothing raw corrupts the protocol line.
  meshLogSetMirror(!serial_interface.isConsoleSharedWithProtocol());
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
  WiFi.setAutoReconnect(true);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
      if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
          WIFI_DEBUG_PRINTLN("WiFi disconnected. Flagging for reconnect...");
          wifi_needs_reconnect = true;
      } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
          WIFI_DEBUG_PRINTLN("WiFi connected successfully!");
          wifi_needs_reconnect = false;
      }
  });

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
  // #411: mirror captured serial-log lines to the live console EXCEPT where the
  // framed protocol runs on Serial itself (USB-serial companion) -- there it stays
  // capture-only so nothing raw corrupts the protocol line.
  meshLogSetMirror(!serial_interface.isConsoleSharedWithProtocol());
  CW_PHASE("ESP32:post the_mesh.startInterface");

#ifdef OFFBAND_OBSERVER
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
  offband::bytesToHexUpper(the_mesh.self_id.pub_key, PUB_KEY_SIZE,
                             observer_device_id, sizeof(observer_device_id));
  offband::wifiObserverSetMeshContext(
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
#if defined(OFFBAND_OBSERVER)
  // #141/#148: register the live-apply hooks + apply persisted display prefs.
  offband::setDisplayAlwaysOnApplier(&applyDisplayAlwaysOn);
  ui_task.setAlwaysOn(offband::getDisplayAlwaysOn());
  offband::setDisplayRotationApplier(&applyDisplayRotation);
  offband::setDisplayRotationSupportedQuery(&displayRotationSupported);
  // Only restore a persisted rotation on displays that actually support it (#148).
  if (ui_task.displaySupportsRotation()) ui_task.requestRotation(offband::getDisplayRotation());
  CW_PHASE("post:display.prefs");
#endif
#endif
  board.onBootComplete();

#if defined(NRF52_PLATFORM)
  // #257: start the hardware watchdog AFTER all boot init (incl. flash/contacts
  // load), so a slow boot can't false-trip it. From here, any main-loop hang
  // auto-reboots within the timeout (RESETREAS=DOG, "Watchdog") instead of
  // wedging until a physical power-cycle. Fed at loop top + sleep entry.
  // Fleet-wide for nRF52 companions; no-op on platforms without nrf_wdt.h.
  board.startWatchdog(30);
  // #275 (P0): start the true, ungated green-LED heartbeat + its ~10 Hz loop-wake
  // timer. The heartbeat is loop-driven (heartbeatTick below), NOT gated by UI /
  // display / connection / traffic / the power-save nap -- it is the liveness signal.
  board.startHeartbeat();
#endif

  CW_PHASE("setup:DONE");
}

void loop() {
#if defined(NRF52_PLATFORM)
  board.feedWatchdog();  // #257: feed from the MAIN LOOP only -> a hung loop trips the WDT
  board.heartbeatTick(); // #275: loop-driven green-LED heartbeat (freezes if the loop hangs)
  #if !defined(OFFBAND_OBSERVER)
  {
    // #377: periodic breadcrumb into the retained ring. Low cadence (30 s) so it
    // does not evict real events from the 4 KB ring; records "up=Ns" so the boot
    // after a hang shows how long this boot ran. Also keeps the ring referenced.
    static uint32_t s_cl_last_ms = 0;
    uint32_t now_ms = millis();
    if (s_cl_last_ms == 0 || now_ms - s_cl_last_ms >= 30000u) {
      s_cl_last_ms = now_ms;
      offband::crashLogf("[hb] up=%us", (unsigned)(now_ms / 1000));
    }
  }
  #endif
#endif
#ifdef OFFBAND_OBSERVER
  // CrashLog v6: per-sub-loop visit marking + heartbeat. Each sub-loop
  // sets its bit on entry; heartbeat reads + resets every ~1s. Lets us
  // PROVE that each sub-loop actually ran in the past 1s window.
  offband::loopIterTick();
  offband::subloopMark(offband::SUBLOOP_WIFI);
  offband::wifiObserverLoop();
  offband::subloopMark(offband::SUBLOOP_MESH);
#endif
  the_mesh.loop();

#ifdef OFFBAND_OBSERVER
  offband::subloopMark(offband::SUBLOOP_SENSORS);
#endif
  sensors.loop();

#if defined(OFFBAND_OBSERVER) && ENV_INCLUDE_GPS == 1
  // #69 Task A: push GPS time-state to observer ~1 Hz so the SNTP arbiter
  // (next task) can see whether GPS currently owns the clock.
  {
    static uint32_t s_gps_state_ms = 0;
    uint32_t _now = millis();
    if (_now - s_gps_state_ms >= 1000) {
      s_gps_state_ms = _now;
      offband::wifiObserverSetGpsTimeState(
          the_mesh.getNodePrefs()->gps_enabled != 0,
          sensors.gpsHasFix());
    }
  }
#endif

#ifdef OFFBAND_OBSERVER
  // #31 Task C: push status snapshot to observer so the pool's
  // publishStatusIfDue() can emit /status.  Throttled to
  // kMinStatusIntervalSec (10 s = min legal status_interval; keeps the
  // pushed snapshot fresh for any configured pool cadence) to avoid
  // snapshot-build cost every iteration.
  //
  // Field sources:
  //   battery_mv     -- board.getBattMilliVolts()
  //   uptime_secs    -- millis() / 1000
  //   error_flags    -- the_mesh.getErrFlags() (Dispatcher::_err_flags)
  //   queue_len      -- the_mesh.getOutboundQueueLen() (_mgr->getOutboundTotal())
  //   noise_floor    -- radio_driver.getNoiseFloor() (RadioLibWrapper)
  //   tx_air_secs    -- the_mesh.getTotalAirTime() / 1000
  //   rx_air_secs    -- the_mesh.getReceiveAirTime() / 1000
  //   recv_errors    -- radio_driver.getPacketsRecvErrors()
  //   radio_freq/bw/sf/cr -- runtime NodePrefs freq/bw/sf/cr (#88)
  //   repeat_enabled -- getNodePrefs()->client_repeat != 0
  {
    static uint32_t s_status_snap_ms = 0;
    uint32_t _now = millis();
    if (_now - s_status_snap_ms >= offband::kMinStatusIntervalSec * 1000U) {
      s_status_snap_ms = _now;
      offband::MqttStatusSnapshot snap = {};
      snap.battery_mv     = static_cast<int>(board.getBattMilliVolts());
      snap.uptime_secs    = static_cast<uint32_t>(_now / 1000UL);
      snap.error_flags    = the_mesh.getErrFlags();
      snap.queue_len      = static_cast<uint16_t>(the_mesh.getOutboundQueueLen());
      snap.noise_floor    = radio_driver.getNoiseFloor();
      snap.tx_air_secs    = static_cast<uint32_t>(the_mesh.getTotalAirTime() / 1000UL);
      snap.rx_air_secs    = static_cast<uint32_t>(the_mesh.getReceiveAirTime() / 1000UL);
      snap.recv_errors    = static_cast<uint32_t>(radio_driver.getPacketsRecvErrors());
      // #88: report the ACTUAL runtime radio config from NodePrefs (set via
      // companion-API CMD_SET_RADIO_PARAMS, surfaced to HA/HACS through
      // SELF_INFO) -- NOT the compile-time LORA_* macros, which on the observer
      // env default to esp32_base 869.618/SF8 and never reflect a runtime re-tune.
      snap.radio_freq     = the_mesh.getNodePrefs()->freq;
      snap.radio_bw       = the_mesh.getNodePrefs()->bw;
      snap.radio_sf       = the_mesh.getNodePrefs()->sf;
      snap.radio_cr       = the_mesh.getNodePrefs()->cr;
      snap.repeat_enabled = (the_mesh.getNodePrefs()->client_repeat != 0);
      // #31 Task D: publish position in /status, selected EXACTLY as the
      // companion advert path selects its location, so the MQTT position always
      // agrees with the advert (design D4: reuse the existing advert_loc_policy,
      // no separate MQTT knob).  This firmware's NodePrefs (NodePrefs.h) defines
      // only ADVERT_LOC_NONE and ADVERT_LOC_SHARE -- there is NO ADVERT_LOC_PREFS
      // and NO prefs node_lat/lon here (that 3-policy split lives in the repeater's
      // CommonCLI::buildAdvertData, a different NodePrefs).  Mirror MyMesh::advert()
      // / CMD_SEND_SELF_ADVERT exactly: NONE => no location; otherwise (SHARE) use
      // sensors.node_lat/lon.  sensors.node_lat/lon are doubles already in decimal
      // degrees -- the same units createSelfAdvert/AdvertDataBuilder consume before
      // their internal *1E6 micro-degree scaling -- so they are emitted as-is.
      if (the_mesh.getNodePrefs()->advert_loc_policy == ADVERT_LOC_NONE) {
        snap.loc_valid = false;  // advert carries no location
      } else {                   // ADVERT_LOC_SHARE: publish sensors.node_lat/lon
        // exactly as the advert does -- whether the position came from GPS or was
        // set manually via CMD_SET_ADVERT_LATLON (both write sensors.node_lat/lon).
        // NOT gated on a live GPS fix (the advert isn't), so a manual or last-known
        // position publishes too. Suppress only the 0,0 null-island (our sole, safer
        // divergence from the advert). node_lat/lon are unconditional SensorManager
        // members, so no ENV_INCLUDE_GPS guard is needed -- compiles on no-GPS boards.
        snap.node_lat  = sensors.node_lat;
        snap.node_lon  = sensors.node_lon;
        snap.loc_valid = (sensors.node_lat != 0.0 || sensors.node_lon != 0.0);
      }
      offband::wifiObserverSetStatusSnapshot(snap);
    }
  }
#endif

#ifdef DISPLAY_CLASS
#ifdef OFFBAND_OBSERVER
  offband::subloopMark(offband::SUBLOOP_UI);
#endif
  ui_task.loop();
#endif

#ifdef OFFBAND_OBSERVER
  // Emit heartbeat if 1s elapsed since last. Cheap timestamp check.
  offband::heartbeatTick(millis());
#endif
  rtc_clock.tick();

  if (!the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#endif
  }

#if defined(ESP32) && defined(WIFI_SSID)
  // Safely attempt to reconnect every 10 seconds if flagged
  if (wifi_needs_reconnect && (millis() - last_wifi_reconnect_attempt > 10000)) {
    WIFI_DEBUG_PRINTLN("Attempting manual WiFi reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();
    last_wifi_reconnect_attempt = millis();
  }
#endif
}
