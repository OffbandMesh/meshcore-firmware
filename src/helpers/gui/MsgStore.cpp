#include "MsgStore.h"

#include <new>
#include <string.h>

#include "../BadgeStore.h"   // utf8Fit: cut text without splitting a character

namespace offband {
namespace gui {

// ------------------------------------------------------------- file layout

// Both files: a header, then one fixed-size entry per slot, entry i at
// kHeaderBytes + i * entry_size (#1472). Entry size and count guard against
// a file from a different layout being read as this one.
struct FileHeader {
  char magic[4];
  uint16_t version;
  uint16_t entry_size;
  uint16_t count;      // slots in the file
  uint16_t reserved;
  uint32_t next_seq;
};
static_assert(sizeof(FileHeader) == 16, "the file header is 16 bytes");
static constexpr size_t kHeaderBytes = sizeof(FileHeader);

static const char kIndexMagic[4] = {'O', 'B', 'M', 'I'};
static const char kRingMagic[4] = {'O', 'B', 'M', 'R'};

// An index entry is the thread's slot number, then the thread (kind Empty
// for a free slot).
struct IndexEntry {
  uint16_t slot;
  uint16_t reserved;
  Thread thread;
};
static_assert(sizeof(IndexEntry) == 84, "an index entry is 84 bytes");

// CRC-16/CCITT-FALSE. SPIFFS writes 256-byte pages, so a power cut can tear
// a 192-byte entry that straddles two; the CRC finds it at load.
static uint16_t crc16(const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint16_t c = 0xFFFF;
  while (len--) {
    c ^= uint16_t(*p++) << 8;
    for (int b = 0; b < 8; b++) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
  }
  return c;
}

static uint16_t recordCrc(MsgRecord r) {
  r.crc = 0;
  return crc16(&r, sizeof(r));
}

static uint16_t threadCrc(Thread t) {
  t.crc = 0;
  return crc16(&t, sizeof(t));
}

uint16_t storeCrc16(const void* data, size_t len) { return crc16(data, len); }

static bool bit(const uint8_t* b, size_t i) { return (b[i >> 3] >> (i & 7)) & 1; }
static void setBit(uint8_t* b, size_t i) { b[i >> 3] |= uint8_t(1u << (i & 7)); }
static void clearBit(uint8_t* b, size_t i) { b[i >> 3] &= uint8_t(~(1u << (i & 7))); }
static size_t bitBytes(size_t n) { return (n + 7) / 8; }

// ----------------------------------------------------------------- lifetime

void MsgStore::release() {
  delete[] _msgs;
  delete[] _threads;
  delete[] _per_thread;
  delete[] _slot_dirty;
  delete[] _thread_dirty;
  _msgs = nullptr;
  _threads = nullptr;
  _per_thread = nullptr;
  _slot_dirty = nullptr;
  _thread_dirty = nullptr;
  _cap = _max_threads = _count = 0;
  _seq = 0;
  _ring_header_dirty = _index_header_dirty = false;
  _scan_slot = _scan_thread = 0;
  _dirty_bits = 0;
}

bool MsgStore::begin(uint16_t capacity, uint16_t max_threads) {
  release();
  if (capacity == 0 || max_threads == 0) return false;

  // Over 4 KB, so on the S3 the records come from PSRAM.
  _msgs = new (std::nothrow) MsgRecord[capacity];
  _threads = new (std::nothrow) Thread[max_threads];
  _per_thread = new (std::nothrow) uint16_t[max_threads];
  _slot_dirty = new (std::nothrow) uint8_t[bitBytes(capacity)];
  _thread_dirty = new (std::nothrow) uint8_t[bitBytes(max_threads)];
  if (!_msgs || !_threads || !_per_thread || !_slot_dirty || !_thread_dirty) {
    release();
    return false;
  }
  memset(_msgs, 0, sizeof(MsgRecord) * capacity);
  memset(_threads, 0, sizeof(Thread) * max_threads);
  memset(_slot_dirty, 0, bitBytes(capacity));
  memset(_thread_dirty, 0, bitBytes(max_threads));
  _cap = capacity;
  _max_threads = max_threads;
  return true;
}

MsgStore::~MsgStore() { release(); }

void MsgStore::dirtySlot(const MsgRecord* m) {
  const size_t i = size_t(m - _msgs);
  if (!bit(_slot_dirty, i)) {
    setBit(_slot_dirty, i);
    _dirty_bits++;
  }
  _ring_header_dirty = true;
  _revision++;
}

void MsgStore::dirtyThread(int t) {
  if (!bit(_thread_dirty, size_t(t))) {
    setBit(_thread_dirty, size_t(t));
    _dirty_bits++;
  }
  _index_header_dirty = true;
  _revision++;
}

static uint32_t countBits(const uint8_t* b, size_t n) {
  uint32_t c = 0;
  for (size_t i = 0; i < n; i++) c += bit(b, i);
  return c;
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
    dirtyThread(t);
  }
  char name_now[kThreadNameLen];
  copyText(name_now, kThreadNameLen, name);
  if (strcmp(name_now, _threads[t].name) != 0) {   // a rename is a change; a lookup isn't
    memcpy(_threads[t].name, name_now, kThreadNameLen);
    dirtyThread(t);
  }
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
  dirtySlot(m);
  dirtyThread(t);
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

uint32_t MsgStore::nextSeqAfter(uint32_t seq) const {
  uint32_t best = 0;
  if (!_msgs) return 0;
  for (uint16_t i = 0; i < _cap; i++) {
    const uint32_t s = _msgs[i].seq;
    if (s > seq && (best == 0 || s < best)) best = s;
  }
  return best;
}

void MsgStore::raiseSeqFloor(uint32_t floor) {
  if (_seq >= floor) return;
  _seq = floor;
  _ring_header_dirty = true;    // both headers carry next_seq
  _index_header_dirty = true;
  _revision++;
}

uint16_t MsgStore::countAfter(uint32_t seq) const {
  uint16_t n = 0;
  if (!_msgs) return 0;
  for (uint16_t i = 0; i < _cap; i++) {
    if (_msgs[i].seq > seq) n++;
  }
  return n;
}

uint32_t MsgStore::findOwn(int t, const char* text, size_t len) const {
  if (!live(t) || !text) return 0;
  const size_t n = offband::utf8Fit(text, len, kMsgTextMax);
  uint32_t best = 0;
  for (uint16_t i = 0; i < _cap; i++) {
    const MsgRecord& m = _msgs[i];
    if (m.seq == 0 || !m.own || m.thread != t || m.text_len != n) continue;
    if (m.delivery == uint8_t(Delivery::Delivered)) continue;   // a resend is of one not yet delivered
    if (memcmp(m.text, text, n) == 0 && m.seq > best) best = m.seq;
  }
  return best;
}

bool MsgStore::setDelivery(uint32_t seq, Delivery d) {
  MsgRecord* m = ref(seq);
  if (!m || !m->own) return false;
  if (m->delivery != uint8_t(d)) {
    m->delivery = uint8_t(d);
    dirtySlot(m);
  }
  return true;
}

bool MsgStore::addEcho(uint32_t seq) {
  MsgRecord* m = ref(seq);
  if (!m || !m->own) return false;
  if (m->echoes < 0xFF) {
    m->echoes++;
    dirtySlot(m);
  }
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
  if (live(t) && _threads[t].unread != 0) {
    _threads[t].unread = 0;
    dirtyThread(t);
  }
}

uint32_t MsgStore::totalUnread() const {
  uint32_t n = 0;
  for (int t = 0; t < _max_threads; t++) n += _threads[t].unread;
  return n;
}

// -------------------------------------------------------------- persistence

bool BufferSink::reserve(size_t capacity) {
  if (capacity <= _cap) return _buf != nullptr || capacity == 0;
  delete[] _buf;
  _buf = new (std::nothrow) uint8_t[capacity];   // over 4 KB: PSRAM on the S3
  _cap = _buf ? capacity : 0;
  _len = 0;
  return _buf != nullptr;
}

bool BufferSink::write(const void* data, size_t len) {
  if (!_buf || len > _cap - _len) return false;
  memcpy(_buf + _len, data, len);
  _len += len;
  return true;
}

BufferSink::~BufferSink() { delete[] _buf; }

size_t MsgStore::indexFileBytes() const { return kHeaderBytes + sizeof(IndexEntry) * _max_threads; }
size_t MsgStore::ringFileBytes() const { return kHeaderBytes + sizeof(MsgRecord) * _cap; }

static void makeHeader(FileHeader* h, const char magic[4], uint16_t entry_size, uint16_t count,
                       uint32_t next_seq) {
  memset(h, 0, sizeof(*h));
  memcpy(h->magic, magic, 4);
  h->version = kMsgStoreVersion;
  h->entry_size = entry_size;
  h->count = count;
  h->next_seq = next_seq;
}

static IndexEntry indexEntry(const Thread* threads, uint16_t slot) {
  IndexEntry e = {};
  e.slot = slot;
  e.thread = threads[slot];
  e.thread.crc = threadCrc(e.thread);
  return e;
}

bool MsgStore::anyDirty() const { return _dirty_bits > 0 || _ring_header_dirty || _index_header_dirty; }

void MsgStore::clearDirty() {
  clearDirty(true);
  clearDirty(false);
}

void MsgStore::clearDirty(bool ring) {
  if (ring) {
    if (_slot_dirty) memset(_slot_dirty, 0, bitBytes(_cap));
    _ring_header_dirty = false;
    _scan_slot = 0;
  } else {
    if (_thread_dirty) memset(_thread_dirty, 0, bitBytes(_max_threads));
    _index_header_dirty = false;
    _scan_thread = 0;
  }
  _dirty_bits = (_slot_dirty ? countBits(_slot_dirty, _cap) : 0) +
                (_thread_dirty ? countBits(_thread_dirty, _max_threads) : 0);
}

// Thread entries, then slots, then each touched file's header last, so a
// header never claims a next_seq whose records aren't written yet. A change
// that lands behind the scan (it arrived mid-burst) is found by a second pass
// from the start before any header goes out.
bool MsgStore::nextDirty(DirtyWrite* out) {
  if (!anyDirty() || !out) return false;
  for (int pass = 0; pass < 2 && _dirty_bits > 0; pass++) {
    for (; _scan_thread < _max_threads; _scan_thread++) {
      if (!bit(_thread_dirty, _scan_thread)) continue;
      clearBit(_thread_dirty, _scan_thread);
      _dirty_bits--;
      const IndexEntry e = indexEntry(_threads, _scan_thread);
      memcpy(_entry, &e, sizeof(e));   // stays valid until the next call
      *out = {false, uint32_t(kHeaderBytes + sizeof(IndexEntry) * _scan_thread), _entry,
              uint16_t(sizeof(IndexEntry))};
      _scan_thread++;
      return true;
    }
    for (; _scan_slot < _cap; _scan_slot++) {
      if (!bit(_slot_dirty, _scan_slot)) continue;
      clearBit(_slot_dirty, _scan_slot);
      _dirty_bits--;
      _msgs[_scan_slot].crc = recordCrc(_msgs[_scan_slot]);   // as it goes out
      *out = {true, uint32_t(kHeaderBytes + sizeof(MsgRecord) * _scan_slot), &_msgs[_scan_slot],
              uint16_t(sizeof(MsgRecord))};
      _scan_slot++;
      return true;
    }
    _scan_thread = _scan_slot = 0;   // bits left behind the cursors: once more from the start
  }
  FileHeader* h = reinterpret_cast<FileHeader*>(_hdr);
  if (_index_header_dirty) {
    _index_header_dirty = false;
    makeHeader(h, kIndexMagic, sizeof(IndexEntry), _max_threads, _seq);
    *out = {false, 0, _hdr, uint16_t(kHeaderBytes)};
    if (!_ring_header_dirty) _scan_slot = _scan_thread = 0;
    return true;
  }
  _ring_header_dirty = false;
  makeHeader(h, kRingMagic, sizeof(MsgRecord), _cap, _seq);
  *out = {true, 0, _hdr, uint16_t(kHeaderBytes)};
  _scan_slot = _scan_thread = 0;
  return true;
}

static bool readAll(ByteSource& in, void* buf, size_t len) { return in.read(buf, len) == len; }

bool MsgStore::saveIndex(ByteSink& out) const {
  if (!_threads) return false;
  FileHeader h;
  makeHeader(&h, kIndexMagic, sizeof(IndexEntry), _max_threads, _seq);
  if (!out.write(&h, sizeof(h))) return false;
  for (uint16_t t = 0; t < _max_threads; t++) {
    const IndexEntry e = indexEntry(_threads, t);
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
    if (e.slot != i || e.slot >= _max_threads || e.thread.kind == uint8_t(ThreadKind::Empty) ||
        e.thread.kind > uint8_t(ThreadKind::Room) || e.thread.crc != threadCrc(e.thread)) {
      continue;   // a free slot, one this build can't hold, not a thread, or torn
    }
    e.thread.name[kThreadNameLen - 1] = 0;
    _threads[e.slot] = e.thread;
  }
  if (h.next_seq > _seq) _seq = h.next_seq;
  clearDirty(false);
  return LoadResult::Ok;
}

bool MsgStore::saveRing(ByteSink& out) const {
  if (!_msgs) return false;
  FileHeader h;
  makeHeader(&h, kRingMagic, sizeof(MsgRecord), _cap, _seq);
  if (!out.write(&h, sizeof(h))) return false;
  for (uint16_t i = 0; i < _cap; i++) {
    MsgRecord r = _msgs[i];
    r.crc = recordCrc(r);
    if (!out.write(&r, sizeof(r))) return false;
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
  MsgRecord rec;
  for (uint16_t i = 0; i < h.count; i++) {
    if (!readAll(in, &rec, sizeof(rec))) {
      memset(_msgs, 0, sizeof(MsgRecord) * _cap);   // nothing from a cut-short file
      _count = 0;
      return LoadResult::BadHeader;
    }
    if (rec.seq == 0 || !live(rec.thread) || rec.text_len > kMsgTextMax) continue;
    if (rec.crc != recordCrc(rec)) continue;   // torn by a power cut mid-write
    rec.sender[kMsgSenderLen - 1] = 0;
    if (rec.seq > _seq) _seq = rec.seq;
    if (i < _cap && _msgs[i].seq == 0) {   // same layout: same slot
      _msgs[i] = rec;
      _count++;
      continue;
    }
    // A file from a bigger store: keep the newest that fit.
    MsgRecord* slot = nullptr;
    for (uint16_t k = 0; k < _cap; k++) {
      if (_msgs[k].seq == 0) {
        slot = &_msgs[k];
        break;
      }
      if (!slot || _msgs[k].seq < slot->seq) slot = &_msgs[k];
    }
    if (slot->seq == 0) {
      _count++;
    } else if (slot->seq > rec.seq) {
      continue;   // older than everything kept
    }
    *slot = rec;
  }
  if (h.next_seq > _seq) _seq = h.next_seq;
  // A power cut between a message and its thread entry leaves the thread's
  // newest message stale: take it from the messages themselves.
  for (uint16_t t = 0; t < _max_threads; t++) _threads[t].last_seq = _threads[t].last_time = 0;
  for (uint16_t k = 0; k < _cap; k++) {
    const MsgRecord& m = _msgs[k];
    if (m.seq != 0 && m.seq > _threads[m.thread].last_seq) {
      _threads[m.thread].last_seq = m.seq;
      _threads[m.thread].last_time = m.time;
    }
  }
  // A file from a different layout gets rewritten whole by the caller; the
  // same layout needs nothing written back.
  clearDirty(true);
  clearDirty(false);
  return LoadResult::Ok;
}

}  // namespace gui
}  // namespace offband
