// Epic I (#1433), I0 (#1674): the SD card's message archive, I4 (#1678) and
// I8 (#1704). Owner, 2026-10-21, option B: flash keeps the newest messages as
// today, the card holds the full history and only ever catches up with the
// live store. These pin the first copy, the resync after a pulled card, the
// header-after-records rule that keeps an interrupted copy honest, and the
// wrap at the archive's size.

#include <gtest/gtest.h>

#include <string.h>

#include <string>
#include <vector>

#include "helpers/gui/MsgStore.h"
#include "helpers/gui/SdArchive.h"

using namespace offband::gui;

namespace {

const uint8_t kChanA[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
const uint8_t kChanB[16] = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
constexpr uint32_t kOwner = 0x0FFBA0D1;   // this radio
constexpr uint32_t kOtherRadio = 0x12345678;

// A file in memory. `fail_after` writes succeed, then every write fails: a
// card pulled mid-write. The bytes survive, as they would on the card.
class MemFile : public RandomFile {
public:
  std::vector<uint8_t> bytes;
  int fail_after = -1;   // -1: never fail
  int fail_only = -1;    // this one write fails and the ones after it land: a bad sector
  int writes = 0;
  int attempts = 0;
  bool readAt(uint32_t off, void* data, size_t len) override {
    if (off + len > bytes.size()) return false;
    memcpy(data, bytes.data() + off, len);
    return true;
  }
  bool writeAt(uint32_t off, const void* data, size_t len) override {
    if (attempts++ == fail_only) return false;
    if (fail_after >= 0 && writes >= fail_after) return false;
    writes++;
    if (off + len > bytes.size()) bytes.resize(off + len, 0);
    memcpy(bytes.data() + off, data, len);
    return true;
  }
  uint32_t size() override { return uint32_t(bytes.size()); }
  bool flush() override { return fail_after < 0 || writes < fail_after; }
};

uint32_t add(MsgStore& s, int t, const std::string& text) {
  return s.addIncoming(t, 1000, "Offband-peer", text.c_str(), text.size(), 1, 20);
}

std::string textOf(const MsgRecord& m) { return std::string(m.text, m.text_len); }

struct Fixture {
  MsgStore live;
  MemFile rec, thr;
  int chan_a = -1, chan_b = -1;
  explicit Fixture(uint16_t cap = 100) {
    live.begin(cap, 16);
    chan_a = live.thread(ThreadKind::Channel, kChanA, 16, "#alpha");
    chan_b = live.thread(ThreadKind::Channel, kChanB, 16, "#bravo");
  }
};

}  // namespace

// ------------------------------------------------------------------ opening

TEST(SdArchive, EmptyFilesAreLaidOutFresh) {
  Fixture f;
  SdArchive a;
  EXPECT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  EXPECT_TRUE(a.ready());
  EXPECT_EQ(0u, a.archivedThrough());
  EXPECT_EQ(a.recordsFileBytes(), f.rec.size());
  EXPECT_EQ(a.threadsFileBytes(), f.thr.size());
  EXPECT_EQ(kArchiveHeaderLen + 50 * sizeof(MsgRecord), a.recordsFileBytes());
}

TEST(SdArchive, ReopensWhatItWrote) {
  Fixture f;
  add(f.live, f.chan_a, "one");
  add(f.live, f.chan_a, "two");
  {
    SdArchive a;
    ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
    EXPECT_EQ(0u, a.catchUp(f.live, 10));
  }
  SdArchive b;
  ASSERT_EQ(ArchiveOpen::Ok, b.open(f.rec, f.thr, kOwner, 50, 8));
  EXPECT_EQ(2u, b.archivedThrough());
  MsgRecord m;
  ASSERT_TRUE(b.read(2, &m));
  EXPECT_EQ("two", textOf(m));
}

TEST(SdArchive, GarbageIsABadHeaderNotAnArchive) {
  Fixture f;
  f.rec.bytes.assign(kArchiveHeaderLen + 50 * sizeof(MsgRecord), 0x5A);
  f.thr.bytes.assign(kArchiveHeaderLen + 8 * sizeof(ArchiveThread), 0x5A);
  SdArchive a;
  EXPECT_EQ(ArchiveOpen::BadHeader, a.open(f.rec, f.thr, kOwner, 50, 8));
  EXPECT_FALSE(a.ready());
}

TEST(SdArchive, ADifferentSizeIsABadHeader) {
  Fixture f;
  {
    SdArchive a;
    ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  }
  SdArchive b;
  EXPECT_EQ(ArchiveOpen::BadHeader, b.open(f.rec, f.thr, kOwner, 60, 8));
}

TEST(SdArchive, ACutShortFileIsABadHeader) {
  Fixture f;
  {
    SdArchive a;
    ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  }
  f.rec.bytes.resize(f.rec.bytes.size() / 2);
  SdArchive b;
  EXPECT_EQ(ArchiveOpen::BadHeader, b.open(f.rec, f.thr, kOwner, 50, 8));
}

// ------------------------------------------------------- the first copy (I8)

TEST(SdArchive, FirstCopyTakesEverythingOldestFirstInSlices) {
  Fixture f;
  for (int i = 1; i <= 10; i++) add(f.live, i % 2 ? f.chan_a : f.chan_b, "m" + std::to_string(i));
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  EXPECT_EQ(10u, a.pending(f.live));
  EXPECT_EQ(6u, a.catchUp(f.live, 4));
  EXPECT_EQ(4u, a.archivedThrough());
  EXPECT_EQ(0u, a.catchUp(f.live, 100));
  EXPECT_EQ(10u, a.archivedThrough());
  for (uint32_t s = 1; s <= 10; s++) {
    MsgRecord m;
    ArchiveThread t;
    ASSERT_TRUE(a.read(s, &m, &t)) << s;
    EXPECT_EQ("m" + std::to_string(s), textOf(m));
    EXPECT_EQ(0, memcmp(t.key, s % 2 ? kChanA : kChanB, 16));
    EXPECT_STREQ(s % 2 ? "#alpha" : "#bravo", t.name);
  }
}

TEST(SdArchive, AnInterruptedCopyResumesWhereTheHeaderSays) {
  Fixture f;
  for (int i = 1; i <= 8; i++) add(f.live, f.chan_a, "m" + std::to_string(i));
  {
    SdArchive a;
    ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
    a.catchUp(f.live, 3);   // power lost after this slice
  }
  SdArchive b;
  ASSERT_EQ(ArchiveOpen::Ok, b.open(f.rec, f.thr, kOwner, 50, 8));
  EXPECT_EQ(3u, b.archivedThrough());
  EXPECT_EQ(5u, b.pending(f.live));
  EXPECT_EQ(0u, b.catchUp(f.live, 100));
  EXPECT_EQ(8u, b.archivedThrough());
}

// A card pulled mid-slice: the header never claims a record that didn't land.
TEST(SdArchive, TheHeaderNeverClaimsARecordTheCardDoesntHave) {
  Fixture f;
  for (int i = 1; i <= 6; i++) add(f.live, f.chan_a, "m" + std::to_string(i));
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  f.rec.fail_after = f.rec.writes + 2;   // two writes land, then the card is gone
  bool ok = true;
  a.catchUp(f.live, 6, &ok);
  EXPECT_FALSE(ok);
  f.rec.fail_after = -1;   // the card comes back with what reached it
  SdArchive b;
  ASSERT_EQ(ArchiveOpen::Ok, b.open(f.rec, f.thr, kOwner, 50, 8));
  const uint32_t through = b.archivedThrough();
  // The slice failed before its header: the card still says what it said
  // before the slice, which was nothing.
  EXPECT_EQ(0u, through);
  for (uint32_t s = 1; s <= through; s++) {
    MsgRecord m;
    EXPECT_TRUE(b.read(s, &m)) << "header claims seq " << s << " but it isn't readable";
  }
}

// One record write fails and the card keeps taking writes after it (a bad
// sector, a brownout): the header must still stop short of the lost record.
TEST(SdArchive, OneFailedRecordWriteIsNeverCoveredByTheHeader) {
  Fixture f;
  for (int i = 1; i <= 6; i++) add(f.live, f.chan_a, "m" + std::to_string(i));
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  f.rec.fail_only = f.rec.attempts + 2;   // the third record write fails, later ones land
  bool ok = true;
  a.catchUp(f.live, 6, &ok);
  EXPECT_FALSE(ok);
  EXPECT_EQ(0u, a.archivedThrough());
  SdArchive b;
  ASSERT_EQ(ArchiveOpen::Ok, b.open(f.rec, f.thr, kOwner, 50, 8));
  EXPECT_EQ(0u, b.archivedThrough()) << "the header covers seq 3, which never reached the card";
  EXPECT_EQ(0u, b.catchUp(f.live, 100));
  MsgRecord m;
  ASSERT_TRUE(b.read(3, &m));
  EXPECT_EQ("m3", textOf(m));
}

// --------------------------------------------- a card coming back (I8 resync)

TEST(SdArchive, ResyncAddsOnlyWhatArrivedWhileTheCardWasOut) {
  Fixture f;
  for (int i = 1; i <= 5; i++) add(f.live, f.chan_a, "m" + std::to_string(i));
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  a.catchUp(f.live, 100);
  a.close();   // card pulled
  for (int i = 6; i <= 8; i++) add(f.live, f.chan_b, "m" + std::to_string(i));
  SdArchive b;   // card back
  ASSERT_EQ(ArchiveOpen::Ok, b.open(f.rec, f.thr, kOwner, 50, 8));
  EXPECT_EQ(3u, b.pending(f.live));
  EXPECT_EQ(0u, b.catchUp(f.live, 100));
  EXPECT_EQ(8u, b.archivedThrough());
}

TEST(SdArchive, ACaughtUpArchiveWritesNothing) {
  Fixture f;
  for (int i = 1; i <= 5; i++) add(f.live, f.chan_a, "m");
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  a.catchUp(f.live, 100);
  const int before = f.rec.writes + f.thr.writes;
  EXPECT_EQ(0u, a.catchUp(f.live, 100));
  EXPECT_EQ(before, f.rec.writes + f.thr.writes) << "a resync with nothing new wrote anyway";
}

TEST(SdArchive, ACardFailingAgainMidResyncLosesNothingWhenItReturns) {
  Fixture f;
  for (int i = 1; i <= 4; i++) add(f.live, f.chan_a, "m" + std::to_string(i));
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  a.catchUp(f.live, 100);
  for (int i = 5; i <= 10; i++) add(f.live, f.chan_a, "m" + std::to_string(i));
  f.rec.fail_after = f.rec.writes + 1;
  bool ok = true;
  a.catchUp(f.live, 100, &ok);
  EXPECT_FALSE(ok);
  f.rec.fail_after = -1;
  SdArchive b;
  ASSERT_EQ(ArchiveOpen::Ok, b.open(f.rec, f.thr, kOwner, 50, 8));
  EXPECT_EQ(0u, b.catchUp(f.live, 100));
  for (uint32_t s = 1; s <= 10; s++) {
    MsgRecord m;
    ASSERT_TRUE(b.read(s, &m)) << s;
    EXPECT_EQ("m" + std::to_string(s), textOf(m));
  }
}

// Messages that left the live store while the card was out never reach it.
// The archive skips the gap rather than waiting for seqs that can't come.
TEST(SdArchive, AGapLeftByEvictionIsSkippedNotWaitedOn) {
  Fixture f(5);   // a live store of 5 messages
  for (int i = 1; i <= 12; i++) add(f.live, f.chan_a, "m" + std::to_string(i));
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  EXPECT_EQ(5u, a.pending(f.live));
  EXPECT_EQ(0u, a.catchUp(f.live, 100));
  EXPECT_EQ(12u, a.archivedThrough());
  MsgRecord m;
  EXPECT_FALSE(a.read(3, &m)) << "seq 3 was evicted before the card saw it";
  EXPECT_TRUE(a.read(8, &m));
}

// ------------------------------------------------------------ size and edits

TEST(SdArchive, TheOldestGoWhenTheArchiveIsFull) {
  Fixture f(50);
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 10, 8));
  for (int i = 1; i <= 25; i++) {
    add(f.live, f.chan_a, "m" + std::to_string(i));
    a.catchUp(f.live, 100);
  }
  MsgRecord m;
  EXPECT_FALSE(a.read(15, &m)) << "seq 15 should have been overwritten by 25";
  ASSERT_TRUE(a.read(16, &m));
  EXPECT_EQ("m16", textOf(m));
  ASSERT_TRUE(a.read(25, &m));
  EXPECT_EQ("m25", textOf(m));
  EXPECT_EQ(a.recordsFileBytes(), f.rec.size()) << "the file grew past its fixed size";
}

TEST(SdArchive, ADeliveryChangeIsRewrittenInPlace) {
  Fixture f;
  const uint32_t seq = f.live.addOwn(f.chan_a, 1000, "hello", 5, Delivery::Sending);
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  a.catchUp(f.live, 100);
  f.live.setDelivery(seq, Delivery::Delivered);
  EXPECT_TRUE(a.update(f.live, seq));
  MsgRecord m;
  ASSERT_TRUE(a.read(seq, &m));
  EXPECT_EQ(uint8_t(Delivery::Delivered), m.delivery);
}

TEST(SdArchive, UpdatingSomethingNotArchivedDoesNothing) {
  Fixture f;
  const uint32_t seq = add(f.live, f.chan_a, "later");
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  const int before = f.rec.writes;
  EXPECT_TRUE(a.update(f.live, seq));
  EXPECT_EQ(before, f.rec.writes) << "updated a record the card hasn't taken yet";
}

TEST(SdArchive, ThreadsAreKeptOncePerKey) {
  Fixture f;
  for (int i = 1; i <= 6; i++) add(f.live, i % 2 ? f.chan_a : f.chan_b, "m");
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  a.catchUp(f.live, 2);
  a.catchUp(f.live, 2);
  a.catchUp(f.live, 2);
  int used = 0;
  for (uint32_t i = 0; i < 8; i++) {
    ArchiveThread t;
    const uint32_t off = kArchiveHeaderLen + i * sizeof(ArchiveThread);
    ASSERT_TRUE(f.thr.readAt(off, &t, sizeof(t)));
    if (t.kind) used++;
  }
  EXPECT_EQ(2, used);
}

TEST(SdArchive, ReadingAnythingNotThereIsFalse) {
  Fixture f;
  add(f.live, f.chan_a, "m");
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  a.catchUp(f.live, 100);
  MsgRecord m;
  EXPECT_FALSE(a.read(0, &m));
  EXPECT_FALSE(a.read(2, &m));
  EXPECT_FALSE(a.read(51, &m)) << "seq 51 maps to seq 1's slot but isn't seq 1";
}

// ------------------------------------------------- whose card, and resets

TEST(SdArchive, AnotherRadiosCardIsForeignNeverAppendedTo) {
  Fixture f;
  {
    SdArchive a;
    ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOtherRadio, 50, 8));
  }
  const auto before = f.rec.bytes;
  SdArchive b;
  EXPECT_EQ(ArchiveOpen::Foreign, b.open(f.rec, f.thr, kOwner, 50, 8));
  EXPECT_FALSE(b.ready());
  EXPECT_EQ(before, f.rec.bytes);
}

// The live store lost its flash files and restarted at seq 1 while the card
// holds up to 10. Without realigning, nothing new would ever reach the card.
TEST(SdArchive, ARestartedLiveStoreIsMovedAheadOfTheCard) {
  Fixture f;
  for (int i = 1; i <= 10; i++) add(f.live, f.chan_a, "old" + std::to_string(i));
  {
    SdArchive a;
    ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
    a.catchUp(f.live, 100);
  }
  MsgStore fresh;   // the reset store
  fresh.begin(100, 16);
  const int ch = fresh.thread(ThreadKind::Channel, kChanA, 16, "#alpha");
  SdArchive b;
  ASSERT_EQ(ArchiveOpen::Ok, b.open(f.rec, f.thr, kOwner, 50, 8));
  EXPECT_TRUE(b.alignLive(fresh));
  const uint32_t seq = add(fresh, ch, "new");
  EXPECT_GT(seq, 10u);
  EXPECT_EQ(0u, b.catchUp(fresh, 100));
  MsgRecord m;
  ASSERT_TRUE(b.read(seq, &m));
  EXPECT_EQ("new", textOf(m));
  ASSERT_TRUE(b.read(5, &m)) << "the old history must survive the reset";
  EXPECT_EQ("old5", textOf(m));
}

// The caller only ever calls catchUp: the realignment can't be forgotten.
TEST(SdArchive, CatchUpRealignsARestartedStoreOnItsOwn) {
  Fixture f;
  for (int i = 1; i <= 10; i++) add(f.live, f.chan_a, "old");
  {
    SdArchive a;
    ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
    a.catchUp(f.live, 100);
  }
  MsgStore fresh;
  fresh.begin(100, 16);
  const int ch = fresh.thread(ThreadKind::Channel, kChanA, 16, "#alpha");
  SdArchive b;
  ASSERT_EQ(ArchiveOpen::Ok, b.open(f.rec, f.thr, kOwner, 50, 8));
  b.catchUp(fresh, 100);   // nothing to copy yet, but it aligns
  const uint32_t seq = add(fresh, ch, "new");
  EXPECT_GT(seq, 10u);
  EXPECT_EQ(0u, b.catchUp(fresh, 100));
  MsgRecord m;
  EXPECT_TRUE(b.read(seq, &m));
}

TEST(SdArchive, ALiveStoreAlreadyAheadIsLeftAlone) {
  Fixture f;
  for (int i = 1; i <= 3; i++) add(f.live, f.chan_a, "m");
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  a.catchUp(f.live, 1);
  EXPECT_FALSE(a.alignLive(f.live));
  EXPECT_EQ(3u, f.live.lastSeq());
}

// ------------------------------------------------- damage and limits

TEST(SdArchive, ATornRecordOnTheCardIsNotReadBack) {
  Fixture f;
  for (int i = 1; i <= 3; i++) add(f.live, f.chan_a, "m" + std::to_string(i));
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  a.catchUp(f.live, 100);
  const uint32_t off = kArchiveHeaderLen + 2 * sizeof(MsgRecord);   // seq 2's slot
  f.rec.bytes[off + 40] ^= 0xFF;
  MsgRecord m;
  EXPECT_FALSE(a.read(2, &m));
  EXPECT_TRUE(a.read(3, &m));
}

TEST(SdArchive, AFullThreadTableSkipsTheUnlabeledButKeepsGoing) {
  MsgStore live;
  live.begin(100, 16);
  uint8_t key[16];
  for (int t = 0; t < 4; t++) {
    memset(key, 10 + t, sizeof(key));
    const int th = live.thread(ThreadKind::Channel, key, 16, "#c");
    add(live, th, "m" + std::to_string(t + 1));
  }
  MemFile rec, thr;
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(rec, thr, kOwner, 50, 2));   // room for 2 threads
  bool ok = false;
  EXPECT_EQ(0u, a.catchUp(live, 100, &ok));
  EXPECT_TRUE(ok);
  EXPECT_EQ(4u, a.archivedThrough());
  MsgRecord m;
  EXPECT_TRUE(a.read(1, &m));
  EXPECT_TRUE(a.read(2, &m));
  EXPECT_FALSE(a.read(3, &m)) << "no thread slot left, so it was skipped";
}

TEST(SdArchive, ACardErrorOnTheThreadTableStopsTheSlice) {
  Fixture f;
  add(f.live, f.chan_a, "m1");
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  f.thr.fail_after = f.thr.writes;   // the thread write fails
  bool ok = true;
  EXPECT_EQ(1u, a.catchUp(f.live, 100, &ok));
  EXPECT_FALSE(ok);
  EXPECT_EQ(0u, a.archivedThrough()) << "a card error was taken for a full table and skipped";
}

// Settings' "Messages on card": what was written, kept across a reopen, never
// counting a skip, capped at the slots once the oldest are overwritten.
TEST(SdArchive, StoredCountsWhatTheCardHolds) {
  Fixture f(5);
  for (int i = 1; i <= 8; i++) add(f.live, f.chan_a, "m");   // 3 evicted: a gap
  {
    SdArchive a;
    ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 10, 8));
    EXPECT_EQ(0u, a.stored());
    a.catchUp(f.live, 100);
    EXPECT_EQ(5u, a.stored()) << "the evicted 3 never reached the card";
  }
  SdArchive b;
  ASSERT_EQ(ArchiveOpen::Ok, b.open(f.rec, f.thr, kOwner, 10, 8));
  EXPECT_EQ(5u, b.stored());
  for (int i = 0; i < 12; i++) {
    add(f.live, f.chan_a, "more");
    b.catchUp(f.live, 100);
  }
  EXPECT_EQ(10u, b.stored()) << "capped at the archive's 10 slots";
}

TEST(SdArchive, AFailedSliceDoesntCount) {
  Fixture f;
  for (int i = 1; i <= 4; i++) add(f.live, f.chan_a, "m");
  SdArchive a;
  ASSERT_EQ(ArchiveOpen::Fresh, a.open(f.rec, f.thr, kOwner, 50, 8));
  f.rec.fail_only = f.rec.attempts + 2;
  bool ok = true;
  a.catchUp(f.live, 100, &ok);
  EXPECT_FALSE(ok);
  EXPECT_EQ(0u, a.stored());
}

TEST(SdArchive, NothingWorksBeforeOpen) {
  Fixture f;
  add(f.live, f.chan_a, "m");
  SdArchive a;
  EXPECT_EQ(0u, a.pending(f.live));
  bool ok = true;
  EXPECT_EQ(0u, a.catchUp(f.live, 100, &ok));
  EXPECT_FALSE(ok);
  MsgRecord m;
  EXPECT_FALSE(a.read(1, &m));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
