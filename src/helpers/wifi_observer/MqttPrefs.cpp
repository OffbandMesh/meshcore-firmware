#include "MqttPrefs.h"

#ifdef WITH_MQTT_UPLINK

#include <stddef.h>
#include <helpers/TxtDataHelpers.h>
#include <string.h>

namespace {
constexpr uint32_t kFixedStatusIntervalMs = 300000;
}

void MqttPrefsStore::setDefaults(MqttPrefs& prefs) {
  memset(&prefs, 0, sizeof(prefs));
  prefs.magic = kMagic;
  // Plan 1: ship with letsmesh-us (kLetsmeshUsBit=0x04) as the only
  // default-enabled broker. EastMesh's default was eastmesh-au (0x01);
  // we change it because (a) letsmesh-us is documented in our prior-art
  // catalog as the most-likely-tried broker for US-based deployments
  // and (b) Plan 2 replaces this whole static-array + bitmask pattern
  // with an NVS-backed configurable list, so the default value here
  // is short-lived.
  prefs.enabled_mask = 0x04;  // kLetsmeshUsBit; see MqttUplink.h:118.
  prefs.packets_enabled = 1;
  prefs.raw_enabled = 0;
  prefs.status_enabled = 1;
  prefs.tx_enabled = 0;
  prefs.deprecated_web_enabled = 0;
  prefs.deprecated_web_stats_enabled = 0;
  prefs.legacy_wifi_powersave = 0;
  prefs.status_interval_ms = kFixedStatusIntervalMs;
  StrHelper::strncpy(prefs.iata, MQTT_DEFAULT_IATA, sizeof(prefs.iata));
#ifdef WIFI_SSID
  StrHelper::strncpy(prefs.legacy_wifi_ssid, WIFI_SSID, sizeof(prefs.legacy_wifi_ssid));
#endif
#ifdef WIFI_PWD
  StrHelper::strncpy(prefs.legacy_wifi_pwd, WIFI_PWD, sizeof(prefs.legacy_wifi_pwd));
#endif
}

bool MqttPrefsStore::load(FILESYSTEM* fs, MqttPrefs& prefs) {
  setDefaults(prefs);
  if (fs == nullptr || !fs->exists(kFilename)) {
    return false;
  }

#if defined(RP2040_PLATFORM)
  File file = fs->open(kFilename, "r");
#else
  File file = fs->open(kFilename);
#endif
  if (!file) {
    return false;
  }

  MqttPrefs persisted{};
  size_t bytes_to_read = min(static_cast<size_t>(file.size()), sizeof(persisted));
  bool ok = bytes_to_read >= sizeof(persisted.magic) &&
            file.read(reinterpret_cast<uint8_t*>(&persisted), bytes_to_read) == bytes_to_read;
  file.close();

  if (!ok || persisted.magic != kMagic) {
    // Legacy or corrupt MQTT prefs should not survive migration. Remove the
    // stale file and persist a clean current-format default copy immediately.
    fs->remove(kFilename);
    save(fs, prefs);
    return false;
  }
  prefs = persisted;
  if (prefs.legacy_wifi_powersave > 2) {
    prefs.legacy_wifi_powersave = 0;
  }
  if (prefs.iata[0] == 0) {
    StrHelper::strncpy(prefs.iata, MQTT_DEFAULT_IATA, sizeof(prefs.iata));
  }
  prefs.status_interval_ms = kFixedStatusIntervalMs;
  prefs.enabled_mask &= 0x07;
  return true;
}

bool MqttPrefsStore::save(FILESYSTEM* fs, const MqttPrefs& prefs) {
  if (fs == nullptr) {
    return false;
  }
  if (fs->exists(kFilename) && !fs->remove(kFilename)) {
    return false;
  }
#if defined(RP2040_PLATFORM)
  File file = fs->open(kFilename, "w");
#else
  File file = fs->open(kFilename, "w", true);
#endif
  if (!file) {
    return false;
  }
  bool ok = file.write(reinterpret_cast<const uint8_t*>(&prefs), sizeof(prefs)) == sizeof(prefs);
  file.close();
  return ok;
}

#endif
