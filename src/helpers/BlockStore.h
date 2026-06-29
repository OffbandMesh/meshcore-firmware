#pragma once

#include <cstdint>
#include <cstring>

// BlockStore -- a fixed-capacity set of blocked node public-key prefixes.
//
// This is the portable, firmware-side "ignore list" core for the Block/Ignore
// Users feature (Offband-only; see docs/architecture/2026-06-28-block-user-
// firmware-app-contract.md). It is deliberately dependency-free (no Arduino,
// no heap) so it compiles in the native googletest env AND on-device, and so
// its behavior is unit-testable in isolation. Persistence and the 0xC2 sync
// protocol live in MyMesh, not here -- this class is pure in-memory state.
//
// Keys are stored as raw BLOCK_KEY_SIZE-byte buffers (the identity pubkey, the
// real cryptographic identity -- see Identity.h). Lookup/compare is a constant-
// time-irrelevant linear memcmp scan; the capacity is small (32) so O(n) is fine.

#ifndef MAX_BLOCKED_KEYS
  #define MAX_BLOCKED_KEYS 32
#endif

#ifndef BLOCK_KEY_SIZE
  #define BLOCK_KEY_SIZE 32
#endif

class BlockStore {
public:
  BlockStore() : _count(0) {}

  int count() const { return _count; }

  // Pointer to the i-th stored key (BLOCK_KEY_SIZE bytes). Caller must pass a
  // valid index in [0, count()); used by the 0xC2 LIST reply. Returns nullptr
  // for an out-of-range index rather than reading past the array.
  const uint8_t* keyAt(int i) const {
    if (i < 0 || i >= _count) return nullptr;
    return _keys[i];
  }

  bool contains(const uint8_t* pubkey) const {
    return indexOf(pubkey) >= 0;
  }

  // Add a key. Returns true if the key is present after the call (already
  // there -> dedup, count unchanged, still true). Returns false only when the
  // store is full and the key is not already present.
  bool add(const uint8_t* pubkey) {
    if (indexOf(pubkey) >= 0) return true;        // dedup
    if (_count >= MAX_BLOCKED_KEYS) return false; // full
    memcpy(_keys[_count], pubkey, BLOCK_KEY_SIZE);
    _count++;
    return true;
  }

  // Remove a key. Returns true if it was present (and removed), false if not.
  // Swap-remove: the last entry fills the gap, so order is not preserved.
  bool remove(const uint8_t* pubkey) {
    int idx = indexOf(pubkey);
    if (idx < 0) return false;
    int last = _count - 1;
    if (idx != last) {
      memcpy(_keys[idx], _keys[last], BLOCK_KEY_SIZE);
    }
    _count--;
    return true;
  }

  void clear() { _count = 0; }

private:
  int indexOf(const uint8_t* pubkey) const {
    for (int i = 0; i < _count; i++) {
      if (memcmp(_keys[i], pubkey, BLOCK_KEY_SIZE) == 0) return i;
    }
    return -1;
  }

  uint8_t _keys[MAX_BLOCKED_KEYS][BLOCK_KEY_SIZE];
  int _count;
};
