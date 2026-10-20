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

#include <stdint.h>

// kTuneSilent, kChannelTuneUnset and encodeChannelTune, for the per-channel tune seeded
// below. Header-only constexpr, and the builtin table is internal-linkage, so including
// it here adds nothing to the image that MyMesh.cpp does not already pull in.
#include <helpers/ui/TuneSlots.h>

namespace offband {

struct PreflashChannel {
  const char* name;       // stored with the '#', as the client stores it
  const char* psk_base64; // first 16 bytes of SHA-256 over `name`
  // #1625: the channel's tune AS STORED (id + 1; 0 means "no choice", inherit the global
  // channel tune). Seeded on first boot only, same as the channel itself.
  uint8_t tune;
};

// No choice: this channel sounds with whatever the global channel tune is.
constexpr uint8_t kPreflashTuneInherit = kChannelTuneUnset;

// Silent: this channel arrives without a sound at all.
//
// Abend asked for #test and #echo to be quiet by default, and this is the mechanism that
// already exists to do it. There is no per-channel All/Mentions/Off on the badge -- that
// lives in GuiPrefs and only GUI builds carry it -- and the device-wide notify scope is
// the only place mentions-only can be set. What a channel CAN carry is its own tune, and
// tune 0 is silence by construction: tuneRtttl(kTuneSilent) returns nullptr and the
// caller plays nothing.
//
// So these two channels are quiet for everything, including @mentions. That is a real
// difference from what was asked for and it is the owner's call, 2026-10-18: a per-channel
// mentions exception would need a new per-channel pref on the badge, and this needs none.
constexpr uint8_t kPreflashTuneSilent = encodeChannelTune(kTuneSilent);

static_assert(kPreflashTuneSilent != kChannelTuneUnset,
              "silence must be a CHOICE, distinct from having made none");

// Order is the order they appear on the badge, after Public.
static const PreflashChannel kQccPreflashChannels[] = {
  { "#queencitycon", "WojRddTbHscmYOHBE0xQiw==", kPreflashTuneInherit },
  { "#okimesh",      "MIiOP18SYKy07P2ZXXM6uA==", kPreflashTuneInherit },
  // Traffic channels: quiet by default. A badge that chimes for every echo reply is a
  // badge somebody switches off entirely, which costs them the channels that matter.
  { "#test",         "nNj88ipHMztZHZaiuEi3Pw==", kPreflashTuneSilent  },
  { "#echo",         "XSXPQLH1tKfrDPlwNjSpSA==", kPreflashTuneSilent  },
  { "#offband",      "zai/JZlzNvmZzoQ41CAapw==", kPreflashTuneInherit },
};

static const int kQccPreflashChannelCount =
    (int)(sizeof(kQccPreflashChannels) / sizeof(kQccPreflashChannels[0]));

}  // namespace offband
