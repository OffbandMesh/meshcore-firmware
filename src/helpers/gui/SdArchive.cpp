#include "SdArchive.h"

#include <string.h>

#include <new>

namespace offband {
namespace gui {

namespace {

// Both files start with a 16-byte header:
//   magic[4], version u16, count u16, value u32, crc u32 (over the first 12)
// `count` is the slots (records) or the thread entries (threads). `value` is
// archived_through in the records file and the owning radio's tag in the
// threads file.
constexpr char kRecMagic[4] = {'O', 'B', 'S', 'A'};
constexpr char kThrMagic[4] = {'O', 'B', 'S', 'T'};

struct Header {
  char magic[4];
  uint16_t version;
  uint16_t count;
  uint32_t value;
  uint32_t crc;
};
static_assert(sizeof(Header) == kArchiveHeaderLen, "the archive header is 16 bytes");

uint32_t headerCrc(const Header& h) { return storeCrc16(&h, 12); }

Header makeHeader(const char* magic, uint16_t count, uint32_t value) {
  Header h = {};
  memcpy(h.magic, magic, 4);
  h.version = kArchiveVersion;
  h.count = count;
  h.value = value;
  h.crc = headerCrc(h);
  return h;
}

bool headerOk(const Header& h, const char* magic, uint16_t count) {
  return memcmp(h.magic, magic, 4) == 0 && h.crc == headerCrc(h) &&
         h.version == kArchiveVersion && h.count == count;
}

// A record as it sits on the card: its CRC covers it with the thread index
// already rewritten to the archive's own.
uint16_t cardCrc(MsgRecord r) {
  r.crc = 0;
  return storeCrc16(&r, sizeof(r));
}

uint32_t threadCrc(ArchiveThread t) {
  t.crc = 0;
  return storeCrc16(&t, sizeof(t));
}

bool threadValid(const ArchiveThread& e) { return e.kind != 0 && e.crc == threadCrc(e); }

// Sizes a new file with one write at its end. FAT leaves the clusters in
// between as whatever they held, which is safe here: every read checks the
// slot's seq and CRC (and a thread entry its kind and CRC), so leftover bytes
// never pass as a record. Zero-filling 960 KB instead would block the loop.
bool sizeTo(RandomFile& f, uint32_t total) {
  const uint8_t z = 0;
  return total == 0 || f.writeAt(total - 1, &z, 1);
}

uint32_t threadOffset(uint16_t i) {
  return uint32_t(kArchiveHeaderLen) + uint32_t(i) * uint32_t(sizeof(ArchiveThread));
}

}  // namespace

SdArchive::~SdArchive() { close(); }

uint32_t SdArchive::recordsFileBytes() const {
  return uint32_t(kArchiveHeaderLen) + uint32_t(_slots) * uint32_t(sizeof(MsgRecord));
}

uint32_t SdArchive::threadsFileBytes() const { return threadOffset(_max_threads); }

ArchiveOpen SdArchive::open(RandomFile& records, RandomFile& threads, uint32_t owner,
                            uint16_t slots, uint16_t max_threads) {
  close();
  if (slots == 0 || max_threads == 0) return ArchiveOpen::BadHeader;
  _slots = slots;
  _max_threads = max_threads;
  const uint32_t rec_bytes = recordsFileBytes();
  const uint32_t thr_bytes = threadsFileBytes();

  ArchiveThread* table = new (std::nothrow) ArchiveThread[max_threads];
  if (!table) return ArchiveOpen::IoError;
  memset(table, 0, sizeof(ArchiveThread) * max_threads);

  ArchiveOpen result;
  uint32_t through = 0;
  if (records.size() == 0 && threads.size() == 0) {
    const Header rh = makeHeader(kRecMagic, slots, 0);
    const Header th = makeHeader(kThrMagic, max_threads, owner);
    if (!sizeTo(records, rec_bytes) || !sizeTo(threads, thr_bytes) ||
        !records.writeAt(0, &rh, sizeof(rh)) || !threads.writeAt(0, &th, sizeof(th)) ||
        !records.flush() || !threads.flush()) {
      delete[] table;
      return ArchiveOpen::IoError;
    }
    result = ArchiveOpen::Fresh;
  } else {
    if (records.size() != rec_bytes || threads.size() != thr_bytes) {
      delete[] table;
      return ArchiveOpen::BadHeader;
    }
    Header rh, th;
    if (!records.readAt(0, &rh, sizeof(rh)) || !threads.readAt(0, &th, sizeof(th))) {
      delete[] table;
      return ArchiveOpen::IoError;
    }
    if (!headerOk(rh, kRecMagic, slots) || !headerOk(th, kThrMagic, max_threads)) {
      delete[] table;
      return ArchiveOpen::BadHeader;
    }
    if (th.value != owner) {
      delete[] table;
      return ArchiveOpen::Foreign;
    }
    if (!threads.readAt(threadOffset(0), table, sizeof(ArchiveThread) * max_threads)) {
      delete[] table;
      return ArchiveOpen::IoError;
    }
    // A torn or never-written entry is a free slot, not a thread.
    for (uint16_t i = 0; i < max_threads; i++) {
      if (!threadValid(table[i])) memset(&table[i], 0, sizeof(ArchiveThread));
    }
    through = rh.value;
    result = ArchiveOpen::Ok;
  }
  _rec = &records;
  _thr = &threads;
  _threads = table;
  _through = through;
  return result;
}

void SdArchive::close() {
  delete[] _threads;
  _threads = nullptr;
  _rec = nullptr;
  _thr = nullptr;
  _through = 0;
}

bool SdArchive::alignLive(MsgStore& live) const {
  if (!ready() || live.lastSeq() >= _through) return false;
  live.raiseSeqFloor(_through);
  return true;
}

size_t SdArchive::pending(const MsgStore& live) const {
  return ready() ? live.countAfter(_through) : 0;
}

bool SdArchive::writeHeader() {
  const Header h = makeHeader(kRecMagic, _slots, _through);
  return _rec->writeAt(0, &h, sizeof(h)) && _rec->flush();
}

int SdArchive::threadFor(const Thread& t) {
  int free_slot = -1;
  for (uint16_t i = 0; i < _max_threads; i++) {
    ArchiveThread& e = _threads[i];
    if (e.kind == 0) {
      if (free_slot < 0) free_slot = i;
      continue;
    }
    if (e.kind != t.kind || memcmp(e.key, t.key, kThreadKeyLen) != 0) continue;
    if (strncmp(e.name, t.name, kThreadNameLen) != 0) {   // a rename reaches the card too
      ArchiveThread n = e;
      memcpy(n.name, t.name, kThreadNameLen);
      n.name[kThreadNameLen - 1] = '\0';
      n.crc = threadCrc(n);
      if (!_thr->writeAt(threadOffset(i), &n, sizeof(n)) || !_thr->flush()) return kThreadIoError;
      e = n;
    }
    return i;
  }
  if (free_slot < 0) return kThreadTableFull;
  ArchiveThread n = {};
  n.kind = t.kind;
  memcpy(n.key, t.key, kThreadKeyLen);
  memcpy(n.name, t.name, kThreadNameLen);
  n.name[kThreadNameLen - 1] = '\0';
  n.crc = threadCrc(n);
  if (!_thr->writeAt(threadOffset(uint16_t(free_slot)), &n, sizeof(n)) || !_thr->flush()) {
    return kThreadIoError;
  }
  _threads[free_slot] = n;
  return free_slot;
}

bool SdArchive::writeRecord(const MsgRecord& m, uint16_t archive_thread) {
  MsgRecord r = m;
  r.thread = archive_thread;
  r.crc = cardCrc(r);
  const uint32_t slot = m.seq % _slots;
  const uint32_t off = uint32_t(kArchiveHeaderLen) + slot * uint32_t(sizeof(MsgRecord));
  return _rec->writeAt(off, &r, sizeof(r));
}

size_t SdArchive::catchUp(MsgStore& live, size_t budget, bool* ok) {
  if (ok) *ok = ready();
  if (!ready()) return 0;
  alignLive(live);
  uint32_t through = _through;
  size_t done = 0;
  while (done < budget) {
    const uint32_t seq = live.nextSeqAfter(through);
    if (seq == 0) break;
    const MsgRecord* m = live.msg(seq);
    const Thread* t = m ? live.threadAt(m->thread) : nullptr;
    // A message whose thread is gone can't be labeled; it's skipped, not
    // waited on, like a seq evicted while the card was out.
    if (m && t) {
      const int at = threadFor(*t);
      // The archive's thread table is full: this message has no label on the
      // card. Skipped like an orphan; the rest still go. A card error stops.
      if (at == kThreadIoError || (at >= 0 && !writeRecord(*m, uint16_t(at)))) {
        if (ok) *ok = false;
        return live.countAfter(_through);   // the header still says what's really there
      }
    }
    through = seq;
    done++;
  }
  if (through != _through) {
    const uint32_t before = _through;
    _through = through;
    if (!writeHeader()) {
      _through = before;
      if (ok) *ok = false;
    }
  }
  return live.countAfter(_through);
}

bool SdArchive::update(const MsgStore& live, uint32_t seq) {
  if (!ready()) return false;
  if (seq == 0 || seq > _through) return true;   // not on the card yet: the catch-up takes it
  const MsgRecord* m = live.msg(seq);
  const Thread* t = m ? live.threadAt(m->thread) : nullptr;
  if (!m || !t) return true;
  const int at = threadFor(*t);
  if (at == kThreadIoError) return false;
  if (at < 0) return true;   // no label on the card for it: nothing to rewrite
  return writeRecord(*m, uint16_t(at)) && _rec->flush();
}

bool SdArchive::read(uint32_t seq, MsgRecord* out, ArchiveThread* thread) {
  if (!ready() || seq == 0 || seq > _through || !out) return false;
  const uint32_t off =
      uint32_t(kArchiveHeaderLen) + (seq % _slots) * uint32_t(sizeof(MsgRecord));
  MsgRecord r;
  if (!_rec->readAt(off, &r, sizeof(r))) return false;
  if (r.seq != seq || r.crc != cardCrc(r) || r.thread >= _max_threads) return false;
  if (thread) {
    const ArchiveThread& e = _threads[r.thread];
    if (e.kind == 0) return false;
    *thread = e;
  }
  *out = r;
  return true;
}

}  // namespace gui
}  // namespace offband
