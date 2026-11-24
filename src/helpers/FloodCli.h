#pragma once

// #2251 (Feature #2246): the repeater's flood-delivery commands.
//
//   get flood                 settings and counters on one line
//   set flood.quiet on|off    the idle wait before a forwarded flood packet (#2250)
//   set flood.retry on|off    one retry when no downstream echo is heard (#2249)
//
// Pure: parse and format only, so it is host-tested (test/test_flood_echo).
// The caller owns the settings, persists them when this reports a change, and
// supplies the counters.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "FloodEchoTracker.h"

namespace offband {

struct FloodSettings {
    uint8_t quiet = 1;   // persisted as repeat.f_quiet
    uint8_t retry = 1;   // persisted as repeat.f_retry
};

struct FloodStats {
    FloodEchoCounters echo;
    uint32_t quiet_holds = 0;
    uint32_t quiet_caps = 0;
};

enum class FloodCliResult : uint8_t {
    NotMine,    // not a flood command: the caller keeps dispatching
    Handled,    // reply written, nothing changed
    Changed,    // reply written, a setting changed: persist it
};

// One line, so it fits a remote CLI reply (160 bytes, even with every counter at
// its maximum) and a single MeshLog line. Keys:
//   fwd       forwards tracked for an echo       echo      ...that drew one
//   rtx       retries sent                       rtx_echo  ...that then drew one
//   no_nbr    retries skipped: no recent echo    evict     unresolved, pushed out
//   hold      forwards held for quiet air        cap       ...that hit the 3 s cap
inline int formatFloodStatus(char* out, size_t cap, const FloodSettings& s, const FloodStats& st) {
    return snprintf(out, cap,
        "quiet=%s retry=%s | fwd=%lu echo=%lu rtx=%lu rtx_echo=%lu "
        "no_nbr=%lu evict=%lu hold=%lu cap=%lu",
        s.quiet ? "on" : "off", s.retry ? "on" : "off",
        (unsigned long)st.echo.tracked, (unsigned long)st.echo.echoed,
        (unsigned long)st.echo.retries, (unsigned long)st.echo.retry_echoed,
        (unsigned long)st.echo.suppressed, (unsigned long)st.echo.evicted,
        (unsigned long)st.quiet_holds, (unsigned long)st.quiet_caps);
}

namespace detail {
// "on" / "off" exactly, after leading spaces and before trailing spaces.
inline int parseOnOff(const char* v) {
    while (*v == ' ') v++;
    size_t n = strlen(v);
    while (n > 0 && v[n - 1] == ' ') n--;
    if (n == 2 && strncmp(v, "on", 2) == 0) return 1;
    if (n == 3 && strncmp(v, "off", 3) == 0) return 0;
    return -1;
}
inline bool isExact(const char* cmd, const char* key) {
    size_t k = strlen(key);
    if (strncmp(cmd, key, k) != 0) return false;
    const char* rest = cmd + k;
    while (*rest == ' ') rest++;
    return *rest == 0;
}
}  // namespace detail

inline FloodCliResult handleFloodCommand(const char* cmd, FloodSettings& s, const FloodStats& st,
                                         char* reply, size_t cap) {
    if (detail::isExact(cmd, "get flood")) {
        formatFloodStatus(reply, cap, s, st);
        return FloodCliResult::Handled;
    }
    uint8_t* field = nullptr;
    const char* rest = nullptr;
    // ONLY these two exact keys. The common CLI owns other `set flood.*` keys
    // (flood.max, flood.max.unscoped, flood.max.advert, flood.advert.interval);
    // a catch-all here would steal them (Gemini review of #2251).
    if (strncmp(cmd, "set flood.quiet ", 16) == 0)      { field = &s.quiet; rest = cmd + 16; }
    else if (strncmp(cmd, "set flood.retry ", 16) == 0) { field = &s.retry; rest = cmd + 16; }
    else return FloodCliResult::NotMine;

    int v = detail::parseOnOff(rest);
    if (v < 0) {
        snprintf(reply, cap, "Err - expected on or off");
        return FloodCliResult::Handled;
    }
    bool changed = (*field != (uint8_t)v);
    *field = (uint8_t)v;
    snprintf(reply, cap, "OK");
    return changed ? FloodCliResult::Changed : FloodCliResult::Handled;
}

}  // namespace offband
