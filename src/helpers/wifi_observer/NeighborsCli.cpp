// src/helpers/wifi_observer/NeighborsCli.cpp -- see NeighborsCli.h (#1398).

#include "NeighborsCli.h"
#include <stdio.h>
#include <string.h>

namespace offband {

static const NeighborsCliHooks* s_hooks = nullptr;

void setNeighborsCliHooks(const NeighborsCliHooks* hooks) { s_hooks = hooks; }

static bool hooksReady() {
    return s_hooks != nullptr && s_hooks->neighbor_count != nullptr &&
           s_hooks->discover_neighbors != nullptr && s_hooks->scope_summary != nullptr &&
           s_hooks->start_scopes != nullptr;
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
    if (s.total > 0) {
        snprintf(reply, reply_size,
                 "discover.scopes: querying %d neighbors, one at a time (up to 20 s each); "
                 "last walk: %d responded, %d timeout, %d send_failed of %d\n",
                 n, s.responded, s.timeout, s.send_failed, s.total);
    } else {
        snprintf(reply, reply_size,
                 "discover.scopes: querying %d neighbors, one at a time (up to 20 s each)\n", n);
    }
    return true;
}

// cmd is exactly name, allowing trailing spaces or a CR/LF the serial line may carry.
static bool isCommand(const char* cmd, const char* name) {
    const size_t n = strlen(name);
    if (strncmp(cmd, name, n) != 0) return false;
    for (const char* p = cmd + n; *p != '\0'; p++) {
        if (*p != ' ' && *p != '\r' && *p != '\n' && *p != '\t') return false;
    }
    return true;
}

bool handleNeighborsCli(const char* cmd, char* reply, size_t reply_size) {
    if (cmd == nullptr || reply == nullptr || reply_size == 0) return false;
    const bool is_neighbors = isCommand(cmd, "discover.neighbors");
    const bool is_scopes = isCommand(cmd, "discover.scopes");
    if (!is_neighbors && !is_scopes) return false;
    if (!hooksReady()) {
        snprintf(reply, reply_size, "ERROR: %s: not available on this build\n",
                 is_neighbors ? "discover.neighbors" : "discover.scopes");
        return true;
    }
    return is_neighbors ? handleDiscoverNeighbors(reply, reply_size)
                        : handleDiscoverScopes(reply, reply_size);
}

}  // namespace offband
