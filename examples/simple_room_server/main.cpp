#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <SafeBoot.h>

#include "MyMesh.h"
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


#ifdef ETHERNET_ENABLED
  #define ETHERNET_CLI_BANNER "MeshCore Room Server CLI"
  #include <helpers/nrf52/EthernetCLI.h>
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[MAX_POST_TEXT_LEN+1];
#ifdef ETHERNET_ENABLED
static char ethernet_command[MAX_POST_TEXT_LEN+1];
#endif

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

  // #472: uniform CrashLog boot-survival (after board.begin() so the reset reason is cached).
  // THE PAIR THAT MATTERS. On RC32 the board dies between these two; on
  // RC52 both fire, which is how #855 was answered with device evidence
  // instead of USB inference.
  OFFBAND_BEACON("setup:before crashLogStandardInit");
  offband::crashLogStandardInit(board, "room-server");
  OFFBAND_BEACON("setup:post crashLogStandardInit");
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Room ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;
#ifdef ETHERNET_ENABLED
  ethernet_command[0] = 0;
#endif

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#ifdef ETHERNET_ENABLED
  ethernet_start_task();
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

  board.onBootComplete();
}

void loop() {
  offband::crashLogStandardTick(millis());  // #472: deferred previous-boot re-dump for late serial connect

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
    reply[0] = 0;
#ifdef ETHERNET_ENABLED
    if (!ethernet_handle_command(command, reply)) {
      the_mesh.handleCommand(0, command, reply);
    }
#else
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
#endif
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
      flushSerialConsole();  // #1035: emit the USB-CDC terminator so a 64-multiple reply isn't held host-side
    }

    command[0] = 0;  // reset command buffer
  }

#ifdef ETHERNET_ENABLED
  ethernet_loop_maintain();
  if (ethernet_read_line(ethernet_command, sizeof(ethernet_command))) {
    char reply[160];
    reply[0] = 0;
    if (!ethernet_handle_command(ethernet_command, reply)) {
      the_mesh.handleCommand(0, ethernet_command, reply);
    }
    ethernet_send_reply(reply);
    ethernet_command[0] = 0;
  }
#endif

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
