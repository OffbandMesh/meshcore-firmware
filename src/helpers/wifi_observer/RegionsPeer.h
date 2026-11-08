// src/helpers/wifi_observer/RegionsPeer.h
//
// #1910: where a repeater sends a decrypted peer packet while its scope query is out
// (Epic #1903, plan docs/initiatives/regional-operation/repeater-neighbors/PLAN.md).
// A neighbor's regions reply is encrypted to the repeater, so it only decrypts when the
// neighbor is matched as a peer. The repeater's peers are its ACL clients, and a
// neighbor is usually not one, so searchPeersByHash() adds the in-flight neighbor after
// the ACL under kRegionsPeerIdx. That match must never reach the code that serves
// clients: their requests, logins and CLI commands. Arduino-free, so it is
// native-tested.
//
// Two rules, from the plan's Review Focus:
//   1. A packet matched through kRegionsPeerIdx goes to the regions handler when it is
//      the walk's reply, and nowhere otherwise.
//   2. A neighbor that is also an ACL client is matched as that client first (same keys,
//      same secret); its regions reply still goes to the walk, and everything else it
//      sends still goes to the client paths.
#pragma once
#include <stdint.h>
#include "ObserverNeighbors.h"   // ScopeReply

namespace offband {

// The matched-peer index for the regions request in flight. Negative, so every ACL path
// that bounds-checks its index (i < 0 || i >= clients) refuses it by construction.
constexpr int kRegionsPeerIdx = -2;
static_assert(kRegionsPeerIdx < 0, "the regions peer is never a valid ACL index");

enum class PeerRoute : uint8_t { Regions, Client, Drop };

// acl_idx: the matched index (an ACL index, or kRegionsPeerIdx). is_response: the
// packet is a PAYLOAD_TYPE_RESPONSE. reply: what the walk makes of the sender's key and
// the reply's tag (ScopeReply::None when no walk is running or it is not ours).
inline PeerRoute routePeerData(int acl_idx, int num_clients, bool is_response, ScopeReply reply) {
    if (is_response && reply != ScopeReply::None) return PeerRoute::Regions;
    if (acl_idx < 0 || acl_idx >= num_clients) return PeerRoute::Drop;
    return PeerRoute::Client;
}

}  // namespace offband
