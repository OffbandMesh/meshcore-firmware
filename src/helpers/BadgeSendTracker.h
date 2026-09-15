#pragma once

// #1227: DMs typed on the badge, from their first attempt to delivered or failed.
//
// The mesh sends every attempt; this only decides what happens next. MeshCore hashes
// the attempt number into a DM's expected ACK (BaseChatMesh::composeMsgPacket), so each
// attempt waits for a different ACK, and a late ACK for an earlier attempt still
// counts. Retries reuse the first attempt's timestamp and text, as the phone app does.
//
// Pure: no Arduino and no mesh. The caller passes the time in, so native tests can
// drive every path.

#include <stdint.h>
#include <string.h>

namespace offband {

enum class BadgeSend : uint8_t { None = 0, Sending, Delivered, Failed };

template <int SLOTS, int MAX_ATTEMPTS, int TEXT_CAP>
class BadgeSendTracker {
  // Handles are 16-bit and never reused while a slot holds one.
  static_assert(SLOTS > 0 && SLOTS < 0xFFFF, "one handle per slot, 16-bit handles");
  // MeshCore hashes only attempt & 3 into the ACK, so a fifth attempt would reuse the
  // first attempt's ACK.
  static_assert(MAX_ATTEMPTS > 0 && MAX_ATTEMPTS <= 4, "attempts 0-3 have distinct ACKs");

public:
  // An attempt waits at least this long, so a zero timeout from the mesh can't make one
  // pass of the caller's loop send the same DM again.
  static constexpr uint32_t kMinTimeoutMs = 1000;

  struct Slot {
    uint16_t  handle;              // 0: never used
    BadgeSend status;
    uint8_t   attempts;            // attempts sent so far
    uint32_t  timestamp;           // shared by every attempt
    uint32_t  deadline;            // millis when the current attempt times out
    uint32_t  acks[MAX_ATTEMPTS];  // each attempt's expected ACK
    uint8_t   key[6];              // the recipient's public-key prefix
    char      text[TEXT_CAP + 1];
  };

  BadgeSendTracker() { memset(_slots, 0, sizeof(_slots)); }

  // Starts a DM. Returns its handle, never 0, or 0 when every slot is still sending or
  // the text doesn't fit.
  uint16_t begin(const uint8_t key_prefix[6], uint32_t timestamp, const char* text) {
    if (key_prefix == nullptr || text == nullptr || strlen(text) > (size_t)TEXT_CAP) return 0;
    Slot* s = freeSlot();
    if (s == nullptr) return 0;
    const uint16_t handle = nextHandle();
    memset(s, 0, sizeof(*s));
    s->handle = handle;
    s->status = BadgeSend::Sending;
    s->timestamp = timestamp;
    memcpy(s->key, key_prefix, 6);
    memcpy(s->text, text, strlen(text) + 1);
    return handle;
  }

  // The mesh sent the next attempt: record its expected ACK and when it times out.
  void sent(uint16_t handle, uint32_t expected_ack, uint32_t now_ms, uint32_t timeout_ms) {
    Slot* s = sendingSlot(handle);
    if (s == nullptr || s->attempts >= MAX_ATTEMPTS) return;
    s->acks[s->attempts++] = expected_ack;
    s->deadline = now_ms + (timeout_ms < kMinTimeoutMs ? kMinTimeoutMs : timeout_ms);
  }

  // The mesh couldn't send an attempt at all.
  void sendFailed(uint16_t handle) {
    Slot* s = sendingSlot(handle);
    if (s != nullptr) s->status = BadgeSend::Failed;
  }

  // An ACK arrived. Returns the handle of the DM it answers, or 0 when it isn't ours.
  // A repeat of an ACK already seen returns the handle again, so the caller can keep it
  // away from the phone. A late ACK turns a failed DM into a delivered one: the
  // recipient did get it.
  uint16_t ack(uint32_t ack_crc) {
    if (ack_crc == 0) return 0;   // MeshCore's "no ACK expected"
    for (Slot& s : _slots) {
      if (s.handle == 0) continue;
      for (int i = 0; i < s.attempts; i++) {
        if (s.acks[i] == ack_crc) {
          s.status = BadgeSend::Delivered;
          return s.handle;
        }
      }
    }
    return 0;
  }

  // Call often. Returns a DM whose current attempt timed out and needs another, or 0.
  // The caller sends attempt number find(handle)->attempts and reports it with sent(),
  // or reports sendFailed(); until it does, the same handle comes back. A DM whose last
  // attempt has timed out becomes Failed here instead.
  uint16_t due(uint32_t now_ms) {
    for (Slot& s : _slots) {
      if (s.status != BadgeSend::Sending || s.attempts == 0) continue;
      if ((int32_t)(now_ms - s.deadline) < 0) continue;
      if (s.attempts >= MAX_ATTEMPTS) {
        s.status = BadgeSend::Failed;
        continue;
      }
      return s.handle;
    }
    return 0;
  }

  BadgeSend status(uint16_t handle) const {
    const Slot* s = find(handle);
    return s != nullptr ? s->status : BadgeSend::None;
  }

  const Slot* find(uint16_t handle) const {
    if (handle == 0) return nullptr;
    for (const Slot& s : _slots) {
      if (s.handle == handle) return &s;
    }
    return nullptr;
  }

private:
  Slot _slots[SLOTS];
  uint16_t _last = 0;

  Slot* sendingSlot(uint16_t handle) {
    if (handle == 0) return nullptr;
    for (Slot& s : _slots) {
      if (s.handle == handle && s.status == BadgeSend::Sending) return &s;
    }
    return nullptr;
  }

  // A never-used slot first, then the oldest finished one. A DM still sending is never
  // dropped to make room.
  Slot* freeSlot() {
    Slot* best = nullptr;
    uint16_t best_age = 0;
    for (Slot& s : _slots) {
      if (s.handle == 0) return &s;
      if (s.status == BadgeSend::Sending) continue;
      const uint16_t age = (uint16_t)(_last - s.handle);
      if (best == nullptr || age > best_age) {
        best = &s;
        best_age = age;
      }
    }
    return best;
  }

  // Handles count up and skip 0 and any handle a slot still holds, so a stale handle
  // from the UI never names someone else's DM.
  uint16_t nextHandle() {
    for (;;) {
      if (++_last == 0) _last = 1;
      if (find(_last) == nullptr) return _last;
    }
  }
};

}  // namespace offband
