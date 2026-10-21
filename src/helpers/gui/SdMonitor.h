#pragma once

// The SD card's presence, for hot-swap (Epic I #1433, I7 #1703) and the
// header icon (I9 #1705).
//
// The R8's Expansion Board V2 wires no card-detect line (schematic V2.03,
// #1675), so presence is found by asking the card: a cheap status probe
// while it's working, a mount attempt while it isn't. This decides when to
// ask and what the answers mean; the caller does the asking.
//
// Owner, 2026-10-21: the header shows a green disk while the card works, and
// a red one when a card was in use and is gone or one is in and won't mount.
// A board that has never seen a card shows nothing.
//
// Pure: no Arduino. The caller passes times in.

#include <stdint.h>

namespace offband {
namespace gui {

enum class SdState : uint8_t {
  NeverSeen,  // no card since boot
  Working,    // mounted and answering
  Removed,    // was working, now gone
  Failed,     // a card answers but won't mount
};

enum class SdAction : uint8_t { None, Mount, Probe, Unmount };
enum class MountOutcome : uint8_t { Ok, NoCard, Unreadable };
enum class SdIcon : uint8_t { None, Green, Red };

class SdMonitor {
public:
  static constexpr uint32_t kProbeMs = 2000;       // while working
  static constexpr uint32_t kRetryMs = 3000;       // while no card answers
  static constexpr uint32_t kBackoffMaxMs = 60000; // a card that keeps failing

  // At boot, before the first poll: a card has worked in this radio before
  // (a saved flag; owner pick A, 2026-10-21). Until one answers, the slot
  // then reads as a card that's gone (red), not one never fitted (nothing).
  void remember(bool card_used);

  // What to do now, if anything. Asking is due at boot.
  SdAction poll(uint32_t now_ms);
  // `took_ms`: how long the attempt held the bus. An empty slot is asked
  // again no sooner than 100x that, so asking costs at most ~1% of the time
  // the display shares the bus with (owner, 2026-10-21: the poll mustn't
  // degrade performance). Cheap attempts keep the 3 s interval.
  void mountResult(MountOutcome r, uint32_t now_ms, uint32_t took_ms = 0);
  void probeResult(bool ok, uint32_t now_ms);
  // After Unmount: the caller has let go of the card.
  void unmounted(uint32_t now_ms);

  SdState state() const { return _state; }
  SdIcon icon() const;
  bool working() const { return _state == SdState::Working; }
  // Bumped on every state change, so a screen redraws only when needed.
  uint32_t revision() const { return _revision; }
  uint32_t retryDelayMs() const { return _delay; }

private:
  void set(SdState s);
  SdState _state = SdState::NeverSeen;
  bool _unmount_due = false;
  bool _asked = false;       // a mount has been tried since boot
  uint32_t _next = 0;        // when to ask next
  uint32_t _delay = kRetryMs;
  uint32_t _revision = 0;
};

// Settings text (I9). Bytes as the page shows them: "28.9 GB", "512 MB".
const char* sdStatusText(SdState s);
void sdSizeText(uint64_t bytes, char* out, unsigned out_len);
// The Settings list's SD card value (mockup 2): the free space while the
// card works ("28.9 GB free"), "Unavailable" once a card has gone, "Can't
// read card" for one that won't mount, "No card" if none was ever seen.
// *alarm: the list shows it in red.
void sdListValue(SdState s, bool usage_ok, uint64_t free_bytes, char* out, unsigned out_len,
                 bool* alarm);

}  // namespace gui
}  // namespace offband
