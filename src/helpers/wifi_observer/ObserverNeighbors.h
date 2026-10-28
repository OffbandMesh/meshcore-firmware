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

}  // namespace offband
