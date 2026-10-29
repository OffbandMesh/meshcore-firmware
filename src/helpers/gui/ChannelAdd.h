#pragma once

// Adding a channel on the device (Epic R #1743, R0 #1747): the rules, with no
// Arduino and no crypto, so the native tests cover them. The hashtag key
// itself is sha256 and lives in helpers/ChannelKeys.h.
//
// MeshCore's channel kinds (docs/companion_protocol.md, "Channel Types"):
//   Public    a publicly known 16-byte key
//   hashtag   the first 16 bytes of sha256("#name"): anyone with the name has it
//   private   a random 16-byte key, shared by hand

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

constexpr size_t kChannelKeyLen = 16;
constexpr size_t kChannelKeyHexLen = 2 * kChannelKeyLen;
// A channel's name, '#' included, fits ChannelDetails::name with its NUL: a
// buffer for one needs kChannelNameMax + 1 bytes.
constexpr size_t kChannelNameMax = 31;

// A hashtag name as the radio keys it: one leading '#', then the name exactly
// as typed. CASE IS KEPT: the key is sha256 of the name byte for byte, and
// the Offband client keys "#QueenCityCon" as typed, so folding here would put
// a radio on a different channel from a phone that typed the same name. Leading
// '#'s and spaces at either end are dropped, and a space inside is not
// allowed. False, with *out empty, when nothing is left, a space is inside,
// or it would be longer than kChannelNameMax.
bool hashtagName(const char* typed, char* out, size_t n);

// #1896: "Public" typed into the hashtag join means the real Public (owner,
// 2026-10-28). Public's key is fixed, not derived from its name, so making it a
// hashtag channel would put the radio on "#public", a different channel. True
// for "public" in any case, with or without leading '#'s and outer spaces.
bool namesPublic(const char* typed);

// A private key typed as hex. Spaces, dashes and colons are skipped, so a key
// shown in groups can be typed as shown; any other character means it isn't
// a key. Returns how many hex digits it read; the key is complete, and *out
// written, only when that is exactly kChannelKeyHexLen. More than that (too
// many digits, or a character that is neither hex nor a separator) is
// kChannelKeyBad.
constexpr size_t kChannelKeyBad = kChannelKeyHexLen + 1;
size_t parseChannelKey(const char* text, uint8_t out[kChannelKeyLen]);

// What a typing field takes, key by key, so a wrong name or key can't be
// typed at all: a hashtag takes no space and no second '#' (the name is the
// key); a key takes hex digits only.
bool hashtagAccepts(char c);
bool channelKeyAccepts(char c);

// The key as hex, in groups of four ("4f1c 8a20 ..."), for the key screen.
// `n` must hold kChannelKeyHexLen + 7 spaces + NUL (40 bytes).
void formatChannelKey(const uint8_t key[kChannelKeyLen], char* out, size_t n);

// One slot of the radio's channel table, as these rules need it.
struct ChannelSlot {
  bool used;                      // has a name
  uint8_t key[kChannelKeyLen];
};

// The first unused slot, or -1 when all `count` are in use. Never slot 0: it is
// Public's, even while empty (#1896), and the radio refuses anything else there.
int firstFreeChannelSlot(const ChannelSlot* slots, int count);

// The slot already holding this key, or -1. The key is what makes a channel
// the same channel; names are local.
int findChannelByKey(const ChannelSlot* slots, int count, const uint8_t key[kChannelKeyLen]);

// Slots in use, for "3 of 40 channel slots used".
int usedChannelSlots(const ChannelSlot* slots, int count);

}  // namespace gui
}  // namespace offband
