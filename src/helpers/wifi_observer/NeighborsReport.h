// src/helpers/wifi_observer/NeighborsReport.h
//
// #1399: the CoreScope /neighbors report body (Feature #1385). The ingestor that reads
// it is CoreScope's handleNeighborsReport: it records self.scopes against origin_id,
// and each neighbor's scopes only when that neighbor's status is "responded". A
// timeout is not "scopes cleared", and a neighbor missing from a truncated report is
// not gone. Arduino-free, so it is native-tested against a golden report.
//
//   {"timestamp":..,"origin":..,"origin_id":..,
//    "self":{"scopes":..,"default_scope":..},
//    "neighbors":[{"pubkey":<UPPER hex>,"snr":<dB>,"rssi":<dBm|null>,
//                  "heard_secs_ago":<s|null>,"scopes":..,"status":..}, ...],
//    "total_neighbors":N,"queried_neighbors":Q,"truncated":<bool>}

#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ObserverNeighbors.h"

namespace offband {

// CoreScope's cap on one report.
constexpr size_t kNeighborsReportMax = 10240;

// Memory. The build buffer is PSRAM on a board that has it. Each broker's queued copy
// is NOT: esp-mqtt allocates it with heap_caps_malloc(MALLOC_CAP_DEFAULT), which takes
// internal RAM first whatever the board, out of the heap TLS needs. So the copies are
// queued one broker at a time (NeighborsFanOut), each only when the heap fits, and the
// report is capped smaller on boards without PSRAM, where the buffer is internal too.
constexpr size_t kNeighborsReportMaxNoPsram = 4096;

inline size_t neighborsReportCap(bool has_psram) {
    return has_psram ? kNeighborsReportMax : kNeighborsReportMaxNoPsram;
}

// Headroom kept above the TLS floor when taking memory for the report, for whatever
// else the same loop pass allocates.
constexpr uint32_t kNeighborsHeapMargin = 4096;

// True when taking len more bytes still leaves free_heap (internal) at or above floor
// plus the margin. Checked before the buffer is allocated and before each broker's copy
// is queued; a "no" waits or skips rather than pushing a TLS session out of memory.
inline bool neighborsHeapFits(uint32_t free_heap, uint32_t floor, size_t len) {
    return (uint64_t)free_heap >= (uint64_t)floor + (uint64_t)len + kNeighborsHeapMargin;
}

// Hands one built report to the connected brokers one at a time, so at most about one
// queued copy is in memory at once: next() offers the next broker only while the heap
// fits, which in practice waits for the previous copy to be sent and freed. Brokers not
// offered within kNeighborsFanOutDeadlineMs are dropped and counted as skipped.
constexpr uint32_t kNeighborsFanOutDeadlineMs = 60000;

class NeighborsFanOut {
public:
    void begin(uint16_t slot_mask, uint32_t now_ms) {
        pending_ = slot_mask;
        started_ms_ = now_ms;
        offered_ = false;
        accepted_ = skipped_ = 0;
        active_ = slot_mask != 0;
    }

    bool active() const { return active_; }

    // The broker slot to queue a copy on now, or -1 (wait, or finished). Call
    // result() after every slot it returns.
    int next(uint32_t now_ms, bool heap_fits) {
        if (!active_ || offered_) return -1;
        if ((uint32_t)(now_ms - started_ms_) >= kNeighborsFanOutDeadlineMs) {
            for (uint16_t m = pending_; m != 0; m &= (uint16_t)(m - 1)) skipped_++;
            pending_ = 0;
            active_ = false;
            return -1;
        }
        if (!heap_fits || pending_ == 0) return -1;
        for (int s = 0; s < 16; s++) {
            if (pending_ & (1u << s)) {
                pending_ &= (uint16_t)~(1u << s);
                offered_ = true;
                return s;
            }
        }
        return -1;
    }

    void result(bool accepted) {
        if (!offered_) return;
        offered_ = false;
        if (accepted) accepted_++;
        else skipped_++;
        if (pending_ == 0) active_ = false;
    }

    int accepted() const { return accepted_; }
    int skipped() const { return skipped_; }

private:
    uint16_t pending_ = 0;
    uint32_t started_ms_ = 0;
    bool offered_ = false;
    bool active_ = false;
    int accepted_ = 0;
    int skipped_ = 0;
};

struct NeighborsReportInput {
    const char* timestamp;       // as /status stamps it; nullptr = ""
    const char* origin;          // the observer's name (escaped here)
    const char* origin_id;       // the observer's key, as /status sends it
    const char* self_scopes;     // the observer's own scopes, comma separated
    const char* default_scope;
    const ScopeResult* rows;     // a scope query walk's results
    int n_rows;
    int total_neighbors;         // neighbors when the walk started, before any cap
    uint32_t now;                // RTC epoch seconds
    bool (*plausible)(uint32_t epoch);   // clock sanity; nullptr = every age unknown
};

// The report's timestamp: UTC RFC 3339 with a zone, "YYYY-MM-DDTHH:MM:SSZ". CoreScope
// accepts only RFC 3339 here, and a timestamp it cannot parse bypasses its
// newest-report-wins guard, so an old report could overwrite a newer one. (/status
// keeps its own zoneless local format.) Returns the length, or -1 if out is too small.
int formatReportTimestamp(uint32_t epoch, char* out, size_t out_size);

// Rows with an outcome (not Pending) are listed: those with a known age first, then
// most recently heard, then strongest SNR, then by key. When the next row would not
// fit in buf_size, the rest are left out and "truncated" is true; the report itself
// always closes. self_scopes longer than kScopesLen - 1 is cut at its last whole name. heard_secs_ago is null unless both our clock and the time the row
// was heard look set and the row is not from the future. An rssi of 0 (no reading)
// is null. Returns the length written (NUL excluded), or -1 when even an empty
// report does not fit.
int buildNeighborsJson(char* buf, size_t buf_size, const NeighborsReportInput& in);

}  // namespace offband
