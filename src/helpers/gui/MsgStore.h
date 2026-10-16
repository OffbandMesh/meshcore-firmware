#pragma once

// The on-device message store (#1427, PLAN.md §3 Epic C): every message the
// node sends or receives, kept on the device so the phone download no longer
// means the device forgets.
//
// Threads are channels, DMs and rooms, each identified by a key that doesn't
// change when the phone renames or re-sorts things (a contact's public key, a
// channel's secret), as in BadgeStore. Messages live in one table of fixed
// 192-byte records, in PSRAM. When it's full (owner, 2026-10-15): the oldest
// message of the busiest channel or room goes; a DM goes only when no channel
// or room messages are left, and then from the busiest DM thread. Unread
// counts live with the thread and outlive evicted messages.
//
// Persistence goes through ByteSink / ByteSource, so the flash (C3) and SD
// (I4) backends only open files; the format lives here. Each file starts with
// a versioned header. A file with a bad header is reported, never trusted: the
// caller keeps it aside and starts empty (PLAN.md §8).
//
// Pure: no Arduino and no mesh. The caller passes times in.

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

constexpr size_t kMsgTextMax = 160;    // D5 decision 11
constexpr size_t kMsgSenderLen = 14;   // a channel message's sender, with terminator
constexpr size_t kThreadKeyLen = 32;   // a public key; a channel secret is zero-padded
constexpr size_t kThreadNameLen = 32;
// 2: fixed slot layout and a CRC per entry (#1472, #1427). Version 1 files
// (never released) are kept aside unread.
constexpr uint16_t kMsgStoreVersion = 2;

enum class ThreadKind : uint8_t { Empty = 0, Channel, Direct, Room };
enum class Delivery : uint8_t { None = 0, Sending, Delivered, Failed };

// One message. Fixed size so the ring can be written record by record.
struct MsgRecord {
  uint32_t seq;      // 0: a free slot. Later messages have larger numbers.
  uint32_t time;     // RTC seconds when it came or went
  uint16_t thread;   // index into the thread table
  uint8_t own;       // 1: sent by this node
  uint8_t delivery;  // Delivery, own messages only
  uint8_t hops;      // 0xFF: direct or not known
  uint8_t echoes;    // own channel sends: repeats heard ("heard xN")
  int8_t snr_q4;     // SNR x 4 when received; 0 when not known
  uint8_t text_len;  // bytes in text, at most kMsgTextMax
  char sender[kMsgSenderLen];  // a channel message's sender, NUL-terminated
  uint16_t crc;                // over the record with this field 0, set as it's written
  char text[kMsgTextMax];      // not NUL-terminated; text_len says how much
};
static_assert(sizeof(MsgRecord) == 192, "the on-disk record is 192 bytes");

struct Thread {
  uint8_t kind;      // ThreadKind
  uint8_t muted;
  uint16_t unread;
  uint32_t last_seq;   // newest message's seq; 0 before any
  uint32_t last_time;
  uint8_t key[kThreadKeyLen];
  char name[kThreadNameLen];   // NUL-terminated
  uint16_t crc;                // over the entry with this field 0, set as it's written
  uint8_t reserved[2];         // zero; room for per-thread flags without a version bump
};
static_assert(sizeof(Thread) == 80, "the on-disk thread entry is 80 bytes");

// Where persistence writes to and reads from: a file in practice, memory in
// tests.
class ByteSink {
public:
  virtual bool write(const void* data, size_t len) = 0;
  virtual ~ByteSink() {}
};
class ByteSource {
public:
  // Bytes read, which is less than len only at the end.
  virtual size_t read(void* data, size_t len) = 0;
  virtual ~ByteSource() {}
};

// A sink into one buffer allocated once (#1463): the store is serialized into
// it in one fast pass, a consistent snapshot, and the flash backend then
// writes it out a slice per loop tick.
class BufferSink : public ByteSink {
public:
  bool reserve(size_t capacity);   // false if it can't be allocated
  void clear() { _len = 0; }
  bool write(const void* data, size_t len) override;   // false when full
  const uint8_t* data() const { return _buf; }
  size_t size() const { return _len; }
  ~BufferSink();

private:
  uint8_t* _buf = nullptr;
  size_t _cap = 0;
  size_t _len = 0;
};

// One in-place write (#1472): `len` bytes of `data` at `offset` in the index
// file (ring == false) or the ring file.
struct DirtyWrite {
  bool ring;
  uint32_t offset;
  const void* data;
  uint16_t len;
};

enum class LoadResult : uint8_t {
  Ok,
  Empty,           // nothing to read (no file yet)
  BadHeader,       // not a store file, or cut short, or sizes that don't match
  UnknownVersion,  // a store file from a newer or older layout
};

class MsgStore {
public:
  // Room for `capacity` messages (1,000 on the V4, PLAN.md §9) and
  // `max_threads` threads. Allocated once; false if it doesn't fit, and the
  // store then holds nothing (PLAN.md §8: history shows as unavailable).
  bool begin(uint16_t capacity, uint16_t max_threads);
  bool ready() const { return _msgs != nullptr; }
  ~MsgStore();

  // The thread for this kind and key, made if new; its name is refreshed
  // either way. -1 when the table is full.
  int thread(ThreadKind kind, const uint8_t* key, size_t key_len, const char* name);
  int find(ThreadKind kind, const uint8_t* key, size_t key_len) const;
  const Thread* threadAt(int t) const;
  uint16_t threadCount() const;
  uint16_t maxThreads() const { return _max_threads; }   // thread slots, live or not

  // A message that arrived; counts as unread. Text over kMsgTextMax is cut at
  // a UTF-8 character boundary. Returns its seq, 0 if not stored.
  uint32_t addIncoming(int t, uint32_t time, const char* sender, const char* text, size_t len,
                       uint8_t hops, int8_t snr_q4);
  // A message this node sent; never unread.
  uint32_t addOwn(int t, uint32_t time, const char* text, size_t len, Delivery d);

  const MsgRecord* msg(uint32_t seq) const;
  // The newest own message in thread t with exactly this text (after the
  // 160-byte cut) that isn't yet delivered, or 0: a resend is the same
  // message (#1464).
  uint32_t findOwn(int t, const char* text, size_t len) const;
  bool setDelivery(uint32_t seq, Delivery d);
  bool addEcho(uint32_t seq);   // an own channel send heard repeated; stops at 255

  // Thread t's messages, oldest first; the newest `max` when there are more.
  // Returns how many seqs went into `out`.
  size_t threadMessages(int t, uint32_t* out, size_t max) const;

  void markRead(int t);
  uint32_t totalUnread() const;
  uint16_t count() const { return _count; }   // messages held
  uint16_t capacity() const { return _cap; }

  // Persistence. Both files have a fixed layout (#1472): a 16-byte header,
  // then one entry per thread slot (index) or message slot (ring), entry i
  // at a fixed offset. A change rewrites only its own entry in place.
  size_t indexFileBytes() const;
  size_t ringFileBytes() const;
  size_t indexBytesMax() const { return indexFileBytes(); }
  size_t ringBytesMax() const { return ringFileBytes(); }

  // In-place writes still owed: changed slots and thread entries, then each
  // touched file's header. Each call hands out one and clears it.
  bool anyDirty() const;
  bool nextDirty(DirtyWrite* out);
  void clearDirty();               // after a full write of both files
  void clearDirty(bool ring);      // after a full write of one

  // Whole files: the first write, a rebuild, and the tests.
  bool saveIndex(ByteSink& out) const;
  LoadResult loadIndex(ByteSource& in);
  bool saveRing(ByteSink& out) const;
  LoadResult loadRing(ByteSource& in);

private:
  bool live(int t) const;
  void release();
  MsgRecord* slotForNew();
  MsgRecord* add(int t, uint32_t time, const char* text, size_t len);
  MsgRecord* ref(uint32_t seq) const;
  void dirtySlot(const MsgRecord* m);
  void dirtyThread(int t);

  MsgRecord* _msgs = nullptr;
  uint8_t* _slot_dirty = nullptr;     // one bit per message slot
  uint8_t* _thread_dirty = nullptr;   // one bit per thread slot
  bool _ring_header_dirty = false;
  bool _index_header_dirty = false;
  uint32_t _dirty_bits = 0;           // slot and thread bits set
  uint16_t _scan_slot = 0;            // nextDirty's resume points
  uint16_t _scan_thread = 0;
  uint8_t _hdr[16];                   // the header nextDirty last handed out
  uint8_t _entry[84];                 // the index entry nextDirty last handed out
  Thread* _threads = nullptr;
  uint16_t* _per_thread = nullptr;   // scratch: message count per thread, for eviction
  uint16_t _cap = 0;
  uint16_t _max_threads = 0;
  uint16_t _count = 0;
  uint32_t _seq = 0;
};

}  // namespace gui
}  // namespace offband
