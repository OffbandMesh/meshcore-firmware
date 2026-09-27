#pragma once

// #1232: whether a packet the badge just heard is a repeat of one of its own channel
// sends. A repeater passes a flood packet on with its payload unchanged, so a repeat
// has the send's packet hash. MyMesh's logRx() sees every packet before the duplicate
// filter drops it, and asks here; a match means a repeater carried the message on.
//
// A send is watched for kWatchMs. Repeats come within seconds; after that nothing is
// pending, and logRx() skips hashing what it hears.
//
// Pure: no Arduino and no mesh. The caller passes millis() in.

#include <stdint.h>
#include <string.h>

namespace offband {

template <int SLOTS, int HASH_LEN>
class HeardRepeats {
  static_assert(SLOTS > 0 && HASH_LEN > 0, "room for one hash");

  struct Slot {
    uint32_t id;               // the caller's id for the send; 0 is an empty slot
    uint32_t sent_ms;
    uint8_t hash[HASH_LEN];
  };
  Slot _slots[SLOTS] = {};
  int _next = 0;

  static bool live(const Slot& s, uint32_t now_ms) { return s.id != 0 && now_ms - s.sent_ms < kWatchMs; }

public:
  static constexpr uint32_t kWatchMs = 5UL * 60UL * 1000UL;

  // A send to watch for, by its on-air hash and the caller's id for it (not 0). The
  // oldest send is forgotten to make room.
  void watch(const uint8_t* hash, uint32_t id, uint32_t now_ms) {
    if (id == 0) return;
    Slot& s = _slots[_next];
    s.id = id;
    s.sent_ms = now_ms;
    memcpy(s.hash, hash, HASH_LEN);
    _next = (_next + 1) % SLOTS;
  }

  // Whether any send is still being watched, so there's a reason to hash a packet.
  bool watching(uint32_t now_ms) const {
    for (const Slot& s : _slots) {
      if (live(s, now_ms)) return true;
    }
    return false;
  }

  // A packet was heard: the id of the send it repeats, or 0.
  uint32_t heard(const uint8_t* hash, uint32_t now_ms) const {
    for (const Slot& s : _slots) {
      if (live(s, now_ms) && memcmp(s.hash, hash, HASH_LEN) == 0) return s.id;
    }
    return 0;
  }
};

}  // namespace offband
