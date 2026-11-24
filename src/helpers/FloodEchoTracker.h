#pragma once

// #2249 (Feature #2246): echo-confirmed retry for forwarded flood packets.
//
// A repeater forwards a flood packet once and normally never learns whether
// anyone downstream picked it up. It can tell, though: a downstream repeater that
// forwards the packet transmits it again with a longer path, and this repeater
// hears that copy before the duplicate check drops it. This tracker remembers the
// last few flood packets this repeater forwarded and asks for exactly ONE
// retransmission of any that drew no such echo within its window.
//
// Pure logic, no radio and no clock of its own: the caller passes `now`, the
// packet hash (Packet::calculatePacketHash) and the hop count, so the whole
// decision is host-testable (test/test_flood_echo).
//
// What a retry is, on the wire: the same packet, byte for byte. Every node that
// already has it drops the copy by hash, so the cost is one extra transmission
// and nothing else -- no protocol change, stock radios unaffected.

#include <stdint.h>
#include <string.h>
#include "Packet.h"

namespace offband {

struct FloodEchoCounters {
    uint32_t tracked = 0;          // forwards recorded for an echo
    uint32_t echoed = 0;           // forwards that drew an echo on the first send
    uint32_t retries = 0;          // retries handed back for transmission
    uint32_t retry_echoed = 0;     // retries that then drew an echo
    uint32_t suppressed = 0;       // due, but no downstream neighbour heard recently
    uint32_t evicted = 0;          // dropped unresolved to make room
};

class FloodEchoTracker {
public:
    static constexpr int kSlots = 4;
    // Retries pause when no echo of anything has been heard for this long: a
    // repeater with no downstream neighbour would otherwise double its airtime for
    // nothing. Until the FIRST echo after boot, retries stay paused too.
    static constexpr uint32_t kNeighbourStaleMs = 30UL * 60UL * 1000UL;
    // A retry handed out but never reported sent (dropped from a full queue, say)
    // is forgotten after this long, so a slot can't leak.
    static constexpr uint32_t kRetrySendGraceMs = 10000;

    // Record a forwarded flood packet that has just finished transmitting.
    // `hops` is its path hash count AS SENT (this repeater's hash included).
    // A second call for a hash already handed out as a retry is that retry being
    // sent: it re-arms the echo window but can never earn another retry.
    void onForwardSent(const uint8_t* hash, uint8_t hops, const mesh::Packet& pkt,
                       uint32_t now, uint32_t echo_window_ms) {
        Entry* e = find(hash);
        if (e != nullptr) {
            if (e->retried) e->deadline = now + echo_window_ms;
            return;   // a re-send of a first-send entry: nothing new to learn
        }
        // A hash already handed out as a retry whose slot has since been evicted
        // must NOT come back as a fresh first send, or it could earn a second
        // retry. The retired ring outlives the slot for exactly this case.
        const bool already_retried = wasRetried(hash);
        e = freeSlot();
        if (e == nullptr) {               // full: the oldest deadline goes
            e = &slots_[0];
            for (int i = 1; i < kSlots; ++i)
                if ((int32_t)(slots_[i].deadline - e->deadline) < 0) e = &slots_[i];
            counters_.evicted++;
        }
        e->used = true;
        memcpy(e->hash, hash, MAX_HASH_SIZE);
        e->hops = hops;
        e->deadline = now + echo_window_ms;
        e->retried = already_retried;
        e->pkt = pkt;
        if (!already_retried) counters_.tracked++;
    }

    // Every received flood packet, BEFORE the duplicate check. A copy of a
    // tracked packet with a path at least as long as ours means a repeater at
    // our depth or beyond carried it: that is the echo. A shorter path is an
    // upstream copy arriving late and proves nothing about downstream.
    void onHeard(const uint8_t* hash, uint8_t hops, uint32_t now) {
        Entry* e = find(hash);
        if (e == nullptr || hops < e->hops) return;
        if (e->retried) counters_.retry_echoed++; else counters_.echoed++;
        last_echo_ms_ = now;
        any_echo_ = true;
        e->used = false;
    }

    // Call from the loop. Returns a packet to retransmit, or nullptr. The caller
    // copies it into a fresh pool packet and queues it; the tracker keeps its own
    // copy until the retry's echo window closes. At most one per call.
    const mesh::Packet* takeDue(uint32_t now) {
        for (int i = 0; i < kSlots; ++i) {
            Entry& e = slots_[i];
            if (!e.used || (int32_t)(now - e.deadline) < 0) continue;
            if (e.retried) {             // the retry's window closed too: done
                e.used = false;
                continue;
            }
            if (!neighbourRecent(now)) {
                counters_.suppressed++;
                e.used = false;
                continue;
            }
            e.retried = true;
            e.deadline = now + kRetrySendGraceMs;
            memcpy(retired_[retired_next_], e.hash, MAX_HASH_SIZE);
            retired_next_ = (uint8_t)((retired_next_ + 1) % kRetired);
            if (retired_count_ < kRetired) retired_count_++;
            counters_.retries++;
            return &e.pkt;
        }
        return nullptr;
    }

    bool neighbourRecent(uint32_t now) const {
        return any_echo_ && (now - last_echo_ms_) < kNeighbourStaleMs;
    }

    const FloodEchoCounters& counters() const { return counters_; }
    int pending() const {
        int n = 0;
        for (int i = 0; i < kSlots; ++i) n += slots_[i].used ? 1 : 0;
        return n;
    }

    // Echo window for a packet of the given airtime, where `tx_delay_factor` is
    // the repeater's own flood hold factor (MyMesh::getRetransmitDelay holds
    // 0..5 x factor x airtime). A downstream repeater holds as long as we do, then
    // spends one airtime transmitting; the margin covers its receive processing.
    static uint32_t echoWindowMs(uint32_t airtime_ms, float tx_delay_factor) {
        float hold = 5.0f * (tx_delay_factor > 0.0f ? tx_delay_factor : 0.0f);
        return (uint32_t)((hold + 1.0f) * (float)airtime_ms) + 500;
    }

private:
    struct Entry {
        bool used = false;
        bool retried = false;
        uint8_t hops = 0;
        uint8_t hash[MAX_HASH_SIZE] = {0};
        uint32_t deadline = 0;
        mesh::Packet pkt;
    };

    Entry* find(const uint8_t* hash) {
        for (int i = 0; i < kSlots; ++i)
            if (slots_[i].used && memcmp(slots_[i].hash, hash, MAX_HASH_SIZE) == 0) return &slots_[i];
        return nullptr;
    }
    Entry* freeSlot() {
        for (int i = 0; i < kSlots; ++i) if (!slots_[i].used) return &slots_[i];
        return nullptr;
    }
    bool wasRetried(const uint8_t* hash) const {
        for (int i = 0; i < retired_count_; ++i)
            if (memcmp(retired_[i], hash, MAX_HASH_SIZE) == 0) return true;
        return false;
    }

    // Hashes handed out as retries, newest overwriting oldest. Twice the slot
    // count, so it outlives any slot that could be evicted under it.
    static constexpr int kRetired = 2 * kSlots;
    uint8_t retired_[kRetired][MAX_HASH_SIZE] = {{0}};
    uint8_t retired_next_ = 0;
    uint8_t retired_count_ = 0;

    Entry slots_[kSlots];
    FloodEchoCounters counters_;
    uint32_t last_echo_ms_ = 0;
    bool any_echo_ = false;
};

// Which forwarded packets the tracker should follow. Adverts are excluded by
// the owner's ruling (periodic, large, repeated anyway); a packet with no path is
// this node's own origination, not a forward.
inline bool floodEchoEligible(const mesh::Packet& p) {
    return p.isRouteFlood() && p.getPathHashCount() >= 1
        && p.getPayloadType() != PAYLOAD_TYPE_ADVERT;
}

}  // namespace offband
