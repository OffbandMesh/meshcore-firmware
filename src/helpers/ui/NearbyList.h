#pragma once

// #1234: who's around. Every node heard lately, newest first, one row per node. The
// screen offers it the contacts it has heard from and MeshCore's advert table, which
// also holds nodes that aren't contacts. Pure, so it is unit-tested natively.

#include <stdint.h>
#include <string.h>

namespace badgeui {

struct NearbyNode {
  uint32_t heard;     // when, by our clock
  uint8_t key[32];    // a contact's whole key, or the advert table's prefix
  uint8_t key_len;    // how much of `key` is known: 32, or 7 from the advert table
  uint8_t type;       // ADV_TYPE_*, 0 when not known
  uint8_t hops;       // repeaters on the heard path, 0xFF when not known
  bool contact;
  char name[24];
};

template <int CAP>
class NearbyList {
  static_assert(CAP > 0, "room for a node");
  NearbyNode _nodes[CAP];
  int _count = 0;

  static bool same(const NearbyNode& a, const NearbyNode& b) {
    const int n = a.key_len < b.key_len ? a.key_len : b.key_len;
    return n > 0 && memcmp(a.key, b.key, (size_t)n) == 0;
  }

  // What both know, in one: the contact's whole key, type and name, the heard hops,
  // and the newer time.
  static void merge(NearbyNode& into, const NearbyNode& n) {
    if (n.contact && !into.contact) {
      const uint32_t heard = into.heard;
      const uint8_t hops = into.hops;
      into = n;
      if (heard > into.heard) into.heard = heard;
      if (hops != 0xFF) into.hops = hops;
      return;
    }
    if (n.heard > into.heard) into.heard = n.heard;
    if (!n.contact && n.hops != 0xFF) into.hops = n.hops;
    else if (into.hops == 0xFF) into.hops = n.hops;
  }

  void place(int i) {   // move entry i to where its time puts it
    NearbyNode t = _nodes[i];
    while (i > 0 && _nodes[i - 1].heard < t.heard) {
      _nodes[i] = _nodes[i - 1];
      i--;
    }
    while (i + 1 < _count && _nodes[i + 1].heard > t.heard) {
      _nodes[i] = _nodes[i + 1];
      i++;
    }
    _nodes[i] = t;
  }

public:
  void clear() { _count = 0; }
  int count() const { return _count; }
  const NearbyNode& at(int i) const { return _nodes[i]; }

  // A node heard at n.heard. The same node offered again is merged into its row; when
  // the list is full, the oldest row goes, unless this node is older still.
  void offer(const NearbyNode& n) {
    for (int i = 0; i < _count; i++) {
      if (same(_nodes[i], n)) {
        merge(_nodes[i], n);
        place(i);
        return;
      }
    }
    if (_count == CAP) {
      if (n.heard <= _nodes[CAP - 1].heard) return;
      _count--;   // drop the oldest
    }
    _nodes[_count] = n;
    _count++;
    place(_count - 1);
  }
};

}  // namespace badgeui
