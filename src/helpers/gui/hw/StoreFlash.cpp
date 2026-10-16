#include "StoreFlash.h"

#include <Arduino.h>
#include <MeshLog.h>

using namespace offband::gui;

#ifndef GUI_STORE_SLICE_BYTES
  // A rebuild's slice: ~43 bytes per ms of SPIFFS write on the V4 (#1458).
  #define GUI_STORE_SLICE_BYTES 512
#endif

static constexpr uint32_t kWriteDelayMs = 5000;   // bundles a burst of changes

static const char* const kIndexPath = "/msgidx";
static const char* const kRingPath = "/msgring";
static const char* const kIndexTmp = "/msgidx.tmp";
static const char* const kRingTmp = "/msgring.tmp";
static const char* const kIndexBad = "/msgidx.bad";
static const char* const kRingBad = "/msgring.bad";

namespace {

class FileSource : public ByteSource {
public:
  explicit FileSource(File& f) : _f(f) {}
  size_t read(void* data, size_t len) override { return _f ? _f.read(static_cast<uint8_t*>(data), len) : 0; }

private:
  File& _f;
};

bool due(uint32_t now, uint32_t at) { return int32_t(now - at) >= 0; }

}  // namespace

bool StoreFlash::begin(FILESYSTEM* fs, MsgStore* store) {
  _fs = fs;
  _store = store;
  if (!_fs || !_store || !_store->ready()) return false;
  const size_t need = _store->ringFileBytes() > _store->indexFileBytes() ? _store->ringFileBytes()
                                                                          : _store->indexFileBytes();
  return _snap.reserve(need);
}

// A rebuild cut off between removing the old file and renaming the new one
// leaves only the complete temp file.
static void recover(FILESYSTEM* fs, const char* path, const char* tmp) {
  if (!fs->exists(path) && fs->exists(tmp)) fs->rename(tmp, path);
}

// Keeps a file that didn't load aside, unread, so nothing overwrites it. If
// the rename fails it stays where it is and the next rebuild replaces it.
static void keepAside(FILESYSTEM* fs, const char* path, const char* bad) {
  fs->remove(bad);
  if (!fs->rename(path, bad)) {
    mesh_log_line(MLOG_BOOT, "[store] could not keep %s aside; the next save replaces it\n", path);
  }
}

static LoadResult worse(LoadResult a, LoadResult b) {
  if (a == LoadResult::UnknownVersion || b == LoadResult::UnknownVersion) return LoadResult::UnknownVersion;
  if (a == LoadResult::BadHeader || b == LoadResult::BadHeader) return LoadResult::BadHeader;
  if (a == LoadResult::Ok || b == LoadResult::Ok) return LoadResult::Ok;
  return LoadResult::Empty;
}

// Loads one file. Anything but a good file of exactly the expected size is
// rebuilt whole at the first write.
static LoadResult loadOne(FILESYSTEM* fs, const char* path, const char* bad, size_t expect, bool ring,
                          MsgStore* store, bool* rebuild) {
  *rebuild = true;
  if (!fs->exists(path)) return LoadResult::Empty;
  File f = fs->open(path, "r");
  const size_t size = f ? f.size() : 0;
  FileSource src(f);
  const LoadResult r = ring ? store->loadRing(src) : store->loadIndex(src);
  f.close();
  if (r == LoadResult::BadHeader || r == LoadResult::UnknownVersion) {
    keepAside(fs, path, bad);
  } else if (r == LoadResult::Ok && size == expect) {
    *rebuild = false;   // in place from here on
  }
  return r;
}

LoadResult StoreFlash::load() {
  if (!_fs || !_store || !_store->ready()) return LoadResult::Empty;
  recover(_fs, kIndexPath, kIndexTmp);
  recover(_fs, kRingPath, kRingTmp);
  const LoadResult idx =
      loadOne(_fs, kIndexPath, kIndexBad, _store->indexFileBytes(), false, _store, &_rebuild_index);
  // With no index, every message is dropped as belonging to no thread.
  const LoadResult ring =
      loadOne(_fs, kRingPath, kRingBad, _store->ringFileBytes(), true, _store, &_rebuild_ring);
  _last_load = worse(idx, ring);
  if (_rebuild_index || _rebuild_ring) changed(millis());   // lay the files out once
  mesh_log_line(MLOG_BOOT, "[store] load index=%d ring=%d threads=%u msgs=%u rebuild=%d%d\n", int(idx),
                int(ring), (unsigned)_store->threadCount(), (unsigned)_store->count(), _rebuild_index ? 1 : 0,
                _rebuild_ring ? 1 : 0);
  return _last_load;
}

void StoreFlash::changed(uint32_t now_ms) {
  if (_due == 0) _due = (now_ms + kWriteDelayMs) | 1;   // never 0: 0 means nothing pending
  if (_pending_since == 0) _pending_since = now_ms | 1;
}

// ------------------------------------------------------------ whole rebuilds

bool StoreFlash::startRebuild(Which w) {
  _snap.clear();
  const bool ring = (w == File_Ring);
  if (!(ring ? _store->saveRing(_snap) : _store->saveIndex(_snap))) return false;
  _store->clearDirty(ring);   // the snapshot holds every entry
  _tmp_file = _fs->open(ring ? kRingTmp : kIndexTmp, "w", true);
  if (!_tmp_file) return false;
  _state = Rebuilding;
  _rebuilding = w;
  _slices.begin(_snap.size(), GUI_STORE_SLICE_BYTES);
  _t0 = millis();
  _step_max_ms = 0;
  return true;
}

bool StoreFlash::finishRebuild(bool commit) {
  const bool ring = (_rebuilding == File_Ring);
  _state = Idle;
  _slices.cancel();
  _tmp_file.close();
  const char* path = ring ? kRingPath : kIndexPath;
  const char* tmp = ring ? kRingTmp : kIndexTmp;
  bool ok = false;
  if (commit) {
    // SPIFFS rename won't replace a file: the old one goes first (see recover()).
    _fs->remove(path);
    ok = _fs->rename(tmp, path);
  } else {
    _fs->remove(tmp);
  }
  if (ok) {
    if (ring) _rebuild_ring = false;
    else _rebuild_index = false;
  }
#if defined(OFFBAND_MEM_TELEMETRY)
  mesh_log_line(MLOG_BOOT, "[mem] store_rebuild file=%s bytes=%u ms=%lu step_max_ms=%lu ok=%d\n",
                ring ? "ring" : "index", (unsigned)_snap.size(), (unsigned long)(millis() - _t0),
                (unsigned long)_step_max_ms, ok ? 1 : 0);
#endif
  return ok;
}

// --------------------------------------------------------- in-place writes

bool StoreFlash::openInPlace() {
  _idx_file = _fs->open(kIndexPath, "r+");
  _ring_file = _fs->open(kRingPath, "r+");
  if (!_idx_file || !_ring_file) {
    closeInPlace();
    return false;
  }
  _state = InPlace;
  _t0 = millis();
  _bytes = _writes = _step_max_ms = 0;
  return true;
}

void StoreFlash::closeInPlace() {
  _idx_file.close();
  _ring_file.close();
  _state = Idle;
}

bool StoreFlash::writeOne(const DirtyWrite& w) {
  File& f = w.ring ? _ring_file : _idx_file;
  if (!f.seek(w.offset, SeekSet)) return false;
  return f.write(static_cast<const uint8_t*>(w.data), w.len) == w.len;
}

// ------------------------------------------------------------------- loop

void StoreFlash::loop(uint32_t now_ms, bool fs_busy, bool screen_idle) {
  if (!_fs || !_store || fs_busy) return;
  const bool held_too_long = _pending_since && now_ms - _pending_since >= kMaxHoldMs;
  if (!screen_idle && !held_too_long) return;   // a write now would hitch the screen
  if (_state == Idle && _due == 0) _pending_since = 0;   // all caught up

  if (_state == Rebuilding) {
    size_t from, to;
    if (_slices.next(&from, &to)) {
      const uint32_t t = millis();
      const size_t len = to - from;
      const bool ok = _tmp_file.write(_snap.data() + from, len) == len;
      const uint32_t ms = millis() - t;
      if (ms > _step_max_ms) _step_max_ms = ms;
      if (!ok) {
        finishRebuild(false);
        changed(now_ms);   // try again later
      }
      return;
    }
    if (!finishRebuild(true)) changed(now_ms);
    return;
  }

  if (_state == InPlace) {
    DirtyWrite w;
    if (_store->nextDirty(&w)) {
      const uint32_t t = millis();
      if (!writeOne(w)) {
        // The file isn't what we think it is: lay both out again.
        closeInPlace();
        _rebuild_index = _rebuild_ring = true;
        changed(now_ms);
        return;
      }
      const uint32_t ms = millis() - t;
      if (ms > _step_max_ms) _step_max_ms = ms;
      _bytes += w.len;
      _writes++;
      return;
    }
    closeInPlace();
    if (_store->anyDirty()) changed(now_ms);   // a change that came in at the very end
#if defined(OFFBAND_MEM_TELEMETRY)
    mesh_log_line(MLOG_BOOT, "[mem] store_save writes=%lu bytes=%lu ms=%lu step_max_ms=%lu\n",
                  (unsigned long)_writes, (unsigned long)_bytes, (unsigned long)(millis() - _t0),
                  (unsigned long)_step_max_ms);
#endif
    return;
  }

  if (_due == 0 || !due(now_ms, _due)) return;
  _due = 0;   // a change from here on schedules the next burst
  if (_rebuild_index) {
    if (!startRebuild(File_Index)) changed(now_ms);
    else if (_rebuild_ring || _store->anyDirty()) changed(now_ms);   // more to do after this
    return;
  }
  if (_rebuild_ring) {
    if (!startRebuild(File_Ring)) changed(now_ms);
    else if (_store->anyDirty()) changed(now_ms);
    return;
  }
  if (_store->anyDirty() && !openInPlace()) {
    _rebuild_index = _rebuild_ring = true;   // a file went missing
    changed(now_ms);
  }
}

void StoreFlash::flushNow() {
  if (!_fs || !_store) return;
  if (_state == Rebuilding) {
    size_t from, to;
    bool ok = true;
    while (ok && _slices.next(&from, &to)) ok = _tmp_file.write(_snap.data() + from, to - from) == to - from;
    finishRebuild(ok);
  }
  for (int i = 0; i < 2 && (_rebuild_index || _rebuild_ring); i++) {
    if (!startRebuild(_rebuild_index ? File_Index : File_Ring)) return;
    size_t from, to;
    bool ok = true;
    while (ok && _slices.next(&from, &to)) ok = _tmp_file.write(_snap.data() + from, to - from) == to - from;
    finishRebuild(ok);
  }
  if (_state != InPlace && _store->anyDirty() && !openInPlace()) return;
  DirtyWrite w;
  while (_state == InPlace && _store->nextDirty(&w)) {
    if (!writeOne(w)) break;
  }
  closeInPlace();
  _due = 0;
  _pending_since = 0;
}
