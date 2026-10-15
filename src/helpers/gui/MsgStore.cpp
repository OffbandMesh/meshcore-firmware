#include "MsgStore.h"

#include <new>
#include <string.h>

#include "../BadgeStore.h"   // utf8Fit: cut text without splitting a character

namespace offband {
namespace gui {

// ------------------------------------------------------------- file headers

// Both files start with: magic, version, entry size, entry count, reserved,
// next seq. Entry size and count guard against a file from a different
// layout being read as this one.
struct FileHeader {
  char magic[4];
  uint16_t version;
  uint16_t entry_size;
  uint16_t count;
  uint16_t reserved;
  uint32_t next_seq;
};
static_assert(sizeof(FileHeader) == 16, "the file header is 16 bytes");

static const char kIndexMagic[4] = {'O', 'B', 'M', 'I'};
static const char kRingMagic[4] = {'O', 'B', 'M', 'R'};

// An index entry is the thread's slot number, then the thread.
struct IndexEntry {
  uint16_t slot;
  uint16_t reserved;
  Thread thread;
};
static_assert(sizeof(IndexEntry) == 84, "an index entry is 84 bytes");

// ----------------------------------------------------------------- lifetime

bool MsgStore::begin(uint16_t capacity, uint16_t max_threads) {
  delete[] _msgs;
  delete[] _threads;
  delete[] _per_thread;
  delete[] _order;
  _msgs = nullptr;
  _threads = nullptr;
  _per_thread = nullptr;
  _order = nullptr;
  _cap = _max_threads = _count = 0;
  _seq = 0;
  if (capacity == 0 || max_threads == 0) return false;

  // Over 4 KB, so on the S3 these come from PSRAM.
  _msgs = new (std::nothrow) MsgRecord[capacity];
  _threads = new (std::nothrow) Thread[max_threads];
  _per_thread = new (std::nothrow) uint16_t[max_threads];
  _order = new (std::nothrow) uint16_t[capacity];
  if (!_msgs || !_threads || !_per_thread || !_order) {
    begin(0, 0);   // frees whatever did come back
    return false;
  }
  memset(_msgs, 0, sizeof(MsgRecord) * capacity);
  memset(_threads, 0, sizeof(Thread) * max_threads);
  _cap = capacity;
  _max_threads = max_threads;
  return true;
}

MsgStore::~MsgStore() {
  delete[] _msgs;
  delete[] _threads;
  delete[] _per_thread;
  delete[] _order;
}

// ------------------------------------------------------------------ threads

bool MsgStore::live(int t) const {
  return _threads && t >= 0 && t < _max_threads && _threads[t].kind != uint8_t(ThreadKind::Empty);
}

static void copyText(char* dst, size_t cap, const char* src) {
  if (!src) src = "";
  const size_t len = offband::utf8Fit(src, strlen(src), cap - 1);
  memcpy(dst, src, len);
  dst[len] = 0;
}

int MsgStore::find(ThreadKind kind, const uint8_t* key, size_t key_len) const {
  if (!_threads || !key || key_len > kThreadKeyLen) return -1;
  uint8_t k[kThreadKeyLen] = {};
  memcpy(k, key, key_len);
  for (int t = 0; t < _max_threads; t++) {
    if (_threads[t].kind == uint8_t(kind) && memcmp(_threads[t].key, k, kThreadKeyLen) == 0) return t;
  }
  return -1;
}

int MsgStore::thread(ThreadKind kind, const uint8_t* key, size_t key_len, const char* name) {
  if (!_threads || !key || key_len > kThreadKeyLen || kind == ThreadKind::Empty) return -1;
  int t = find(kind, key, key_len);
  if (t < 0) {
    for (int i = 0; i < _max_threads; i++) {
      if (_threads[i].kind == uint8_t(ThreadKind::Empty)) {
        t = i;
        break;
      }
    }
    if (t < 0) return -1;
    memset(&_threads[t], 0, sizeof(Thread));
    _threads[t].kind = uint8_t(kind);
    memcpy(_threads[t].key, key, key_len);
  }
  copyText(_threads[t].name, kThreadNameLen, name);
  return t;
}

const Thread* MsgStore::threadAt(int t) const { return live(t) ? &_threads[t] : nullptr; }

uint16_t MsgStore::threadCount() const {
  uint16_t n = 0;
  for (int t = 0; t < _max_threads; t++) {
    if (_threads[t].kind != uint8_t(ThreadKind::Empty)) n++;
  }
  return n;
}

// ----------------------------------------------------------------- messages

// The slot a new message goes in. While there's room, a free one. When full
// (owner, 2026-10-15): the oldest message of the busiest channel or room
// thread; only with none of those left, the oldest of the busiest DM thread.
MsgRecord* MsgStore::slotForNew() {
  if (_count < _cap) {
    for (uint16_t i = 0; i < _cap; i++) {
      if (_msgs[i].seq == 0) return &_msgs[i];
    }
  }
  memset(_per_thread, 0, sizeof(uint16_t) * _max_threads);
  for (uint16_t i = 0; i < _cap; i++) {
    if (_msgs[i].seq != 0 && _msgs[i].thread < _max_threads) _per_thread[_msgs[i].thread]++;
  }
  int victim = -1;
  for (int pass = 0; pass < 2 && victim < 0; pass++) {
    for (int t = 0; t < _max_threads; t++) {
      if (_per_thread[t] == 0) continue;
      const bool direct = _threads[t].kind == uint8_t(ThreadKind::Direct);
      if (direct != (pass == 1)) continue;   // pass 0: channels and rooms; pass 1: DMs
      if (victim < 0 || _per_thread[t] > _per_thread[victim]) victim = t;
    }
  }
  MsgRecord* oldest = nullptr;
  for (uint16_t i = 0; i < _cap; i++) {
    MsgRecord& m = _msgs[i];
    if (m.seq != 0 && m.thread == victim && (!oldest || m.seq < oldest->seq)) oldest = &m;
  }
  if (oldest) _count--;   // the evicted message no longer counts
  return oldest;
}

MsgRecord* MsgStore::add(int t, uint32_t time, const char* text, size_t len) {
  if (!live(t) || !text) return nullptr;
  MsgRecord* m = slotForNew();
  if (!m) return nullptr;
  memset(m, 0, sizeof(*m));
  if (++_seq == 0) _seq = 1;
  m->seq = _seq;
  m->time = time;
  m->thread = uint16_t(t);
  m->hops = 0xFF;
  const size_t n = offband::utf8Fit(text, len, kMsgTextMax);
  memcpy(m->text, text, n);
  m->text_len = uint8_t(n);
  _threads[t].last_seq = m->seq;
  _threads[t].last_time = time;
  _count++;
  return m;
}

uint32_t MsgStore::addIncoming(int t, uint32_t time, const char* sender, const char* text, size_t len,
                               uint8_t hops, int8_t snr_q4) {
  MsgRecord* m = add(t, time, text, len);
  if (!m) return 0;
  copyText(m->sender, kMsgSenderLen, sender);
  m->hops = hops;
  m->snr_q4 = snr_q4;
  if (_threads[t].unread < 0xFFFF) _threads[t].unread++;
  return m->seq;
}

uint32_t MsgStore::addOwn(int t, uint32_t time, const char* text, size_t len, Delivery d) {
  MsgRecord* m = add(t, time, text, len);
  if (!m) return 0;
  m->own = 1;
  m->delivery = uint8_t(d);
  return m->seq;
}

MsgRecord* MsgStore::ref(uint32_t seq) const {
  if (seq == 0 || !_msgs) return nullptr;
  for (uint16_t i = 0; i < _cap; i++) {
    if (_msgs[i].seq == seq) return &_msgs[i];
  }
  return nullptr;
}

const MsgRecord* MsgStore::msg(uint32_t seq) const { return ref(seq); }

bool MsgStore::setDelivery(uint32_t seq, Delivery d) {
  MsgRecord* m = ref(seq);
  if (!m || !m->own) return false;
  m->delivery = uint8_t(d);
  return true;
}

bool MsgStore::addEcho(uint32_t seq) {
  MsgRecord* m = ref(seq);
  if (!m || !m->own) return false;
  if (m->echoes < 0xFF) m->echoes++;
  return true;
}

// Keeps the newest `max` seqs of thread t in `out`, sorted, without a
// full-size buffer: each seq is inserted in order and the oldest drops off.
size_t MsgStore::threadMessages(int t, uint32_t* out, size_t max) const {
  if (!live(t) || !out || max == 0) return 0;
  size_t n = 0;
  for (uint16_t i = 0; i < _cap; i++) {
    const MsgRecord& m = _msgs[i];
    if (m.seq == 0 || m.thread != t) continue;
    if (n == max) {
      if (m.seq < out[0]) continue;   // older than everything kept
      memmove(&out[0], &out[1], sizeof(uint32_t) * (n - 1));
      n--;
    }
    size_t j = n++;
    while (j > 0 && out[j - 1] > m.seq) {
      out[j] = out[j - 1];
      j--;
    }
    out[j] = m.seq;
  }
  return n;
}

void MsgStore::markRead(int t) {
  if (live(t)) _threads[t].unread = 0;
}

uint32_t MsgStore::totalUnread() const {
  uint32_t n = 0;
  for (int t = 0; t < _max_threads; t++) n += _threads[t].unread;
  return n;
}

// -------------------------------------------------------------- persistence

static bool readAll(ByteSource& in, void* buf, size_t len) { return in.read(buf, len) == len; }

bool MsgStore::saveIndex(ByteSink& out) const {
  if (!_threads) return false;
  FileHeader h = {};
  memcpy(h.magic, kIndexMagic, 4);
  h.version = kMsgStoreVersion;
  h.entry_size = sizeof(IndexEntry);
  h.count = threadCount();
  h.next_seq = _seq;
  if (!out.write(&h, sizeof(h))) return false;
  for (int t = 0; t < _max_threads; t++) {
    if (_threads[t].kind == uint8_t(ThreadKind::Empty)) continue;
    IndexEntry e = {};
    e.slot = uint16_t(t);
    e.thread = _threads[t];
    if (!out.write(&e, sizeof(e))) return false;
  }
  return true;
}

// A header that reads in full: Empty if there were no bytes at all.
static LoadResult readHeader(ByteSource& in, FileHeader* h, const char magic[4], uint16_t entry_size) {
  const size_t got = in.read(h, sizeof(*h));
  if (got == 0) return LoadResult::Empty;
  if (got != sizeof(*h) || memcmp(h->magic, magic, 4) != 0) return LoadResult::BadHeader;
  if (h->version != kMsgStoreVersion) return LoadResult::UnknownVersion;
  if (h->entry_size != entry_size) return LoadResult::BadHeader;
  return LoadResult::Ok;
}

LoadResult MsgStore::loadIndex(ByteSource& in) {
  if (!_threads) return LoadResult::BadHeader;
  memset(_threads, 0, sizeof(Thread) * _max_threads);
  FileHeader h;
  const LoadResult r = readHeader(in, &h, kIndexMagic, sizeof(IndexEntry));
  if (r != LoadResult::Ok) return r;
  for (uint16_t i = 0; i < h.count; i++) {
    IndexEntry e;
    if (!readAll(in, &e, sizeof(e))) {
      memset(_threads, 0, sizeof(Thread) * _max_threads);   // nothing from a cut-short file
      return LoadResult::BadHeader;
    }
    if (e.slot >= _max_threads || e.thread.kind == uint8_t(ThreadKind::Empty) ||
        e.thread.kind > uint8_t(ThreadKind::Room)) {
      continue;   // a slot this build can't hold, or not a thread
    }
    e.thread.name[kThreadNameLen - 1] = 0;
    _threads[e.slot] = e.thread;
  }
  if (h.next_seq > _seq) _seq = h.next_seq;
  return LoadResult::Ok;
}

bool MsgStore::saveRing(ByteSink& out) const {
  if (!_msgs) return false;
  // Oldest first, so a load into a smaller store can skip to the newest.
  uint16_t n = 0;
  for (uint16_t i = 0; i < _cap; i++) {
    if (_msgs[i].seq == 0) continue;
    uint16_t j = n++;
    while (j > 0 && _msgs[_order[j - 1]].seq > _msgs[i].seq) {
      _order[j] = _order[j - 1];
      j--;
    }
    _order[j] = i;
  }
  FileHeader h = {};
  memcpy(h.magic, kRingMagic, 4);
  h.version = kMsgStoreVersion;
  h.entry_size = sizeof(MsgRecord);
  h.count = n;
  h.next_seq = _seq;
  if (!out.write(&h, sizeof(h))) return false;
  for (uint16_t k = 0; k < n; k++) {
    if (!out.write(&_msgs[_order[k]], sizeof(MsgRecord))) return false;
  }
  return true;
}

LoadResult MsgStore::loadRing(ByteSource& in) {
  if (!_msgs) return LoadResult::BadHeader;
  memset(_msgs, 0, sizeof(MsgRecord) * _cap);
  _count = 0;
  FileHeader h;
  const LoadResult r = readHeader(in, &h, kRingMagic, sizeof(MsgRecord));
  if (r != LoadResult::Ok) return r;
  // The file is oldest first: with more than fit, the oldest are skipped.
  const uint16_t skip = h.count > _cap ? uint16_t(h.count - _cap) : 0;
  MsgRecord rec;
  for (uint16_t i = 0; i < h.count; i++) {
    if (!readAll(in, &rec, sizeof(rec))) {
      memset(_msgs, 0, sizeof(MsgRecord) * _cap);   // nothing from a cut-short file
      _count = 0;
      return LoadResult::BadHeader;
    }
    if (i < skip || rec.seq == 0 || !live(rec.thread) || rec.text_len > kMsgTextMax) continue;
    rec.sender[kMsgSenderLen - 1] = 0;
    _msgs[_count++] = rec;
    if (rec.seq > _seq) _seq = rec.seq;
  }
  if (h.next_seq > _seq) _seq = h.next_seq;
  return LoadResult::Ok;
}

}  // namespace gui
}  // namespace offband
