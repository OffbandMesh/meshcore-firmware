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

// The report's buffer and each broker's queued copy of it are heap. On a board with
// PSRAM a block this size is placed there (allocations above 4 KB prefer PSRAM in the
// ESP32-S3 framework config), so the full CoreScope cap is used. Without PSRAM it comes
// out of the internal RAM TLS needs, so the report is capped smaller and says so with
// "truncated".
constexpr size_t kNeighborsReportMaxNoPsram = 4096;

inline size_t neighborsReportCap(bool has_psram) {
    return has_psram ? kNeighborsReportMax : kNeighborsReportMaxNoPsram;
}

// Headroom kept above the TLS floor when taking memory for the report, for whatever
// else the same loop pass allocates.
constexpr uint32_t kNeighborsHeapMargin = 4096;

// True when taking len more bytes still leaves free_heap at or above floor plus the
// margin. Checked before the buffer is allocated and before each broker's copy is
// queued; a "no" skips that step rather than pushing a TLS session out of memory.
inline bool neighborsHeapFits(uint32_t free_heap, uint32_t floor, size_t len) {
    return (uint64_t)free_heap >= (uint64_t)floor + (uint64_t)len + kNeighborsHeapMargin;
}

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
