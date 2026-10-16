// Epic C (#1427): the on-device message store, pinned before it is built
// (C1, #1461). PLAN.md §3 Epic C and §8 (a bad file is never trusted).

#include <gtest/gtest.h>

#include <string.h>

#include <vector>

#include "helpers/gui/ChunkedSave.h"
#include "helpers/gui/MsgStore.h"

using namespace offband::gui;

namespace {

const uint8_t kChanA[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
const uint8_t kChanB[16] = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
uint8_t kPeer[32];

class MemSink : public ByteSink {
public:
  std::vector<uint8_t> bytes;
  bool write(const void* data, size_t len) override {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    bytes.insert(bytes.end(), p, p + len);
    return true;
  }
};

class MemSource : public ByteSource {
public:
  explicit MemSource(const std::vector<uint8_t>& b) : _b(b) {}
  size_t read(void* data, size_t len) override {
    size_t n = len < _b.size() - _pos ? len : _b.size() - _pos;
    memcpy(data, _b.data() + _pos, n);
    _pos += n;
    return n;
  }

private:
  std::vector<uint8_t> _b;
  size_t _pos = 0;
};

uint32_t addText(MsgStore& s, int t, uint32_t time, const char* text) {
  return s.addIncoming(t, time, "bob", text, strlen(text), 2, 24);
}

std::string textOf(const MsgRecord* m) { return std::string(m->text, m->text_len); }

}  // namespace

// --------------------------------------------------------------------- setup

TEST(MsgStore, BeginAllocatesOnce) {
  MsgStore s;
  EXPECT_FALSE(s.ready());
  ASSERT_TRUE(s.begin(1000, 64));
  EXPECT_TRUE(s.ready());
  EXPECT_EQ(s.capacity(), 1000);
  EXPECT_EQ(s.count(), 0);
  EXPECT_EQ(s.threadCount(), 0);
  EXPECT_EQ(s.totalUnread(), 0u);
}

// ------------------------------------------------------------------- threads

TEST(MsgStore, ThreadsAreFoundByKeyNotName) {
  MsgStore s;
  ASSERT_TRUE(s.begin(16, 8));
  memset(kPeer, 7, sizeof(kPeer));
  const int a = s.thread(ThreadKind::Channel, kChanA, 16, "Public");
  const int b = s.thread(ThreadKind::Channel, kChanB, 16, "Public");   // same name, other key
  const int d = s.thread(ThreadKind::Direct, kPeer, 32, "Bob");
  ASSERT_GE(a, 0);
  ASSERT_GE(b, 0);
  ASSERT_GE(d, 0);
  EXPECT_NE(a, b);
  EXPECT_EQ(s.thread(ThreadKind::Channel, kChanA, 16, "Renamed"), a);   // same key: same thread
  EXPECT_STREQ(s.threadAt(a)->name, "Renamed");
  EXPECT_EQ(s.find(ThreadKind::Direct, kPeer, 32), d);
  EXPECT_EQ(s.find(ThreadKind::Room, kPeer, 32), -1);   // kind is part of the identity
  EXPECT_EQ(s.threadCount(), 3);
}

TEST(MsgStore, ThreadTableFull) {
  MsgStore s;
  ASSERT_TRUE(s.begin(16, 2));
  EXPECT_GE(s.thread(ThreadKind::Channel, kChanA, 16, "a"), 0);
  EXPECT_GE(s.thread(ThreadKind::Channel, kChanB, 16, "b"), 0);
  memset(kPeer, 9, sizeof(kPeer));
  EXPECT_EQ(s.thread(ThreadKind::Direct, kPeer, 32, "c"), -1);
}

// ----------------------------------------------------------------- the ring

TEST(MsgStore, ThreadListsOldestFirst) {
  MsgStore s;
  ASSERT_TRUE(s.begin(16, 8));
  const int a = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  const int b = s.thread(ThreadKind::Channel, kChanB, 16, "b");
  const uint32_t a1 = addText(s, a, 100, "one");
  addText(s, b, 101, "other");
  const uint32_t a2 = addText(s, a, 102, "two");
  uint32_t seqs[8];
  ASSERT_EQ(s.threadMessages(a, seqs, 8), 2u);
  EXPECT_EQ(seqs[0], a1);
  EXPECT_EQ(seqs[1], a2);
  EXPECT_EQ(textOf(s.msg(a2)), "two");
  EXPECT_EQ(s.threadAt(a)->last_seq, a2);
  EXPECT_EQ(s.threadAt(a)->last_time, 102u);
}

TEST(MsgStore, ThreadListGivesTheNewestWhenCapped) {
  MsgStore s;
  ASSERT_TRUE(s.begin(16, 8));
  const int a = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  uint32_t all[5];
  for (int i = 0; i < 5; i++) all[i] = addText(s, a, 100 + i, "m");
  uint32_t seqs[3];
  ASSERT_EQ(s.threadMessages(a, seqs, 3), 3u);
  EXPECT_EQ(seqs[0], all[2]);
  EXPECT_EQ(seqs[2], all[4]);
}

// Eviction (owner, 2026-10-15, #1461): when full, the oldest message of the
// busiest channel goes; DMs go only when no channel or room messages remain.
TEST(MsgStore, FullStoreDropsFromTheBusiestChannel) {
  MsgStore s;
  ASSERT_TRUE(s.begin(4, 8));
  const int a = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  const int b = s.thread(ThreadKind::Channel, kChanB, 16, "b");
  const uint32_t a1 = addText(s, a, 1, "a1");
  const uint32_t a2 = addText(s, a, 2, "a2");
  const uint32_t a3 = addText(s, a, 3, "a3");
  const uint32_t b1 = addText(s, b, 4, "b1");   // full
  const uint32_t b2 = addText(s, b, 5, "b2");   // a is busiest: a1 goes
  EXPECT_EQ(s.count(), 4);
  EXPECT_EQ(s.msg(a1), nullptr);
  EXPECT_NE(s.msg(a2), nullptr);
  EXPECT_NE(s.msg(a3), nullptr);
  EXPECT_NE(s.msg(b1), nullptr);
  EXPECT_EQ(textOf(s.msg(b2)), "b2");
}

TEST(MsgStore, ChannelMessagesGoBeforeDirectOnes) {
  MsgStore s;
  ASSERT_TRUE(s.begin(4, 8));
  memset(kPeer, 3, sizeof(kPeer));
  const int dm = s.thread(ThreadKind::Direct, kPeer, 32, "Bob");
  const int ch = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  const uint32_t d1 = addText(s, dm, 1, "d1");
  const uint32_t d2 = addText(s, dm, 2, "d2");
  const uint32_t d3 = addText(s, dm, 3, "d3");
  const uint32_t c1 = addText(s, ch, 4, "c1");   // full; the DM thread is busiest
  addText(s, dm, 5, "d4");                       // ...but the channel message goes first
  EXPECT_EQ(s.msg(c1), nullptr);
  EXPECT_NE(s.msg(d1), nullptr);
  // With no channel or room messages left, the busiest DM thread's oldest goes.
  addText(s, dm, 6, "d5");
  EXPECT_EQ(s.msg(d1), nullptr);
  EXPECT_NE(s.msg(d2), nullptr);
  EXPECT_NE(s.msg(d3), nullptr);
}

TEST(MsgStore, RoomMessagesGoWithChannelMessages) {
  MsgStore s;
  ASSERT_TRUE(s.begin(2, 8));
  uint8_t room_key[32];
  memset(room_key, 4, sizeof(room_key));
  memset(kPeer, 3, sizeof(kPeer));
  const int room = s.thread(ThreadKind::Room, room_key, 32, "Room");
  const int dm = s.thread(ThreadKind::Direct, kPeer, 32, "Bob");
  const uint32_t r1 = addText(s, room, 1, "r1");
  const uint32_t d1 = addText(s, dm, 2, "d1");
  addText(s, dm, 3, "d2");
  EXPECT_EQ(s.msg(r1), nullptr);
  EXPECT_NE(s.msg(d1), nullptr);
}

// At the V4's full 1,000, a thread still lists in order (the bench measures
// the time in C6; Gemini, #1461).
TEST(MsgStore, ThreadListAtFullCapacity) {
  MsgStore s;
  ASSERT_TRUE(s.begin(1000, 64));
  const int a = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  const int b = s.thread(ThreadKind::Channel, kChanB, 16, "b");
  uint32_t last_a = 0;
  for (int i = 0; i < 1200; i++) {
    const uint32_t q = addText(s, (i % 3 == 0) ? a : b, 1000 + i, "m");
    if (i % 3 == 0) last_a = q;
  }
  EXPECT_EQ(s.count(), 1000);
  uint32_t seqs[400];
  const size_t n = s.threadMessages(a, seqs, 400);
  ASSERT_GT(n, 0u);
  for (size_t i = 1; i < n; i++) EXPECT_LT(seqs[i - 1], seqs[i]);
  EXPECT_EQ(seqs[n - 1], last_a);
}

// ------------------------------------------------------------------- unread

TEST(MsgStore, UnreadCountsLiveWithTheThread) {
  MsgStore s;
  ASSERT_TRUE(s.begin(2, 8));
  const int a = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  const int b = s.thread(ThreadKind::Channel, kChanB, 16, "b");
  addText(s, a, 1, "x");
  addText(s, a, 2, "y");
  addText(s, a, 3, "z");   // evicts the first: the unread count stays 3
  addText(s, b, 4, "w");
  EXPECT_EQ(s.threadAt(a)->unread, 3);
  EXPECT_EQ(s.totalUnread(), 4u);
  s.markRead(a);
  EXPECT_EQ(s.threadAt(a)->unread, 0);
  EXPECT_EQ(s.totalUnread(), 1u);
}

TEST(MsgStore, OwnMessagesAreNeverUnread) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  const int a = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  const uint32_t q = s.addOwn(a, 10, "hi all", 6, Delivery::Sending);
  ASSERT_NE(q, 0u);
  EXPECT_EQ(s.threadAt(a)->unread, 0);
  const MsgRecord* m = s.msg(q);
  EXPECT_EQ(m->own, 1);
  EXPECT_EQ(m->delivery, uint8_t(Delivery::Sending));
  EXPECT_TRUE(s.setDelivery(q, Delivery::Delivered));
  EXPECT_EQ(s.msg(q)->delivery, uint8_t(Delivery::Delivered));
  EXPECT_TRUE(s.addEcho(q));
  EXPECT_TRUE(s.addEcho(q));
  EXPECT_EQ(s.msg(q)->echoes, 2);
}

TEST(MsgStore, EchoesStopAt255AndOnlyOnOwn) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  const int a = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  const uint32_t own = s.addOwn(a, 10, "x", 1, Delivery::None);
  const uint32_t in = addText(s, a, 11, "y");
  for (int i = 0; i < 300; i++) s.addEcho(own);
  EXPECT_EQ(s.msg(own)->echoes, 255);
  EXPECT_FALSE(s.addEcho(in));
  EXPECT_FALSE(s.setDelivery(in, Delivery::Delivered));
}

// --------------------------------------------------------- the 160-byte limit

TEST(MsgStore, TextIsCutAt160OnACharacterBoundary) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  const int a = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  std::string exact(160, 'a');
  EXPECT_EQ(s.msg(addText(s, a, 1, exact.c_str()))->text_len, 160);
  std::string longer(200, 'b');
  EXPECT_EQ(s.msg(addText(s, a, 2, longer.c_str()))->text_len, 160);
  // 159 ASCII bytes, then a 2-byte character that would straddle 160.
  std::string utf = std::string(159, 'c') + "\xC3\xA9";
  EXPECT_EQ(s.msg(addText(s, a, 3, utf.c_str()))->text_len, 159);
}

// ------------------------------------------------------------- persistence

TEST(MsgStore, RoundTrip) {
  MsgStore a;
  ASSERT_TRUE(a.begin(8, 8));
  memset(kPeer, 5, sizeof(kPeer));
  const int ch = a.thread(ThreadKind::Channel, kChanA, 16, "Public");
  const int dm = a.thread(ThreadKind::Direct, kPeer, 32, "Bob");
  addText(a, ch, 100, "hello");
  const uint32_t own = a.addOwn(dm, 101, "hi bob", 6, Delivery::Delivered);
  a.addEcho(own);

  MemSink idx, ring;
  ASSERT_TRUE(a.saveIndex(idx));
  ASSERT_TRUE(a.saveRing(ring));

  MsgStore b;
  ASSERT_TRUE(b.begin(8, 8));
  MemSource idx_in(idx.bytes), ring_in(ring.bytes);
  ASSERT_EQ(b.loadIndex(idx_in), LoadResult::Ok);
  ASSERT_EQ(b.loadRing(ring_in), LoadResult::Ok);
  EXPECT_EQ(b.threadCount(), 2);
  EXPECT_EQ(b.find(ThreadKind::Direct, kPeer, 32), dm);
  EXPECT_EQ(b.threadAt(ch)->unread, 1);
  EXPECT_EQ(b.count(), 2);
  ASSERT_NE(b.msg(own), nullptr);
  EXPECT_EQ(textOf(b.msg(own)), "hi bob");
  EXPECT_EQ(b.msg(own)->echoes, 1);
  // New messages continue the sequence instead of reusing a number.
  EXPECT_GT(addText(b, ch, 102, "next"), own);
}

TEST(MsgStore, LoadIntoASmallerRingKeepsTheNewest) {
  MsgStore a;
  ASSERT_TRUE(a.begin(8, 8));
  const int ch = a.thread(ThreadKind::Channel, kChanA, 16, "a");
  uint32_t last = 0;
  for (int i = 0; i < 6; i++) last = addText(a, ch, 100 + i, "m");
  MemSink idx, ring;
  ASSERT_TRUE(a.saveIndex(idx));
  ASSERT_TRUE(a.saveRing(ring));
  MsgStore b;
  ASSERT_TRUE(b.begin(4, 8));
  MemSource idx_in(idx.bytes), ring_in(ring.bytes);
  ASSERT_EQ(b.loadIndex(idx_in), LoadResult::Ok);
  ASSERT_EQ(b.loadRing(ring_in), LoadResult::Ok);
  EXPECT_EQ(b.count(), 4);
  EXPECT_NE(b.msg(last), nullptr);
}

// ---------------------------------------------- bad files are never trusted

TEST(MsgStore, EmptyFileIsEmpty) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  std::vector<uint8_t> none;
  MemSource a(none), b(none);
  EXPECT_EQ(s.loadIndex(a), LoadResult::Empty);
  EXPECT_EQ(s.loadRing(b), LoadResult::Empty);
}

TEST(MsgStore, GarbageIsABadHeader) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  std::vector<uint8_t> junk(300, 0x5A);
  MemSource a(junk), b(junk);
  EXPECT_EQ(s.loadIndex(a), LoadResult::BadHeader);
  EXPECT_EQ(s.loadRing(b), LoadResult::BadHeader);
  EXPECT_EQ(s.threadCount(), 0);
  EXPECT_EQ(s.count(), 0);
}

TEST(MsgStore, TruncatedFileIsABadHeaderAndLoadsNothing) {
  MsgStore a;
  ASSERT_TRUE(a.begin(8, 8));
  const int ch = a.thread(ThreadKind::Channel, kChanA, 16, "a");
  addText(a, ch, 1, "x");
  addText(a, ch, 2, "y");
  MemSink ring;
  ASSERT_TRUE(a.saveRing(ring));
  ring.bytes.resize(ring.bytes.size() - 10);   // cut inside the last record
  MsgStore b;
  ASSERT_TRUE(b.begin(8, 8));
  MemSource in(ring.bytes);
  EXPECT_EQ(b.loadRing(in), LoadResult::BadHeader);
  EXPECT_EQ(b.count(), 0);
}

TEST(MsgStore, UnknownVersionIsReportedAndLoadsNothing) {
  MsgStore a;
  ASSERT_TRUE(a.begin(8, 8));
  a.thread(ThreadKind::Channel, kChanA, 16, "a");
  MemSink idx;
  ASSERT_TRUE(a.saveIndex(idx));
  // The header is magic (4 bytes) then version (uint16): make it a future one.
  ASSERT_GE(idx.bytes.size(), 6u);
  const uint16_t future = kMsgStoreVersion + 1;
  memcpy(&idx.bytes[4], &future, 2);
  MsgStore b;
  ASSERT_TRUE(b.begin(8, 8));
  MemSource in(idx.bytes);
  EXPECT_EQ(b.loadIndex(in), LoadResult::UnknownVersion);
  EXPECT_EQ(b.threadCount(), 0);
}

// A crash between the index write and the ring write can leave messages that
// point at a thread the index doesn't have. They are dropped at load, never
// shown under some other thread (Gemini, #1461).
TEST(MsgStore, MessagesForAMissingThreadAreDroppedAtLoad) {
  MsgStore a;
  ASSERT_TRUE(a.begin(8, 8));
  const int ch = a.thread(ThreadKind::Channel, kChanA, 16, "a");
  const int other = a.thread(ThreadKind::Channel, kChanB, 16, "b");
  const uint32_t keep = addText(a, ch, 1, "kept");
  const uint32_t orphan = addText(a, other, 2, "orphan");
  MemSink ring;
  ASSERT_TRUE(a.saveRing(ring));

  MsgStore idx_src;   // an index that only knows the first thread
  ASSERT_TRUE(idx_src.begin(8, 8));
  idx_src.thread(ThreadKind::Channel, kChanA, 16, "a");
  MemSink idx;
  ASSERT_TRUE(idx_src.saveIndex(idx));

  MsgStore b;
  ASSERT_TRUE(b.begin(8, 8));
  MemSource idx_in(idx.bytes), ring_in(ring.bytes);
  ASSERT_EQ(b.loadIndex(idx_in), LoadResult::Ok);
  ASSERT_EQ(b.loadRing(ring_in), LoadResult::Ok);
  EXPECT_NE(b.msg(keep), nullptr);
  EXPECT_EQ(b.msg(orphan), nullptr);
  EXPECT_EQ(b.count(), 1);
}

// ------------------------------------------------- C4: resends of own DMs

// The app resends an unacknowledged DM with attempt > 0. That is the same
// message, not a new one (#1464).
TEST(MsgStore, ResendFindsTheNewestMatchingOwnMessage) {
  MsgStore s;
  ASSERT_TRUE(s.begin(16, 8));
  memset(kPeer, 6, sizeof(kPeer));
  const int dm = s.thread(ThreadKind::Direct, kPeer, 32, "Bob");
  const int ch = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  s.addOwn(dm, 1, "hello", 5, Delivery::Sending);
  const uint32_t second = s.addOwn(dm, 2, "hello", 5, Delivery::Sending);
  s.addOwn(ch, 3, "hello", 5, Delivery::None);              // other thread
  s.addIncoming(dm, 4, "", "hello", 5, 0xFF, 0);            // not own
  EXPECT_EQ(s.findOwn(dm, "hello", 5), second);
  EXPECT_EQ(s.findOwn(dm, "hell", 4), 0u);                  // text must match exactly
  EXPECT_EQ(s.findOwn(dm, "other", 5), 0u);
}

// "OK" fails, a second "OK" is delivered, then the app resends the first:
// the resend must not land on the delivered one (Gemini, #1464).
TEST(MsgStore, ResendSkipsDeliveredMessages) {
  MsgStore s;
  ASSERT_TRUE(s.begin(16, 8));
  memset(kPeer, 6, sizeof(kPeer));
  const int dm = s.thread(ThreadKind::Direct, kPeer, 32, "Bob");
  const uint32_t first = s.addOwn(dm, 1, "OK", 2, Delivery::Sending);
  const uint32_t second = s.addOwn(dm, 2, "OK", 2, Delivery::Sending);
  s.setDelivery(second, Delivery::Delivered);
  EXPECT_EQ(s.findOwn(dm, "OK", 2), first);
  s.setDelivery(first, Delivery::Delivered);
  EXPECT_EQ(s.findOwn(dm, "OK", 2), 0u);
}

// ------------------------------------------- C3: snapshot, written in slices

// The flash backend serializes into one buffer, then writes it out a slice
// per loop tick (#1463). The slices must reassemble to the same file.
TEST(MsgStoreFlash, SnapshotWrittenInSlicesRoundTrips) {
  MsgStore a;
  ASSERT_TRUE(a.begin(50, 8));
  const int ch = a.thread(ThreadKind::Channel, kChanA, 16, "a");
  for (int i = 0; i < 40; i++) addText(a, ch, 100 + i, "a message of some length");

  BufferSink snap;
  ASSERT_TRUE(snap.reserve(a.ringBytesMax()));
  ASSERT_TRUE(a.saveRing(snap));
  EXPECT_LE(snap.size(), a.ringBytesMax());

  MemSink file;
  ChunkedSave slices;
  slices.begin(snap.size(), 512);
  size_t from, to, steps = 0;
  while (slices.next(&from, &to)) {
    ASSERT_TRUE(file.write(snap.data() + from, to - from));
    steps++;
  }
  EXPECT_EQ(steps, (snap.size() + 511) / 512);

  MsgStore b;
  ASSERT_TRUE(b.begin(50, 8));
  MemSink idx;
  ASSERT_TRUE(a.saveIndex(idx));
  MemSource idx_in(idx.bytes), ring_in(file.bytes);
  ASSERT_EQ(b.loadIndex(idx_in), LoadResult::Ok);
  ASSERT_EQ(b.loadRing(ring_in), LoadResult::Ok);
  EXPECT_EQ(b.count(), 40);
}

TEST(MsgStoreFlash, BufferSinkRefusesOverflow) {
  BufferSink s;
  ASSERT_TRUE(s.reserve(8));
  EXPECT_TRUE(s.write("12345678", 8));
  EXPECT_FALSE(s.write("9", 1));
  s.clear();
  EXPECT_TRUE(s.write("9", 1));
  EXPECT_EQ(s.size(), 1u);
}

TEST(MsgStoreFlash, SnapshotFitsTheWorstCase) {
  MsgStore a;
  ASSERT_TRUE(a.begin(20, 4));
  for (int t = 0; t < 4; t++) {
    uint8_t key[16];
    memset(key, t + 10, sizeof(key));
    a.thread(ThreadKind::Channel, key, 16, "t");
  }
  const int ch = a.find(ThreadKind::Channel, (const uint8_t*)"\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a\x0a", 16);
  for (int i = 0; i < 25; i++) addText(a, ch, i, std::string(160, 'z').c_str());
  BufferSink ring, idx;
  ASSERT_TRUE(ring.reserve(a.ringBytesMax()));
  ASSERT_TRUE(idx.reserve(a.indexBytesMax()));
  EXPECT_TRUE(a.saveRing(ring));
  EXPECT_TRUE(a.saveIndex(idx));
  EXPECT_EQ(ring.size(), a.ringBytesMax());   // full store: exactly the worst case
  EXPECT_EQ(idx.size(), a.indexBytesMax());
}

// ------------------------------- #1472: in-place writes of what changed only

namespace {

// A file image the size the store's fixed layout says, updated in place.
struct FileImage {
  std::vector<uint8_t> idx, ring;
  void fullWrite(const MsgStore& s) {
    MemSink i, r;
    ASSERT_TRUE(s.saveIndex(i));
    ASSERT_TRUE(s.saveRing(r));
    idx = i.bytes;
    ring = r.bytes;
  }
  // Applies every pending in-place write; returns how many bytes went out.
  size_t applyDirty(MsgStore& s) {
    size_t bytes = 0;
    DirtyWrite w;
    while (s.nextDirty(&w)) {
      std::vector<uint8_t>& f = w.ring ? ring : idx;
      EXPECT_LE(w.offset + w.len, f.size());
      if (w.offset + w.len > f.size()) return bytes;
      memcpy(&f[w.offset], w.data, w.len);
      bytes += w.len;
    }
    return bytes;
  }
};

}  // namespace

TEST(MsgStoreInPlace, FileSizesAreFixed) {
  MsgStore s;
  ASSERT_TRUE(s.begin(1000, 64));
  EXPECT_EQ(s.ringFileBytes(), 16u + 1000u * 192u);
  EXPECT_EQ(s.indexFileBytes(), 16u + 64u * 84u);
  MemSink r, i;
  ASSERT_TRUE(s.saveRing(r));    // empty store: still the whole layout
  ASSERT_TRUE(s.saveIndex(i));
  EXPECT_EQ(r.bytes.size(), s.ringFileBytes());
  EXPECT_EQ(i.bytes.size(), s.indexFileBytes());
}

TEST(MsgStoreInPlace, OneMessageWritesOneSlotNotTheFile) {
  MsgStore s;
  ASSERT_TRUE(s.begin(1000, 64));
  const int ch = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  for (int i = 0; i < 1000; i++) addText(s, ch, i, "fill");
  FileImage f;
  f.fullWrite(s);
  s.clearDirty();
  EXPECT_FALSE(s.anyDirty());

  addText(s, ch, 5000, "one more");   // evicts one, fills its slot
  EXPECT_TRUE(s.anyDirty());
  const size_t bytes = f.applyDirty(s);
  // One slot, one thread entry, two headers: well under 1 KB, not 192 KB.
  EXPECT_LE(bytes, 192u + 84u + 16u + 16u);
  EXPECT_FALSE(s.anyDirty());

  MsgStore b;
  ASSERT_TRUE(b.begin(1000, 64));
  MemSource i(f.idx), r(f.ring);
  ASSERT_EQ(b.loadIndex(i), LoadResult::Ok);
  ASSERT_EQ(b.loadRing(r), LoadResult::Ok);
  EXPECT_EQ(b.count(), 1000);
  uint32_t seqs[1];
  ASSERT_EQ(b.threadMessages(ch, seqs, 1), 1u);
  EXPECT_EQ(textOf(b.msg(seqs[0])), "one more");
  EXPECT_EQ(b.threadAt(ch)->unread, s.threadAt(ch)->unread);
}

TEST(MsgStoreInPlace, StatusAndEchoChangesAreDirtyToo) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  const int ch = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  const uint32_t q = s.addOwn(ch, 1, "x", 1, Delivery::Sending);
  FileImage f;
  f.fullWrite(s);
  s.clearDirty();
  s.addEcho(q);
  EXPECT_TRUE(s.anyDirty());
  f.applyDirty(s);
  s.setDelivery(q, Delivery::Delivered);
  f.applyDirty(s);
  s.markRead(ch);   // only the thread entry
  f.applyDirty(s);
  MsgStore b;
  ASSERT_TRUE(b.begin(8, 8));
  MemSource i(f.idx), r(f.ring);
  ASSERT_EQ(b.loadIndex(i), LoadResult::Ok);
  ASSERT_EQ(b.loadRing(r), LoadResult::Ok);
  EXPECT_EQ(b.msg(q)->echoes, 1);
  EXPECT_EQ(b.msg(q)->delivery, uint8_t(Delivery::Delivered));
}

// A message that arrives while a burst is being written, in a slot the burst
// has already passed, must still go out (Gemini, #1472).
TEST(MsgStoreInPlace, ChangeMidBurstIsNotLost) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  const int ch = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  FileImage f;
  f.fullWrite(s);
  s.clearDirty();
  addText(s, ch, 1, "a");          // slot 0
  const uint32_t b = addText(s, ch, 2, "b");   // slot 1
  DirtyWrite w;
  for (int k = 0; k < 3; k++) {    // the thread entry, slot 0, slot 1: the cursor is past them
    ASSERT_TRUE(s.nextDirty(&w));
    std::vector<uint8_t>& file = w.ring ? f.ring : f.idx;
    memcpy(&file[w.offset], w.data, w.len);
  }
  s.setDelivery(b, Delivery::None);   // not own: no change
  s.markRead(ch);                     // the thread again, behind the cursor
  addText(s, ch, 3, "c");             // slot 2
  // Drain everything that is left, then load: nothing may be missing.
  f.applyDirty(s);
  EXPECT_FALSE(s.anyDirty());
  MsgStore r;
  ASSERT_TRUE(r.begin(8, 8));
  MemSource i(f.idx), rg(f.ring);
  ASSERT_EQ(r.loadIndex(i), LoadResult::Ok);
  ASSERT_EQ(r.loadRing(rg), LoadResult::Ok);
  EXPECT_EQ(r.count(), 3);
  EXPECT_EQ(r.threadAt(ch)->unread, 1);   // markRead then one more
}

// A power cut between a message and its thread entry leaves last_seq stale;
// it is rebuilt from the messages at load (Gemini, #1472).
TEST(MsgStoreInPlace, LastSeqIsRebuiltFromTheMessages) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  const int ch = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  addText(s, ch, 1, "a");
  MemSink idx;
  ASSERT_TRUE(s.saveIndex(idx));   // the index as of one message
  const uint32_t newest = addText(s, ch, 2, "b");
  MemSink ring;
  ASSERT_TRUE(s.saveRing(ring));   // the ring got the second one
  MsgStore r;
  ASSERT_TRUE(r.begin(8, 8));
  MemSource i(idx.bytes), rg(ring.bytes);
  ASSERT_EQ(r.loadIndex(i), LoadResult::Ok);
  ASSERT_EQ(r.loadRing(rg), LoadResult::Ok);
  EXPECT_EQ(r.threadAt(ch)->last_seq, newest);
  EXPECT_EQ(r.threadAt(ch)->last_time, 2u);
}

// SPIFFS writes 256-byte pages, so a 192-byte record can straddle two and
// tear on a power cut. A torn record or thread entry fails its CRC and is
// dropped at load, never shown as garbage (Gemini Epic gate, #1427).
TEST(MsgStoreInPlace, TornRecordIsDroppedAtLoad) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  const int ch = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  const uint32_t good = addText(s, ch, 1, "good");
  const uint32_t torn = addText(s, ch, 2, "torn");
  FileImage f;
  f.fullWrite(s);
  // Flip a byte inside the second record's text: half new, half old.
  const size_t rec1 = 16 + 192 * 1;
  f.ring[rec1 + 40] ^= 0x5A;
  MsgStore r;
  ASSERT_TRUE(r.begin(8, 8));
  MemSource i(f.idx), rg(f.ring);
  ASSERT_EQ(r.loadIndex(i), LoadResult::Ok);
  ASSERT_EQ(r.loadRing(rg), LoadResult::Ok);
  EXPECT_NE(r.msg(good), nullptr);
  EXPECT_EQ(r.msg(torn), nullptr);
  EXPECT_EQ(r.count(), 1);
}

TEST(MsgStoreInPlace, TornThreadEntryIsDroppedAtLoad) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  const int a = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  s.thread(ThreadKind::Channel, kChanB, 16, "b");
  FileImage f;
  f.fullWrite(s);
  f.idx[16 + 84 * 1 + 50] ^= 0x33;   // inside the second entry's name
  MsgStore r;
  ASSERT_TRUE(r.begin(8, 8));
  MemSource i(f.idx);
  ASSERT_EQ(r.loadIndex(i), LoadResult::Ok);
  EXPECT_EQ(r.threadCount(), 1);
  EXPECT_EQ(r.find(ThreadKind::Channel, kChanA, 16), a);
}

TEST(MsgStoreInPlace, InPlaceWritesCarryAValidCrc) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  const int ch = s.thread(ThreadKind::Channel, kChanA, 16, "a");
  FileImage f;
  f.fullWrite(s);
  s.clearDirty();
  const uint32_t q = s.addIncoming(ch, 5, "alice", "late sender set", 15, 3, 12);   // sender after add()
  f.applyDirty(s);
  MsgStore r;
  ASSERT_TRUE(r.begin(8, 8));
  MemSource i(f.idx), rg(f.ring);
  ASSERT_EQ(r.loadIndex(i), LoadResult::Ok);
  ASSERT_EQ(r.loadRing(rg), LoadResult::Ok);
  ASSERT_NE(r.msg(q), nullptr);
  EXPECT_STREQ(r.msg(q)->sender, "alice");
  EXPECT_EQ(r.msg(q)->hops, 3);
}

TEST(MsgStoreInPlace, NewThreadIsWrittenInItsSlot) {
  MsgStore s;
  ASSERT_TRUE(s.begin(8, 8));
  FileImage f;
  f.fullWrite(s);   // empty files at full size
  s.clearDirty();
  memset(kPeer, 2, sizeof(kPeer));
  const int dm = s.thread(ThreadKind::Direct, kPeer, 32, "Bob");
  addText(s, dm, 1, "hi");
  f.applyDirty(s);
  MsgStore b;
  ASSERT_TRUE(b.begin(8, 8));
  MemSource i(f.idx), r(f.ring);
  ASSERT_EQ(b.loadIndex(i), LoadResult::Ok);
  ASSERT_EQ(b.loadRing(r), LoadResult::Ok);
  EXPECT_EQ(b.find(ThreadKind::Direct, kPeer, 32), dm);
  EXPECT_EQ(b.count(), 1);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
