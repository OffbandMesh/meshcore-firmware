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

#ifndef MQTT_RING_SLOTS
  #define MQTT_RING_SLOTS 16
#endif
#ifndef MQTT_RING_MSG_MAX
  #define MQTT_RING_MSG_MAX 512
#endif
#ifndef MQTT_RING_MAX_READERS
  #define MQTT_RING_MAX_READERS 10
#endif

class MqttRingLog {
public:
    MqttRingLog() {
        memset(len_, 0, sizeof(len_));
        memset(seq_, 0, sizeof(seq_));
        for (int i = 0; i < MQTT_RING_MAX_READERS; i++) cursor_[i] = 0;
    }

    // Append a payload. Returns its sequence number (1-based), or 0 if rejected
    // (empty or larger than MQTT_RING_MSG_MAX). Overwrites the oldest slot when
    // full -- a reader that has not kept up loses the overrun (documented,
    // best-effort; see lapped()).
    uint32_t append(const uint8_t* payload, size_t len) {
        if (payload == nullptr || len == 0 || len > MQTT_RING_MSG_MAX) return 0;
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

    // True when the writer overran this reader's cursor (data was lost).
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
        if (c < tailMinus1()) c = tailMinus1();
        if (c < head_) cursor_[reader] = c + 1;
    }

    // Abandon the backlog and jump to the head (used when a reader is hopelessly
    // lapped, or a broker is (re)attached and should not replay stale traffic).
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
    uint32_t head_ = 0;
};
