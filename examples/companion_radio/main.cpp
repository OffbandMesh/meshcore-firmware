#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <SafeBoot.h>
#include "MyMesh.h"

// #740: raw-UART0 boot beacon. This translation unit owns the single
// constructor(101) instance; see helpers/BootBeacon.h for why it exists.
#define OFFBAND_BEACON_DEFINE_CTOR
#include "helpers/BootBeacon.h"

#ifdef OFFBAND_OBSERVER
  #include "helpers/wifi_observer/WifiObserver.h"
  #include "helpers/diagnostics/CrashLog.h"
  #include "helpers/wifi_observer/ConfigSchema.h"   // #141: getDisplayAlwaysOn()
  #include "helpers/wifi_observer/ObserverCli.h"     // #141: setDisplayAlwaysOnApplier()
  // CW_PHASE: tracing macro for setup() crash localization. With
  // CrashLog v2's ESP_LOG hook + shutdown handler, the last phase
  // line surviving in the ring buffer pinpoints where setup() died.
  // #740: mirrored to the raw-UART0 beacon as well -- the CrashLog ring rides
  // USB-CDC, which on a native-USB S3 dies with the chip and therefore can
  // never witness a failed boot.
  #define CW_PHASE(name) do { OFFBAND_BEACON(name); offband::crashLogf("[setup] phase: %s", name); } while (0)
#else
  // #740: non-observer companions have no CrashLog phase tracing. The beacon
  // works everywhere, so the phase markers still reach UART0 when
  // OFFBAND_BOOT_BEACON is set, and compile to nothing when it is not.
  #define CW_PHASE(name) OFFBAND_BEACON(name)
#endif

// #472 (was #377, nRF52-only): uniform boot-survival CrashLog for ALL non-observer
// companions (ESP32 + nRF52). The observer path includes CrashLog above; every other
// companion gets it here via the shared helper (which also re-exposes crashLogf).
#if !defined(OFFBAND_OBSERVER)
  #include "helpers/diagnostics/CrashLogStandard.h"
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

// interface manager
#include <helpers/MultiSerialInterface.h>
MultiSerialInterface interface_manager;

// include bluetooth interface
#if defined(BLE_PIN_CODE)
  #ifdef ESP32
    // include esp32 bluetooth interface
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #elif defined(NRF52_PLATFORM)
    // include nrf52 bluetooth interface
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #else
    #error "SerialBLEInterface is not defined for this platform"
  #endif
#endif

// include wifi interface
#ifdef WIFI_SSID
  #ifndef TCP_PORT
    #define TCP_PORT 5000
  #endif
  #ifdef ESP32
    // include esp32 wifi interface
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface wifi_interface;
  #else
    #error "SerialWifiInterface is not defined for this platform"
  #endif
#endif

// include usb interface
#if defined(ENABLE_USB_INTERFACE)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface usb_serial_interface;
#endif

// include ethernet interface
#if defined(ETHERNET_ENABLED)
  #include <helpers/ethernet/EthernetInterface.h>
  ETHERNET_CLASS ethernet_interface;
#endif

// include hardware serial interface
#if defined(SERIAL_RX)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface hardware_serial_interface;
  HardwareSerial companion_serial(1);
#endif

// platform file system
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

// #668: the legacy per-platform `serial_interface` declarations lived here.
// 1.17.0 replaced them with the interface objects declared above
// (bluetooth_interface / wifi_interface / usb_serial_interface /
// ethernet_interface / hardware_serial_interface) registered into
// interface_manager. The merge added those WITHOUT removing these, so every
// companion build carried a second, fully-allocated interface object -- 4,624
// bytes of dead .bss on ESP32, and on nRF52 a second SerialBLEInterface whose
// begin() was still being called, double-initialising the BLE stack (#668).

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &interface_manager);
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
  // #740: first statement in setup(), ahead of Serial.begin() -- the beacon does
  // not depend on the Arduino core, so this lands even if Serial.begin() stalls.
  OFFBAND_BEACON("setup:ENTRY -- before Serial.begin");

#if defined(OFFBAND_POWER_TELEMETRY_SWEEP)
  // #766: BOOT-TIME ADC sweep -- runs HERE, as the second statement in setup(),
  // and this placement is the whole point of it.
  //
  // The first sweep ran from loop(), which was useless: by then our own firmware
  // has claimed most of these pins as outputs (PIN_TFT_EN=6, PIN_TFT_RST=4,
  // PIN_TFT_BL=5, PIN_TFT_DC=16, PIN_TFT_SCL=17, SENSOR_RST_PIN=2). Reading an
  // output we are actively driving tells you what WE put there, not what the
  // board wired. GPIO6 read a saturated 3189 mV on both ADC_CTRL states for
  // exactly that reason -- we hold it high as TFT_EN.
  //
  // That matters because the RC32 carrier schematic (RC32_V1.0-schematic.pdf)
  // does carry `Battery_ADC`, `ADC_IN` and `ADC_Ctrl` nets -- so a sense path
  // EXISTS, contrary to my earlier reading of the datasheet, which documents
  // VBAT only as a power input. Row-pairing in the schematic places `ADC_IN`
  // adjacent to GPIO6, the same pin we drive as TFT_EN.
  //
  // Here, nothing has been initialised: no board.begin(), no display, no radio.
  // Every pin is still in its power-on state, so what we read is the HARDWARE,
  // which is the only thing worth measuring. Sweeping the full ADC1+ADC2 range
  // (GPIO1..20) rather than a curated subset -- the last sweep's blind spot was
  // a subset chosen before I had checked our own pin assignments.
  //
  // Raw beacon output, not mesh_log_line(): MeshLog is not up this early.
  {
    char b[96];
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    for (int pin = 1; pin <= 20; pin++) {
      int mv = analogReadMilliVolts(pin);
      snprintf(b, sizeof(b), "BOOTADC %2d = %4d mV", pin, mv);
      offband_beacon_line(b);
    }
    // Then again with ADC_CTRL asserted, so a gated divider shows up as a change.
#if defined(PIN_ADC_CTRL)
    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, HIGH);
    delay(20);
    for (int pin = 1; pin <= 20; pin++) {
      int mv = analogReadMilliVolts(pin);
      snprintf(b, sizeof(b), "BOOTADC-CTRLHI %2d = %4d mV", pin, mv);
      offband_beacon_line(b);
    }
    pinMode(PIN_ADC_CTRL, INPUT);   // release; do not fight whatever owns it
#endif
  }
#endif

  // Serial.begin() software-resets the UART and flushes its FIFO, which would
  // truncate the line above mid-transmission. Drain first. (Gemini review.)
  OFFBAND_BEACON_FLUSH();
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
  // ⚠ #741/#756: THIS MUST NOT BE ZERO. Zero is what caused the bug it was
  // meant to prevent.
  //
  // HWCDC::write (framework-arduinoespressif32/cores/esp32/HWCDC.cpp:440-470):
  //
  //     uint32_t tries = tx_timeout_ms;
  //     while (connected && to_send) {
  //         if (last_toSend == to_send) { tries--; delay(1); }
  //         if (tries == 0) { connected = false; }   // the escape hatch
  //     }
  //
  // With tx_timeout_ms == 0, `tries--` UNDERFLOWS 0 -> 4,294,967,295 and the
  // escape can never fire: ~50 days of 1 ms iterations. Observed on
  // rc32-bench-1 blocking 49.9 minutes inside crashLogBegin(), resuming only
  // when a host opened the port (#702).
  //
  // 1 ms preserves the original #149/#216 intent -- do not stall the main loop
  // on serial -- while leaving `tries` able to reach zero and bail out.
  // DO NOT "optimise" this back to 0.
#if defined(ESP32) && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(1);
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
  // #740: bracketed -- SafeBoot can deep-sleep here, which looks identical to a
  // hang from outside. A "before" with no "post" names it unambiguously.
  OFFBAND_BEACON("setup:before SafeBoot::checkAndMaybeSleep");
  SafeBoot::checkAndMaybeSleep();
  OFFBAND_BEACON("setup:post SafeBoot::checkAndMaybeSleep");
#ifdef OFFBAND_OBSERVER
  offband::wifiObserverBegin();
#endif
  CW_PHASE("post:wifiObserverBegin");

  // #740: board.begin() is the established boundary -- nvs_count increments
  // after it, and on a failed RST it never incremented (#702). Bracket it so we
  // can tell "never reached" from "entered and did not return".
  OFFBAND_BEACON("setup:before board.begin");
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

#if !defined(OFFBAND_OBSERVER)
  // #472 (was #377, nRF52-only): uniform boot-survival CrashLog for ALL non-observer
  // companions (ESP32 + nRF52). Wires the board reset-reason into CrashLog, dumps a
  // ring that survived the previous reset, records a boot breadcrumb. The observer
  // path wires CrashLog via wifiObserverBegin() above.
  // #740: bracketed. The first instrumented boot stopped between
  // post:board.begin and display.begin()'s spiAttachMISO log, which leaves
  // exactly this call and the early part of display.begin() as candidates.
  // This split names which.
  OFFBAND_BEACON("setup:before crashLogStandardInit");
  offband::crashLogStandardInit(board, "companion");
  OFFBAND_BEACON("setup:post crashLogStandardInit");
#endif

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  OFFBAND_BEACON("setup:before display.begin");   // #740: split from crashLogStandardInit
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

  // #668: interface bring-up for EVERY platform now happens once, below, via
  // interface_manager. The legacy single-serial_interface block that used to sit
  // here was NOT removed when 1.17.0's MultiSerialInterface was ported -- only the
  // ESP32 branch got ported -- so on nRF52 SerialBLEInterface::begin() ran TWICE
  // (once here, once at the common block) and the second full BLE bring-up
  // crash-looped the board. USB companions survived it only because a duplicated
  // ArduinoSerialInterface::begin(Serial) just re-binds a stream.
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
  // #668: same as the nRF52 branch above -- interface bring-up is owned by
  // interface_manager below. RP2040's duplication was never fatal because its BLE
  // path is commented out, so it only ever double-bound an ArduinoSerialInterface,
  // but it is the same defect and is removed with it.
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
#else
  #error "need to define filesystem"
#endif

// add bluetooth interface
#if defined(BLE_PIN_CODE)
  bluetooth_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  interface_manager.addInterface(InterfaceType::Bluetooth, &bluetooth_interface);
#endif

// add wifi interface
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
  // PORTED to upstream's MultiSerialInterface (1.17.0): the ESP32 branch used to
  // select ONE serial_interface here and call the_mesh.startInterface() on it.
  // interface_manager now owns registration (BLE/WiFi/USB/Ethernet/HardwareSerial
  // are registered around this block) and is started once below, so keeping the
  // old selection would start the mesh interface twice. Offband observer wiring
  // moved to just after startInterface(interface_manager) -- it needs self_id.
  CW_PHASE("ESP32:before wifi_interface.begin");
  wifi_interface.begin(TCP_PORT);
  interface_manager.addInterface(InterfaceType::WiFi, &wifi_interface);
  CW_PHASE("ESP32:post wifi_interface.begin");
#endif

// add usb interface
#if defined(ENABLE_USB_INTERFACE)
  usb_serial_interface.begin(Serial);
  interface_manager.addInterface(InterfaceType::USB, &usb_serial_interface);
#endif

// add ethernet interface
#if defined(ETHERNET_ENABLED)
  ethernet_interface.begin();
  interface_manager.addInterface(InterfaceType::Ethernet, &ethernet_interface);
#endif

// add hardware serial interface
#if defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  hardware_serial_interface.begin(companion_serial);
  interface_manager.addInterface(InterfaceType::HardwareSerial, &hardware_serial_interface);
#endif

  the_mesh.startInterface(interface_manager);
  CW_PHASE("ESP32:post the_mesh.startInterface");
  // #411: mirror captured serial-log lines to the live console EXCEPT where the
  // framed protocol runs on Serial itself (USB-serial companion) -- there it stays
  // capture-only so nothing raw corrupts the protocol line.
  // PORTED: isConsoleSharedWithProtocol() is a BaseSerialInterface virtual that
  // only ArduinoSerialInterface overrides true; MultiSerialInterface does NOT
  // override it, so asking the manager would always answer false and wrongly
  // enable mirroring on a USB companion. Ask the interface that owns Serial.
#if defined(ENABLE_USB_INTERFACE)
  meshLogSetMirror(!usb_serial_interface.isConsoleSharedWithProtocol());
#else
  meshLogSetMirror(true);
#endif
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

// #763: sampling cadence for the battery discharge trace. 30 s -> 120 samples
// per hour, which resolves a multi-hour LiPo curve without flooding the ring or
// the wire. Override per-env if a run needs finer or coarser granularity.
#if defined(OFFBAND_POWER_TELEMETRY) && !defined(OFFBAND_POWER_TELEMETRY_MS)
  #define OFFBAND_POWER_TELEMETRY_MS 30000
#endif

void loop() {
#if !defined(OFFBAND_OBSERVER)
  offband::crashLogStandardTick(millis());  // #472: deferred previous-boot re-dump (all non-observer companions)
#endif

#if defined(OFFBAND_POWER_TELEMETRY)
  // #763: battery discharge telemetry, for the unattended power-characterisation
  // run (Heltec beta Q01 -- runtime on a power bank / LiPo).
  //
  // Emitted through mesh_log_line() rather than Serial so it rides the SAME two
  // channels as every other log producer: the caplog ring (downloadable after the
  // run) and, on a native-USB board built with OFFBAND_MESHLOG_UART0, the raw
  // UART0 mirror. The mirror is the one that matters here -- the entire point of
  // this trace is a board running on battery with NO host attached, which is
  // exactly the case where USB-CDC gives us nothing (#702).
  //
  // MLOG_BOOT (level 0) so the line survives whatever caplog level is configured.
  //
  // No need to force caplog on: since #763 the UART0 mirror emits independently
  // of the capture flag, so this reports on a wire whether or not anyone thought
  // to enable recording before walking away. If capture IS on, the same lines
  // also land in the downloadable ring.
  {
    static uint32_t s_pwr_ms = 0;
    uint32_t now_ms = millis();
    if (s_pwr_ms == 0 || now_ms - s_pwr_ms >= (uint32_t)OFFBAND_POWER_TELEMETRY_MS) {
      s_pwr_ms = now_ms;
      mesh_log_line(MLOG_BOOT, "[pwr] mv=%u up=%us\n",
                    (unsigned)board.getBattMilliVolts(),
                    (unsigned)(now_ms / 1000UL));

#if defined(OFFBAND_POWER_TELEMETRY_PROBE) && defined(PIN_VBAT_READ) && defined(PIN_ADC_CTRL)
      // TEMPORARY diagnostic (#766). getBattMilliVolts() returns 0 on the RC32
      // with a LiPo on VBAT. Since it returns `adc_mult * raw` and adc_mult is
      // pinned at 4.9, a zero result means `raw` itself is zero -- the ADC read,
      // not the scaling. This probe separates the three candidates in ONE flash
      // rather than three:
      //
      //   both polarities read 0 counts  -> pin/ADC setup never happened, or the
      //                                     divider is not populated (hardware)
      //   LOW reads, HIGH does not       -> ADC_CTRL_ENABLED polarity is inverted
      //   counts nonzero but mv == 0     -> ADC calibration, not the signal
      //
      // Raw counts AND calibrated mV are both reported because
      // analogReadMilliVolts() applies eFuse calibration and can return 0 while
      // analogRead() is perfectly healthy -- that is a different defect, and
      // reporting only mV would hide it.
      //
      // Costs two 10 ms settling delays per sample (20 ms per 30 s). Acceptable
      // in a diag build, and the reason this sits behind its own flag rather
      // than riding OFFBAND_POWER_TELEMETRY: it must not survive into a real
      // discharge run.
      //
      // Driving ADC_CTRL both ways adds no new risk -- HeltecRC32Board::begin()
      // already drives this pin, so the probe only exercises states the stock
      // firmware already produces.
      {
        pinMode(PIN_ADC_CTRL, OUTPUT);
        analogReadResolution(12);
        analogSetAttenuation(ADC_2_5db);

        digitalWrite(PIN_ADC_CTRL, HIGH);
        delay(10);
        int cnt_hi = analogRead(PIN_VBAT_READ);
        int mv_hi  = analogReadMilliVolts(PIN_VBAT_READ);

        digitalWrite(PIN_ADC_CTRL, LOW);
        delay(10);
        int cnt_lo = analogRead(PIN_VBAT_READ);
        int mv_lo  = analogReadMilliVolts(PIN_VBAT_READ);

        digitalWrite(PIN_ADC_CTRL, !ADC_CTRL_ENABLED);   // restore resting state

        mesh_log_line(MLOG_BOOT,
                      "[pwr:probe] pin=%d ctrl=%d hi{cnt=%d mv=%d} lo{cnt=%d mv=%d}\n",
                      (int)PIN_VBAT_READ, (int)PIN_ADC_CTRL,
                      cnt_hi, mv_hi, cnt_lo, mv_lo);
      }
#endif

#if defined(OFFBAND_POWER_TELEMETRY_SWEEP) && defined(PIN_ADC_CTRL)
      // #766: ADC1 pin sweep. PIN_VBAT_READ=7 has no vendor basis for this board
      // -- the RC32 datasheet documents VBAT only as a power INPUT and never
      // specifies a sense divider, and its §3.2.1 module table lists GPIO7 as the
      // radio's SELECT line. GPIO7 appears to have been inherited from sibling
      // Heltec S3 boards (e213/e290 both use it), not derived from RC32 docs.
      //
      // Candidates are ADC1 (GPIO1..10) minus the three the radio owns -- GPIO1
      // (BUSY), GPIO9 (RESET), GPIO10 (CS). Reconfiguring those would disturb the
      // radio mid-operation. Ordered 4,5,6 first: those are the ADC1 pins Heltec
      // actually breaks out on the header, so they carry the strongest prior.
      //
      // Each pin is read with ADC_CTRL both HIGH and LOW. The signature we want is
      // not "nonzero" -- a floating or rail-tied pin is nonzero too -- it is
      // RESPONDING TO ADC_CTRL, since switching the divider in is that pin's only
      // job. Responders are flagged '*'.
      //
      // The '*' is a reading aid, NOT an identification. Raw values for every pin
      // are always printed so nothing is hidden behind the flag, and the firmware
      // never selects a pin. The real proof is the trend: across a discharge, the
      // divider is the pin that TRACKS the cell downward. Nothing else does. That
      // is an identification that can be defended; a single-sample auto-pick would
      // just be a guess that later readings would inherit silently (cf. #754,
      // where telemetry compared a value against itself and always passed).
      //
      // 11 dB attenuation for near-full-scale range -- a divider output can sit
      // well above the 1.25 V ceiling that 2.5 dB gives, and clipping every
      // candidate to the same saturated value would hide the very difference we
      // are looking for.
      {
        static const uint8_t kSweepPins[] = { 4, 5, 6, 2, 3, 7, 8 };
        const int kN = (int)(sizeof(kSweepPins) / sizeof(kSweepPins[0]));
        int hi[sizeof(kSweepPins)], lo[sizeof(kSweepPins)];

        pinMode(PIN_ADC_CTRL, OUTPUT);
        analogReadResolution(12);
        analogSetAttenuation(ADC_11db);

        digitalWrite(PIN_ADC_CTRL, HIGH);
        delay(10);
        for (int i = 0; i < kN; i++) hi[i] = analogReadMilliVolts(kSweepPins[i]);

        digitalWrite(PIN_ADC_CTRL, LOW);
        delay(10);
        for (int i = 0; i < kN; i++) lo[i] = analogReadMilliVolts(kSweepPins[i]);

        digitalWrite(PIN_ADC_CTRL, !ADC_CTRL_ENABLED);   // restore resting state

        char sline[200];
        int n = 0;
        for (int i = 0; i < kN && n < (int)sizeof(sline) - 24; i++) {
          int d = hi[i] - lo[i];
          if (d < 0) d = -d;
          n += snprintf(sline + n, sizeof(sline) - n, "%u:%d/%d%s ",
                        (unsigned)kSweepPins[i], hi[i], lo[i], (d > 100) ? "*" : "");
        }
        mesh_log_line(MLOG_BOOT, "[pwr:sweep] %s(mv hi/lo, *=responds)\n", sline);
      }
#endif
    }
  }
#endif
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
    // (deferred re-dump now runs uniformly at loop top via crashLogStandardTick; #472)
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
  interface_manager.loop();
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
  //   repeat_enabled -- getNodePrefs()->isRepeatEn()  (1.17.0 moved the flag
  //                     into the repeat sub-object; client_repeat is deprecated)
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
      // 1.17.0 moved this into NodePrefs::repeat.disable_fwd behind isRepeatEn();
      // the old client_repeat member is retained upstream but marked DEPRECATED
      // and is no longer the source of truth.
      snap.repeat_enabled = the_mesh.getNodePrefs()->isRepeatEn();
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
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif

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
