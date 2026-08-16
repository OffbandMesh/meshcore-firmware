// src/helpers/config/WifiConfigProvider.h
//
// Epic #300 item 1 / #370: the shared `wifi.*` config provider.
//
// Extracted from wifi_observer/ObserverCli.cpp so ANY WiFi-capable role
// (observer, companion #365, repeater #301) registers the SAME provider for the
// `wifi.` key space instead of the observer owning the prefix. Single ownership
// dissolves the first-provider-wins shadowing hazard (#366) for `wifi.*`.
//
// Keys: `wifi.ssid`, `wifi.pwd` (write-only on GET), `wifi.enabled`, and the
// read-only `wifi.status`. Persistence is the `"wifi"` NVS namespace via
// Preferences (role-neutral) -- NOT a NodePrefs/DataStore offset.
//
// STATUS SOURCE (#684, resolved 2026-08-01): `wifi.status` reports ONE uniform
// vocabulary for every WiFi-capable role. On an OBSERVER build it reads the
// authoritative `WifiBootstrap` STA/AP state machine; on a non-observer build
// (companion #365, repeater #301) it derives the SAME words from the raw WiFi
// driver + stored creds. The `WifiBootstrap` header is included ONLY under
// `OFFBAND_OBSERVER`, so this provider links with zero `wifi_observer/` sources
// off-observer -- closing the gap #370 left and unblocking #462. (Owner D1/D3:
// uniform WiFi across all WiFi-capable roles; one status vocabulary, not two.)
//
// The handlers are exposed here (not file-static) because the observer's `_sys`
// string CLI (dispatchObserverCli, still in ObserverCli.cpp) calls them too --
// one implementation, two callers (the CLI grammar and the wire dispatcher).
// The provider self-registers during static init; nothing to call from main().

#pragma once
#include <stddef.h>

namespace offband {

// wifi.ssid / wifi.pwd setter. `field` is the part after "wifi." ("ssid"|"pwd").
// PSK never echoed (pwd ACK reports length only). Reply is NUL-terminated human
// text; returns true when handled (incl. an ERROR reply).
bool handleSetWifiField(char* reply, size_t reply_size,
                        const char* field, const char* value);

// wifi.ssid / wifi.status getter. `field` is the part after "wifi.". wifi.pwd is
// write-only and returns an ERROR here.
bool handleGetWifi(char* reply, size_t reply_size, const char* field);

// wifi.enabled policy flag (NVS "wifi"/"enabled", default true; reboot-to-apply).
bool handleSetWifiEnabled(char* reply, size_t reply_size, bool enabled);

// #689/#696: credential clear. `what` is nullptr/""/"pwd" (clear the PSK only,
// the default) or "all" (also clear the SSID). Uses Preferences::remove(), NOT
// putString(""), which does not reliably clear an ESP32 NVS key (#98). This is
// the only escape from a stored PSK, which otherwise raises the STA scan-auth
// threshold and permanently blocks OPEN-network association (#692).
bool handleClearWifi(char* reply, size_t reply_size, const char* what);

}  // namespace offband
