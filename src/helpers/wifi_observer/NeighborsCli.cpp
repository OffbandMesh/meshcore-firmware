// src/helpers/wifi_observer/NeighborsCli.cpp -- see NeighborsCli.h (#1398).

#include "NeighborsCli.h"
#include "NeighborsSchedule.h"   // #1399: the interval range
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace offband {

static const NeighborsCliHooks* s_hooks = nullptr;

void setNeighborsCliHooks(const NeighborsCliHooks* hooks) { s_hooks = hooks; }

static bool hooksReady() {
    return s_hooks != nullptr && s_hooks->neighbor_count != nullptr &&
           s_hooks->discover_neighbors != nullptr && s_hooks->scope_summary != nullptr &&
           s_hooks->start_scopes != nullptr && s_hooks->read_settings != nullptr &&
           s_hooks->save_settings != nullptr && s_hooks->schedule_status != nullptr;
}

static bool handleDiscoverNeighbors(char* reply, size_t reply_size) {
    int seconds_left = 0;
    switch (s_hooks->discover_neighbors(&seconds_left)) {
        case DiscoverResult::Busy:
            snprintf(reply, reply_size,
                     "discover.neighbors: still collecting answers to the last request "
                     "(%d s left); %d neighbors in table\n", seconds_left, s_hooks->neighbor_count());
            return true;
        case DiscoverResult::Failed:
            snprintf(reply, reply_size,
                     "ERROR: discover.neighbors: no request queued (no free packet); try again\n");
            return true;
        case DiscoverResult::Queued:
            break;
    }
    snprintf(reply, reply_size,
             "discover.neighbors: request queued; %d neighbors in table, answers arrive "
             "over the next minute\n", s_hooks->neighbor_count());
    return true;
}

static bool handleDiscoverScopes(char* reply, size_t reply_size) {
    ScopeWalkSummary s;
    memset(&s, 0, sizeof(s));
    s_hooks->scope_summary(&s);
    if (s.running) {
        snprintf(reply, reply_size,
                 "discover.scopes: running, %d of %d done (%d responded, %d timeout, "
                 "%d send_failed)\n", s.done, s.total, s.responded, s.timeout, s.send_failed);
        return true;
    }
    const int n = s_hooks->neighbor_count();
    if (n == 0) {
        snprintf(reply, reply_size,
                 "discover.scopes: no neighbors yet; run discover.neighbors or wait for adverts\n");
        return true;
    }
    if (!s_hooks->start_scopes()) {
        snprintf(reply, reply_size,
                 "ERROR: discover.scopes: no memory for the query; not started\n");
        return true;
    }
    // Two lines when there was a last walk: one _sys frame holds 139 characters, and the
    // channel sends each line as its own frame.
    if (s.total > 0) {
        snprintf(reply, reply_size,
                 "discover.scopes: querying %d neighbors, one at a time (up to 20 s each), "
                 "then publishing /neighbors\n"
                 "last walk: %d responded, %d timeout, %d send_failed of %d\n",
                 n, s.responded, s.timeout, s.send_failed, s.total);
    } else {
        snprintf(reply, reply_size,
                 "discover.scopes: querying %d neighbors, one at a time (up to 20 s each), "
                 "then publishing /neighbors\n", n);
    }
    return true;
}

static bool isSpace(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

// cmd is exactly name, allowing trailing spaces or a CR/LF the serial line may carry.
static bool isCommand(const char* cmd, const char* name) {
    const size_t n = strlen(name);
    if (strncmp(cmd, name, n) != 0) return false;
    for (const char* p = cmd + n; *p != '\0'; p++) {
        if (!isSpace(*p)) return false;
    }
    return true;
}

// cmd is "<verb> <key>[ value]": the value (trailing whitespace dropped) into value,
// or nullptr when cmd is not that verb and key.
static const char* argOf(const char* cmd, const char* verb, const char* key, char* value, size_t value_size) {
    const size_t v = strlen(verb), k = strlen(key);
    if (strncmp(cmd, verb, v) != 0 || cmd[v] != ' ') return nullptr;
    const char* p = cmd + v;
    while (*p == ' ') p++;
    if (strncmp(p, key, k) != 0 || (p[k] != '\0' && !isSpace(p[k]))) return nullptr;
    p += k;
    while (isSpace(*p)) p++;
    size_t n = strlen(p);
    while (n > 0 && isSpace(p[n - 1])) n--;
    if (n >= value_size) {
        snprintf(value, value_size, "?");   // too long to be any valid value: refused as such
        return value;
    }
    memcpy(value, p, n);
    value[n] = '\0';
    return value;
}

static bool saveOrSay(const NeighborsSettings& s, char* reply, size_t reply_size, const char* key) {
    if (s_hooks->save_settings(s)) return true;
    snprintf(reply, reply_size, "ERROR: failed to save %s (NVS write failed)\n", key);
    return false;
}

static bool sameWord(const char* a, const char* b) {   // ASCII, ignoring case
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    }
    return *a == *b;
}

static bool handleSetEnabled(const char* value, char* reply, size_t reply_size) {
    bool on;
    if (sameWord(value, "on") || strcmp(value, "1") == 0) on = true;
    else if (sameWord(value, "off") || strcmp(value, "0") == 0) on = false;
    else {
        snprintf(reply, reply_size, "ERROR: usage: set mqtt.neighbors on|off\n");
        return true;
    }
    NeighborsSettings s;
    s_hooks->read_settings(&s);
    s.enabled = on;
    if (!saveOrSay(s, reply, reply_size, "mqtt.neighbors")) return true;
    if (on) {
        snprintf(reply, reply_size, "mqtt.neighbors = on, every %u h\n", (unsigned)s.interval_hours);
    } else {
        snprintf(reply, reply_size, "mqtt.neighbors = off\n");
    }
    return true;
}

static bool handleSetInterval(const char* value, char* reply, size_t reply_size) {
    char* end = nullptr;
    const long h = strtol(value, &end, 10);
    if (value[0] == '\0' || end == nullptr || *end != '\0') {
        snprintf(reply, reply_size, "ERROR: usage: set mqtt.neighbors.interval <%u..%u hours>\n",
                 (unsigned)kNeighborsIntervalMinHours, (unsigned)kNeighborsIntervalMaxHours);
        return true;
    }
    if (h < kNeighborsIntervalMinHours || h > kNeighborsIntervalMaxHours) {
        snprintf(reply, reply_size, "ERROR: mqtt.neighbors.interval %ld out of range [%u, %u] hours\n",
                 h, (unsigned)kNeighborsIntervalMinHours, (unsigned)kNeighborsIntervalMaxHours);
        return true;
    }
    NeighborsSettings s;
    s_hooks->read_settings(&s);
    s.interval_hours = (uint16_t)h;
    if (!saveOrSay(s, reply, reply_size, "mqtt.neighbors.interval")) return true;
    snprintf(reply, reply_size, "mqtt.neighbors.interval = %u\n", (unsigned)s.interval_hours);
    return true;
}

// "<1m", "45m" under an hour, else "20h48m".
static void formatDuration(uint32_t s, char* out, size_t out_size) {
    if (s < 60u) snprintf(out, out_size, "<1m");
    else if (s < 3600u) snprintf(out, out_size, "%um", (unsigned)(s / 60u));
    else snprintf(out, out_size, "%uh%02um", (unsigned)(s / 3600u), (unsigned)((s % 3600u) / 60u));
}

size_t formatNeighborsStatusLine(char* out, size_t out_size) {
    if (out == nullptr || out_size == 0) return 0;
    out[0] = '\0';
    if (!hooksReady()) return 0;
    NeighborsSettings set;
    s_hooks->read_settings(&set);
    NeighborsScheduleStatus st;
    memset(&st, 0, sizeof(st));
    s_hooks->schedule_status(&st);

    char when[24], last[24];
    if (st.cycle_running) snprintf(when, sizeof(when), "cycle running");
    else if (st.next_in_s <= 0) snprintf(when, sizeof(when), "due");
    else {
        char d[16];
        formatDuration((uint32_t)st.next_in_s, d, sizeof(d));
        snprintf(when, sizeof(when), "next in %s", d);
    }
    if (st.has_published) {
        char d[16];
        formatDuration(st.last_ago_s, d, sizeof(d));
        snprintf(last, sizeof(last), "%s ago", d);
    } else {
        snprintf(last, sizeof(last), "never");
    }

    int n;
    if (set.enabled) {
        n = snprintf(out, out_size, "nbr: on, every %u h, %s%s, last %s\n",
                     (unsigned)set.interval_hours, when,
                     (!st.cycle_running && st.next_in_s <= 0) ? " (waiting for clock and a broker)" : "",
                     last);
    } else {
        n = snprintf(out, out_size, "nbr: off%s, last %s\n", st.cycle_running ? ", report running" : "", last);
    }
    if (n < 0) return 0;
    return (size_t)n < out_size ? (size_t)n : out_size - 1;
}

bool handleNeighborsCli(const char* cmd, char* reply, size_t reply_size) {
    if (cmd == nullptr || reply == nullptr || reply_size == 0) return false;
    const bool is_neighbors = isCommand(cmd, "discover.neighbors");
    const bool is_scopes = isCommand(cmd, "discover.scopes");
    char value[16];
    const char* set_on = argOf(cmd, "set", "mqtt.neighbors", value, sizeof(value));
    const char* set_int = set_on ? nullptr : argOf(cmd, "set", "mqtt.neighbors.interval", value, sizeof(value));
    char unused[4];
    const bool get_on = argOf(cmd, "get", "mqtt.neighbors", unused, sizeof(unused)) != nullptr;
    const bool get_int = argOf(cmd, "get", "mqtt.neighbors.interval", unused, sizeof(unused)) != nullptr;
    if (!is_neighbors && !is_scopes && !set_on && !set_int && !get_on && !get_int) return false;
    if (!hooksReady()) {
        snprintf(reply, reply_size, "ERROR: %s: not available on this build\n",
                 is_neighbors ? "discover.neighbors" : is_scopes ? "discover.scopes"
                 : (set_int || get_int) ? "mqtt.neighbors.interval" : "mqtt.neighbors");
        return true;
    }
    if (is_neighbors) return handleDiscoverNeighbors(reply, reply_size);
    if (is_scopes) return handleDiscoverScopes(reply, reply_size);
    if (set_on) return handleSetEnabled(set_on, reply, reply_size);
    if (set_int) return handleSetInterval(set_int, reply, reply_size);
    NeighborsSettings s;
    s_hooks->read_settings(&s);
    if (get_on) snprintf(reply, reply_size, "mqtt.neighbors = %s\n", s.enabled ? "on" : "off");
    else snprintf(reply, reply_size, "mqtt.neighbors.interval = %u\n", (unsigned)s.interval_hours);
    return true;
}

}  // namespace offband
