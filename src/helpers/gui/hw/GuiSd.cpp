#include "GuiSd.h"

#if defined(PIN_SD_CS)

#include <stdio.h>
#include <sys/stat.h>

#include <Arduino.h>
#include <MeshLog.h>

using namespace offband::gui;

namespace {

constexpr const char* kDir = "/sd/offband";
constexpr const char* kRecPath = "/sd/offband/msgarc.bin";
constexpr const char* kThrPath = "/sd/offband/msgthr.bin";
// Records copied per loop tick. Each is one 192-byte write plus a header
// flush per slice; small enough that a tick stays short.
constexpr size_t kSlice = 16;

// Moves a file that isn't this radio's archive out of the way. Kept, never
// deleted: it may be someone's history.
void keepAside(const char* path, const char* why) {
  char dst[64];
  for (int i = 0; i < 100; i++) {
    snprintf(dst, sizeof(dst), "%s.%s%d", path, why, i);
    struct stat st;
    if (stat(dst, &st) != 0) {
      // A failed rename leaves the file in place: the archive won't open,
      // the card shows red, and Settings offers Format card.
      if (rename(path, dst) != 0) mesh_log_line(MLOG_BOOT, "[sd] couldn't set %s aside\n", path);
      return;
    }
  }
}

}  // namespace

bool GuiSd::openArchive(MsgStore& live) {
  mkdir(kDir, 0775);
  for (int attempt = 0; attempt < 2; attempt++) {
    if (!_rec.open(kRecPath) || !_thr.open(kThrPath)) return false;
    const ArchiveOpen r = _arch.open(_rec, _thr, _owner);
    mesh_log_line(MLOG_BOOT, "[sd] archive open %d through=%u\n", int(r),
                  unsigned(_arch.archivedThrough()));
    if (r == ArchiveOpen::Ok || r == ArchiveOpen::Fresh) {
      const size_t n = _arch.pending(live);
      _copy_total = n > 0xFFFF ? 0xFFFF : uint16_t(n);
      _copy_done = 0;
      _rev++;
      return true;
    }
    _rec.close();
    _thr.close();
    if (r == ArchiveOpen::IoError) return false;
    const char* why = r == ArchiveOpen::Foreign ? "foreign" : "bad";
    keepAside(kRecPath, why);
    keepAside(kThrPath, why);
  }
  return false;
}

void GuiSd::closeArchive() {
  if (_arch.ready()) {
    _last_through = _arch.archivedThrough();
    _had_card = true;
  }
  _arch.close();
  _rec.close();
  _thr.close();
  if (_copy_total) _rev++;
  _copy_total = 0;
  _copy_done = 0;
}

void GuiSd::cardFailed(uint32_t now_ms) {
  mesh_log_line(MLOG_BOOT, "[sd] card error, treating as pulled\n");
  closeArchive();
  _mon.probeResult(false, now_ms);
}

void GuiSd::noteChanged(uint32_t seq) {
  if (!seq) return;
  for (uint8_t i = 0; i < _changed_n; i++) {
    if (_changed[i] == seq) return;
  }
  if (_changed_n == kChangedMax) {
    for (uint8_t i = 1; i < kChangedMax; i++) _changed[i - 1] = _changed[i];
    _changed_n--;
  }
  _changed[_changed_n++] = seq;
}

void GuiSd::loop(uint32_t now_ms, MsgStore& live) {
  if (!_begun) return;   // no owner tag yet: an archive opened now would be nobody's
  switch (_mon.poll(now_ms)) {
    case SdAction::Mount: {
      const uint32_t t0 = millis();
      const MountOutcome r = _card.mount();
      const uint32_t took = millis() - t0;
      // Bench (owner, 2026-10-21: the poll mustn't degrade performance): the
      // first ask and any slow one go to the log.
      if (!_logged_ask || took > kSlowMs) {
        mesh_log_line(MLOG_BOOT, "[sd] ask: %d in %u ms\n", int(r), unsigned(took));
        _logged_ask = true;
      }
      if (r == MountOutcome::Ok && !openArchive(live)) {
        _card.unmount();
        _mon.mountResult(MountOutcome::Unreadable, now_ms, took);
        return;
      }
      _mon.mountResult(r, now_ms, took);
      if (r == MountOutcome::Ok) _copy_started = now_ms;
      return;
    }
    case SdAction::Probe: {
      const uint32_t t0 = millis();
      const bool ok = _card.probe();
      const uint32_t took = millis() - t0;
      if (took > kSlowMs) mesh_log_line(MLOG_BOOT, "[sd] slow probe: %u ms\n", unsigned(took));
      _mon.probeResult(ok, now_ms);
      return;
    }
    case SdAction::Unmount:
      closeArchive();
      _card.unmount();
      _mon.unmounted(now_ms);
      return;
    case SdAction::None:
      break;
  }
  if (!_mon.working() || !_arch.ready()) return;

  bool ok = true;
  const uint32_t t0 = millis();
  const size_t left = _arch.catchUp(live, kSlice, &ok);
  const uint32_t took = millis() - t0;
  if (_copy_total && took > _slice_max) _slice_max = took;   // reported when the copy ends
  if (!ok) {
    cardFailed(now_ms);
    return;
  }
  if (_copy_total) {
    const uint16_t done = left >= _copy_total ? 0 : uint16_t(_copy_total - left);
    if (done != _copy_done) {
      _copy_done = done;
      _rev++;
    }
    if (left == 0) {
      mesh_log_line(MLOG_BOOT, "[sd] caught up: %u in %u ms, slowest slice %u ms\n", unsigned(_copy_total),
                    unsigned(now_ms - _copy_started), unsigned(_slice_max));
      _slice_max = 0;
      _copy_total = 0;
      _copy_done = 0;
      _rev++;
    }
    return;   // finish the catch-up before rewriting changes
  }
  if (_changed_n) {
    const uint32_t seq = _changed[0];
    for (uint8_t i = 1; i < _changed_n; i++) _changed[i - 1] = _changed[i];
    _changed_n--;
    if (!_arch.update(live, seq)) cardFailed(now_ms);
  }
}

uint32_t GuiSd::messagesOnCard() const { return _arch.ready() ? _arch.stored() : 0; }

uint16_t GuiSd::waiting(const MsgStore& live) const {
  if (_arch.ready() || !_had_card) return 0;
  return live.countAfter(_last_through);
}

bool GuiSd::format(uint32_t now_ms, MsgStore& live) {
  closeArchive();
  const bool ok = _card.format();
  if (ok) {
    _mon.mountResult(openArchive(live) ? MountOutcome::Ok : MountOutcome::Unreadable, now_ms);
    _copy_started = now_ms;
  }
  _rev++;
  return ok;
}

#endif  // PIN_SD_CS
