#pragma once

#include <stdint.h>

// #1753 / Epic #1752: the one place the default path-hash size is decided.
//
// WHY THIS IS A SHARED CONSTANT AND NOT TWO LITERALS
// --------------------------------------------------
// `path_hash_mode` is declared independently in two prefs structs -- `CommonCLI.h`
// for the repeater, room server and sensor roles, and `companion_radio/NodePrefs.h`
// for the companion. They are separate structs with separate persisted layouts, so
// the default has to be written twice.
//
// Writing the LITERAL twice is the fragile part: a role added later copies one struct
// and inherits whatever that one happened to say, and two roles originating at
// different path-hash sizes is the kind of split nobody notices until packets from
// one role behave differently on the mesh than packets from another. Naming it once
// means a new role picks up the decision rather than a number. (Gemini review, #1753.)
//
// The value is a MODE, not a byte count: the size in bytes is `mode + 1`, as every
// caller spells out -- `sendFlood(pkt, delay, _prefs.path_hash_mode + 1)`.

namespace offband {

/// Default path-hash mode: 1 means **2-byte** path hashes.
///
/// Offband shipped 1-byte (mode 0) until #1752. The wider MeshCore ecosystem has moved
/// to 2 bytes and some regions to 3, because 256 distinct path hashes collide on a mesh
/// of any size.
///
/// The accepted cost is a halved hop ceiling: `(n + 1) * size <= MAX_PATH_SIZE` with
/// MAX_PATH_SIZE 64 gives 32 hops at 2 bytes, against 63 at 1 byte (where the six-bit
/// hop-count field, not MAX_PATH_SIZE, is the real limit). That was the owner's call,
/// made knowingly.
///
/// This only affects packets a node ORIGINATES. A relay appends its hash at the
/// packet's own declared size rather than its preference, so nodes on different modes
/// interoperate in both directions and the fleet can move incrementally.
///
/// Load clamps the stored value to 0..2; mode 3 is reserved.
static const uint8_t DEFAULT_PATH_HASH_MODE = 1;

}  // namespace offband
