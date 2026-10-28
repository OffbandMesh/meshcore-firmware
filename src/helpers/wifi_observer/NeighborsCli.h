// src/helpers/wifi_observer/NeighborsCli.h
//
// #1398: the observer's discover.neighbors / discover.scopes commands (Feature #1385).
// The work happens in the companion's MyMesh; this layer parses the command and words
// the reply, through hooks the companion registers at boot, the same way the display
// commands reach the UI (DisplayConfigProvider). Arduino-free, so it is native-tested.
//
//   discover.neighbors  -- ask nearby repeaters to identify themselves now; answers
//                          arrive over the next minute and go into the neighbor table
//   discover.scopes     -- ask each neighbor in the table for its flood regions, one at
//                          a time; while a walk is running, report its progress instead

#pragma once
#include <stddef.h>

namespace offband {

struct ScopeWalkSummary {
    bool running;
    int  total;        // neighbors in the walk
    int  done;         // closed so far, any outcome
    int  responded;
    int  timeout;
    int  send_failed;
};

enum class DiscoverResult { Queued, Busy, Failed };

struct NeighborsCliHooks {
    int  (*neighbor_count)();
    // Queued: a request is on its way. Busy: the last request's answer window is still
    // open (seconds_left set). Failed: no packet could be built.
    DiscoverResult (*discover_neighbors)(int* seconds_left);
    void (*scope_summary)(ScopeWalkSummary* out);
    bool (*start_scopes)();         // false when the walk has no storage
};

// nullptr unregisters. The hooks object must outlive every call.
void setNeighborsCliHooks(const NeighborsCliHooks* hooks);

// True when cmd is a discover.* command (reply written, success or error);
// false when it is not one of ours.
bool handleNeighborsCli(const char* cmd, char* reply, size_t reply_size);

}  // namespace offband
