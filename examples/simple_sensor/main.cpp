#include "SensorMesh.h"
#include <SafeBoot.h>
#include "helpers/diagnostics/CrashLogStandard.h"   // #472: uniform boot-survival CrashLog
// #936 -- boot beacon. This role accepted OFFBAND_BOOT_BEACON and emitted
// NOTHING: the flag compiled, the env was named _diag, and the instrument was
// dead. The beacon was wired into examples/companion_radio only, so three of
// the four roles we ship had a diagnostic that looked live and did nothing --
// worse than none, because it is trusted.
//
// OFFBAND_BEACON_DEFINE_CTOR emits the constructor(101) that prints APP:CTOR
// before every other static ctor. It must appear in exactly ONE translation
// unit per program; each example is its own program, so this is that unit.
//
// Compiles to nothing unless OFFBAND_BOOT_BEACON is set -- OFFBAND_BEACON() is
// ((void)0) otherwise -- so non-diag builds of this role are byte-identical.
#define OFFBAND_BEACON_DEFINE_CTOR
#include "helpers/BootBeacon.h"
#include "helpers/CdcConsoleFlush.h"   // #1035: right-flush (ZLP) for the USB-Serial-JTAG console


#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

class MyMesh : public SensorMesh {
public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
     : SensorMesh(board, radio, ms, rng, rtc, tables), 
       battery_data(12*24, 5*60)    // 24 hours worth of battery data, every 5 minutes
  {
  }

protected:
  /* ========================== custom logic here ========================== */
  Trigger low_batt, critical_batt;
  TimeSeriesData  battery_data;

  void onSensorDataRead() override {
    float batt_voltage = getVoltage(TELEM_CHANNEL_SELF);

    battery_data.recordData(getRTCClock(), batt_voltage);   // record battery
    alertIf(batt_voltage < 3.4f, critical_batt, HIGH_PRI_ALERT, "Battery is critical!");
    alertIf(batt_voltage < 3.6f, low_batt, LOW_PRI_ALERT, "Battery is low");
  }

  int querySeriesData(uint32_t start_secs_ago, uint32_t end_secs_ago, MinMaxAvg dest[], int max_num) override {
    battery_data.calcMinMaxAvg(getRTCClock(), start_secs_ago, end_secs_ago, &dest[0], TELEM_CHANNEL_SELF, LPP_VOLTAGE);
    return 1;
  }

  bool handleCustomCommand(uint32_t sender_timestamp, char* command, char* reply) override {
    if (strcmp(command, "magic") == 0) {    // example 'custom' command handling
      strcpy(reply, "**Magic now done**");
      return true;   // handled
    }
    return false;  // not handled
  }
  /* ======================================================================= */
};

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[160];

void setup() {
  // Earliest point application code runs. If this line appears after a reset
  // but nothing later does, the fault is in setup(); if it does not appear at
  // all, the fault is at or before the bootloader's jump to the app.
  OFFBAND_BEACON("setup:ENTRY -- before Serial.begin");
  // Serial.begin() software-resets the UART controller and FLUSHES its FIFO, so
  // anything still in flight is truncated. Drain first. (#740)
  OFFBAND_BEACON_FLUSH();
  Serial.begin(115200);
  delay(1000);

  // SafeBoot: pre-init power guard. See src/SafeBoot.h.
  OFFBAND_BEACON("setup:before SafeBoot::checkAndMaybeSleep");
  SafeBoot::checkAndMaybeSleep();
  OFFBAND_BEACON("setup:post SafeBoot::checkAndMaybeSleep");

  OFFBAND_BEACON("setup:before board.begin");
  board.begin();
  OFFBAND_BEACON("setup:post board.begin");

  // THE PAIR THAT MATTERS. On RC32 the board dies between these two; on
  // RC52 both fire, which is how #855 was answered with device evidence
  // instead of USB inference.
  OFFBAND_BEACON("setup:before crashLogStandardInit");
  offband::crashLogStandardInit(board, "sensor");   // #472: uniform CrashLog (after board.begin() for reset reason)
  OFFBAND_BEACON("setup:post crashLogStandardInit");
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Sensor ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

  // #446/#519: arm the runtime WatchDog now that init has succeeded (a failed
  // radio_init/store halts before this point, so a bad boot never WDT-loops).
  // Feed from loop() only -> a hung loop trips it -> auto-reset. startWatchdog is
  // a MainBoard virtual: ESP32/nRF52 override, no-op on other platforms.
  board.startWatchdog(30);
#if defined(NRF52_PLATFORM)
  // #275: the nRF52 WDT counts during sleep (CONFIG.SLEEP=Run), so it MUST be paired
  // with the ~10 Hz loop-wake heartbeat -- otherwise an RF-silent nap starves the feed
  // and false-trips the WDT (the repeater/companion do the same). ESP32 doesn't need
  // this: ESP32Board::sleep() feeds on entry and light-sleep pauses the task tick.
  board.startHeartbeat();
#endif
}

void loop() {
  offband::crashLogStandardTick(millis());  // #472: deferred previous-boot re-dump for late serial connect
  board.feedWatchdog();                     // #446/#519: keep the runtime WatchDog fed; a hung loop trips it -> auto-reset
#if defined(NRF52_PLATFORM)
  board.heartbeatTick();                    // #275: loop-driven heartbeat + wake so idle naps still service the WDT
#endif

  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
    }
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    reply[0] = 0;   // #765: the `if (reply[0])` guard below reads this either way
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
      flushSerialConsole();  // #1035: emit the USB-CDC terminator so a 64-multiple reply isn't held host-side
    }

    command[0] = 0;  // reset command buffer
  }

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif
}
