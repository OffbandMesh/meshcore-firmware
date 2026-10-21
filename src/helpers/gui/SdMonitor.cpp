#include "SdMonitor.h"

#include <stdio.h>

namespace offband {
namespace gui {

namespace {
// Wrap-safe "has `now` reached `due`".
bool reached(uint32_t now, uint32_t due) { return int32_t(now - due) >= 0; }
}  // namespace

void SdMonitor::set(SdState s) {
  if (s == _state) return;
  _state = s;
  _revision++;
}

void SdMonitor::remember(bool card_used) {
  if (card_used && _state == SdState::NeverSeen) set(SdState::Removed);
}

SdAction SdMonitor::poll(uint32_t now_ms) {
  if (_unmount_due) return SdAction::Unmount;
  if (!_asked) return SdAction::Mount;   // boot: ask straight away
  if (!reached(now_ms, _next)) return SdAction::None;
  return _state == SdState::Working ? SdAction::Probe : SdAction::Mount;
}

void SdMonitor::mountResult(MountOutcome r, uint32_t now_ms, uint32_t took_ms) {
  _asked = true;
  switch (r) {
    case MountOutcome::Ok:
      _delay = kRetryMs;
      set(SdState::Working);
      _next = now_ms + kProbeMs;
      return;
    case MountOutcome::NoCard:
      // Nothing answers: an empty slot costs little to ask, so no backoff,
      // unless asking turned out slow, when the interval stretches to keep it
      // near 1% of the time. A card that was failing and is now gone resets
      // its backoff.
      _delay = kRetryMs;
      if (took_ms > kRetryMs / 100) {
        _delay = took_ms >= kBackoffMaxMs / 100 ? kBackoffMaxMs : took_ms * 100;
      }
      if (_state == SdState::Failed) set(SdState::Removed);
      _next = now_ms + _delay;
      return;
    case MountOutcome::Unreadable:
      // A card answers but won't mount: back off, never a tight loop.
      if (_state == SdState::Failed) {
        _delay = _delay >= kBackoffMaxMs / 2 ? kBackoffMaxMs : _delay * 2;
      } else {
        _delay = kRetryMs;
      }
      set(SdState::Failed);
      _next = now_ms + _delay;
      return;
  }
}

void SdMonitor::probeResult(bool ok, uint32_t now_ms) {
  if (_state != SdState::Working) return;
  if (ok) {
    _next = now_ms + kProbeMs;
    return;
  }
  _unmount_due = true;
}

void SdMonitor::unmounted(uint32_t now_ms) {
  if (!_unmount_due && _state != SdState::Working) return;   // already handled
  _unmount_due = false;
  _delay = kRetryMs;
  set(SdState::Removed);
  _next = now_ms + kRetryMs;
}

SdIcon SdMonitor::icon() const {
  switch (_state) {
    case SdState::Working: return SdIcon::Green;
    case SdState::Removed:
    case SdState::Failed: return SdIcon::Red;
    case SdState::NeverSeen: break;
  }
  return SdIcon::None;
}

const char* sdStatusText(SdState s) {
  switch (s) {
    case SdState::Working: return "Working";
    case SdState::Removed: return "Not detected";
    case SdState::Failed: return "Can't read card";
    case SdState::NeverSeen: break;
  }
  return "No card";
}

void sdListValue(SdState s, bool usage_ok, uint64_t free_bytes, char* out, unsigned out_len,
                 bool* alarm) {
  if (alarm) *alarm = s == SdState::Removed || s == SdState::Failed;
  if (!out || out_len == 0) return;
  switch (s) {
    case SdState::Working:
      if (usage_ok) {
        char size[16];
        sdSizeText(free_bytes, size, sizeof size);
        snprintf(out, out_len, "%s free", size);
      } else {
        snprintf(out, out_len, "%s", sdStatusText(s));
      }
      return;
    case SdState::Removed: snprintf(out, out_len, "Unavailable"); return;
    case SdState::Failed:
    case SdState::NeverSeen: break;
  }
  snprintf(out, out_len, "%s", sdStatusText(s));
}

// Decimal units, as cards are labeled: a 32 GB card reads about 31.8 GB.
void sdSizeText(uint64_t bytes, char* out, unsigned out_len) {
  if (!out || out_len == 0) return;
  // Rounded to the nearest MB first, so 999.6 MB reads "1.0 GB", not "1000 MB".
  if ((bytes + 500000ULL) / 1000000ULL >= 1000ULL) {
    const uint64_t tenths = (bytes + 50000000ULL) / 100000000ULL;
    snprintf(out, out_len, "%u.%u GB", unsigned(tenths / 10), unsigned(tenths % 10));
  } else {
    snprintf(out, out_len, "%u MB", unsigned((bytes + 500000ULL) / 1000000ULL));
  }
}

}  // namespace gui
}  // namespace offband
