// WifiEventLog -- one log line per station Wi-Fi event, so every join, leave and
// address change is on the record with its reason and timing (#2232).
//
// Why it exists: R2-SAW crashed twice inside a station session that lasted under
// a second. The router logged the leave as reason 8 ("sending station is
// leaving"), so the station itself left, but nothing on the device said so, or
// what had just called into Wi-Fi. This line closes that gap. It is on in every
// build that carries the Wi-Fi telemetry transport, not an opt-in.
//
// Pure: no Arduino, host-tested in test/test_wifi_event_log.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

namespace offband {

// 802.11 and ESP-IDF station disconnect reasons. A name only for codes we can
// state exactly; anything else prints as its number alone, never a wrong label.
inline const char* wifiReasonName(uint8_t r) {
    switch (r) {
        case 1:   return "UNSPECIFIED";
        case 2:   return "AUTH_EXPIRE";
        case 3:   return "AUTH_LEAVE";
        case 4:   return "ASSOC_EXPIRE";
        case 5:   return "ASSOC_TOOMANY";
        case 6:   return "NOT_AUTHED";
        case 7:   return "NOT_ASSOCED";
        case 8:   return "ASSOC_LEAVE";
        case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
        case 16:  return "GROUP_KEY_UPDATE_TIMEOUT";
        case 23:  return "802_1X_AUTH_FAILED";
        case 200: return "BEACON_TIMEOUT";
        case 201: return "NO_AP_FOUND";
        case 202: return "AUTH_FAIL";
        case 203: return "ASSOC_FAIL";
        case 204: return "HANDSHAKE_TIMEOUT";
        case 205: return "CONNECTION_FAIL";
        default:  return nullptr;
    }
}

enum class WifiEv : uint8_t { StaStart, StaConnected, GotIp, LostIp, Disconnected, StaStop };

inline const char* wifiEvName(WifiEv e) {
    switch (e) {
        case WifiEv::StaStart:     return "sta_start";
        case WifiEv::StaConnected: return "assoc";
        case WifiEv::GotIp:        return "got_ip";
        case WifiEv::LostIp:       return "lost_ip";
        case WifiEv::Disconnected: return "disconnected";
        case WifiEv::StaStop:      return "sta_stop";
    }
    return "?";
}

// A session shorter than this from association to disconnect is counted as
// short. The router showed two populations, 0-1 s and 5-7 s.
constexpr uint32_t kShortSessionMs = 2000;

// Timestamps (ms since boot) of the last of each step. 0 = not yet.
struct WifiEventTrack {
    uint32_t begin_ms = 0;  // last WiFi.begin() call by the transport
    uint32_t end_ms   = 0;  // last end() call by the transport
    uint32_t assoc_ms = 0;  // last association
    uint32_t ip_ms    = 0;  // last address
    uint32_t joins    = 0;  // associations since boot
    uint32_t short_sessions = 0;  // disconnects within kShortSessionMs of association
};

// Update the track for an event. Call before formatting.
inline void wifiEventApply(WifiEventTrack& t, WifiEv ev, uint32_t now) {
    switch (ev) {
        case WifiEv::StaConnected: t.assoc_ms = now; t.joins++; break;
        case WifiEv::GotIp:        t.ip_ms = now; break;
        case WifiEv::Disconnected:
            if (t.assoc_ms != 0 && now - t.assoc_ms < kShortSessionMs) t.short_sessions++;
            break;
        default: break;
    }
}

// Appends " <key>=<ms>ms", or " <key>=none" when the step never happened.
inline int wifiAppendSince(char* buf, size_t n, const char* key, uint32_t now, uint32_t at) {
    if (at == 0) return snprintf(buf, n, " %s=none", key);
    return snprintf(buf, n, " %s=%lums", key, (unsigned long)(now - at));
}

// Formats the line, without a trailing newline. reason is used only for
// Disconnected. Returns the length snprintf would have written.
inline int formatWifiEvent(char* buf, size_t n, WifiEv ev, uint32_t now,
                           const WifiEventTrack& t, uint8_t reason, int rssi) {
    if (buf == nullptr || n == 0) return 0;
    int len = snprintf(buf, n, "[WIFI] %s", wifiEvName(ev));
    // Once the buffer is full, keep counting the wanted length but write
    // nothing, and never form a pointer past the buffer.
    auto room = [&]() -> size_t { return (len >= 0 && (size_t)len < n) ? n - (size_t)len : 0; };
    auto at   = [&]() -> char*  { return room() ? buf + len : buf + n - 1; };
    auto add  = [&](int w) { if (w > 0) len += w; };
    if (ev == WifiEv::Disconnected) {
        const char* nm = wifiReasonName(reason);
        if (nm != nullptr) add(snprintf(at(), room(), " reason=%u(%s)", (unsigned)reason, nm));
        else               add(snprintf(at(), room(), " reason=%u", (unsigned)reason));
    }
    if (ev == WifiEv::StaConnected) add(snprintf(at(), room(), " rssi=%d", rssi));
    add(wifiAppendSince(at(), room(), "since_begin", now, t.begin_ms));
    add(wifiAppendSince(at(), room(), "since_end", now, t.end_ms));
    if (ev == WifiEv::Disconnected || ev == WifiEv::GotIp || ev == WifiEv::LostIp) {
        add(wifiAppendSince(at(), room(), "since_assoc", now, t.assoc_ms));
    }
    if (ev == WifiEv::Disconnected) {
        add(wifiAppendSince(at(), room(), "since_ip", now, t.ip_ms));
    }
    add(snprintf(at(), room(), " joins=%lu short=%lu",
                 (unsigned long)t.joins, (unsigned long)t.short_sessions));
    return len;
}

}  // namespace offband
