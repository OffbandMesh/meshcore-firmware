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
//                          a time, and publish the /neighbors report when done; while a
//                          walk is running, report its progress instead
//
// #1399, the periodic report's settings (persisted, applied at once):
//   set mqtt.neighbors on|off          get mqtt.neighbors
//   set mqtt.neighbors.interval <h>    get mqtt.neighbors.interval     (12..336, default 24)
// and the "nbr:" line `mqtt status` adds on the observer.

#pragma once
#include <stddef.h>
#include <stdint.h>

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

struct NeighborsSettings {
    bool     enabled;
    uint16_t interval_hours;
};

// Where the periodic report stands, for the "nbr:" status line.
struct NeighborsScheduleStatus {
    bool     cycle_running;   // a refresh, walk or publish is under way
    int32_t  next_in_s;       // to the next cycle; <= 0 = due, waiting for clock and broker
                              // (when not running; a due cycle starts once both are there)
    bool     has_published;
    uint32_t last_ago_s;      // since the last report went out (when has_published)
};

struct NeighborsCliHooks {
    int  (*neighbor_count)();
    // Queued: a request is on its way. Busy: the last request's answer window is still
    // open (seconds_left set). Failed: no packet could be built.
    DiscoverResult (*discover_neighbors)(int* seconds_left);
    void (*scope_summary)(ScopeWalkSummary* out);
    bool (*start_scopes)();         // false when the walk has no storage
    void (*read_settings)(NeighborsSettings* out);
    // Persist and apply; false when the save failed (nothing applied).
    bool (*save_settings)(const NeighborsSettings& s);
    void (*schedule_status)(NeighborsScheduleStatus* out);
};

// nullptr unregisters. The hooks object must outlive every call.
void setNeighborsCliHooks(const NeighborsCliHooks* hooks);

// True when cmd is a discover.* command or a set/get of mqtt.neighbors[.interval]
// (reply written, success or error); false when it is not one of ours.
bool handleNeighborsCli(const char* cmd, char* reply, size_t reply_size);

// The "nbr: ..." line `mqtt status` appends, newline included. Writes "" when the
// hooks are not registered. Returns the length written.
size_t formatNeighborsStatusLine(char* out, size_t out_size);

}  // namespace offband
