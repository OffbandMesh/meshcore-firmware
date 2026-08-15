#pragma once

#include <cstdint>
#include <cstring>

// Append-only ring of published MQTT payloads with per-reader cursors (#175).
//
// One copy of each message is retained; each broker ("reader") holds only a
// 4-byte cursor, so memory is O(ring), not O(brokers x ring). This is what lets
// a broker go offline during TLS rotation and resume where it left off.
//
// Deliberately dependency-free (no Arduino/ESP headers) so it builds in
// [env:native] and is unit-testable in isolation -- same pattern as BlockStore.h.

// #710: 16 -> 32 slots (+8,192 B, funded by the #701 trim which freed 14,660 B).
//
// SIZED FROM MEASURED FLEET DATA, not a guess. A rotated-out broker must survive
// (N-1) x MQTT_ROTATE_DWELL_MS without its cursor being overrun; it drops when
//     packets_in >  MQTT_RING_SLOTS       i.e.   lambda > SLOTS / T_out
//
// CoreScope, 68 active observers (map.okimesh.org /api/observers, packetsLastHour):
//     mean 0.93/min   p50 0.20   p90 2.77   max 6.57
// Excluding the top 5 (non-Offband and suspected multi-reporting outliers) the
// busiest legitimate observer is 3.08/min. The 24 h network histogram puts
// minute-level peaks at ~2.3x the mean, so budget ~7.1/min burst for that node:
//     4 rotating brokers (180 s out): 7.1 x 3 = ~21 slots
//     5 rotating brokers (240 s out): 7.1 x 4 = ~28 slots
// 32 covers the busiest observed node through a p99 burst at up to 5 rotating
// TLS brokers. 16 was adequate only up to 3 brokers on SUSTAINED rates and had
// no burst margin.
//
// This is still one extrapolation deep (hourly average x network burst ratio).
// droppedCount() now measures it directly -- re-size from real drop counts on a
// busy observer rather than from this arithmetic.
// #726: 32 -> 20. MQTT_RING_MSG_MAX had to double (see below) so parsed packets
// stop being rejected; slot count comes down to pay for it. 20 x 1024 = 20,480 B
// (+4,096 over the old 32 x 512), which fits the headroom #701 freed.
//
// Depth trade: 20 messages covers ~3 rotating TLS brokers at the measured busiest
// observer (3.08/min sustained, ~7.1/min burst); it is marginal at 4+. Dropping the
// derived JSON fields CoreScope never reads (packet_type/payload_len/route/path/
// hash/len/time/date, ~130 B) would buy the depth back -- tracked separately.
#ifndef MQTT_RING_SLOTS
  #define MQTT_RING_SLOTS 20
#endif
// #726: 512 -> 1024. MUST be >= the buffer buildPacketJson() writes into.
// publishParsedPacket() builds the /packets JSON into char json[1024] and hands it
// to publishPacket() -> append(). At 512 every payload between 513 and 1023 bytes
// was REJECTED and silently discarded: append returned 0, nothing logged, no
// counter moved (droppedCount tracks cursor overrun at commit(), not a refused
// append). The JSON embeds "raw":"<whole packet in hex>", so with 316 B of other
// fields the ceiling was ~98 BYTES of packet -- a max 255 B MeshCore packet builds
// an 826 B body and never reached any broker. Transport-independent: the drop is
// upstream of broker fan-out, so a plaintext always-on slot was hit identically.
//
// Introduced by #175 (62ad4439 added the ring at 512 while the builder already used
// 1024) and found in the field on v1.5.0-beta1: an observer published the first,
// smaller message to CoreScope and silently dropped the two larger ones.
//
// KEEP THIS >= the json[] buffer in publishParsedPacket(). If that buffer grows,
// this must grow with it, or the same silent hole reopens.
#ifndef MQTT_RING_MSG_MAX
  #define MQTT_RING_MSG_MAX 1024
#endif
#ifndef MQTT_RING_MAX_READERS
  #define MQTT_RING_MAX_READERS 10
#endif

class MqttRingLog {
public:
    MqttRingLog() {
        memset(len_, 0, sizeof(len_));
        memset(seq_, 0, sizeof(seq_));
        for (int i = 0; i < MQTT_RING_MAX_READERS; i++) { cursor_[i] = 0; dropped_[i] = 0; }
        rejected_ = 0;
    }

    // Append a payload. Returns its sequence number (1-based), or 0 if rejected
    // (empty or larger than MQTT_RING_MSG_MAX). Overwrites the oldest slot when
    // full -- a reader that has not kept up loses the overrun (documented,
    // best-effort; see lapped()).
    // #726: count refused appends. An oversize payload never enters the ring at
    // all, so droppedCount() (cursor overrun) cannot see it -- that is exactly how
    // the 512-byte ceiling stayed invisible for so long. Separate counter, so
    // "too big to publish" and "reader fell behind" are never confused again.
    uint32_t rejectedCount() const { return rejected_; }

    uint32_t append(const uint8_t* payload, size_t len) {
        if (payload == nullptr || len == 0) return 0;
        if (len > MQTT_RING_MSG_MAX) { rejected_++; return 0; }
        uint32_t s = ++head_;
        uint32_t idx = (s - 1) % MQTT_RING_SLOTS;
        memcpy(buf_[idx], payload, len);
        len_[idx] = static_cast<uint16_t>(len);
        seq_[idx] = s;
        return s;
    }

    uint32_t head() const { return head_; }

    // Oldest sequence still retained (1-based); 0 when empty.
    uint32_t tail() const {
        if (head_ == 0) return 0;
        return (head_ > MQTT_RING_SLOTS) ? (head_ - MQTT_RING_SLOTS + 1) : 1;
    }

    // Messages this reader has not yet consumed, clamped to what is retained.
    uint32_t lag(uint8_t reader) const {
        if (reader >= MQTT_RING_MAX_READERS) return 0;
        uint32_t c = cursor_[reader];
        if (c < tailMinus1()) c = tailMinus1();      // lapped: clamp
        return (head_ > c) ? (head_ - c) : 0;
    }

    // #710: monotonic count of messages destroyed before this reader consumed
    // them. Unlike lapped(), this must NOT reset when the reader catches up --
    // lapped() is derived from the current cursor position, so it erases its own
    // evidence the moment a rotated-out broker drains, which is why silent
    // publish-ring loss was undetectable in the field.
    //
    // Counted in commit() ONLY -- the single site that mutates cursor_. lag() and
    // peek() clamp a local copy and are const, so counting there would re-tally
    // the same loss on every poll.
    uint32_t droppedCount(uint8_t reader) const {
        return (reader < MQTT_RING_MAX_READERS) ? dropped_[reader] : 0;
    }

    // True when the writer overran this reader's cursor (data was lost).
    // NOTE: transient -- see droppedCount() for the durable measure.
    bool lapped(uint8_t reader) const {
        if (reader >= MQTT_RING_MAX_READERS) return false;
        return head_ > MQTT_RING_SLOTS && cursor_[reader] < tailMinus1();
    }

    // Copy this reader's next unread message out. Returns false when caught up.
    // Does NOT advance -- call commit() only after a successful publish, so a
    // failed publish is retried rather than dropped.
    bool peek(uint8_t reader, uint8_t* out, size_t out_cap,
              size_t& out_len, uint32_t& out_seq) const {
        if (reader >= MQTT_RING_MAX_READERS || out == nullptr) return false;
        uint32_t c = cursor_[reader];
        if (c < tailMinus1()) c = tailMinus1();      // lapped: jump to oldest kept
        if (c >= head_) return false;                // caught up
        uint32_t want = c + 1;
        uint32_t idx = (want - 1) % MQTT_RING_SLOTS;
        if (seq_[idx] != want) return false;         // slot recycled under us
        if (len_[idx] > out_cap) return false;
        memcpy(out, buf_[idx], len_[idx]);
        out_len = len_[idx];
        out_seq = want;
        return true;
    }

    // Advance past the message peek() just returned.
    void commit(uint8_t reader) {
        if (reader >= MQTT_RING_MAX_READERS) return;
        uint32_t c = cursor_[reader];
        const uint32_t t = tailMinus1();
        // #710: the clamp below IS the data-loss event -- the writer overran this
        // reader and (t - c) messages were destroyed unread. Count before
        // clamping; monotonic, so it survives the reader catching up (lapped()
        // does not, which is why field loss was invisible).
        if (c < t) { dropped_[reader] += (t - c); c = t; }
        if (c < head_) cursor_[reader] = c + 1;
    }

    // Abandon the backlog and jump to the head (used when a reader is hopelessly
    // lapped, or a broker is (re)attached and should not replay stale traffic).
    //
    // #710: deliberately does NOT count toward droppedCount(). resync() serves two
    // purposes -- discarding a hopeless backlog (real loss) and attaching a broker
    // that must not replay stale traffic (not loss) -- and the caller knows which.
    // Counting both would make the metric ambiguous, so droppedCount() means
    // strictly "destroyed by writer overrun", never "deliberately skipped".
    void resync(uint8_t reader) {
        if (reader >= MQTT_RING_MAX_READERS) return;
        cursor_[reader] = head_;
    }

private:
    // Cursor value meaning "has consumed everything before the oldest retained".
    uint32_t tailMinus1() const {
        uint32_t t = tail();
        return (t == 0) ? 0 : (t - 1);
    }

    uint8_t  buf_[MQTT_RING_SLOTS][MQTT_RING_MSG_MAX];
    uint16_t len_[MQTT_RING_SLOTS];
    uint32_t seq_[MQTT_RING_SLOTS];
    uint32_t cursor_[MQTT_RING_MAX_READERS];
    uint32_t dropped_[MQTT_RING_MAX_READERS];   // #710: monotonic overrun loss
    uint32_t rejected_ = 0;                     // #726: payloads too big to append
    uint32_t head_ = 0;
};
