#include "StoreFlash.h"

#include <Arduino.h>
#include <MeshLog.h>

using namespace offband::gui;

#ifndef GUI_STORE_SLICE_BYTES
  // ~43 bytes per ms of SPIFFS write on the V4 (#1458's contacts save), so a
  // slice stays near 12 ms.
  #define GUI_STORE_SLICE_BYTES 512
#endif

static constexpr uint32_t kIndexDelayMs = 5000;   // threads and unread: soon
static constexpr uint32_t kRingDelayMs = 60000;   // the messages: lazily

static const char* const kIndexPath = "/msgidx";
static const char* const kRingPath = "/msgring";
static const char* const kIndexTmp = "/msgidx.tmp";
static const char* const kRingTmp = "/msgring.tmp";
static const char* const kIndexBad = "/msgidx.bad";
static const char* const kRingBad = "/msgring.bad";

// The open temp file, kept across loop ticks while a write is sliced.
static File s_file;

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
  const size_t need = _store->ringBytesMax() > _store->indexBytesMax() ? _store->ringBytesMax()
                                                                        : _store->indexBytesMax();
  return _snap.reserve(need);
}

// A swap cut off between removing the old file and renaming the new one
// leaves only the complete temp file.
static void recover(FILESYSTEM* fs, const char* path, const char* tmp) {
  if (!fs->exists(path) && fs->exists(tmp)) fs->rename(tmp, path);
}

// Keeps a file that didn't load aside, unread, so nothing overwrites it. If
// the rename fails it stays where it is and the next save replaces it: say so.
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

LoadResult StoreFlash::load() {
  if (!_fs || !_store || !_store->ready()) return LoadResult::Empty;
  recover(_fs, kIndexPath, kIndexTmp);
  recover(_fs, kRingPath, kRingTmp);

  LoadResult idx = LoadResult::Empty;
  if (_fs->exists(kIndexPath)) {
    File f = _fs->open(kIndexPath, "r");
    FileSource src(f);
    idx = _store->loadIndex(src);
    f.close();
    if (idx == LoadResult::BadHeader || idx == LoadResult::UnknownVersion) keepAside(_fs, kIndexPath, kIndexBad);
  }
  LoadResult ring = LoadResult::Empty;
  if (_fs->exists(kRingPath)) {
    File f = _fs->open(kRingPath, "r");
    FileSource src(f);
    ring = _store->loadRing(src);   // with no index, every record is dropped
    f.close();
    if (ring == LoadResult::BadHeader || ring == LoadResult::UnknownVersion) keepAside(_fs, kRingPath, kRingBad);
  }
  _last_load = worse(idx, ring);
  mesh_log_line(MLOG_BOOT, "[store] load index=%d ring=%d threads=%u msgs=%u\n", int(idx), int(ring),
                (unsigned)_store->threadCount(), (unsigned)_store->count());
  return _last_load;
}

void StoreFlash::indexChanged(uint32_t now_ms) {
  if (_index_due == 0) _index_due = (now_ms + kIndexDelayMs) | 1;   // never 0: 0 means nothing pending
}

void StoreFlash::ringChanged(uint32_t now_ms) {
  indexChanged(now_ms);
  if (_ring_due == 0) _ring_due = (now_ms + kRingDelayMs) | 1;
}

bool StoreFlash::startWrite(Which w) {
  _snap.clear();
  const bool ok = (w == File_Index) ? _store->saveIndex(_snap) : _store->saveRing(_snap);
  if (!ok) return false;
  s_file = _fs->open(w == File_Index ? kIndexTmp : kRingTmp, "w", true);
  if (!s_file) return false;
  _writing = w;
  _slices.begin(_snap.size(), GUI_STORE_SLICE_BYTES);
  _slice_max_ms = 0;
  _write_t0 = millis();
  return true;
}

bool StoreFlash::finishWrite(bool commit) {
  const Which w = _writing;
  _writing = File_None;
  _slices.cancel();
  s_file.close();
  const char* path = (w == File_Index) ? kIndexPath : kRingPath;
  const char* tmp = (w == File_Index) ? kIndexTmp : kRingTmp;
  bool ok = false;
  if (commit) {
    // SPIFFS rename won't replace a file: the old one goes first (see recover()).
    _fs->remove(path);
    ok = _fs->rename(tmp, path);
  } else {
    _fs->remove(tmp);
  }
#if defined(OFFBAND_MEM_TELEMETRY)
  mesh_log_line(MLOG_BOOT, "[mem] store_save file=%s bytes=%u ms=%lu slice_max_ms=%lu ok=%d\n",
                w == File_Index ? "index" : "ring", (unsigned)_snap.size(),
                (unsigned long)(millis() - _write_t0), (unsigned long)_slice_max_ms, ok ? 1 : 0);
#endif
  return ok;
}

void StoreFlash::loop(uint32_t now_ms, bool fs_busy) {
  if (!_fs || !_store || fs_busy) return;

  if (_writing != File_None) {
    size_t from, to;
    if (_slices.next(&from, &to)) {
      const uint32_t t = millis();
      const size_t len = to - from;
      const bool ok = s_file.write(_snap.data() + from, len) == len;
      const uint32_t ms = millis() - t;
      if (ms > _slice_max_ms) _slice_max_ms = ms;
      if (!ok) {
        const Which w = _writing;
        finishWrite(false);
        if (w == File_Index) indexChanged(now_ms);   // try again later
        else ringChanged(now_ms);
      }
      return;
    }
    const Which w = _writing;
    if (!finishWrite(true)) {
      if (w == File_Index) indexChanged(now_ms);
      else ringChanged(now_ms);
    }
    return;
  }

  // The index first: it is small and holds the unread counts.
  if (_index_due && due(now_ms, _index_due)) {
    _index_due = 0;   // a change during the write schedules the next one
    if (!startWrite(File_Index)) indexChanged(now_ms);
  } else if (_ring_due && due(now_ms, _ring_due)) {
    _ring_due = 0;
    if (!startWrite(File_Ring)) ringChanged(now_ms);
  }
}

void StoreFlash::writeAll(Which w) {
  if (!startWrite(w)) return;
  size_t from, to;
  while (_slices.next(&from, &to)) {
    if (s_file.write(_snap.data() + from, to - from) != to - from) {
      finishWrite(false);
      return;
    }
  }
  finishWrite(true);
}

void StoreFlash::flushNow() {
  if (!_fs || !_store) return;
  if (_writing != File_None) {
    // The snapshot in progress may predate the latest change: drop it and
    // write both files fresh.
    finishWrite(false);
    _index_due = _ring_due = 1;
  }
  if (_index_due) {
    _index_due = 0;
    writeAll(File_Index);
  }
  if (_ring_due) {
    _ring_due = 0;
    writeAll(File_Ring);
  }
}
