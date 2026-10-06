#pragma once

// Queen City Con 0x4 badge: the channels a fresh badge boots holding (#1364).
//
// Reached only through -D OFFBAND_PREFLASH_CHANNELS, which only this variant's env sets,
// so the sixty other variants that build the companion role never see this file.
//
// Public is NOT here. It is added by MyMesh for every board, from PUBLIC_GROUP_PSK, and
// stays where it is.
//
// FIRST BOOT ONLY. These are seeded when a badge has no stored channel set at all; a
// badge that has been configured keeps exactly the channels it has, and a flash does not
// add, reorder or duplicate anything. The owner, 2026-10-05, on existing badges: "we can
// add them manually, I don't see a problem with that since I already added most of them
// to mine."
//
// ---------------------------------------------------------------------------------------
// The keys are DERIVED, not invented, and not secret.
//
// Every one of these is a hashtag channel, so its key follows from its name. The source of
// record is the client: `derivePskFromHashtag` in `lib/models/channel.dart` -- the first 16
// bytes of SHA-256 over the name INCLUDING the leading '#'. Names are stored with the '#'.
//
// So anyone who knows the name has the key; publishing it here discloses nothing that the
// channel name does not. Regenerate and check any line below with:
//
//   python -c "import hashlib,base64,sys; \
//              print(base64.b64encode(hashlib.sha256(sys.argv[1].encode()).digest()[:16]).decode())" '#queencitycon'
//
// addChannel() takes the key base64-encoded and accepts a 16-byte (128-bit) key, hashing
// it to the channel hash itself, so these go in exactly as the client would send them.
// ---------------------------------------------------------------------------------------

namespace offband {

struct PreflashChannel {
  const char* name;       // stored with the '#', as the client stores it
  const char* psk_base64; // first 16 bytes of SHA-256 over `name`
};

// Order is the order they appear on the badge, after Public.
static const PreflashChannel kQccPreflashChannels[] = {
  { "#queencitycon", "WojRddTbHscmYOHBE0xQiw==" },
  { "#okimesh",      "MIiOP18SYKy07P2ZXXM6uA==" },
  { "#test",         "nNj88ipHMztZHZaiuEi3Pw==" },
  { "#echo",         "XSXPQLH1tKfrDPlwNjSpSA==" },
  { "#offband",      "zai/JZlzNvmZzoQ41CAapw==" },
};

static const int kQccPreflashChannelCount =
    (int)(sizeof(kQccPreflashChannels) / sizeof(kQccPreflashChannels[0]));

}  // namespace offband
