#include <gtest/gtest.h>
#include <cstring>

#include "helpers/wifi_observer/ObserverNeighbors.h"

using offband::NeighbourInfo;
using offband::NeighborTable;
using offband::RxRssiStamps;

// A distinct, recognizable public key per test node.
static void keyFor(uint8_t* key, uint8_t tag) {
    for (int i = 0; i < (int)offband::kNeighborKeyLen; i++) key[i] = (uint8_t)(tag + i);
}

static const NeighbourInfo* findKey(const NeighbourInfo* rows, int n, uint8_t tag) {
    uint8_t key[offband::kNeighborKeyLen];
    keyFor(key, tag);
    for (int i = 0; i < n; i++) {
        if (memcmp(rows[i].pub_key, key, sizeof(key)) == 0) return &rows[i];
    }
    return nullptr;
}

// ---- NeighborTable --------------------------------------------------------

TEST(NeighborTable, EmptyTableCountsZero) {
    NeighborTable<4> t;
    EXPECT_EQ(t.count(), 0);
    NeighbourInfo out[4];
    EXPECT_EQ(t.copyTo(out, 4), 0);
}

TEST(NeighborTable, PutNewStoresEveryField) {
    NeighborTable<4> t;
    uint8_t k[offband::kNeighborKeyLen]; keyFor(k, 0x10);
    t.put(k, 1000, 2000, -7.25f, -101);
    ASSERT_EQ(t.count(), 1);

    NeighbourInfo out[4];
    ASSERT_EQ(t.copyTo(out, 4), 1);
    EXPECT_EQ(memcmp(out[0].pub_key, k, sizeof(k)), 0);
    EXPECT_EQ(out[0].advert_timestamp, 1000u);
    EXPECT_EQ(out[0].heard_timestamp, 2000u);
    EXPECT_EQ(out[0].snr, -29);   // SNR x4, the repeater's convention
    EXPECT_EQ(out[0].rssi, -101);
}

TEST(NeighborTable, MatchExistingUpdatesInPlace) {
    NeighborTable<4> t;
    uint8_t k[offband::kNeighborKeyLen]; keyFor(k, 0x20);
    t.put(k, 100, 200, 1.0f, -90);
    t.put(k, 300, 400, 5.5f, -70);
    ASSERT_EQ(t.count(), 1);   // same node, not a second row

    NeighbourInfo out[4];
    ASSERT_EQ(t.copyTo(out, 4), 1);
    EXPECT_EQ(out[0].advert_timestamp, 300u);
    EXPECT_EQ(out[0].heard_timestamp, 400u);
    EXPECT_EQ(out[0].snr, 22);
    EXPECT_EQ(out[0].rssi, -70);
}

TEST(NeighborTable, EmptySlotsFillBeforeAnyEviction) {
    NeighborTable<3> t;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    uint8_t b[offband::kNeighborKeyLen]; keyFor(b, 0x40);
    uint8_t c[offband::kNeighborKeyLen]; keyFor(c, 0x50);
    t.put(a, 1, 500, 0, -80);
    t.put(b, 1, 100, 0, -80);   // older than a, but a free slot exists
    t.put(c, 1, 300, 0, -80);
    EXPECT_EQ(t.count(), 3);

    NeighbourInfo out[3];
    int n = t.copyTo(out, 3);
    EXPECT_NE(findKey(out, n, 0x30), nullptr);
    EXPECT_NE(findKey(out, n, 0x40), nullptr);
    EXPECT_NE(findKey(out, n, 0x50), nullptr);
}

TEST(NeighborTable, FullTableEvictsLeastRecentlyHeard) {
    NeighborTable<3> t;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    uint8_t b[offband::kNeighborKeyLen]; keyFor(b, 0x40);
    uint8_t c[offband::kNeighborKeyLen]; keyFor(c, 0x50);
    uint8_t d[offband::kNeighborKeyLen]; keyFor(d, 0x60);
    t.put(a, 1, 500, 0, -80);
    t.put(b, 1, 100, 0, -80);   // least recently heard
    t.put(c, 1, 300, 0, -80);
    t.put(d, 1, 600, 0, -80);
    EXPECT_EQ(t.count(), 3);

    NeighbourInfo out[3];
    int n = t.copyTo(out, 3);
    EXPECT_EQ(findKey(out, n, 0x40), nullptr);   // b evicted
    EXPECT_NE(findKey(out, n, 0x30), nullptr);
    EXPECT_NE(findKey(out, n, 0x50), nullptr);
    EXPECT_NE(findKey(out, n, 0x60), nullptr);
}

TEST(NeighborTable, RefreshedNodeIsNotTheOneEvicted) {
    NeighborTable<2> t;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    uint8_t b[offband::kNeighborKeyLen]; keyFor(b, 0x40);
    uint8_t c[offband::kNeighborKeyLen]; keyFor(c, 0x50);
    t.put(a, 1, 100, 0, -80);
    t.put(b, 1, 200, 0, -80);
    t.put(a, 1, 300, 0, -80);   // a heard again: b is now the oldest
    t.put(c, 1, 400, 0, -80);

    NeighbourInfo out[2];
    int n = t.copyTo(out, 2);
    EXPECT_NE(findKey(out, n, 0x30), nullptr);
    EXPECT_EQ(findKey(out, n, 0x40), nullptr);
    EXPECT_NE(findKey(out, n, 0x50), nullptr);
}

TEST(NeighborTable, ZeroHeardTimeStillCountsAsOccupied) {
    // heard_timestamp == 0 is the empty-slot marker. A clock that reads 0 must not
    // make a real neighbor vanish from count() or be overwritten as if free.
    NeighborTable<2> t;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    uint8_t b[offband::kNeighborKeyLen]; keyFor(b, 0x40);
    t.put(a, 0, 0, 0, -80);
    EXPECT_EQ(t.count(), 1);
    t.put(b, 0, 0, 0, -80);
    EXPECT_EQ(t.count(), 2);
}

TEST(NeighborTable, SnrAndRssiClampToInt8) {
    NeighborTable<2> t;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    uint8_t b[offband::kNeighborKeyLen]; keyFor(b, 0x40);
    t.put(a, 1, 1, 40.0f, -140);    // 160 and -140 do not fit int8
    t.put(b, 1, 2, -40.0f, 20);
    NeighbourInfo out[2];
    int n = t.copyTo(out, 2);
    ASSERT_EQ(n, 2);
    const NeighbourInfo* ra = findKey(out, n, 0x30);
    const NeighbourInfo* rb = findKey(out, n, 0x40);
    ASSERT_NE(ra, nullptr);
    ASSERT_NE(rb, nullptr);
    EXPECT_EQ(ra->snr, 127);
    EXPECT_EQ(ra->rssi, -128);
    EXPECT_EQ(rb->snr, -128);
    EXPECT_EQ(rb->rssi, 20);
}

TEST(NeighborTable, CopyToHonorsMaxAndBadArgs) {
    NeighborTable<4> t;
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t k[offband::kNeighborKeyLen]; keyFor(k, (uint8_t)(0x30 + 0x10 * i));
        t.put(k, 1, 10 + i, 0, -80);
    }
    NeighbourInfo out[4];
    EXPECT_EQ(t.copyTo(out, 2), 2);
    EXPECT_EQ(t.copyTo(out, 0), 0);
    EXPECT_EQ(t.copyTo(out, -1), 0);
    EXPECT_EQ(t.copyTo(nullptr, 4), 0);
    EXPECT_EQ(t.copyTo(out, 4), 3);
}

TEST(NeighborTable, ClearEmptiesTheTable) {
    NeighborTable<2> t;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    t.put(a, 1, 1, 0, -80);
    t.clear();
    EXPECT_EQ(t.count(), 0);
}

TEST(NeighborTable, MatchBehindAnOlderSlotUpdatesNotEvicts) {
    // b sits in a later slot than the older a; hearing b again must update b, not
    // evict a as the least recently heard.
    NeighborTable<2> t;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    uint8_t b[offband::kNeighborKeyLen]; keyFor(b, 0x40);
    t.put(a, 1, 100, 0, -80);
    t.put(b, 1, 200, 0, -80);
    t.put(b, 2, 300, 0, -60);
    ASSERT_EQ(t.count(), 2);

    NeighbourInfo out[2];
    int n = t.copyTo(out, 2);
    const NeighbourInfo* ra = findKey(out, n, 0x30);
    const NeighbourInfo* rb = findKey(out, n, 0x40);
    ASSERT_NE(ra, nullptr);
    ASSERT_NE(rb, nullptr);
    EXPECT_EQ(ra->heard_timestamp, 100u);
    EXPECT_EQ(rb->heard_timestamp, 300u);
    EXPECT_EQ(rb->rssi, -60);
}

TEST(NeighborTable, ZeroHeardTimeIsStoredAsOne) {
    NeighborTable<1> t;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    t.put(a, 0, 0, 0, -80);
    NeighbourInfo out[1];
    ASSERT_EQ(t.copyTo(out, 1), 1);
    EXPECT_EQ(out[0].heard_timestamp, 1u);
}

TEST(NeighborTable, ClampBoundariesAreExact) {
    NeighborTable<4> t;
    const int rssi_in[4]  = { -128, -129, 127, 128 };
    const int8_t rssi_out[4] = { -128, -128, 127, 127 };
    const float snr_in[4]  = { -32.0f, -32.25f, 31.75f, 32.0f };   // x4: -128, -129, 127, 128
    const int8_t snr_out[4] = { -128, -128, 127, 127 };
    for (int i = 0; i < 4; i++) {
        uint8_t k[offband::kNeighborKeyLen]; keyFor(k, (uint8_t)(0x30 + 0x10 * i));
        t.put(k, 1, 10 + i, snr_in[i], rssi_in[i]);
    }
    NeighbourInfo out[4];
    int n = t.copyTo(out, 4);
    ASSERT_EQ(n, 4);
    for (int i = 0; i < 4; i++) {
        const NeighbourInfo* r = findKey(out, n, (uint8_t)(0x30 + 0x10 * i));
        ASSERT_NE(r, nullptr) << i;
        EXPECT_EQ(r->rssi, rssi_out[i]) << i;
        EXPECT_EQ(r->snr, snr_out[i]) << i;
    }
}

TEST(NeighborTable, UnknownRssiKeepsTheLastReading) {
    NeighborTable<2> t;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    t.put(a, 1, 100, 0, -90);
    t.put(a, 2, 200, 0, 0, false);   // no reading for this advert
    NeighbourInfo out[2];
    ASSERT_EQ(t.copyTo(out, 2), 1);
    EXPECT_EQ(out[0].rssi, -90);
    EXPECT_EQ(out[0].heard_timestamp, 200u);   // the rest of the row still updates
}

TEST(NeighborTable, UnknownRssiOnANewRowIsNoReading) {
    NeighborTable<1> t;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    uint8_t b[offband::kNeighborKeyLen]; keyFor(b, 0x40);
    t.put(a, 1, 100, 0, -90);
    t.put(b, 1, 200, 0, -55, false);   // evicts a; must not inherit a's -90
    NeighbourInfo out[1];
    ASSERT_EQ(t.copyTo(out, 1), 1);
    EXPECT_NE(findKey(out, 1, 0x40), nullptr);
    EXPECT_EQ(out[0].rssi, 0);
}

// ---- advert filter ---------------------------------------------------------

TEST(NeighborAdvert, ShareNeedsTransportCodesBothZero) {
    EXPECT_TRUE(offband::isShareAdvert(true, 0, 0));
    EXPECT_FALSE(offband::isShareAdvert(true, 0, 1));
    EXPECT_FALSE(offband::isShareAdvert(true, 5, 0));
    EXPECT_FALSE(offband::isShareAdvert(false, 0, 0));   // no transport codes at all
}

TEST(NeighborAdvert, OnlyDirectNonShareRepeaterAdvertsCount) {
    EXPECT_TRUE(offband::isNeighborAdvert(0, false, true, true));
    EXPECT_FALSE(offband::isNeighborAdvert(1, false, true, true));    // relayed one hop
    EXPECT_FALSE(offband::isNeighborAdvert(0, true, true, true));     // passed on by a Share
    EXPECT_FALSE(offband::isNeighborAdvert(0, false, false, true));   // unparseable app data
    EXPECT_FALSE(offband::isNeighborAdvert(0, false, true, false));   // a companion or room
}

// ---- regions request (#1398 T2) -------------------------------------------

TEST(RegionsRequest, LayoutIsWhatTheRepeaterParses) {
    // simple_repeater MyMesh::onAnonDataRecv: data[0..3] = sender timestamp (our tag),
    // data[4] = sub-type, and handleAnonRegionsReq reads data[5] as the reply path length.
    uint8_t buf[offband::kRegionsRequestLen + 4];
    memset(buf, 0xEE, sizeof(buf));
    const uint32_t tag = 0x12345678u;
    ASSERT_EQ(offband::buildRegionsRequest(tag, buf), offband::kRegionsRequestLen);
    uint32_t got;
    memcpy(&got, buf, 4);
    EXPECT_EQ(got, tag);
    EXPECT_EQ(buf[4], 0x01);   // ANON_REQ_TYPE_REGIONS, reused, not a new allocation
    EXPECT_EQ(buf[5], 0x00);   // reply path length 0: answer us zero-hop direct
    EXPECT_EQ(buf[6], 0xEE);   // nothing written past the request
}

TEST(RegionsRequest, SubTypeIsNotALoginByte) {
    // The repeater treats data[4] == 0 or >= ' ' as a login password; the regions
    // sub-type must sit in the gap between them or the request becomes a login.
    EXPECT_NE(offband::kAnonReqTypeRegions, 0);
    EXPECT_LT(offband::kAnonReqTypeRegions, ' ');
}

// ---- RxRssiStamps ----------------------------------------------------------

TEST(RxRssiStamps, LookupReturnsTheStampedValue) {
    RxRssiStamps<4> s;
    int pkt;
    s.stamp(&pkt, -97);
    int8_t v = 0;
    ASSERT_TRUE(s.lookup(&pkt, &v));
    EXPECT_EQ(v, -97);
}

TEST(RxRssiStamps, UnknownPacketIsNotFound) {
    RxRssiStamps<4> s;
    int a, b;
    s.stamp(&a, -97);
    int8_t v = 42;
    EXPECT_FALSE(s.lookup(&b, &v));
    EXPECT_EQ(v, 42);   // untouched on a miss
    EXPECT_FALSE(s.lookup(nullptr, &v));
}

TEST(RxRssiStamps, ReusedPacketIsRestampedNotDuplicated) {
    // The packet pool recycles the same Packet objects; a reused one must report the
    // newest reception, and must not consume a second slot.
    RxRssiStamps<2> s;
    int a, b;
    s.stamp(&a, -100);
    s.stamp(&a, -60);
    s.stamp(&b, -70);
    int8_t v = 0;
    ASSERT_TRUE(s.lookup(&a, &v));
    EXPECT_EQ(v, -60);
    ASSERT_TRUE(s.lookup(&b, &v));
    EXPECT_EQ(v, -70);
}

TEST(RxRssiStamps, PoolSizedTableNeverEvictsALivePacket) {
    // Sized to the packet pool, every distinct packet keeps its slot.
    int pkts[16];
    RxRssiStamps<16> s;
    for (int i = 0; i < 16; i++) s.stamp(&pkts[i], -50 - i);
    for (int i = 0; i < 16; i++) {
        int8_t v = 0;
        ASSERT_TRUE(s.lookup(&pkts[i], &v)) << i;
        EXPECT_EQ(v, -50 - i);
    }
}

TEST(RxRssiStamps, OverflowEvictsOldestAndClamps) {
    int pkts[3];
    RxRssiStamps<2> s;
    s.stamp(&pkts[0], -300);   // clamps to int8
    s.stamp(&pkts[1], -80);
    s.stamp(&pkts[2], -81);    // a third distinct packet evicts the oldest
    int8_t v = 0;
    EXPECT_FALSE(s.lookup(&pkts[0], &v));
    ASSERT_TRUE(s.lookup(&pkts[1], &v));
    EXPECT_EQ(v, -80);
    ASSERT_TRUE(s.lookup(&pkts[2], &v));
    EXPECT_EQ(v, -81);

    RxRssiStamps<1> c;
    c.stamp(&pkts[0], -300);
    ASSERT_TRUE(c.lookup(&pkts[0], &v));
    EXPECT_EQ(v, -128);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
