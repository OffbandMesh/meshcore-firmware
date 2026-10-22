#pragma once

// MeshCore channel keys that are derived, not shared (Epic R #1743, R0 #1747;
// docs/companion_protocol.md, "Channel Types").

#include <stddef.h>
#include <stdint.h>

namespace offband {

constexpr size_t kChannelSecretLen = 16;

// A hashtag channel's key: the first 16 bytes of sha256 of its name, '#'
// included, exactly as given (any case folding is the caller's). "#test"
// gives 9cd8fcf22a47333b591d96a2b848b73f.
void hashtagChannelKey(const char* name, uint8_t out[kChannelSecretLen]);

// The Public channel's well-known key.
extern const uint8_t kPublicChannelKey[kChannelSecretLen];

}  // namespace offband
