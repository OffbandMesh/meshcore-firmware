#pragma once

// The message store's flash backend (#1463, PLAN.md §3 C3). Two files on the
// SPIFFS partition: /msgidx (threads and unread counts, written 5 s after a
// change) and /msgring (the messages, written lazily, 60 s after a change).
//
// A write never blocks the loop for long: the store is serialized into a PSRAM
// buffer in one fast pass (a consistent snapshot), then written to a temp file
// GUI_STORE_SLICE_BYTES per loop tick and swapped in once complete, as the
// contacts save does (#1458). A file that loads badly is renamed aside as
// .bad and the store starts empty (PLAN.md §8).

#include <stdint.h>

#include <helpers/IdentityStore.h>   // FILESYSTEM
#include <helpers/gui/ChunkedSave.h>
#include <helpers/gui/MsgStore.h>

class StoreFlash {
public:
  // Allocates the snapshot buffer (PSRAM). False if it can't; the store
  // then lives in RAM only and is lost at reset.
  bool begin(FILESYSTEM* fs, offband::gui::MsgStore* store);

  // Reads both files at boot. Returns the worse of the two results; a bad
  // file is kept aside and the store starts empty.
  offband::gui::LoadResult load();
  offband::gui::LoadResult lastLoad() const { return _last_load; }

  // Something changed: schedule the file's next write.
  void indexChanged(uint32_t now_ms);
  void ringChanged(uint32_t now_ms);   // a ring change changes the index too

  // A slice per call. `fs_busy` (a contacts save is running) defers it, so
  // the two never write in the same tick.
  void loop(uint32_t now_ms, bool fs_busy);

  bool busy() const { return _writing != File_None; }
  // Writes whatever is pending, in one pass: before a reboot.
  void flushNow();

private:
  enum Which : uint8_t { File_None, File_Index, File_Ring };
  bool startWrite(Which w);
  bool finishWrite(bool commit);
  void writeAll(Which w);

  FILESYSTEM* _fs = nullptr;
  offband::gui::MsgStore* _store = nullptr;
  offband::gui::BufferSink _snap;
  offband::gui::ChunkedSave _slices;
  offband::gui::LoadResult _last_load = offband::gui::LoadResult::Empty;
  Which _writing = File_None;
  uint32_t _index_due = 0;   // 0: nothing pending
  uint32_t _ring_due = 0;
  uint32_t _slice_max_ms = 0;
  uint32_t _write_t0 = 0;
};
