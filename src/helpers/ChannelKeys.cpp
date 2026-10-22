#include "ChannelKeys.h"

#include <string.h>

#include <Utils.h>

namespace offband {

void hashtagChannelKey(const char* name, uint8_t out[kChannelSecretLen]) {
  const char* n = name ? name : "";
  mesh::Utils::sha256(out, kChannelSecretLen, reinterpret_cast<const uint8_t*>(n), int(strlen(n)));
}

// 8b3387e9c5cdea6ac9e5edbaa115cd72: PUBLIC_GROUP_PSK ("izOH6cXN6mrJ5e26oRXNcg==")
// decoded, as the protocol documents it.
const uint8_t kPublicChannelKey[kChannelSecretLen] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                                      0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};

}  // namespace offband
