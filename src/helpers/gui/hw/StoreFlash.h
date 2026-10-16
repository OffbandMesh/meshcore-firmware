#pragma once

// The message store's flash backend (#1463, #1472). Two SPIFFS files with a
// fixed layout: /msgidx (threads and unread counts) and /msgring (the
// messages). Entry i sits at a fixed offset, so a change rewrites only its
// own entry in place, one entry (at most 192 bytes) per loop tick, starting
// 5 s after the first change of a burst.
//
// A file is written whole only when it is missing or the wrong size: the
// first boot, or after a bad load. That rebuild goes through a PSRAM
// snapshot written to a temp file a slice per tick, then swapped in. A file
// that loads badly is renamed aside as .bad and the store starts empty
// (PLAN.md §8).

#include <stdint.h>

#include <FS.h>
#include <helpers/IdentityStore.h>   // FILESYSTEM
#include <helpers/gui/ChunkedSave.h>
#include <helpers/gui/MsgStore.h>

class StoreFlash {
public:
  // Allocates the rebuild buffer (PSRAM). False if it can't; the store then
  // lives in RAM only and is lost at reset.
  bool begin(FILESYSTEM* fs, offband::gui::MsgStore* store);

  // Reads both files at boot. Returns the worse of the two results; a bad
  // file is kept aside and the store starts empty.
  offband::gui::LoadResult load();
  offband::gui::LoadResult lastLoad() const { return _last_load; }

  // The store changed: write what changed soon. Both names do the same.
  void indexChanged(uint32_t now_ms) { changed(now_ms); }
  void ringChanged(uint32_t now_ms) { changed(now_ms); }

  // One write per call. `fs_busy` (a contacts save is running) defers it, so
  // the two never write in the same tick.
  void loop(uint32_t now_ms, bool fs_busy);

  bool busy() const { return _state != Idle; }
  // Writes whatever is pending, in one pass: before a reboot.
  void flushNow();

private:
  enum State : uint8_t { Idle, Rebuilding, InPlace };
  enum Which : uint8_t { File_Index, File_Ring };
  void changed(uint32_t now_ms);
  bool startRebuild(Which w);
  bool finishRebuild(bool commit);
  bool openInPlace();
  void closeInPlace();
  bool writeOne(const offband::gui::DirtyWrite& w);

  FILESYSTEM* _fs = nullptr;
  offband::gui::MsgStore* _store = nullptr;
  offband::gui::BufferSink _snap;
  offband::gui::ChunkedSave _slices;
  offband::gui::LoadResult _last_load = offband::gui::LoadResult::Empty;
  State _state = Idle;
  Which _rebuilding = File_Index;
  bool _rebuild_index = false;   // the file is missing or the wrong size
  bool _rebuild_ring = false;
  uint32_t _due = 0;             // 0: nothing pending
  File _idx_file;
  File _ring_file;
  File _tmp_file;
  // Telemetry for one burst of in-place writes, or one rebuild.
  uint32_t _t0 = 0;
  uint32_t _bytes = 0;
  uint32_t _writes = 0;
  uint32_t _step_max_ms = 0;
};
