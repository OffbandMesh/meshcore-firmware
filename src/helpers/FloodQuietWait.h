#pragma once

// #2250 (Feature #2246): idle wait before forwarding a flood packet.
//
// Dispatcher::checkSend() only checks the channel at the instant a hold expires,
// via isReceiving(). When a reception has just ENDED the channel looks clear, so a
// repeater can transmit 70 ms after another packet -- exactly when the repeaters
// downstream are busy passing that other packet along. This decides how much
// longer a forwarded flood packet should wait for the air to go quiet.
//
// Pure: the caller supplies the times, so the decision is host-tested
// (test/test_flood_echo, FloodQuiet.*).

#include <stdint.h>
#include <Packet.h>

namespace offband {

// The extra hold is never longer than this per packet. A busy channel delays a
// forward; it can't starve it.
static constexpr uint32_t kFloodQuietCapMs = 3000;

// Only forwarded flood packets wait: direct-route packets (ACKs, path returns,
// direct replies) and this node's own originations (path count 0) keep stock
// timing.
inline bool floodQuietEligible(const mesh::Packet& p) {
    return p.isRouteFlood() && p.getPathHashCount() >= 1;
}

// Milliseconds the packet must still wait; 0 = send now.
//   quiet_until   when the channel counts as quiet: end of the last reception
//                 plus its airtime plus jitter (meaningless when !rx_seen)
//   rx_seen       a reception has armed quiet_until since boot
//   held          this packet has already been held at least once
//   hold_start    when its hold began (meaningful only when held)
inline uint32_t floodQuietHoldMs(uint32_t now, uint32_t quiet_until, bool rx_seen,
                                 bool held, uint32_t hold_start,
                                 uint32_t cap_ms = kFloodQuietCapMs) {
    if (!rx_seen) return 0;
    int32_t remaining = (int32_t)(quiet_until - now);
    if (remaining <= 0) return 0;
    uint32_t used = held ? (now - hold_start) : 0;
    if (used >= cap_ms) return 0;               // cap reached: send anyway
    uint32_t left = cap_ms - used;
    return (uint32_t)remaining < left ? (uint32_t)remaining : left;
}

// The per-packet hold state Dispatcher::checkSend() keeps. Extracted so the
// stateful part is host-tested too (Gemini review of #2250 found the cap reset
// when a higher-priority direct packet passed a held forward).
//
// The held packet is identified by its pool pointer. The hold ends ONLY when that
// packet is released by the gate or popped for sending, never because something
// else went out first, so the 3 s cap is measured across interruptions.
class FloodQuietGate {
public:
    // `head` is what the queue would send next; `eligible` is
    // floodQuietEligible(*head). Returns true when the head must wait.
    bool mustWait(const void* head, bool eligible, uint32_t now,
                  uint32_t quiet_until, bool rx_seen) {
        if (head == nullptr || !eligible) return false;   // the hold survives this
        const bool held = (head == held_);
        uint32_t wait = floodQuietHoldMs(now, quiet_until, rx_seen, held, start_);
        if (wait > 0) {
            if (!held) { held_ = head; start_ = now; holds_++; }
            return true;
        }
        if (held) {
            if ((now - start_) >= kFloodQuietCapMs) caps_++;
            held_ = nullptr;
        }
        return false;
    }
    // A packet left the queue for transmission.
    void onPopped(const void* pkt) { if (pkt == held_) held_ = nullptr; }

    uint32_t holds() const { return holds_; }
    uint32_t caps() const { return caps_; }

private:
    const void* held_ = nullptr;
    uint32_t start_ = 0;
    uint32_t holds_ = 0;
    uint32_t caps_ = 0;
};

}  // namespace offband
