#pragma once

// The SD card's message archive (Epic I #1433, I4 #1678 and I8 #1704).
//
// Owner, 2026-10-21, option B: flash keeps the newest 1,000 messages as it
// always has (the live MsgStore, persisted by StoreFlash), and the card holds
// the full history, up to kArchiveSlots. So the archive never feeds the GUI
// directly and a pulled card never empties a chat; it only ever catches up
// with the live store.
//
// Records are MsgRecords, slot `seq % slots` of a fixed-size file, so each
// one says which message it is and can be rewritten in place when its
// delivery state or echo count changes. Threads get their own table keyed by
// the thread's key, so a live thread slot that is later reused can't relabel
// what's already on the card. The header's `archived_through` is written
// after the records it covers: an interrupted catch-up never claims messages
// the card doesn't have.
//
// Pure: no Arduino, no SD driver. The caller hands in two RandomFiles.

#include <stddef.h>
#include <stdint.h>

#include "MsgStore.h"

namespace offband {
namespace gui {

constexpr uint16_t kArchiveSlots = 5000;     // PLAN.md §9
constexpr uint16_t kArchiveThreads = 256;
constexpr uint16_t kArchiveVersion = 1;
constexpr size_t kArchiveHeaderLen = 16;

// A file the archive can seek in. A file on the card in practice, memory in
// the tests.
class RandomFile {
public:
  virtual bool readAt(uint32_t offset, void* data, size_t len) = 0;
  virtual bool writeAt(uint32_t offset, const void* data, size_t len) = 0;
  virtual uint32_t size() = 0;
  virtual bool flush() = 0;
  virtual ~RandomFile() {}
};

struct ArchiveThread {
  uint8_t kind;                 // ThreadKind; 0 = free slot
  uint8_t reserved[3];
  uint8_t key[kThreadKeyLen];
  char name[kThreadNameLen];    // NUL-terminated
  uint32_t crc;                 // over the entry with this field 0
};
static_assert(sizeof(ArchiveThread) == 72, "the on-card thread entry is 72 bytes");

enum class ArchiveOpen : uint8_t {
  Ok,         // an archive was there and is in use
  Fresh,      // empty files: a new archive was laid out
  BadHeader,  // not an archive, the wrong layout, or cut short: the caller keeps it aside
  Foreign,    // another radio's archive: the caller keeps it aside, never appends to it
  IoError,    // the card stopped answering
};

class SdArchive {
public:
  // Opens or lays out the archive in `records` and `threads`. `owner` names
  // this radio (from its public key), so a card moved from another radio is
  // never mixed into this one's history. Nothing else works until this
  // returns Ok or Fresh.
  ArchiveOpen open(RandomFile& records, RandomFile& threads, uint32_t owner,
                   uint16_t slots = kArchiveSlots, uint16_t max_threads = kArchiveThreads);
  void close();
  bool ready() const { return _rec != nullptr; }
  ~SdArchive();

  // After open: a live store whose numbering restarted behind the card (its
  // flash files were lost) is moved ahead of it, or nothing new would ever
  // reach the card. Returns true if it had to.
  bool alignLive(MsgStore& live) const;

  // The highest seq the card holds; 0 for an empty archive.
  uint32_t archivedThrough() const { return _through; }

  // Live messages the card doesn't have yet (seq > archivedThrough).
  size_t pending(const MsgStore& live) const;

  // Copies up to `budget` of them, oldest first, then commits the header.
  // Returns how many are still pending. The first boot with a card and a
  // card coming back are both just this; a card error is reported through
  // `ok`. It realigns a restarted live store first (alignLive), so a caller
  // can't forget to.
  //
  // The header never covers a record whose write failed. It does move past a
  // message that will never reach the card: one evicted while the card was
  // out, one whose thread is gone, or one with no room in a full thread
  // table. Those are skipped, not waited on.
  size_t catchUp(MsgStore& live, size_t budget, bool* ok = nullptr);

  // Rewrites one archived record from the live store (a delivery or echo
  // change). False if the card failed; true if there was nothing to do.
  bool update(const MsgStore& live, uint32_t seq);

  // Reads one archived message back. False if that seq isn't on the card.
  bool read(uint32_t seq, MsgRecord* out, ArchiveThread* thread = nullptr);

  uint16_t slots() const { return _slots; }
  uint32_t recordsFileBytes() const;
  uint32_t threadsFileBytes() const;

private:
  bool writeHeader();
  static constexpr int kThreadTableFull = -1;
  static constexpr int kThreadIoError = -2;
  // The archive's thread index, made if new, or one of the two above.
  int threadFor(const Thread& t);
  bool writeRecord(const MsgRecord& m, uint16_t archive_thread);

  RandomFile* _rec = nullptr;
  RandomFile* _thr = nullptr;
  // The thread table, read once at open and written through: a lookup per
  // archived record must not read the whole table off the card.
  ArchiveThread* _threads = nullptr;
  uint16_t _slots = 0;
  uint16_t _max_threads = 0;
  uint32_t _through = 0;
};

}  // namespace gui
}  // namespace offband
