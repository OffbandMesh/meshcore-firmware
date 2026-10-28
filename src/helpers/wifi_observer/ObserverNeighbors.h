#pragma once
#include <cstdint>
#include <cstring>

// Dependency-free, host-testable pieces of the companion observer's CoreScope
// /neighbors report (Feature #1385, plan docs/initiatives/regional-operation/
// observer-neighbors/PLAN.md).
//
// NeighborTable is the companion's zero-hop neighbor table: one row per repeater
// heard directly, mirroring simple_repeater's NeighbourInfo / putNeighbour()
// (match the key, else take the least recently heard slot) with an RSSI column
// added. Rows hold the raw public key rather than a mesh::Identity so the table
// stays free of the crypto and Arduino headers; callers convert at the edge.
// NeighbourInfo keeps the repeater's MeshCore spelling so the two stay
// recognizably the same structure.
//
// heard_timestamp == 0 marks a free slot, as on the repeater. put() stores at
// least 1 so a clock that reads 0 cannot make a real neighbor look free.
//
// RxRssiStamps carries a packet's RSSI from receive time to advert-processing
// time. Dispatcher::checkRecv() can queue a flood packet for later (the client-set
// rxdelay), so the radio's last RSSI at onAdvertRecv() may belong to a newer
// packet. logRx() runs in the receive iteration itself and stamps the value here,
// keyed by the Packet object. Size it to the packet pool and no live packet is
// ever evicted; a reused Packet is simply restamped.

namespace offband {

constexpr unsigned kNeighborKeyLen = 32;   // == PUB_KEY_SIZE, asserted at the call site

inline int8_t clampToInt8(int v) {
    if (v < -128) return -128;
    if (v > 127) return 127;
    return (int8_t)v;
}

// Transport codes { 0, 0 } mark a 'Share': an advert passed on, not heard from its sender.
inline bool isShareAdvert(bool has_transport_codes, uint16_t code0, uint16_t code1) {
    return has_transport_codes && code0 == 0 && code1 == 0;
}

// The repeater's rule for what counts as a neighbor: heard directly (no path hashes),
// not via a Share, and advertising as a repeater.
inline bool isNeighborAdvert(uint8_t path_hash_count, bool is_share, bool advert_valid, bool is_repeater) {
    return path_hash_count == 0 && !is_share && advert_valid && is_repeater;
}

// Anonymous-request sub-type asking a repeater for its flood regions. The same value
// as ANON_REQ_TYPE_REGIONS in simple_repeater/MyMesh.cpp, which answers it through
// handleAnonRegionsReq(): reused, not a new allocation.
constexpr uint8_t kAnonReqTypeRegions = 0x01;
constexpr int kRegionsRequestLen = 6;

// The plaintext of an anon-regions request: {tag (4, host order, as the repeater
// memcpy's it back)}{kAnonReqTypeRegions}{reply path length 0}. A zero-length reply
// path makes the repeater answer zero-hop direct, which is right for a neighbor.
inline int buildRegionsRequest(uint32_t tag, uint8_t* out) {
    memcpy(out, &tag, 4);
    out[4] = kAnonReqTypeRegions;
    out[5] = 0;
    return kRegionsRequestLen;
}

// How long one regions request may wait for its reply before it is a timeout (plan T4).
constexpr uint32_t kRegionQueryTimeoutMs = 20000;

// Bytes kept for one neighbor's region list, NUL included. A repeater can send about
// 170 characters; a longer list is cut at its last whole name (see below).
constexpr size_t kScopesLen = 96;

// Reads a reply built by handleAnonRegionsReq(): {tag}{their clock}{names, comma
// separated}, followed by the cipher's zero padding. False (out untouched) unless it is
// at least the 8-byte header and carries expect_tag. An empty list is a valid answer.
// A list longer than out_cap - 1 is cut at the last comma that fits, so no name is
// reported partly; when not even one name fits, out is "".
inline bool parseRegionsResponse(const uint8_t* data, size_t len, uint32_t expect_tag,
                                 char* out, size_t out_cap) {
    if (data == nullptr || out == nullptr || out_cap == 0 || len < 8) return false;
    uint32_t tag;
    memcpy(&tag, data, 4);
    if (tag != expect_tag) return false;

    const char* names = (const char*)data + 8;
    size_t n = 0;
    while (8 + n < len && names[n] != 0) n++;

    if (n + 1 > out_cap) {
        size_t cut = 0;
        for (size_t i = out_cap - 1; i > 0; i--) {
            if (names[i] == ',') { cut = i; break; }
        }
        n = cut;
    }
    memcpy(out, names, n);
    out[n] = 0;
    return true;
}

// The one regions request in flight. A reply belongs to it only when both the sender's
// key and the tag match: the same node answering one of the client's own requests
// carries the client's tag and must fall through to the client (plan Review #5).
class InFlightRegionsQuery {
public:
    void begin(const uint8_t* pub_key, uint32_t tag, uint32_t now_ms) {
        memcpy(key_, pub_key, kNeighborKeyLen);
        tag_ = tag;
        sent_ms_ = now_ms;
        active_ = true;
    }
    void clear() { active_ = false; }
    bool active() const { return active_; }
    bool matches(const uint8_t* from_key, uint32_t tag) const {
        return active_ && tag == tag_ && memcmp(from_key, key_, kNeighborKeyLen) == 0;
    }
    bool timedOut(uint32_t now_ms) const {
        return active_ && (uint32_t)(now_ms - sent_ms_) >= kRegionQueryTimeoutMs;
    }
    uint32_t tag() const { return tag_; }
    const uint8_t* key() const { return key_; }

private:
    uint8_t  key_[kNeighborKeyLen] = {};
    uint32_t tag_ = 0;
    uint32_t sent_ms_ = 0;
    bool     active_ = false;
};

struct NeighbourInfo {
    uint8_t  pub_key[kNeighborKeyLen];
    uint32_t advert_timestamp;   // the advert's own timestamp (sender clock)
    uint32_t heard_timestamp;    // our RTC when heard; 0 = free slot
    int8_t   snr;                // SNR x4, as on the repeater
    int8_t   rssi;               // dBm; 0 = no reading yet
};

template <int CAP>
class NeighborTable {
    static_assert(CAP > 0, "a neighbor table needs at least one slot");
public:
    NeighborTable() { clear(); }

    void clear() { memset(slots_, 0, sizeof(slots_)); }

    // rssi_known == false (no reading for this advert) keeps a matched row's previous
    // RSSI rather than erasing it; a new row starts at 0, "no reading".
    void put(const uint8_t* pub_key, uint32_t advert_ts, uint32_t heard_ts, float snr,
             int rssi_dbm, bool rssi_known = true) {
        NeighbourInfo* row = &slots_[0];
        bool matched = false;
        uint32_t oldest = 0xFFFFFFFF;
        for (int i = 0; i < CAP; i++) {
            if (slots_[i].heard_timestamp != 0 &&
                memcmp(slots_[i].pub_key, pub_key, kNeighborKeyLen) == 0) {
                row = &slots_[i];
                matched = true;
                break;
            }
            if (slots_[i].heard_timestamp < oldest) {
                row = &slots_[i];
                oldest = row->heard_timestamp;
            }
        }
        memcpy(row->pub_key, pub_key, kNeighborKeyLen);
        row->advert_timestamp = advert_ts;
        row->heard_timestamp = heard_ts != 0 ? heard_ts : 1;
        row->snr = clampToInt8((int)(snr * 4));
        if (rssi_known) {
            row->rssi = clampToInt8(rssi_dbm);
        } else if (!matched) {
            row->rssi = 0;
        }
    }

    int count() const {
        int n = 0;
        for (int i = 0; i < CAP; i++) {
            if (slots_[i].heard_timestamp != 0) n++;
        }
        return n;
    }

    int copyTo(NeighbourInfo* out, int max_out) const {
        if (out == nullptr || max_out <= 0) return 0;
        int n = 0;
        for (int i = 0; i < CAP && n < max_out; i++) {
            if (slots_[i].heard_timestamp != 0) out[n++] = slots_[i];
        }
        return n;
    }

    static constexpr int capacity() { return CAP; }

    // Slot i when it holds a neighbor, else nullptr; i outside [0, CAP) is nullptr.
    const NeighbourInfo* slotAt(int i) const {
        if (i < 0 || i >= CAP || slots_[i].heard_timestamp == 0) return nullptr;
        return &slots_[i];
    }

private:
    NeighbourInfo slots_[CAP];
};

template <int SLOTS>
class RxRssiStamps {
    static_assert(SLOTS > 0, "RxRssiStamps needs at least one slot");
public:
    void stamp(const void* pkt, int rssi_dbm) {
        for (int i = 0; i < SLOTS; i++) {
            if (keys_[i] == pkt) {
                rssi_[i] = clampToInt8(rssi_dbm);
                return;
            }
        }
        keys_[next_] = pkt;
        rssi_[next_] = clampToInt8(rssi_dbm);
        next_ = (next_ + 1) % SLOTS;
    }

    // Leaves *out untouched and returns false when the packet was never stamped.
    bool lookup(const void* pkt, int8_t* out) const {
        if (pkt == nullptr) return false;
        for (int i = 0; i < SLOTS; i++) {
            if (keys_[i] == pkt) {
                *out = rssi_[i];
                return true;
            }
        }
        return false;
    }

private:
    const void* keys_[SLOTS] = {};
    int8_t rssi_[SLOTS] = {};
    int next_ = 0;
};

// One neighbor's outcome in a scope query walk. The names are the CoreScope
// /neighbors status strings; Pending means not reached yet.
enum class ScopeStatus : uint8_t { Pending = 0, Responded, Timeout, SendFailed };

inline const char* scopeStatusName(ScopeStatus s) {
    switch (s) {
        case ScopeStatus::Responded:  return "responded";
        case ScopeStatus::Timeout:    return "timeout";
        case ScopeStatus::SendFailed: return "send_failed";
        default:                      return "pending";
    }
}

struct ScopeResult {
    NeighbourInfo row;            // the neighbor as it was when the walk started
    ScopeStatus   status;
    char          scopes[kScopesLen];   // meaningful only when status == Responded
};

// How a reply relates to the walk. Retired: it answers a request the walk already
// closed (timed out, or replaced by a restart). It is still ours, so the caller drops
// it rather than letting it reach the client, whose login/status branches match a
// sender on a 4-byte key prefix alone.
enum class ScopeReply : uint8_t { None, Current, Retired };

// The walk over the neighbor table (plan T4): one regions request in flight at a
// time, each answered, expired after kRegionQueryTimeoutMs, or failed at send. It
// sends nothing itself and never blocks: the caller's loop() asks poll() for the next
// key, sends it, and reports back with sent(); the reply path reports with replied().
template <int CAP>
class ScopeQueryRun {
    static_assert(CAP > 0, "a scope query walk needs at least one slot");
public:
    static constexpr int kRetired = 4;   // closed requests whose late replies are still ours

    // Snapshot rows[0..n) and start over. A walk already running is discarded; its
    // in-flight request is retired, so a late reply to it is still recognized as ours.
    void start(const NeighbourInfo* rows, int n) {
        restart();
        total_ = n < 0 ? 0 : n;
        if (rows == nullptr) total_ = 0;
        for (int i = 0; i < total_ && n_ < CAP; i++) add(rows[i]);
        running_ = true;
    }

    // The same, straight from a NeighborTable, with no intermediate copy.
    template <int C>
    void start(const NeighborTable<C>& table) {
        restart();
        for (int i = 0; i < C; i++) {
            const NeighbourInfo* r = table.slotAt(i);
            if (r == nullptr) continue;
            total_++;
            if (n_ < CAP) add(*r);
        }
        running_ = true;
    }

    bool running() const { return running_; }

    // The key to send to next, or nullptr when a request is in flight or the walk has
    // ended. Expires a request whose reply is overdue, and ends the walk after the last.
    const uint8_t* poll(uint32_t now_ms) {
        if (!running_) return nullptr;
        if (inflight_.active()) {
            if (!inflight_.timedOut(now_ms)) return nullptr;
            finish(ScopeStatus::Timeout, nullptr);
        }
        if (cur_ >= n_) {
            running_ = false;
            return nullptr;
        }
        return results_[cur_].row.pub_key;
    }

    // Report the send of the key poll() just returned.
    void sent(bool ok, uint32_t tag, uint32_t now_ms) {
        if (!running_ || inflight_.active() || cur_ >= n_) return;
        if (ok) {
            inflight_.begin(results_[cur_].row.pub_key, tag, now_ms);
        } else {
            finish(ScopeStatus::SendFailed, nullptr);
        }
    }

    // True when a reply from from_key carrying tag answers the request in flight.
    bool matches(const uint8_t* from_key, uint32_t tag) const { return inflight_.matches(from_key, tag); }

    ScopeReply classify(const uint8_t* from_key, uint32_t tag) const {
        if (inflight_.matches(from_key, tag)) return ScopeReply::Current;
        for (int i = 0; i < kRetired; i++) {
            if (retired_[i].used && retired_[i].tag == tag &&
                memcmp(retired_[i].key, from_key, kNeighborKeyLen) == 0) return ScopeReply::Retired;
        }
        return ScopeReply::None;
    }

    // Report the parsed region list for the request in flight; ignored when none is.
    void replied(const char* scopes) {
        if (!inflight_.active()) return;
        finish(ScopeStatus::Responded, scopes);
    }

    // The node answered, but with nothing usable (shorter than the reply header). Its
    // scopes are unknown, which the report must not present as "responded, none".
    void unusableReply() {
        if (!inflight_.active()) return;
        finish(ScopeStatus::Timeout, nullptr);
    }

    // Once per closed request: the key it was sent to, so the caller can let go of
    // per-request state (the transient contact). False when nothing new closed.
    bool takeReleased(uint8_t* key_out) {
        if (!released_pending_) return false;
        memcpy(key_out, released_key_, kNeighborKeyLen);
        released_pending_ = false;
        return true;
    }

    int resultCount() const { return n_; }
    int totalCount() const { return total_; }   // neighbors at start, before the CAP cut
    const ScopeResult& result(int i) const { return results_[i]; }
    int queriedCount() const {
        int q = 0;
        for (int i = 0; i < n_; i++) {
            if (results_[i].status != ScopeStatus::Pending) q++;
        }
        return q;
    }

private:
    struct Retired {
        uint8_t  key[kNeighborKeyLen];
        uint32_t tag;
        bool     used;
    };

    void restart() {
        retire();
        n_ = 0;
        total_ = 0;
        cur_ = 0;
    }

    void add(const NeighbourInfo& row) {
        results_[n_].row = row;
        results_[n_].status = ScopeStatus::Pending;
        results_[n_].scopes[0] = 0;
        n_++;
    }

    // Close the request in flight (if any): remember it as retired and hand its key
    // to takeReleased().
    void retire() {
        if (!inflight_.active()) return;
        Retired& r = retired_[retired_next_];
        memcpy(r.key, inflight_.key(), kNeighborKeyLen);
        r.tag = inflight_.tag();
        r.used = true;
        retired_next_ = (retired_next_ + 1) % kRetired;
        memcpy(released_key_, inflight_.key(), kNeighborKeyLen);
        released_pending_ = true;
        inflight_.clear();
    }

    void finish(ScopeStatus s, const char* scopes) {
        ScopeResult& r = results_[cur_];
        r.status = s;
        if (scopes != nullptr) {
            strncpy(r.scopes, scopes, kScopesLen - 1);
            r.scopes[kScopesLen - 1] = 0;
        } else {
            r.scopes[0] = 0;
        }
        retire();
        cur_++;
    }

    ScopeResult results_[CAP];
    InFlightRegionsQuery inflight_;
    Retired retired_[kRetired] = {};
    int retired_next_ = 0;
    uint8_t released_key_[kNeighborKeyLen] = {};
    bool released_pending_ = false;
    int n_ = 0;
    int total_ = 0;
    int cur_ = 0;
    bool running_ = false;
};

}  // namespace offband
