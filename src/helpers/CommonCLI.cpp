#include <Arduino.h>
#include "CommonCLI.h"
#include "ClockSanity.h"  // #607
#include "TxtDataHelpers.h"
#include "AdvertDataHelpers.h"
#include "TxtDataHelpers.h"
#include <RTClib.h>
#include "MeshLog.h"   // serial-capture sink control (#395 caplog verbs)

// Plan 2 v2 Task 11 / #538 / #554: ObserverCli dispatcher (the broker-config
// CLI). Reached as a fall-through from the real command terminals -- bare
// "mqtt ..." verbs in handleCommand, "set mqtt.*" in handleSetCmd, and
// "get mqtt.*" in handleGetCmd (search "#554"). Compiles in for OFFBAND_OBSERVER
// (observer) and OFFBAND_MQTT_POOL (repeater multi-broker pool); other envs have
// zero impact.
//   #554 note: the Plan 2 v2 wiring lived inside handleRegionCmd, which is only
//   entered for "region"-prefixed input -- so mqtt verbs never reached it and the
//   broker CLI was unreachable over serial AND over the client RemoteCommand path
//   on the repeater. Moving it to the actual terminals fixed that.
// ObserverCli.h declares BOTH dispatchObserverCli and wifiObserverPool(); the
// observer pipeline header (WifiObserver.h) stays observer-only.
#if defined(OFFBAND_OBSERVER) || defined(OFFBAND_MQTT_POOL)
  #include "wifi_observer/ObserverCli.h"
#endif
#ifdef OFFBAND_OBSERVER
  #include "wifi_observer/WifiObserver.h"  // wifiObserverPool() accessor
#endif

// #462: role-agnostic shared config-CLI bridge. Any role that compiles
// helpers/config AND defines OFFBAND_CONFIG_CLI routes generic `set/get <key>`
// through the shared dispatcher here. NOT defined on observer envs -- observer
// keeps dispatchObserverCli's richer grammar and stays byte-identical (see #462;
// unifying observer onto the bridge is deferred to #511).
#if defined(OFFBAND_CONFIG_CLI)
  #include "config/ConfigDispatch.h"
#endif

// #200 / LoRa-wek: embed the Offband identity blob in .rodata so the
// flash-history parser (scripts/firmware_identity.py) can recover BUILD-time
// identity by scanning the firmware binary, rather than re-running git at
// flash time against a working tree that may have advanced past the build.
// CommonCLI.cpp is compiled into every env via build_src_filter
// +<helpers/*.cpp>, so this is the right place to anchor it.
//
// #213 / LoRa-x09: constructor-based keepalive (replaces an earlier
// in-version-handler keepalive that worked on ESP32 but was dead-code-
// eliminated on nRF52 builds where the CLI "version" handler itself is
// unreachable from main, e.g. RAK_4631_companion_radio_ble which runs as a
// BLE peripheral with no serial CLI loop).
//
// Why a constructor is universally safe:
//   `__attribute__((used))` alone keeps the symbol in the .o file but the
//   linker's --gc-sections pass (active on every supported platform) still
//   drops the section if no reachable code references it. Newer toolchains
//   support `__attribute__((retain))` (SHF_GNU_RETAIN, GCC 11+ / binutils
//   2.36+) but our arm-none-eabi-gcc is 7.2.1 / binutils 2.29 (2017-era)
//   which silently ignores it. Constructors instead get placed in
//   .init_array.* sections, which are KEEP()'d by every C++ runtime's
//   linker script (mandatory for static-init to work). Verified for nRF52:
//   nrf52_common.ld:122-123 has KEEP(*(SORT(.init_array.*))) and
//   KEEP(*(.init_array)); the project's boards/nrf52840_s140_v6.ld
//   INCLUDEs that. The constructor's body references _xwire_identity_blob,
//   so the marker section becomes reachable from the kept .init_array
//   entry and survives --gc-sections on every platform.
//
// See scripts/inject_offband_version.py "ON-WIRE ABI WARNING" for format.
__attribute__((used))
static const char _xwire_identity_blob[] = OFFBAND_IDENTITY_BLOB;

// Volatile pointer the constructor writes to. volatile prevents the optimizer
// from concluding the constructor body has no observable effect and elising
// the reference (which would defeat the keepalive).
const char* volatile _xwire_identity_blob_kept = nullptr;

// File-scope constructor — runs once at static-init time before setup()/main(),
// before any user code. The function pointer goes into .init_array.65535,
// which the C++ runtime walks at startup and which the linker is required to
// KEEP for static-init correctness. This creates a reachability chain from
// the kept .init_array section all the way down to _xwire_identity_blob,
// ensuring the marker survives --gc-sections on every supported platform.
__attribute__((constructor))
static void _xwire_keepalive_ctor() {
    _xwire_identity_blob_kept = _xwire_identity_blob;
}

#ifndef BRIDGE_MAX_BAUD
#define BRIDGE_MAX_BAUD 115200
#endif

#ifdef ENABLE_WIFI_TELEMETRY
// Bridge to the WiFi telemetry admin API defined in the example's main.cpp.
// Allows the `wifi`/`telemetry` CLI commands to introspect and control the
// duty-cycled WiFi+MQTT publisher over the always-on LoRa admin channel.
extern "C" {
  void wifi_telemetry_set_disabled(int);
  int  wifi_telemetry_is_disabled(void);
  void wifi_telemetry_force_now(void);
  void wifi_telemetry_reset_state(void);
  int  wifi_telemetry_get_status(char* buf, int buflen);
  // D2/D3 persistent-mode controls
  void wifi_telemetry_set_persistent(uint32_t duration_ms);
  int  wifi_telemetry_is_persistent(void);
  uint32_t wifi_telemetry_persistent_remaining_ms(void);
  void wifi_telemetry_caplog_forward(uint32_t window_sec);  // #561 live syslog forward (#566: runtime target)
#ifdef CMD_TRANSPORT_HTTP
  // LoRa#216: HTTP cmd-poll controls
  void     wifi_telemetry_cmd_poll_now(void);
  uint32_t wifi_telemetry_set_cmd_poll_burst_interval(uint32_t seconds);
  uint32_t wifi_telemetry_get_cmd_poll_burst_interval(void);
#endif
}
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

static bool isValidName(const char *n) {
  while (*n) {
    if (*n == '[' || *n == ']' || *n == '\\' || *n == ':' || *n == ',' || *n == '?' || *n == '*') return false;
    n++;
  }
  return true;
}

void CommonCLI::loadPrefs(FILESYSTEM* fs) {
  // #562: common caplog defaults, set before any file load. An old prefs file
  // predating these fields short-reads to EOF in loadPrefsInt and leaves these
  // intact; a fresh node with no prefs file keeps them too.
  _prefs->caplog_enabled = 0;
  _prefs->caplog_level = MLOG_DEBUG;
  // #566: seed the syslog forward sink from build flags (if any); a node with no
  // flag starts with no sink (forward off until `set syslog.host`).
#ifdef WIFI_SYSLOG_HOST
  strncpy(_prefs->syslog_host, WIFI_SYSLOG_HOST, sizeof(_prefs->syslog_host) - 1);
  _prefs->syslog_host[sizeof(_prefs->syslog_host) - 1] = '\0';
#else
  _prefs->syslog_host[0] = '\0';
#endif
#ifdef WIFI_SYSLOG_PORT
  _prefs->syslog_port = WIFI_SYSLOG_PORT;
#else
  _prefs->syslog_port = 514;
#endif
  if (fs->exists("/com_prefs")) {
    loadPrefsInt(fs, "/com_prefs");   // new filename
  } else if (fs->exists("/node_prefs")) {
    loadPrefsInt(fs, "/node_prefs");
    savePrefs(fs);  // save to new filename
    fs->remove("/node_prefs");  // remove old
  }
}

void CommonCLI::loadPrefsInt(FILESYSTEM* fs, const char* filename) {
#if defined(RP2040_PLATFORM)
  File file = fs->open(filename, "r");
#else
  File file = fs->open(filename);
#endif
  if (file) {
    uint8_t pad[8];

    file.read((uint8_t *)&_prefs->airtime_factor, sizeof(_prefs->airtime_factor));    // 0
    file.read((uint8_t *)&_prefs->node_name, sizeof(_prefs->node_name));              // 4
    file.read(pad, 4);                                                                // 36
    file.read((uint8_t *)&_prefs->node_lat, sizeof(_prefs->node_lat));                // 40
    file.read((uint8_t *)&_prefs->node_lon, sizeof(_prefs->node_lon));                // 48
    file.read((uint8_t *)&_prefs->password[0], sizeof(_prefs->password));             // 56
    file.read((uint8_t *)&_prefs->freq, sizeof(_prefs->freq));                        // 72
    file.read((uint8_t *)&_prefs->tx_power_dbm, sizeof(_prefs->tx_power_dbm));        // 76
    file.read((uint8_t *)&_prefs->disable_fwd, sizeof(_prefs->disable_fwd));          // 77
    file.read((uint8_t *)&_prefs->advert_interval, sizeof(_prefs->advert_interval));  // 78
    file.read(pad, 1);                                                                // 79 : 1 byte unused (was rx_boosted_gain in v1.14.1, moved to end for upgrade compat)
    file.read((uint8_t *)&_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));      // 80
    file.read((uint8_t *)&_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));  // 84
    file.read((uint8_t *)&_prefs->guest_password[0], sizeof(_prefs->guest_password)); // 88
    file.read((uint8_t *)&_prefs->direct_tx_delay_factor, sizeof(_prefs->direct_tx_delay_factor)); // 104
    file.read(pad, 4); // 108 : 4 bytes unused
    file.read((uint8_t *)&_prefs->sf, sizeof(_prefs->sf));                                         // 112
    file.read((uint8_t *)&_prefs->cr, sizeof(_prefs->cr));                                         // 113
    file.read((uint8_t *)&_prefs->allow_read_only, sizeof(_prefs->allow_read_only));               // 114
    file.read((uint8_t *)&_prefs->multi_acks, sizeof(_prefs->multi_acks));                         // 115
    file.read((uint8_t *)&_prefs->bw, sizeof(_prefs->bw));                                         // 116
    file.read((uint8_t *)&_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));         // 120
    file.read((uint8_t *)&_prefs->path_hash_mode, sizeof(_prefs->path_hash_mode));                 // 121
    file.read((uint8_t *)&_prefs->loop_detect, sizeof(_prefs->loop_detect));                       // 122
    file.read(pad, 1);                                                                             // 123
    file.read((uint8_t *)&_prefs->flood_max, sizeof(_prefs->flood_max));                           // 124
    file.read((uint8_t *)&_prefs->flood_advert_interval, sizeof(_prefs->flood_advert_interval));   // 125
    file.read((uint8_t *)&_prefs->interference_threshold, sizeof(_prefs->interference_threshold)); // 126
    file.read((uint8_t *)&_prefs->bridge_enabled, sizeof(_prefs->bridge_enabled));                 // 127
    file.read((uint8_t *)&_prefs->bridge_delay, sizeof(_prefs->bridge_delay));                     // 128
    file.read((uint8_t *)&_prefs->bridge_pkt_src, sizeof(_prefs->bridge_pkt_src));                 // 130
    file.read((uint8_t *)&_prefs->bridge_baud, sizeof(_prefs->bridge_baud));                       // 131
    file.read((uint8_t *)&_prefs->bridge_channel, sizeof(_prefs->bridge_channel));                 // 135
    file.read((uint8_t *)&_prefs->bridge_secret, sizeof(_prefs->bridge_secret));                   // 136
    file.read((uint8_t *)&_prefs->powersaving_enabled, sizeof(_prefs->powersaving_enabled));       // 152
    file.read(pad, 3);                                                                             // 153
    file.read((uint8_t *)&_prefs->gps_enabled, sizeof(_prefs->gps_enabled));                       // 156
    file.read((uint8_t *)&_prefs->gps_interval, sizeof(_prefs->gps_interval));                     // 157
    file.read((uint8_t *)&_prefs->advert_loc_policy, sizeof (_prefs->advert_loc_policy));          // 161
    file.read((uint8_t *)&_prefs->discovery_mod_timestamp, sizeof(_prefs->discovery_mod_timestamp)); // 162
    file.read((uint8_t *)&_prefs->adc_multiplier, sizeof(_prefs->adc_multiplier));                 // 166
    file.read((uint8_t *)_prefs->owner_info, sizeof(_prefs->owner_info));                          // 170
    file.read((uint8_t *)&_prefs->rx_boosted_gain, sizeof(_prefs->rx_boosted_gain));              // 290
    file.read((uint8_t *)&_prefs->radio_fem_rxgain, sizeof(_prefs->radio_fem_rxgain));            // 291
    file.read((uint8_t *)&_prefs->flood_max_unscoped, sizeof(_prefs->flood_max_unscoped));        // 292
    file.read((uint8_t *)&_prefs->flood_max_advert, sizeof(_prefs->flood_max_advert));            // 293
    file.read((uint8_t *)&_prefs->ui_led_enabled, sizeof(_prefs->ui_led_enabled));                // 294
    file.read((uint8_t *)&_prefs->ui_display_mode, sizeof(_prefs->ui_display_mode));              // 295
    file.read((uint8_t *)&_prefs->caplog_enabled, sizeof(_prefs->caplog_enabled));                // 296  (#562)
    file.read((uint8_t *)&_prefs->caplog_level, sizeof(_prefs->caplog_level));                    // 297  (#562)
    file.read((uint8_t *)_prefs->syslog_host, sizeof(_prefs->syslog_host));                       // 298  (#566)
    file.read((uint8_t *)&_prefs->syslog_port, sizeof(_prefs->syslog_port));                      // 362  (#566)
    // next: 364
    // NOTE: radio_fem_rxgain stays at offset 291 (its pre-1.16 offset) so existing Offband prefs
    // files survive the 1.16.0 base-update; the new upstream flood fields append after it. For old
    // SPIFFS files predating a field, file.read returns 0 bytes and the field keeps its in-memory
    // default (radio_fem_rxgain: MyMesh ctor = 1 = LNA on).

    // sanitise bad pref values
    _prefs->rx_delay_base = constrain(_prefs->rx_delay_base, 0, 20.0f);
    _prefs->tx_delay_factor = constrain(_prefs->tx_delay_factor, 0, 2.0f);
    _prefs->direct_tx_delay_factor = constrain(_prefs->direct_tx_delay_factor, 0, 2.0f);
    _prefs->airtime_factor = constrain(_prefs->airtime_factor, 0, 9.0f);
    _prefs->freq = constrain(_prefs->freq, 150.0f, 2500.0f);
    _prefs->bw = constrain(_prefs->bw, 7.8f, 500.0f);
    _prefs->sf = constrain(_prefs->sf, 5, 12);
    _prefs->cr = constrain(_prefs->cr, 5, 8);
    _prefs->tx_power_dbm = constrain(_prefs->tx_power_dbm, -9, 30);
    _prefs->multi_acks = constrain(_prefs->multi_acks, 0, 1);
    _prefs->adc_multiplier = constrain(_prefs->adc_multiplier, 0.0f, 10.0f);
    _prefs->path_hash_mode = constrain(_prefs->path_hash_mode, 0, 2);   // NOTE: mode 3 reserved for future

    // sanitise bad bridge pref values
    _prefs->bridge_enabled = constrain(_prefs->bridge_enabled, 0, 1);
    _prefs->bridge_delay = constrain(_prefs->bridge_delay, 0, 10000);
    _prefs->bridge_pkt_src = constrain(_prefs->bridge_pkt_src, 0, 1);
    _prefs->bridge_baud = constrain(_prefs->bridge_baud, 9600, BRIDGE_MAX_BAUD);
    _prefs->bridge_channel = constrain(_prefs->bridge_channel, 0, 14);

    _prefs->powersaving_enabled = constrain(_prefs->powersaving_enabled, 0, 1);

    _prefs->gps_enabled = constrain(_prefs->gps_enabled, 0, 1);
    _prefs->advert_loc_policy = constrain(_prefs->advert_loc_policy, 0, 2);

    // sanitise settings
    _prefs->rx_boosted_gain = constrain(_prefs->rx_boosted_gain, 0, 1); // boolean
    _prefs->radio_fem_rxgain = constrain(_prefs->radio_fem_rxgain, 0, 1); // boolean
    _prefs->ui_led_enabled = constrain(_prefs->ui_led_enabled, 0, 1); // boolean (#542)
    _prefs->ui_display_mode = constrain(_prefs->ui_display_mode, 0, 2); // #542 A2 tristate
    _prefs->caplog_enabled = constrain(_prefs->caplog_enabled, 0, 1);       // #562 boolean
    _prefs->caplog_level = constrain(_prefs->caplog_level, 0, MLOG_PACKET); // #562 level bound
    _prefs->syslog_host[sizeof(_prefs->syslog_host) - 1] = '\0';            // #566 NUL-terminate
    if (_prefs->syslog_port == 0) _prefs->syslog_port = 514;                // #566 sane default

    // #562: apply persisted caplog state at boot (common across roles). Capture
    // is independent of the serial mirror, so this is safe on a USB-serial
    // companion too.
    meshLogSetLevel(_prefs->caplog_level);
    meshLogSetEnabled(_prefs->caplog_enabled != 0);

    file.close();
  }
}

void CommonCLI::savePrefs(FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove("/com_prefs");
  File file = fs->open("/com_prefs", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open("/com_prefs", "w");
#else
  File file = fs->open("/com_prefs", "w", true);
#endif
  if (file) {
    uint8_t pad[8];
    memset(pad, 0, sizeof(pad));

    file.write((uint8_t *)&_prefs->airtime_factor, sizeof(_prefs->airtime_factor));    // 0
    file.write((uint8_t *)&_prefs->node_name, sizeof(_prefs->node_name));              // 4
    file.write(pad, 4);                                                                // 36
    file.write((uint8_t *)&_prefs->node_lat, sizeof(_prefs->node_lat));                // 40
    file.write((uint8_t *)&_prefs->node_lon, sizeof(_prefs->node_lon));                // 48
    file.write((uint8_t *)&_prefs->password[0], sizeof(_prefs->password));             // 56
    file.write((uint8_t *)&_prefs->freq, sizeof(_prefs->freq));                        // 72
    file.write((uint8_t *)&_prefs->tx_power_dbm, sizeof(_prefs->tx_power_dbm));        // 76
    file.write((uint8_t *)&_prefs->disable_fwd, sizeof(_prefs->disable_fwd));          // 77
    file.write((uint8_t *)&_prefs->advert_interval, sizeof(_prefs->advert_interval));  // 78
    file.write(pad, 1);                                                                // 79 : 1 byte unused (rx_boosted_gain moved to end)
    file.write((uint8_t *)&_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));      // 80
    file.write((uint8_t *)&_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));  // 84
    file.write((uint8_t *)&_prefs->guest_password[0], sizeof(_prefs->guest_password)); // 88
    file.write((uint8_t *)&_prefs->direct_tx_delay_factor, sizeof(_prefs->direct_tx_delay_factor)); // 104
    file.write(pad, 4); // 108 : 4 byte unused
    file.write((uint8_t *)&_prefs->sf, sizeof(_prefs->sf));                                         // 112
    file.write((uint8_t *)&_prefs->cr, sizeof(_prefs->cr));                                         // 113
    file.write((uint8_t *)&_prefs->allow_read_only, sizeof(_prefs->allow_read_only));               // 114
    file.write((uint8_t *)&_prefs->multi_acks, sizeof(_prefs->multi_acks));                         // 115
    file.write((uint8_t *)&_prefs->bw, sizeof(_prefs->bw));                                         // 116
    file.write((uint8_t *)&_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));         // 120
    file.write((uint8_t *)&_prefs->path_hash_mode, sizeof(_prefs->path_hash_mode));                 // 121
    file.write((uint8_t *)&_prefs->loop_detect, sizeof(_prefs->loop_detect));                       // 122
    file.write(pad, 1);                                                                             // 123
    file.write((uint8_t *)&_prefs->flood_max, sizeof(_prefs->flood_max));                           // 124
    file.write((uint8_t *)&_prefs->flood_advert_interval, sizeof(_prefs->flood_advert_interval));   // 125
    file.write((uint8_t *)&_prefs->interference_threshold, sizeof(_prefs->interference_threshold)); // 126
    file.write((uint8_t *)&_prefs->bridge_enabled, sizeof(_prefs->bridge_enabled));                 // 127
    file.write((uint8_t *)&_prefs->bridge_delay, sizeof(_prefs->bridge_delay));                     // 128
    file.write((uint8_t *)&_prefs->bridge_pkt_src, sizeof(_prefs->bridge_pkt_src));                 // 130
    file.write((uint8_t *)&_prefs->bridge_baud, sizeof(_prefs->bridge_baud));                       // 131
    file.write((uint8_t *)&_prefs->bridge_channel, sizeof(_prefs->bridge_channel));                 // 135
    file.write((uint8_t *)&_prefs->bridge_secret, sizeof(_prefs->bridge_secret));                   // 136
    file.write((uint8_t *)&_prefs->powersaving_enabled, sizeof(_prefs->powersaving_enabled));       // 152
    file.write(pad, 3);                                                                             // 153
    file.write((uint8_t *)&_prefs->gps_enabled, sizeof(_prefs->gps_enabled));                       // 156
    file.write((uint8_t *)&_prefs->gps_interval, sizeof(_prefs->gps_interval));                     // 157
    file.write((uint8_t *)&_prefs->advert_loc_policy, sizeof(_prefs->advert_loc_policy));           // 161
    file.write((uint8_t *)&_prefs->discovery_mod_timestamp, sizeof(_prefs->discovery_mod_timestamp)); // 162
    file.write((uint8_t *)&_prefs->adc_multiplier, sizeof(_prefs->adc_multiplier));                 // 166
    file.write((uint8_t *)_prefs->owner_info, sizeof(_prefs->owner_info));                          // 170
    file.write((uint8_t *)&_prefs->rx_boosted_gain, sizeof(_prefs->rx_boosted_gain));              // 290
    file.write((uint8_t *)&_prefs->radio_fem_rxgain, sizeof(_prefs->radio_fem_rxgain));            // 291
    file.write((uint8_t *)&_prefs->flood_max_unscoped, sizeof(_prefs->flood_max_unscoped));       // 292
    file.write((uint8_t *)&_prefs->flood_max_advert, sizeof(_prefs->flood_max_advert));           // 293
    file.write((uint8_t *)&_prefs->ui_led_enabled, sizeof(_prefs->ui_led_enabled));               // 294
    file.write((uint8_t *)&_prefs->ui_display_mode, sizeof(_prefs->ui_display_mode));             // 295
    file.write((uint8_t *)&_prefs->caplog_enabled, sizeof(_prefs->caplog_enabled));              // 296  (#562)
    file.write((uint8_t *)&_prefs->caplog_level, sizeof(_prefs->caplog_level));                  // 297  (#562)
    file.write((uint8_t *)_prefs->syslog_host, sizeof(_prefs->syslog_host));                     // 298  (#566)
    file.write((uint8_t *)&_prefs->syslog_port, sizeof(_prefs->syslog_port));                    // 362  (#566)
    // next: 364

    file.close();
  }
}

#define MIN_LOCAL_ADVERT_INTERVAL   60

void CommonCLI::savePrefs() {
  if (_prefs->advert_interval * 2 < MIN_LOCAL_ADVERT_INTERVAL) {
    _prefs->advert_interval = 0;  // turn it off, now that device has been manually configured
  }
  _callbacks->savePrefs();
}

uint8_t CommonCLI::buildAdvertData(uint8_t node_type, uint8_t* app_data) {
  if (_prefs->advert_loc_policy == ADVERT_LOC_NONE) {
    AdvertDataBuilder builder(node_type, _prefs->node_name);
    return builder.encodeTo(app_data);
  } else if (_prefs->advert_loc_policy == ADVERT_LOC_SHARE) {
    AdvertDataBuilder builder(node_type, _prefs->node_name, _sensors->node_lat, _sensors->node_lon);
    return builder.encodeTo(app_data);
  } else {
    AdvertDataBuilder builder(node_type, _prefs->node_name, _prefs->node_lat, _prefs->node_lon);
    return builder.encodeTo(app_data);
  }
}

void CommonCLI::handleCommand(uint32_t sender_timestamp, char* command, char* reply) {
    if (memcmp(command, "poweroff", 8) == 0 || memcmp(command, "shutdown", 8) == 0) {
      _board->powerOff();  // doesn't return
    } else if (memcmp(command, "reboot", 6) == 0) {
      _board->reboot();  // doesn't return
#ifdef OFFBAND_WDT_HANGTEST
    } else if (memcmp(command, "hangtest", 8) == 0) {
      // DEBUG-ONLY -- gated by -D OFFBAND_WDT_HANGTEST=1, so it is NEVER present
      // in a shipping image (verify with: strings firmware.bin | grep hangtest).
      //
      // Deliberately wedges the MAIN LOOP to produce the falsifiable bench test
      // both #257 and #275 require, in one shot:
      //   - #257: the hardware watchdog is fed ONLY from the main loop, so
      //     spinning here starves the feed -> NVIC reset with RESETREAS=DOG,
      //     surfaced as "Watchdog" on the next boot banner, within the
      //     startWatchdog() timeout (~30 s on rak3401).
      //   - #275: the green-LED heartbeat is loop-driven, so it MUST freeze
      //     here. A heartbeat that kept blinking through this would prove the
      //     blink is NOT a true liveness signal.
      //
      // The nop prevents the compiler from eliding a side-effect-free infinite
      // loop (C++ forward-progress rules). Does not return -- by design.
      Serial.println("hangtest: wedging main loop on purpose -- expect LED freeze then Watchdog reset (~30s)");
      Serial.flush();
      for (;;) { __asm__ __volatile__("nop"); }
    } else if (memcmp(command, "resetreason", 11) == 0) {
      // DEBUG-ONLY -- same gate. Reports the reset/shutdown reason captured at
      // boot by the early constructor (NRF52Board nrf52_early_reset_capture).
      //
      // Why this is needed at all: on these boards the reason is otherwise
      // UNREADABLE after the fact -- there is no SWD/J-Link probe on this bench
      // (so no RTT), and the boot banner prints before a USB-CDC host can
      // attach, so it goes to the void. Without this you cannot prove WHY a
      // field node rebooted.
      //
      // The captured values live in RAM for the lifetime of THIS boot, so
      // querying after a watchdog reset reports that reset -- which is exactly
      // how #257's "RESETREAS = Watchdog" acceptance gets evidenced.
      //
      // Superseded by #351 (persistent + permissioned + queryable over the
      // mesh). The MainBoard virtuals used here already exist fleet-wide and
      // return "Not available" on boards that don't implement them, so this is
      // portable, not nRF52-only.
      uint32_t rr = _board->getResetReason();
      uint8_t  sr = _board->getShutdownReason();
      sprintf(reply, "Last reset: %s (0x%lX) | Shutdown: %s (0x%02X)",
              _board->getResetReasonString(rr), (unsigned long)rr,
              _board->getShutdownReasonString(sr), (unsigned)sr);
#endif
    } else if (memcmp(command, "version", 7) == 0 && (command[7] == 0 || command[7] == ' ')) {
      // Offband identity (FF3 / #180). Reports both upstream MeshCore version
      // (via callback into example's MyMesh which #defines FIRMWARE_VERSION) and
      // the Offband-injected build identity from scripts/inject_offband_version.py
      // (FF2 / #179). See VERSIONING.md for the dual-version scheme rationale.
      sprintf(reply, "Upstream MeshCore: %s (%s)\nOffband fork: %s (sha %s, %s, built %s)",
              _callbacks->getFirmwareVer(), _callbacks->getBuildDate(),
              OFFBAND_VERSION, OFFBAND_GIT_SHA, OFFBAND_BRANCH, OFFBAND_BUILD_DATE);
      // #213: the in-handler keepalive moved to a file-scope constructor
      // (_xwire_keepalive_ctor near top of this file). The constructor
      // approach works on every platform; the in-handler line was
      // dead-code-eliminated on nRF52 because this handler isn't reachable
      // from main on BLE-companion builds.
    } else if (memcmp(command, "clkreboot", 9) == 0) {
      // Reset clock
      getRTCClock()->setCurrentTime(1715770351);  // 15 May 2024, 8:50pm
      _board->reboot();  // doesn't return
     } else if (memcmp(command, "advert.zerohop", 14) == 0 && (command[14] == 0 || command[14] == ' ')) {
      // send zerohop advert
      _callbacks->sendSelfAdvertisement(1500, false);  // longer delay, give CLI response time to be sent first
      strcpy(reply, "OK - zerohop advert sent");
    } else if (memcmp(command, "advert", 6) == 0) {
      // send flood advert
      _callbacks->sendSelfAdvertisement(1500, true);  // longer delay, give CLI response time to be sent first
      strcpy(reply, "OK - Advert sent");
    } else if (memcmp(command, "clock sync", 10) == 0) {
      // #607 OWNER DECISION: authenticated admin path (serial console or
      // logged-in remote CLI) -- accepted in BOTH directions. The old
      // "cannot go backwards" refusal locked in future-poisoned clocks.
      uint32_t curr = getRTCClock()->getCurrentTime();
      getRTCClock()->setCurrentTime(sender_timestamp + 1);
      offband::logClockSet("cli-clock-sync", curr, sender_timestamp + 1);
      uint32_t now = getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      sprintf(reply, "OK - clock set: %02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else if (memcmp(command, "start ota", 9) == 0) {
      if (!_board->startOTAUpdate(_prefs->node_name, reply)) {
        strcpy(reply, "Error");
      }
    } else if (memcmp(command, "clock", 5) == 0) {
      uint32_t now = getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      sprintf(reply, "%02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else if (memcmp(command, "time ", 5) == 0) {  // set time (to epoch seconds)
      uint32_t secs = _atoi(&command[5]);
      uint32_t curr = getRTCClock()->getCurrentTime();
      // #607 OWNER DECISION: authenticated owner path -- both directions.
      if (secs > 0) {
        getRTCClock()->setCurrentTime(secs);
        offband::logClockSet("cli-time", curr, secs);
        uint32_t now = getRTCClock()->getCurrentTime();
        DateTime dt = DateTime(now);
        sprintf(reply, "OK - clock set: %02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
      } else {
        strcpy(reply, "(ERR: clock cannot go backwards)");
      }
    } else if (memcmp(command, "neighbors", 9) == 0) {
      _callbacks->formatNeighborsReply(reply);
    } else if (memcmp(command, "neighbor.remove ", 16) == 0) {
      const char* hex = &command[16];
      uint8_t pubkey[PUB_KEY_SIZE];
      int hex_len = min((int)strlen(hex), PUB_KEY_SIZE*2);
      int pubkey_len = hex_len / 2;
      if (mesh::Utils::fromHex(pubkey, pubkey_len, hex)) {
        _callbacks->removeNeighbor(pubkey, pubkey_len);
        strcpy(reply, "OK");
      } else {
        strcpy(reply, "ERR: bad pubkey");
      }
    } else if (memcmp(command, "tempradio ", 10) == 0) {
      strcpy(tmp, &command[10]);
      const char *parts[5];
      int num = mesh::Utils::parseTextParts(tmp, parts, 5);
      float freq  = num > 0 ? strtof(parts[0], nullptr) : 0.0f;
      float bw    = num > 1 ? strtof(parts[1], nullptr) : 0.0f;
      uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
      uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
      int temp_timeout_mins  = num > 4 ? atoi(parts[4]) : 0;
      if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f && temp_timeout_mins > 0) {
        _callbacks->applyTempRadioParams(freq, bw, sf, cr, temp_timeout_mins);
        sprintf(reply, "OK - temp params for %d mins", temp_timeout_mins);
      } else {
        strcpy(reply, "Error, invalid params");
      }
    } else if (memcmp(command, "password ", 9) == 0) {
      // change admin password
      StrHelper::strncpy(_prefs->password, &command[9], sizeof(_prefs->password));
      savePrefs();
      sprintf(reply, "password now: ");
      StrHelper::strncpy(&reply[14], _prefs->password, 160-15);   // echo back just to let admin know for sure!!
    } else if (memcmp(command, "clear stats", 11) == 0) {
      _callbacks->clearStats();
      strcpy(reply, "(OK - stats reset)");
    } else if (memcmp(command, "get ", 4) == 0) {
      handleGetCmd(sender_timestamp, command, reply);
    } else if (memcmp(command, "set ", 4) == 0) {
      handleSetCmd(sender_timestamp, command, reply);
    } else if (sender_timestamp == 0 && strcmp(command, "erase") == 0) {
      bool s = _callbacks->formatFileSystem();
      sprintf(reply, "File system erase: %s", s ? "OK" : "Err");
    } else if (memcmp(command, "ver", 3) == 0) {
      sprintf(reply, "%s (Build: %s)", _callbacks->getFirmwareVer(), _callbacks->getBuildDate());
    } else if (memcmp(command, "board", 5) == 0) {
      sprintf(reply, "%s", _board->getManufacturerName());
    } else if (memcmp(command, "sensor get ", 11) == 0) {
      const char* key = command + 11;
      const char* val = _sensors->getSettingByKey(key);
      if (val != NULL) {
        sprintf(reply, "> %s", val);
      } else {
        strcpy(reply, "null");
      }
    } else if (memcmp(command, "sensor set ", 11) == 0) {
      strcpy(tmp, &command[11]);
      const char *parts[2];
      int num = mesh::Utils::parseTextParts(tmp, parts, 2, ' ');
      const char *key = (num > 0) ? parts[0] : "";
      const char *value = (num > 1) ? parts[1] : "null";
      if (_sensors->setSettingValue(key, value)) {
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "can't find custom var");
      }
    } else if (memcmp(command, "sensor list", 11) == 0) {
      char* dp = reply;
      int start = 0;
      int end = _sensors->getNumSettings();
      if (strlen(command) > 11) {
        start = _atoi(command+12);
      }
      if (start >= end) {
        strcpy(reply, "no custom var");
      } else {
        sprintf(dp, "%d vars\n", end);
        dp = strchr(dp, 0);
        int i;
        for (i = start; i < end && (dp-reply < 134); i++) {
          sprintf(dp, "%s=%s\n",
            _sensors->getSettingName(i),
            _sensors->getSettingValue(i));
          dp = strchr(dp, 0);
        }
        if (i < end) {
          sprintf(dp, "... next:%d", i);
        } else {
          *(dp-1) = 0; // remove last CR
        }
      }
    } else if (memcmp(command, "region", 6) == 0) {
      handleRegionCmd(command, reply);
#if ENV_INCLUDE_GPS == 1
    } else if (memcmp(command, "gps on", 6) == 0) {
      if (_sensors->setSettingValue("gps", "1")) {
        _prefs->gps_enabled = 1;
        savePrefs();
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "gps toggle not found");
      }
    } else if (memcmp(command, "gps off", 7) == 0) {
      if (_sensors->setSettingValue("gps", "0")) {
        _prefs->gps_enabled = 0;
        savePrefs();
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "gps toggle not found");
      }
    } else if (memcmp(command, "gps sync", 8) == 0) {
      LocationProvider * l = _sensors->getLocationProvider();
      if (l != NULL) {
        l->syncTime();
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "gps provider not found");
      }
    } else if (memcmp(command, "gps setloc", 10) == 0) {
      _prefs->node_lat = _sensors->node_lat;
      _prefs->node_lon = _sensors->node_lon;
      savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command, "gps advert", 10) == 0) {
      if (strlen(command) == 10) {
        switch (_prefs->advert_loc_policy) {
          case ADVERT_LOC_NONE:
            strcpy(reply, "> none");
            break;
          case ADVERT_LOC_PREFS:
            strcpy(reply, "> prefs");
            break;
          case ADVERT_LOC_SHARE:
            strcpy(reply, "> share");
            break;
          default:
            strcpy(reply, "error");
        }
      } else if (memcmp(command+11, "none", 4) == 0) {
        _prefs->advert_loc_policy = ADVERT_LOC_NONE;
        savePrefs();
        strcpy(reply, "ok");
      } else if (memcmp(command+11, "share", 5) == 0) {
        _prefs->advert_loc_policy = ADVERT_LOC_SHARE;
        savePrefs();
        strcpy(reply, "ok");
      } else if (memcmp(command+11, "prefs", 5) == 0) {
        _prefs->advert_loc_policy = ADVERT_LOC_PREFS;
        savePrefs();
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "error");
      }
    } else if (memcmp(command, "gps", 3) == 0) {
      LocationProvider * l = _sensors->getLocationProvider();
      if (l != NULL) {
        bool enabled = l->isEnabled(); // is EN pin on ?
        bool fix = l->isValid();       // has fix ?
        int sats = l->satellitesCount();
        bool active = !strcmp(_sensors->getSettingByKey("gps"), "1");
        if (enabled) {
          // #152: GPS time + clock-sync state (visible on any GPS path), appended to
          // the status line and formatted like `clock` so both compare at a glance.
          long gps_epoch = l->getTimestamp();
          bool synced = _sensors->getGpsClockSyncTime() != 0;
          char gbuf[48];
          if (gps_epoch >= (long)GPS_CLOCK_SANE_MIN) {
            DateTime gdt = DateTime((uint32_t)gps_epoch);
            snprintf(gbuf, sizeof(gbuf), "gps %02d:%02d - %d/%d/%d UTC, %s",
              gdt.hour(), gdt.minute(), gdt.day(), gdt.month(), gdt.year(),
              synced ? "synced" : "unsynced");
          } else {
            snprintf(gbuf, sizeof(gbuf), "gps --, %s", synced ? "synced" : "unsynced");
          }
          snprintf(reply, 160, "on, %s, %s, %d sats, %s",
            active?"active":"deactivated",
            fix?"fix":"no fix",
            sats, gbuf);
        } else {
          strcpy(reply, "off");
        }
      } else {
        strcpy(reply, "Can't find GPS");
      }
#endif
    } else if (memcmp(command, "powersaving on", 14) == 0) {
#if defined(NRF52_PLATFORM)
      _prefs->powersaving_enabled = 1;
      savePrefs();
      strcpy(reply, "on - Immediate effect");
#elif defined(ESP32) && !defined(WITH_BRIDGE)
      _prefs->powersaving_enabled = 1;
      savePrefs();
      strcpy(reply, "on - After 2 minutes");
#elif defined(WITH_BRIDGE)
      strcpy(reply, "Bridge not supported");
#else
      strcpy(reply, "Board not supported");
#endif
    } else if (memcmp(command, "powersaving off", 15) == 0) {
      _prefs->powersaving_enabled = 0;
      savePrefs();
      strcpy(reply, "off");
    } else if (memcmp(command, "powersaving", 11) == 0) {
      if (_prefs->powersaving_enabled) {
        strcpy(reply, "on");
      } else {
        strcpy(reply, "off");
      }
    } else if (memcmp(command, "fem on", 6) == 0) {
      _prefs->radio_fem_rxgain = 1;
      _board->setLoRaFemLnaEnabled(true);
      savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command, "fem off", 7) == 0) {
      _prefs->radio_fem_rxgain = 0;
      _board->setLoRaFemLnaEnabled(false);
      savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command, "fem", 3) == 0) {
      sprintf(reply, "fem lna: %s, controllable: %s, hw_state: %s",
              _prefs->radio_fem_rxgain ? "on" : "off",
              _board->canControlLoRaFemLna() ? "yes" : "no",
              _board->isLoRaFemLnaEnabled() ? "on" : "off");
    } else if (memcmp(command, "led on", 6) == 0) {
      _prefs->ui_led_enabled = 1;
      _board->setLedEnabled(true);
      savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command, "led off", 7) == 0) {
      _prefs->ui_led_enabled = 0;
      _board->setLedEnabled(false);
      savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command, "led", 3) == 0) {
      sprintf(reply, "led: %s, controllable: %s",
              _prefs->ui_led_enabled ? "on" : "off",
              _board->canControlLed() ? "yes" : "no");
    } else if (memcmp(command, "display always on", 17) == 0) {
#ifdef DISPLAY_CLASS
      _prefs->ui_display_mode = DISPLAY_MODE_ALWAYS_ON;
      savePrefs();
      strcpy(reply, "display: always on (screen stays lit)");
#else
      strcpy(reply, "display: unsupported (no display on this build)");
#endif
    } else if (memcmp(command, "display always off", 18) == 0) {
#ifdef DISPLAY_CLASS
      _prefs->ui_display_mode = DISPLAY_MODE_ALWAYS_OFF;
      savePrefs();
      strcpy(reply, "display: always off (screen dark)");
#else
      strcpy(reply, "display: unsupported (no display on this build)");
#endif
    } else if (memcmp(command, "display auto", 12) == 0) {
#ifdef DISPLAY_CLASS
      _prefs->ui_display_mode = DISPLAY_MODE_AUTO;
      savePrefs();
      strcpy(reply, "display: auto (on, blanks after timeout)");
#else
      strcpy(reply, "display: unsupported (no display on this build)");
#endif
    } else if (memcmp(command, "display", 7) == 0) {
#ifdef DISPLAY_CLASS
      const char* m = _prefs->ui_display_mode == DISPLAY_MODE_ALWAYS_ON  ? "always on"
                    : _prefs->ui_display_mode == DISPLAY_MODE_ALWAYS_OFF ? "always off"
                    : "auto";
      sprintf(reply, "display: %s", m);
#else
      strcpy(reply, "display: unsupported (no display on this build)");
#endif
#ifdef ENABLE_WIFI_TELEMETRY
    } else if (memcmp(command, "telemetry off", 13) == 0) {
      wifi_telemetry_set_disabled(1);
      strcpy(reply, "ok");
    } else if (memcmp(command, "telemetry on", 12) == 0) {
      wifi_telemetry_set_disabled(0);
      strcpy(reply, "ok");
    } else if (memcmp(command, "telemetry now", 13) == 0) {
      wifi_telemetry_force_now();
      strcpy(reply, "ok (scheduled for next loop pass)");
    } else if (memcmp(command, "telemetry", 9) == 0) {
      sprintf(reply, "telemetry: %s", wifi_telemetry_is_disabled() ? "off" : "on");
    } else if (memcmp(command, "wifi reset", 10) == 0) {
      wifi_telemetry_reset_state();
      strcpy(reply, "ok (will reconnect on next publish)");
    } else if (memcmp(command, "wifi off", 8) == 0) {
      // D3 / issue #57: revert to BURST mode immediately
      wifi_telemetry_set_persistent(0);
      strcpy(reply, "ok - burst mode");
    } else if (memcmp(command, "wifi on", 7) == 0) {
      // D3 / issue #57: enter PERSISTENT_STA mode.
      // Optional minutes argument: "wifi on" -> 15 min default, "wifi on N" -> N min.
      int minutes = 15;  // default
      if (command[7] == ' ' && command[8] != 0) {
        int parsed = atoi(&command[8]);
        if (parsed >= 1 && parsed <= 60) {
          minutes = parsed;
        } else if (parsed > 60) {
          minutes = 60;  // hard cap at 60 min (matches WIFI_PERSISTENT_MAX_MS)
        }
        // parsed < 1 or non-numeric leaves minutes at default 15
      }
      wifi_telemetry_set_persistent((uint32_t)minutes * 60000UL);
      sprintf(reply, "ok - persistent for %d min", minutes);
    } else if (memcmp(command, "wifi", 4) == 0) {
      wifi_telemetry_get_status(reply, 160);
#ifdef CMD_TRANSPORT_HTTP
    // LoRa#216: HTTP cmd-poll CLI commands.
    } else if (memcmp(command, "cmd_poll now", 12) == 0) {
      wifi_telemetry_cmd_poll_now();
      strcpy(reply, "ok (cmd-poll scheduled for next loop pass)");
    } else if (memcmp(command, "cmd_poll interval", 17) == 0) {
      // "cmd_poll interval N" -> N seconds. Out-of-range gets clamped by setter.
      uint32_t seconds = 0;
      if (command[17] == ' ' && command[18] != 0) {
        seconds = (uint32_t)atoi(command + 18);
      }
      if (seconds == 0) {
        // Bare "cmd_poll interval" returns current value, no change
        uint32_t cur_ms = wifi_telemetry_get_cmd_poll_burst_interval();
        snprintf(reply, 160, "cmd_poll burst interval = %lus",
                 (unsigned long)(cur_ms / 1000UL));
      } else {
        uint32_t set_ms = wifi_telemetry_set_cmd_poll_burst_interval(seconds);
        snprintf(reply, 160, "ok - cmd_poll burst interval = %lus (requested %lus)",
                 (unsigned long)(set_ms / 1000UL), (unsigned long)seconds);
      }
#endif
    // D5 / issue #59: STA-mode OTA controls. Order: "ota end" and "ota status"
    // must match before bare "ota" since memcmp(3) would otherwise swallow them.
    } else if (memcmp(command, "ota end", 7) == 0) {
      _board->stopOTAUpdate();
      strcpy(reply, "ok - OTA stopped");
    } else if (memcmp(command, "ota status", 10) == 0) {
      _board->getOTAStatus(reply, 160);
    } else if (memcmp(command, "ota", 3) == 0) {
      // Decision 1A: STA-mode OTA requires explicit `wifi on N` first so we don't
      // race against the BURST publisher tearing the radio down mid-upload.
      if (!wifi_telemetry_is_persistent()) {
        strcpy(reply, "ERR: run 'wifi on N' first");
      } else if (!_board->startOTAUpdateOverSTA(_prefs->node_name, _prefs->password, reply)) {
        // reply already populated by the failure path inside startOTAUpdateOverSTA
        // (e.g. "ERR: STA WiFi not connected", "ERR: OTA already running at ...")
      }
#endif
    // Epic E (#64) / E4 #68: safety/diagnostic log retrieval.
    // Order: "safety log" (10) and "safety state" (12) match before bare "safety" (6).
    // "safety state" must come before "safety log" in source order ONLY if its
    // shorter prefix "safety s" could match "safety log" - they cannot (different
    // 8th char), so order within these two is freely chosen. Bare "safety"
    // defaults to "safety state" per the issue spec.
    } else if (memcmp(command, "safety log tail", 15) == 0) {
      // Epic E / E10 #74: newest-first dump for diagnosing recent boots when
      // the oldest-first log truncates in the 160-byte buffer. Optional integer
      // arg sets max event count (default 5, cap 9 to stay within budget).
      uint8_t max_events = 5;
      if (command[15] == ' ' && command[16] != 0) {
        int parsed = atoi(&command[16]);
        if (parsed >= 1 && parsed <= 9) {
          max_events = (uint8_t)parsed;
        }
      }
      _board->getSafetyLogTail(reply, 160, max_events);
    } else if (memcmp(command, "safety log", 10) == 0) {
      _board->getSafetyLog(reply, 160);
    } else if (memcmp(command, "safety state", 12) == 0) {
      _board->getSafetyState(reply, 160);
    } else if (memcmp(command, "safety partitions", 17) == 0) {
      // Epic E / E8 #72: dump all-partition state for OTA diagnostic visibility.
      _board->getPartitionsInfo(reply, 160);
    } else if (memcmp(command, "safety", 6) == 0) {
      // Bare "safety" -> state snapshot (most useful single-line summary).
      _board->getSafetyState(reply, 160);
    } else if (memcmp(command, "log start", 9) == 0) {
      _callbacks->setLoggingOn(true);
      strcpy(reply, "   logging on");
    } else if (memcmp(command, "log stop", 8) == 0) {
      _callbacks->setLoggingOn(false);
      strcpy(reply, "   logging off");
    } else if (memcmp(command, "log erase", 9) == 0) {
      _callbacks->eraseLogFile();
      strcpy(reply, "   log erased");
    } else if (sender_timestamp == 0 && memcmp(command, "log", 3) == 0) {
      _callbacks->dumpLogFile();
      strcpy(reply, "   EOF");
    // #395: serial-capture (caplog). Control + status are un-gated (bounded
    // replies, reachable over the companion/authenticated channel — same class
    // as the existing `set`/`ota`/`wifi` verbs, and required for the app flow
    // where an operator enables capture over the companion link). The full
    // `caplog dump` is local-console only (`sender_timestamp == 0`); the framed
    // remote download is #396. Each verb carries a terminator check
    // (`command[N] == 0 || ' '`) so a longer word (e.g. "caplog startle") is not
    // swallowed and falls through to "Unknown command".
    } else if (memcmp(command, "caplog start", 12) == 0 && (command[12] == 0 || command[12] == ' ')) {
      uint8_t lvl = MLOG_DEBUG;
      bool ok = true;
      if (command[12] == ' ' && command[13] != 0) {
        ok = meshLogLevelFromName(&command[13], &lvl);
      }
      if (!ok) {
        strcpy(reply, "ERR: level = boot|error|debug|packet");
      } else {
        meshLogSetLevel(lvl);
        meshLogSetEnabled(true);
        _prefs->caplog_level = lvl;        // #562: persist so capture survives reboot
        _prefs->caplog_enabled = 1;
        savePrefs();
        snprintf(reply, 160, "caplog on (level %s)", meshLogLevelName(lvl));
      }
    } else if (memcmp(command, "caplog forward", 14) == 0 && (command[14] == 0 || command[14] == ' ')) {
      // #561: live syslog forward -- stream captured lines off-device during a
      // bounded window (survives a reboot the RAM ring cannot). Telemetry-only.
      // #566: gated on ENABLE_WIFI_TELEMETRY only -- the sink host/port is a
      // runtime pref (set syslog.host/port), no longer a build-time flag.
#if defined(ENABLE_WIFI_TELEMETRY)
      const char* arg = (command[14] == ' ') ? &command[15] : "";
      if (memcmp(arg, "off", 3) == 0) {
        wifi_telemetry_caplog_forward(0);
        meshLogSetEnabled(false);
        _prefs->caplog_enabled = 0;
        savePrefs();
        strcpy(reply, "caplog forward off");
      } else {
        uint32_t sec = (*arg) ? (uint32_t)_atoi(arg) : 300;   // bare -> 5 min default
        if (sec < 30) sec = 30;                                // floor: focused test
        meshLogSetEnabled(true);
        _prefs->caplog_enabled = 1;
        savePrefs();
        wifi_telemetry_caplog_forward(sec);
        snprintf(reply, 160, "caplog forward on %us (streaming to syslog)", (unsigned)sec);
      }
#else
      strcpy(reply, "caplog forward: not available on this build");
#endif
    } else if (memcmp(command, "caplog stop", 11) == 0 && (command[11] == 0 || command[11] == ' ')) {
      meshLogSetEnabled(false);
      _prefs->caplog_enabled = 0;          // #562: persist off across reboot
      savePrefs();
      strcpy(reply, "caplog off");
    } else if (memcmp(command, "caplog erase", 12) == 0 && (command[12] == 0 || command[12] == ' ')) {
      meshLogClear();
      strcpy(reply, "caplog erased");
    } else if (sender_timestamp == 0 && memcmp(command, "caplog dump", 11) == 0 && (command[11] == 0 || command[11] == ' ')) {
      meshLogDumpSerial();
      strcpy(reply, "   EOF");
    } else if ((memcmp(command, "caplog status", 13) == 0 && (command[13] == 0 || command[13] == ' '))
               || (memcmp(command, "caplog", 6) == 0 && command[6] == 0)) {
      snprintf(reply, 160, "caplog: %s level=%s used=%u/%u",
               meshLogIsEnabled() ? "on" : "off",
               meshLogLevelName(meshLogGetLevel()),
               (unsigned)meshLogBytesUsed(), (unsigned)meshLogCapacity());
    } else if (sender_timestamp == 0 && memcmp(command, "stats-packets", 13) == 0 && (command[13] == 0 || command[13] == ' ')) {
      _callbacks->formatPacketStatsReply(reply);
    } else if (sender_timestamp == 0 && memcmp(command, "stats-radio", 11) == 0 && (command[11] == 0 || command[11] == ' ')) {
      _callbacks->formatRadioStatsReply(reply);
    } else if (sender_timestamp == 0 && memcmp(command, "stats-core", 10) == 0 && (command[10] == 0 || command[10] == ' ')) {
      _callbacks->formatStatsReply(reply);
#if defined(OFFBAND_OBSERVER) || defined(OFFBAND_MQTT_POOL)
    // #554: broker-config CLI fall-through for BARE "mqtt ..." verbs
    // (status/enable/disable/view/clear). "get mqtt.*" / "set mqtt.*" are caught
    // earlier by the "get "/"set " intercepts (-> handleGetCmd / handleSetCmd),
    // which carry their own dispatchObserverCli fall-through.
    //
    // Previously the only dispatchObserverCli call site was stranded in
    // handleRegionCmd (reachable only via a "region"-prefixed command), so the
    // broker CLI was unreachable on the repeater (#554) -- both the serial admin
    // console and the client-over-RemoteCommand path route through here.
    //
    // reply is the CLI's fixed 160-byte reply contract (e.g. serial reply[160]);
    // handleCommand carries no reply_size, so 160 is the only size safe on every
    // caller. dispatchObserverCli is fully snprintf-bounded to reply_size, so this
    // never overruns; long "mqtt status" dumps truncate at 160 -- "mqtt view <N>"
    // gives full per-broker detail within the frame.
    } else if (offband::dispatchObserverCli(command, reply, /*reply_size=*/160,
                                             offband::wifiObserverPool())) {
      // handled by the broker-config CLI
#endif
    } else {
      strcpy(reply, "Unknown command");
    }
}

void CommonCLI::handleSetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  const char* config = &command[4];
  if (memcmp(config, "dutycycle ", 10) == 0) {
    float dc = atof(&config[10]);
    if (dc < 1 || dc > 100) {
      strcpy(reply, "ERROR: dutycycle must be 1-100");
    } else {
      _prefs->airtime_factor = (100.0f / dc) - 1.0f;
      savePrefs();
      float actual = 100.0f / (_prefs->airtime_factor + 1.0f);
      int a_int = (int)actual;
      int a_frac = (int)((actual - a_int) * 10.0f + 0.5f);
      sprintf(reply, "OK - %d.%d%%", a_int, a_frac);
    }
  } else if (memcmp(config, "af ", 3) == 0) {
    _prefs->airtime_factor = atof(&config[3]);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "int.thresh ", 11) == 0) {
    _prefs->interference_threshold = atoi(&config[11]);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "agc.reset.interval ", 19) == 0) {
    _prefs->agc_reset_interval = atoi(&config[19]) / 4;
    savePrefs();
    sprintf(reply, "OK - interval rounded to %d", ((uint32_t) _prefs->agc_reset_interval) * 4);
  } else if (memcmp(config, "multi.acks ", 11) == 0) {
    _prefs->multi_acks = atoi(&config[11]);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "allow.read.only ", 16) == 0) {
    _prefs->allow_read_only = memcmp(&config[16], "on", 2) == 0;
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "flood.advert.interval ", 22) == 0) {
    int hours = _atoi(&config[22]);
    if ((hours > 0 && hours < 3) || (hours > 168)) {
      strcpy(reply, "Error: interval range is 3-168 hours");
    } else {
      _prefs->flood_advert_interval = (uint8_t)(hours);
      _callbacks->updateFloodAdvertTimer();
      savePrefs();
      strcpy(reply, "OK");
    }
  } else if (memcmp(config, "advert.interval ", 16) == 0) {
    int mins = _atoi(&config[16]);
    if ((mins > 0 && mins < MIN_LOCAL_ADVERT_INTERVAL) || (mins > 240)) {
      sprintf(reply, "Error: interval range is %d-240 minutes", MIN_LOCAL_ADVERT_INTERVAL);
    } else {
      _prefs->advert_interval = (uint8_t)(mins / 2);
      _callbacks->updateAdvertTimer();
      savePrefs();
      strcpy(reply, "OK");
    }
  } else if (memcmp(config, "guest.password ", 15) == 0) {
    StrHelper::strncpy(_prefs->guest_password, &config[15], sizeof(_prefs->guest_password));
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "prv.key ", 8) == 0) {
    uint8_t prv_key[PRV_KEY_SIZE];
    bool success = mesh::Utils::fromHex(prv_key, PRV_KEY_SIZE, &config[8]);
    // only allow rekey if key is valid
    if (success && mesh::LocalIdentity::validatePrivateKey(prv_key)) {
      mesh::LocalIdentity new_id;
      new_id.readFrom(prv_key, PRV_KEY_SIZE);
      _callbacks->saveIdentity(new_id);
      strcpy(reply, "OK, reboot to apply! New pubkey: ");
      mesh::Utils::toHex(&reply[33], new_id.pub_key, PUB_KEY_SIZE);
    } else {
      strcpy(reply, "Error, bad key");
    }
  } else if (memcmp(config, "name ", 5) == 0) {
    if (isValidName(&config[5])) {
      StrHelper::strncpy(_prefs->node_name, &config[5], sizeof(_prefs->node_name));
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, bad chars");
    }
  } else if (memcmp(config, "repeat ", 7) == 0) {
    _prefs->disable_fwd = memcmp(&config[7], "off", 3) == 0;
    savePrefs();
    strcpy(reply, _prefs->disable_fwd ? "OK - repeat is now OFF" : "OK - repeat is now ON");
#if defined(USE_SX1262) || defined(USE_SX1268) || defined(USE_LR1110)
  } else if (memcmp(config, "radio.rxgain ", 13) == 0) {
    _prefs->rx_boosted_gain = memcmp(&config[13], "on", 2) == 0;
    strcpy(reply, "OK");
    savePrefs();
    _callbacks->setRxBoostedGain(_prefs->rx_boosted_gain);
#endif
  } else if (memcmp(config, "radio ", 6) == 0) {
    strcpy(tmp, &config[6]);
    // #299: parse one MORE part than we consume. parseTextParts silently discards
    // anything past max_num, so 'set radio 910.525,62.5,7,5,foobar' used to apply
    // the first four and drop the junk without a word -- the same misleading
    // silent-accept as the 'get' side. Asking for 5 lets us see the extra and reject.
    const char *parts[5];
    int num = mesh::Utils::parseTextParts(tmp, parts, 5);
    float freq  = num > 0 ? strtof(parts[0], nullptr) : 0.0f;
    float bw    = num > 1 ? strtof(parts[1], nullptr) : 0.0f;
    uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
    uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
    if (num > 4) {
      strcpy(reply, "Error, too many params (expected freq,bw,sf,cr)");
    } else if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
      _prefs->sf = sf;
      _prefs->cr = cr;
      _prefs->freq = freq;
      _prefs->bw = bw;
      _callbacks->savePrefs();
      strcpy(reply, "OK - reboot to apply");
    } else {
      strcpy(reply, "Error, invalid radio params");
    }
  } else if (memcmp(config, "lat ", 4) == 0) {
    _prefs->node_lat = atof(&config[4]);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "lon ", 4) == 0) {
    _prefs->node_lon = atof(&config[4]);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "rxdelay ", 8) == 0) {
    float db = atof(&config[8]);
    if (db >= 0 && db <= 20.0f) {
      _prefs->rx_delay_base = db;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0-20");
    }
  } else if (memcmp(config, "txdelay ", 8) == 0) {
    float f = atof(&config[8]);
    if (f >= 0 && f <= 2.0f) {
      _prefs->tx_delay_factor = f;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0-2");
    }
  } else if (memcmp(config, "flood.max.unscoped ", 19) == 0) {
    uint8_t m = atoi(&config[19]);
    if (m <= 64) {
      _prefs->flood_max_unscoped = m;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, max 64");
    } 
  } else if (memcmp(config, "flood.max.advert ", 17) == 0) {
    uint8_t m = atoi(&config[17]);
    if (m <= 64) {
      _prefs->flood_max_advert = m;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, max 64");
    }
  } else if (memcmp(config, "syslog.host ", 12) == 0) {
    // #566: runtime caplog syslog-forward sink host (empty = forward off).
    strncpy(_prefs->syslog_host, &config[12], sizeof(_prefs->syslog_host) - 1);
    _prefs->syslog_host[sizeof(_prefs->syslog_host) - 1] = '\0';
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "syslog.port ", 12) == 0) {
    int p = atoi(&config[12]);
    if (p > 0 && p <= 65535) {
      _prefs->syslog_port = (uint16_t)p;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, port 1-65535");
    }
  } else if (memcmp(config, "flood.max ", 10) == 0) {
    uint8_t m = atoi(&config[10]);
    if (m <= 64) {
      _prefs->flood_max = m;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, max 64");
    }
  } else if (memcmp(config, "direct.txdelay ", 15) == 0) {
    float f = atof(&config[15]);
    if (f >= 0 && f <= 2.0f) {
      _prefs->direct_tx_delay_factor = f;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0-2");
    }
  } else if (memcmp(config, "owner.info ", 11) == 0) {
    config += 11;
    char *dp = _prefs->owner_info;
    while (*config && dp - _prefs->owner_info < sizeof(_prefs->owner_info)-1) {
      *dp++ = (*config == '|') ? '\n' : *config;    // translate '|' to newline chars
      config++;
    }
    *dp = 0;
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "path.hash.mode ", 15) == 0) {
    config += 15;
    uint8_t mode = atoi(config);
    if (mode < 3) {
      _prefs->path_hash_mode = mode;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0,1, or 2");
    }
  } else if (memcmp(config, "loop.detect ", 12) == 0) {
    config += 12;
    uint8_t mode;
    if (memcmp(config, "off", 3) == 0) {
      mode = LOOP_DETECT_OFF;
    } else if (memcmp(config, "minimal", 7) == 0) {
      mode = LOOP_DETECT_MINIMAL;
    } else if (memcmp(config, "moderate", 8) == 0) {
      mode = LOOP_DETECT_MODERATE;
    } else if (memcmp(config, "strict", 6) == 0) {
      mode = LOOP_DETECT_STRICT;
    } else {
      mode = 0xFF;
      strcpy(reply, "Error, must be: off, minimal, moderate, or strict");
    }
    if (mode != 0xFF) {
      _prefs->loop_detect = mode;
      savePrefs();
      strcpy(reply, "OK");
    }
  } else if (memcmp(config, "tx ", 3) == 0) {
    _prefs->tx_power_dbm = atoi(&config[3]);
    savePrefs();
    _callbacks->setTxPower(_prefs->tx_power_dbm);
    strcpy(reply, "OK");
  } else if (sender_timestamp == 0 && memcmp(config, "freq ", 5) == 0) {
    _prefs->freq = atof(&config[5]);
    savePrefs();
    strcpy(reply, "OK - reboot to apply");
#ifdef WITH_BRIDGE
  } else if (memcmp(config, "bridge.enabled ", 15) == 0) {
    _prefs->bridge_enabled = memcmp(&config[15], "on", 2) == 0;
    _callbacks->setBridgeState(_prefs->bridge_enabled);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "bridge.delay ", 13) == 0) {
    int delay = _atoi(&config[13]);
    if (delay >= 0 && delay <= 10000) {
      _prefs->bridge_delay = (uint16_t)delay;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error: delay must be between 0-10000 ms");
    }
  } else if (memcmp(config, "bridge.source ", 14) == 0) {
    _prefs->bridge_pkt_src = memcmp(&config[14], "rx", 2) == 0;
    savePrefs();
    strcpy(reply, "OK");
#endif
#ifdef WITH_RS232_BRIDGE
  } else if (memcmp(config, "bridge.baud ", 12) == 0) {
    uint32_t baud = atoi(&config[12]);
    if (baud >= 9600 && baud <= BRIDGE_MAX_BAUD) {
      _prefs->bridge_baud = (uint32_t)baud;
      _callbacks->restartBridge();
      savePrefs();
      strcpy(reply, "OK");
    } else {
      sprintf(reply, "Error: baud rate must be between 9600-%d",BRIDGE_MAX_BAUD);
    }
#endif
#ifdef WITH_ESPNOW_BRIDGE
  } else if (memcmp(config, "bridge.channel ", 15) == 0) {
    int ch = atoi(&config[15]);
    if (ch > 0 && ch < 15) {
      _prefs->bridge_channel = (uint8_t)ch;
      _callbacks->restartBridge();
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error: channel must be between 1-14");
    }
  } else if (memcmp(config, "bridge.secret ", 14) == 0) {
    StrHelper::strncpy(_prefs->bridge_secret, &config[14], sizeof(_prefs->bridge_secret));
    _callbacks->restartBridge();
    savePrefs();
    strcpy(reply, "OK");
#endif
  } else if (memcmp(config, "adc.multiplier ", 15) == 0) {
    _prefs->adc_multiplier = atof(&config[15]);
    if (_board->setAdcMultiplier(_prefs->adc_multiplier)) {
      savePrefs();
      if (_prefs->adc_multiplier == 0.0f) {
        strcpy(reply, "OK - using default board multiplier");
      } else {
        sprintf(reply, "OK - multiplier set to %.3f", _prefs->adc_multiplier);
      }
    } else {
      _prefs->adc_multiplier = 0.0f;
      strcpy(reply, "Error: unsupported by this board");
    };
#if defined(OFFBAND_OBSERVER) || defined(OFFBAND_MQTT_POOL)
  // #554: "set mqtt.*" (mqtt.broker.<N>.*, mqtt.iata, mqtt.status_interval) ->
  // broker CLI. dispatchObserverCli strips the "set " head itself. 160 = the
  // fixed CLI reply contract (see handleCommand note); set-echo replies are short.
  } else if (offband::dispatchObserverCli(command, reply, /*reply_size=*/160,
                                          offband::wifiObserverPool())) {
    // handled by the broker-config CLI
#endif
  } else {
    strcpy(reply, "unknown config: ");
    StrHelper::strncpy(&reply[16], config, 160-17);
  }
}

// #299: exact-token match for CLI 'get' keys. The bare memcmp prefix compares that
// gated this chain let any garbage suffix through -- 'get radio foobar' matched the
// "radio" prefix and silently returned the radio settings instead of the standard
// unknown-command error (it also let a longer key be shadowed by a shorter sibling).
//
// The key must consume the WHOLE remaining token, not merely be followed by a space:
// EVERY 'get' key in this chain is valueless (each one just prints a pref), so a
// trailing term is always a user error -- typically a command copied from another
// fork or version, which is exactly the upstream repro (a user typed the
// non-existent 'get radio.fem.rxgain', got the radio settings back, and concluded
// the knob was broken). Accepting "key + space + anything" would leave that repro
// unfixed. Trailing whitespace alone is tolerated.
//
// strncmp (not memcmp) so a config shorter than the key cannot read past its NUL.
static bool isKey(const char* config, const char* key) {
  size_t n = strlen(key);
  if (strncmp(config, key, n) != 0) return false;
  const char* p = &config[n];
  while (*p == ' ') p++;
  return *p == 0;
}

void CommonCLI::handleGetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  const char* config = &command[4];
  if (isKey(config, "dutycycle")) {
    float dc = 100.0f / (_prefs->airtime_factor + 1.0f);
    int dc_int = (int)dc;
    int dc_frac = (int)((dc - dc_int) * 10.0f + 0.5f);
    sprintf(reply, "> %d.%d%%", dc_int, dc_frac);
  } else if (isKey(config, "af")) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->airtime_factor));
  } else if (isKey(config, "int.thresh")) {
    sprintf(reply, "> %d", (uint32_t) _prefs->interference_threshold);
  } else if (isKey(config, "agc.reset.interval")) {
    sprintf(reply, "> %d", ((uint32_t) _prefs->agc_reset_interval) * 4);
  } else if (isKey(config, "multi.acks")) {
    sprintf(reply, "> %d", (uint32_t) _prefs->multi_acks);
  } else if (isKey(config, "allow.read.only")) {
    sprintf(reply, "> %s", _prefs->allow_read_only ? "on" : "off");
  } else if (isKey(config, "flood.advert.interval")) {
    sprintf(reply, "> %d", ((uint32_t) _prefs->flood_advert_interval));
  } else if (isKey(config, "advert.interval")) {
    sprintf(reply, "> %d", ((uint32_t) _prefs->advert_interval) * 2);
  } else if (isKey(config, "guest.password")) {
    sprintf(reply, "> %s", _prefs->guest_password);
  } else if (sender_timestamp == 0 && isKey(config, "prv.key")) {  // from serial command line only
    uint8_t prv_key[PRV_KEY_SIZE];
    int len = _callbacks->getSelfId().writeTo(prv_key, PRV_KEY_SIZE);
    mesh::Utils::toHex(tmp, prv_key, len);
    sprintf(reply, "> %s", tmp);
  } else if (isKey(config, "name")) {
    sprintf(reply, "> %s", _prefs->node_name);
  } else if (isKey(config, "repeat")) {
    sprintf(reply, "> %s", _prefs->disable_fwd ? "off" : "on");
  } else if (isKey(config, "lat")) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->node_lat));
  } else if (isKey(config, "lon")) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->node_lon));
#if defined(USE_SX1262) || defined(USE_SX1268) || defined(USE_LR1110)
  } else if (isKey(config, "radio.rxgain")) {
    sprintf(reply, "> %s", _prefs->rx_boosted_gain ? "on" : "off");
#endif
  } else if (isKey(config, "radio")) {
    char freq[16], bw[16];
    strcpy(freq, StrHelper::ftoa3(_prefs->freq));
    strcpy(bw, StrHelper::ftoa3(_prefs->bw));
    sprintf(reply, "> %s,%s,%d,%d", freq, bw, (uint32_t)_prefs->sf, (uint32_t)_prefs->cr);
  } else if (isKey(config, "rxdelay")) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->rx_delay_base));
  } else if (isKey(config, "txdelay")) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->tx_delay_factor));
  } else if (isKey(config, "flood.max.advert")) {
    sprintf(reply, "> %d", (uint32_t)_prefs->flood_max_advert);
  } else if (isKey(config, "flood.max.unscoped")) {
    sprintf(reply, "> %d", (uint32_t)_prefs->flood_max_unscoped);
  } else if (isKey(config, "flood.max")) {
    sprintf(reply, "> %d", (uint32_t)_prefs->flood_max);
  } else if (isKey(config, "direct.txdelay")) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->direct_tx_delay_factor));
  } else if (isKey(config, "syslog.host")) {
    sprintf(reply, "> %s", _prefs->syslog_host);       // #566 (empty = forward off)
  } else if (isKey(config, "syslog.port")) {
    sprintf(reply, "> %d", (uint32_t)_prefs->syslog_port); // #566
  } else if (isKey(config, "owner.info")) {
    auto start = reply;
    *reply++ = '>';
    *reply++ = ' ';
    const char* sp = _prefs->owner_info;
    while (*sp && reply - start < 159) {
      *reply++ = (*sp == '\n') ? '|' : *sp;    // translate newline back to orig '|'
      sp++;
    }
    *reply = 0;  // set null terminator
  } else if (isKey(config, "path.hash.mode")) {
    sprintf(reply, "> %d", (uint32_t)_prefs->path_hash_mode);
  } else if (isKey(config, "loop.detect")) {
    if (_prefs->loop_detect == LOOP_DETECT_OFF) {
      strcpy(reply, "> off");
    } else if (_prefs->loop_detect == LOOP_DETECT_MINIMAL) {
      strcpy(reply, "> minimal");
    } else if (_prefs->loop_detect == LOOP_DETECT_MODERATE) {
      strcpy(reply, "> moderate");
    } else {
      strcpy(reply, "> strict");
    }
  } else if (isKey(config, "tx")) {
    sprintf(reply, "> %d", (int32_t) _prefs->tx_power_dbm);
  } else if (isKey(config, "freq")) {
    sprintf(reply, "> %s", StrHelper::ftoa3(_prefs->freq));
  } else if (isKey(config, "public.key")) {
    strcpy(reply, "> ");
    mesh::Utils::toHex(&reply[2], _callbacks->getSelfId().pub_key, PUB_KEY_SIZE);
  } else if (isKey(config, "role")) {
    sprintf(reply, "> %s", _callbacks->getRole());
  } else if (isKey(config, "bridge.type")) {
    sprintf(reply, "> %s",
#ifdef WITH_RS232_BRIDGE
            "rs232"
#elif WITH_ESPNOW_BRIDGE
            "espnow"
#else
            "none"
#endif
    );
#ifdef WITH_BRIDGE
  } else if (isKey(config, "bridge.enabled")) {
    sprintf(reply, "> %s", _prefs->bridge_enabled ? "on" : "off");
  } else if (isKey(config, "bridge.delay")) {
    sprintf(reply, "> %d", (uint32_t)_prefs->bridge_delay);
  } else if (isKey(config, "bridge.source")) {
    sprintf(reply, "> %s", _prefs->bridge_pkt_src ? "logRx" : "logTx");
#endif
#ifdef WITH_RS232_BRIDGE
  } else if (isKey(config, "bridge.baud")) {
    sprintf(reply, "> %d", (uint32_t)_prefs->bridge_baud);
#endif
#ifdef WITH_ESPNOW_BRIDGE
  } else if (isKey(config, "bridge.channel")) {
    sprintf(reply, "> %d", (uint32_t)_prefs->bridge_channel);
  } else if (isKey(config, "bridge.secret")) {
    sprintf(reply, "> %s", _prefs->bridge_secret);
#endif
  } else if (isKey(config, "bootloader.ver")) {
  #ifdef NRF52_PLATFORM
      char ver[32];
      if (_board->getBootloaderVersion(ver, sizeof(ver))) {
          sprintf(reply, "> %s", ver);
      } else {
          strcpy(reply, "> unknown");
      }
  #else
      strcpy(reply, "ERROR: unsupported");
  #endif
  } else if (isKey(config, "adc.multiplier")) {
    float adc_mult = _board->getAdcMultiplier();
    if (adc_mult == 0.0f) {
      strcpy(reply, "Error: unsupported by this board");
    } else {
      sprintf(reply, "> %.3f", adc_mult);
    }
  // Power management commands
  } else if (isKey(config, "pwrmgt.support")) {
#ifdef NRF52_POWER_MANAGEMENT
    strcpy(reply, "> supported");
#else
    strcpy(reply, "> unsupported");
#endif
  } else if (isKey(config, "pwrmgt.source")) {
#ifdef NRF52_POWER_MANAGEMENT
    strcpy(reply, _board->isExternalPowered() ? "> external" : "> battery");
#else
    strcpy(reply, "ERROR: Power management not supported");
#endif
  } else if (isKey(config, "pwrmgt.bootreason")) {
#ifdef NRF52_POWER_MANAGEMENT
    sprintf(reply, "> Reset: %s; Shutdown: %s",
      _board->getResetReasonString(_board->getResetReason()),
      _board->getShutdownReasonString(_board->getShutdownReason()));
#else
    strcpy(reply, "ERROR: Power management not supported");
#endif
  } else if (isKey(config, "pwrmgt.bootmv")) {
#ifdef NRF52_POWER_MANAGEMENT
    sprintf(reply, "> %u mV", _board->getBootVoltage());
#else
    strcpy(reply, "ERROR: Power management not supported");
#endif
#if defined(OFFBAND_OBSERVER) || defined(OFFBAND_MQTT_POOL)
  // #554: "get mqtt.broker.<N>.<key>" -> broker CLI (symmetric read of the
  // "set mqtt.broker.*" surface). dispatchObserverCli strips the "get " head.
  // 160 = the fixed CLI reply contract (see handleCommand note).
  } else if (offband::dispatchObserverCli(command, reply, /*reply_size=*/160,
                                          offband::wifiObserverPool())) {
    // handled by the broker-config CLI
#endif
  } else {
    sprintf(reply, "??: %s", config);
  }
}

static char* skipSpaces(char* s) {
  while (*s == ' ') s++;
  return s;
}

static void rtrimSpaces(char* s) {
  char* e = s + strlen(s);
  while (e > s && e[-1] == ' ') *--e = '\0';
}

static char* takeToken(char** cursor) {
  char* p = skipSpaces(*cursor);
  if (*p == '\0') { *cursor = p; return nullptr; }
  char* tok = p;
  while (*p && *p != ' ') p++;
  if (*p) *p++ = '\0';
  *cursor = p;
  return tok;
}

static char* splitNameJump(char* tok) {
  for (char* q = tok; *q; q++) {
    if (*q == '|' || *q == ',') {
      *q = '\0';
      char* jump = skipSpaces(q + 1);
      rtrimSpaces(jump);
      return jump;
    }
  }
  return nullptr;
}

static bool processRegionDefSegment(RegionMap* map, char* tok, RegionEntry** cursor, char* reply) {
  char* jump = splitNameJump(tok);
  char* name = skipSpaces(tok);
  if (*name == '\0') { snprintf(reply, 160, "Err - empty name"); return false; }
  if (jump && *jump == '\0') { snprintf(reply, 160, "Err - empty jump"); return false; }

  RegionEntry* r = map->putRegion(name, (*cursor)->id);
  if (r == NULL) { snprintf(reply, 160, "Err - put failed: %s", name); return false; }
  r->flags = 0;

  if (jump) {
    RegionEntry* j = map->findByNamePrefix(jump);
    if (j == NULL) { snprintf(reply, 160, "Err - unknown jump: %s", jump); return false; }
    *cursor = j;
  } else {
    *cursor = r;
  }
  return true;
}

void CommonCLI::handleRegionCmd(char* command, char* reply) {
  reply[0] = 0;

  // `region def`: must run before parseTextParts mutates the buffer
  char* cmd = skipSpaces(command);
  if (strncmp(cmd, "region def", 10) == 0 && (cmd[10] == ' ' || cmd[10] == '\0')) {
    char* payload = skipSpaces(cmd + 10);
    rtrimSpaces(payload);
    if (*payload == '\0') { snprintf(reply, 160, "Err - empty def"); return; }

    RegionEntry* cursor = &_region_map->getWildcard();
    for (char* tok; (tok = takeToken(&payload)) != nullptr; ) {
      if (!processRegionDefSegment(_region_map, tok, &cursor, reply)) return;
    }
    _region_map->exportTo(reply, 160);
    return;
  }

  const char* parts[4];
  int n = mesh::Utils::parseTextParts(command, parts, 4, ' ');
  if (n == 1) {
    _region_map->exportTo(reply, 160);
  } else if (n >= 2 && strcmp(parts[1], "load") == 0) {
    _callbacks->startRegionsLoad();
  } else if (n >= 2 && strcmp(parts[1], "save") == 0) {
    _prefs->discovery_mod_timestamp = getRTCClock()->getCurrentTime();   // this node is now 'modified' (for discovery info)
    savePrefs();
    bool success = _callbacks->saveRegions();
    strcpy(reply, success ? "OK" : "Err - save failed");
  } else if (n >= 3 && strcmp(parts[1], "allowf") == 0) {
    auto region = _region_map->findByNamePrefix(parts[2]);
    if (region) {
      region->flags &= ~REGION_DENY_FLOOD;
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Err - unknown region");
    }
  } else if (n >= 3 && strcmp(parts[1], "denyf") == 0) {
    auto region = _region_map->findByNamePrefix(parts[2]);
    if (region) {
      region->flags |= REGION_DENY_FLOOD;
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Err - unknown region");
    }
  } else if (n >= 3 && strcmp(parts[1], "get") == 0) {
    auto region = _region_map->findByNamePrefix(parts[2]);
    if (region) {
      auto parent = _region_map->findById(region->parent);
      if (parent && parent->id != 0) {
        sprintf(reply, " %s (%s) %s", region->name, parent->name, (region->flags & REGION_DENY_FLOOD) ? "" : "F");
      } else {
        sprintf(reply, " %s %s", region->name, (region->flags & REGION_DENY_FLOOD) ? "" : "F");
      }
    } else {
      strcpy(reply, "Err - unknown region");
    }
  } else if (n >= 3 && strcmp(parts[1], "home") == 0) {
    auto home = _region_map->findByNamePrefix(parts[2]);
    if (home) {
      _region_map->setHomeRegion(home);
      sprintf(reply, " home is now %s", home->name);
    } else {
      strcpy(reply, "Err - unknown region");
    }
  } else if (n == 2 && strcmp(parts[1], "home") == 0) {
    auto home = _region_map->getHomeRegion();
    sprintf(reply, " home is %s", home ? home->name : "*");
  } else if (n >= 3 && strcmp(parts[1], "default") == 0) {
    if (strcmp(parts[2], "<null>") == 0) {
      _region_map->setDefaultRegion(NULL);
      _callbacks->onDefaultRegionChanged(NULL);
      _callbacks->saveRegions();  // persist in one atomic step
      sprintf(reply, " default scope is now <null>");
    } else {
      auto def = _region_map->findByNamePrefix(parts[2]);
      if (def == NULL) {
        def = _region_map->putRegion(parts[2], 0);  // auto-create the default region
      }
      if (def) {
        def->flags = 0;   // make sure allow flood enabled
        _region_map->setDefaultRegion(def);
        _callbacks->onDefaultRegionChanged(def);
        _callbacks->saveRegions();  // persist in one atomic step
        sprintf(reply, " default scope is now %s", def->name);
      } else {
        strcpy(reply, "Err - region table full");
      }
    }
  } else if (n == 2 && strcmp(parts[1], "default") == 0) {
    auto def = _region_map->getDefaultRegion();
    sprintf(reply, " default scope is %s", def ? def->name : "<null>");
  } else if (n >= 3 && strcmp(parts[1], "put") == 0) {
    auto parent = n >= 4 ? _region_map->findByNamePrefix(parts[3]) : &(_region_map->getWildcard());
    if (parent == NULL) {
      strcpy(reply, "Err - unknown parent");
    } else {
      auto region = _region_map->putRegion(parts[2], parent->id);
      if (region == NULL) {
        strcpy(reply, "Err - unable to put");
      } else {
        region->flags = 0;   // New default: enable flood
        strcpy(reply, "OK - (flood allowed)");
      }
    }
  } else if (n >= 3 && strcmp(parts[1], "remove") == 0) {
    auto region = _region_map->findByName(parts[2]);
    if (region) {
      if (_region_map->removeRegion(*region)) {
        strcpy(reply, "OK");
      } else {
        strcpy(reply, "Err - not empty");
      }
    } else {
      strcpy(reply, "Err - not found");
    }
  } else if (n >= 3 && strcmp(parts[1], "list") == 0) {
    uint8_t mask = 0;
    bool invert = false;
    
    if (strcmp(parts[2], "allowed") == 0) {
      mask = REGION_DENY_FLOOD;
      invert = false;  // list regions that DON'T have DENY flag
    } else if (strcmp(parts[2], "denied") == 0) {
      mask = REGION_DENY_FLOOD;
      invert = true;   // list regions that DO have DENY flag
    } else {
      strcpy(reply, "Err - use 'allowed' or 'denied'");
      return;
    }
    
    int len = _region_map->exportNamesTo(reply, 160, mask, invert);
    if (len == 0) {
      strcpy(reply, "-none-");
    }
  // #554: the dispatchObserverCli fall-through that used to sit HERE was dead --
  // handleRegionCmd is only entered for "region"-prefixed commands, so mqtt verbs
  // never reached it. Moved to the real terminals of handleCommand / handleSetCmd
  // / handleGetCmd (search "#554"). Left this note so it is not re-added here.
#if defined(OFFBAND_CONFIG_CLI)
  } else if (offband::config::dispatchCliLine(command, reply, /*reply_size=*/1024)) {
    // #462: shared config-CLI bridge -- generic `set <key> <value>` / `get <key>`
    // routed to the registered config providers. Runs AFTER the observer CLI so
    // a combined build lets the observer's richer grammar win first; on a
    // non-observer role this is the whole config CLI surface.
#endif
  } else {
    strcpy(reply, "Err - ??");
  }
}
