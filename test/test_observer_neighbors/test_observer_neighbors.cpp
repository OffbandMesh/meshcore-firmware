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

// ---- regions reply (#1398 T3) ---------------------------------------------

// A reply as handleAnonRegionsReq builds it: {tag}{their clock}{csv}, then zero padding
// up to the cipher block, which the receiver sees as part of len.
static size_t makeReply(uint8_t* buf, uint32_t tag, const char* csv, size_t pad) {
    memcpy(buf, &tag, 4);
    const uint32_t clock = 1700000000u;
    memcpy(buf + 4, &clock, 4);
    size_t n = strlen(csv);
    memcpy(buf + 8, csv, n);
    memset(buf + 8 + n, 0, pad);
    return 8 + n + pad;
}

TEST(RegionsReply, ParsesTheNamesAndDropsPadding) {
    uint8_t buf[128];
    size_t len = makeReply(buf, 0xA1B2C3D4u, "*,hao,oh-sw", 5);
    char out[offband::kScopesLen];
    ASSERT_TRUE(offband::parseRegionsResponse(buf, len, 0xA1B2C3D4u, out, sizeof(out)));
    EXPECT_STREQ(out, "*,hao,oh-sw");
}

TEST(RegionsReply, EmptyListIsAValidAnswer) {
    // A repeater that denies flood everywhere answers with no names; that is a reply
    // ("no scopes"), not a failure.
    uint8_t buf[32];
    size_t len = makeReply(buf, 7, "", 8);
    char out[offband::kScopesLen] = "junk";
    ASSERT_TRUE(offband::parseRegionsResponse(buf, len, 7, out, sizeof(out)));
    EXPECT_STREQ(out, "");
}

TEST(RegionsReply, WrongTagOrShortReplyIsRejected) {
    uint8_t buf[64];
    size_t len = makeReply(buf, 7, "hao", 0);
    char out[offband::kScopesLen] = "keep";
    EXPECT_FALSE(offband::parseRegionsResponse(buf, len, 8, out, sizeof(out)));
    EXPECT_FALSE(offband::parseRegionsResponse(buf, 7, 7, out, sizeof(out)));   // no clock
    EXPECT_FALSE(offband::parseRegionsResponse(nullptr, len, 7, out, sizeof(out)));
    EXPECT_FALSE(offband::parseRegionsResponse(buf, len, 7, out, 0));
    EXPECT_STREQ(out, "keep");   // untouched on rejection
}

TEST(RegionsReply, OverlongListCutsAtTheLastWholeName) {
    uint8_t buf[200];
    size_t len = makeReply(buf, 7, "alpha,bravo,charlie", 0);
    char out[13];   // room for "alpha,bravo" (11) + NUL, not ",charlie"
    ASSERT_TRUE(offband::parseRegionsResponse(buf, len, 7, out, sizeof(out)));
    EXPECT_STREQ(out, "alpha,bravo");

    char tiny[4];   // not even the first name fits
    ASSERT_TRUE(offband::parseRegionsResponse(buf, len, 7, tiny, sizeof(tiny)));
    EXPECT_STREQ(tiny, "");
}

TEST(RegionsReply, ListThatExactlyFitsIsKept) {
    uint8_t buf[64];
    size_t len = makeReply(buf, 7, "alpha,bravo", 0);
    char out[12];   // 11 chars + NUL
    ASSERT_TRUE(offband::parseRegionsResponse(buf, len, 7, out, sizeof(out)));
    EXPECT_STREQ(out, "alpha,bravo");
}

TEST(InFlightRegionsQuery, MatchesOnlyItsOwnKeyAndTag) {
    offband::InFlightRegionsQuery q;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    uint8_t b[offband::kNeighborKeyLen]; keyFor(b, 0x40);
    EXPECT_FALSE(q.matches(a, 5));   // nothing in flight: never consume anything

    q.begin(a, 5, 1000);
    EXPECT_TRUE(q.active());
    EXPECT_TRUE(q.matches(a, 5));
    // Review #5: the same node answering one of the client's own requests carries the
    // client's tag. It must fall through to the client, not be swallowed here.
    EXPECT_FALSE(q.matches(a, 6));
    EXPECT_FALSE(q.matches(b, 5));   // another node reusing the tag value

    q.clear();
    EXPECT_FALSE(q.active());
    EXPECT_FALSE(q.matches(a, 5));
}

TEST(InFlightRegionsQuery, TimesOutAfterTheWindowEvenAcrossMillisWrap) {
    offband::InFlightRegionsQuery q;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x30);
    const uint32_t t0 = 0xFFFFF000u;   // a few seconds before millis() wraps
    q.begin(a, 5, t0);
    EXPECT_FALSE(q.timedOut(t0 + offband::kRegionQueryTimeoutMs - 1));
    EXPECT_TRUE(q.timedOut(t0 + offband::kRegionQueryTimeoutMs));
    q.clear();
    EXPECT_FALSE(q.timedOut(t0 + 10u * offband::kRegionQueryTimeoutMs));   // idle never times out
}

// ---- scope query walk (#1398 T4) ------------------------------------------

using offband::ScopeQueryRun;
using offband::ScopeStatus;

static int loadNeighbors(NeighbourInfo* rows, int n) {
    for (int i = 0; i < n; i++) {
        memset(&rows[i], 0, sizeof(rows[i]));
        keyFor(rows[i].pub_key, (uint8_t)(0x10 * (i + 1)));
        rows[i].heard_timestamp = 100 + i;
    }
    return n;
}

static bool keyIs(const uint8_t* key, uint8_t tag) {
    uint8_t k[offband::kNeighborKeyLen];
    keyFor(k, tag);
    return key != nullptr && memcmp(key, k, sizeof(k)) == 0;
}

TEST(ScopeQueryRun, AllThreeOutcomesAndAdvanceToNext) {
    NeighbourInfo rows[3];
    ScopeQueryRun<4> run;
    run.start(rows, loadNeighbors(rows, 3));
    ASSERT_TRUE(run.running());
    uint32_t now = 1000;

    // neighbor 1: answers
    const uint8_t* k = run.poll(now);
    ASSERT_TRUE(keyIs(k, 0x10));
    run.sent(true, 11, now);
    EXPECT_EQ(run.poll(now + 5), nullptr);   // one in flight: nothing else goes out
    EXPECT_TRUE(run.matches(k, 11));
    run.replied("hao,oh-sw");

    // neighbor 2: the send fails (no packet), the walk moves on at once
    k = run.poll(now + 10);
    ASSERT_TRUE(keyIs(k, 0x20));
    run.sent(false, 0, now + 10);

    // neighbor 3: silent, expires after the timeout
    k = run.poll(now + 20);
    ASSERT_TRUE(keyIs(k, 0x30));
    run.sent(true, 33, now + 20);
    EXPECT_EQ(run.poll(now + 20 + offband::kRegionQueryTimeoutMs - 1), nullptr);
    EXPECT_TRUE(run.running());
    EXPECT_EQ(run.poll(now + 20 + offband::kRegionQueryTimeoutMs), nullptr);   // expires, nothing left
    EXPECT_FALSE(run.running());

    ASSERT_EQ(run.resultCount(), 3);
    EXPECT_EQ(run.result(0).status, ScopeStatus::Responded);
    EXPECT_STREQ(run.result(0).scopes, "hao,oh-sw");
    EXPECT_EQ(run.result(1).status, ScopeStatus::SendFailed);
    EXPECT_STREQ(run.result(1).scopes, "");
    EXPECT_EQ(run.result(2).status, ScopeStatus::Timeout);
    EXPECT_STREQ(run.result(2).scopes, "");
    EXPECT_EQ(run.queriedCount(), 3);
}

TEST(ScopeQueryRun, AllTimeoutsStillFinish) {
    // plan Review #1/#3: a neighborhood where nobody answers never hangs the walk.
    NeighbourInfo rows[4];
    ScopeQueryRun<4> run;
    run.start(rows, loadNeighbors(rows, 4));
    uint32_t now = 0xFFFF0000u;   // across a millis() wrap, too
    int sends = 0;
    for (int step = 0; step < 100 && run.running(); step++) {
        const uint8_t* k = run.poll(now);
        if (k != nullptr) { run.sent(true, (uint32_t)(100 + sends), now); sends++; }
        now += offband::kRegionQueryTimeoutMs / 2;
    }
    EXPECT_FALSE(run.running());
    EXPECT_EQ(sends, 4);
    for (int i = 0; i < 4; i++) EXPECT_EQ(run.result(i).status, ScopeStatus::Timeout) << i;
}

TEST(ScopeQueryRun, StrayOrLateReplyChangesNothing) {
    NeighbourInfo rows[2];
    ScopeQueryRun<4> run;
    run.start(rows, loadNeighbors(rows, 2));
    const uint8_t* k1 = run.poll(0);
    run.sent(true, 7, 0);
    EXPECT_FALSE(run.matches(k1, 8));   // not our tag
    run.poll(offband::kRegionQueryTimeoutMs);   // neighbor 1 times out
    EXPECT_FALSE(run.matches(k1, 7));   // its late reply no longer matches anything
    run.replied("late");                 // and a stray replied() with nothing in flight is ignored
    EXPECT_EQ(run.result(0).status, ScopeStatus::Timeout);
    EXPECT_STREQ(run.result(0).scopes, "");
}

TEST(ScopeQueryRun, EmptyTableIsDoneAtOnce) {
    ScopeQueryRun<4> run;
    run.start(nullptr, 0);
    EXPECT_EQ(run.poll(0), nullptr);
    EXPECT_FALSE(run.running());
    EXPECT_EQ(run.resultCount(), 0);
}

TEST(ScopeQueryRun, MoreNeighborsThanSlotsAreCappedNotOverrun) {
    NeighbourInfo rows[6];
    ScopeQueryRun<4> run;
    run.start(rows, loadNeighbors(rows, 6));
    EXPECT_EQ(run.resultCount(), 4);
    EXPECT_EQ(run.totalCount(), 6);   // the report says how many were left out
}

TEST(ScopeQueryRun, RestartDiscardsTheOldWalk) {
    NeighbourInfo rows[2];
    ScopeQueryRun<4> run;
    run.start(rows, loadNeighbors(rows, 2));
    const uint8_t* k = run.poll(0);
    run.sent(true, 9, 0);
    run.start(rows, 1);   // a new discover.scopes while one is running
    EXPECT_FALSE(run.matches(k, 9));   // the old in-flight query is gone
    EXPECT_EQ(run.resultCount(), 1);
    EXPECT_EQ(run.result(0).status, ScopeStatus::Pending);
    EXPECT_EQ(run.queriedCount(), 0);
}

TEST(ScopeQueryRun, LateReplyToAClosedRequestIsStillOurs) {
    // T3 review: after a timeout, a late reply must not reach the client, whose
    // login/status branches match a sender on a key prefix alone.
    NeighbourInfo rows[2];
    ScopeQueryRun<4> run;
    run.start(rows, loadNeighbors(rows, 2));
    uint8_t k1[offband::kNeighborKeyLen];
    memcpy(k1, run.poll(0), sizeof(k1));
    run.sent(true, 7, 0);
    EXPECT_EQ(run.classify(k1, 7), offband::ScopeReply::Current);
    run.poll(offband::kRegionQueryTimeoutMs);   // times out, neighbor 2 is next
    EXPECT_EQ(run.classify(k1, 7), offband::ScopeReply::Retired);
    EXPECT_EQ(run.classify(k1, 8), offband::ScopeReply::None);   // the client's own tag
    uint8_t other[offband::kNeighborKeyLen]; keyFor(other, 0x99);
    EXPECT_EQ(run.classify(other, 7), offband::ScopeReply::None);
}

TEST(ScopeQueryRun, RetiredRingRemembersTheLastFew) {
    NeighbourInfo rows[10];
    ScopeQueryRun<12> run;
    run.start(rows, loadNeighbors(rows, 10));
    uint8_t keys[10][offband::kNeighborKeyLen];
    uint32_t now = 0;
    for (int i = 0; i < 10; i++) {
        memcpy(keys[i], run.poll(now), offband::kNeighborKeyLen);
        run.sent(true, (uint32_t)(50 + i), now);
        run.replied("x");
    }
    // the ring holds the most recent kRetired (8) closed requests
    static_assert(ScopeQueryRun<12>::kRetired == 8, "test written for an 8-entry ring");
    EXPECT_EQ(run.classify(keys[9], 59), offband::ScopeReply::Retired);   // a duplicate reply
    EXPECT_EQ(run.classify(keys[2], 52), offband::ScopeReply::Retired);
    EXPECT_EQ(run.classify(keys[1], 51), offband::ScopeReply::None);
}

TEST(ScopeQueryRun, RestartRetiresTheRequestInFlight) {
    NeighbourInfo rows[2];
    ScopeQueryRun<4> run;
    run.start(rows, loadNeighbors(rows, 2));
    uint8_t k[offband::kNeighborKeyLen];
    memcpy(k, run.poll(0), sizeof(k));
    run.sent(true, 9, 0);
    run.start(rows, 2);
    EXPECT_EQ(run.classify(k, 9), offband::ScopeReply::Retired);   // its late reply is still ours
    uint8_t got[offband::kNeighborKeyLen];
    ASSERT_TRUE(run.takeReleased(got));                            // and its transient is let go
    EXPECT_EQ(memcmp(got, k, sizeof(k)), 0);
}

TEST(ScopeQueryRun, ATimeoutReleasesInTheSamePoll) {
    // T4 review: the caller frees the timed-out request's transient before sending
    // the next one, so it must be available right after the poll() that expired it.
    NeighbourInfo rows[2];
    ScopeQueryRun<4> run;
    run.start(rows, loadNeighbors(rows, 2));
    run.poll(0);
    run.sent(true, 9, 0);
    const uint8_t* next = run.poll(offband::kRegionQueryTimeoutMs);
    ASSERT_TRUE(keyIs(next, 0x20));
    uint8_t got[offband::kNeighborKeyLen];
    ASSERT_TRUE(run.takeReleased(got));
    EXPECT_TRUE(keyIs(got, 0x10));
}

TEST(ScopeQueryRun, MisuseDoesNotCorruptTheWalk) {
    NeighbourInfo rows[2];
    ScopeQueryRun<4> run;
    run.sent(true, 1, 0);        // before any start
    run.replied("x");
    run.unusableReply();
    EXPECT_EQ(run.resultCount(), 0);
    run.start(rows, loadNeighbors(rows, 2));
    run.poll(0);
    run.sent(true, 9, 0);
    run.sent(false, 0, 0);       // a second sent() while one is in flight is ignored
    EXPECT_TRUE(run.matches(rows[0].pub_key, 9));
    run.replied("hao");
    run.poll(1);
    run.sent(true, 10, 1);
    run.replied("oh-sw");
    EXPECT_EQ(run.poll(2), nullptr);   // done
    EXPECT_EQ(run.poll(3), nullptr);   // and stays done
    run.sent(true, 11, 3);             // ignored after the end
    EXPECT_EQ(run.result(0).status, ScopeStatus::Responded);
    EXPECT_EQ(run.result(1).status, ScopeStatus::Responded);
    EXPECT_EQ(run.queriedCount(), 2);
}

TEST(ScopeQueryRun, UnusableReplyIsNotAnEmptyAnswer) {
    NeighbourInfo rows[1];
    ScopeQueryRun<4> run;
    run.start(rows, loadNeighbors(rows, 1));
    run.poll(0);
    run.sent(true, 7, 0);
    run.unusableReply();
    EXPECT_EQ(run.result(0).status, ScopeStatus::Timeout);   // scopes unknown, not "none"
    EXPECT_EQ(run.poll(1), nullptr);
    EXPECT_FALSE(run.running());
}

TEST(ScopeQueryRun, EachClosedRequestIsReleasedOnce) {
    NeighbourInfo rows[2];
    ScopeQueryRun<4> run;
    run.start(rows, loadNeighbors(rows, 2));
    uint8_t got[offband::kNeighborKeyLen];
    EXPECT_FALSE(run.takeReleased(got));   // nothing sent yet
    run.poll(0);
    run.sent(false, 0, 0);                 // a failed send held nothing
    EXPECT_FALSE(run.takeReleased(got));
    run.poll(0);
    run.sent(true, 9, 0);
    EXPECT_FALSE(run.takeReleased(got));   // still in flight
    run.replied("hao");
    ASSERT_TRUE(run.takeReleased(got));
    EXPECT_TRUE(keyIs(got, 0x20));
    EXPECT_FALSE(run.takeReleased(got));   // once
}

TEST(ScopeQueryRun, StartsStraightFromTheNeighborTable) {
    NeighborTable<3> t;
    uint8_t a[offband::kNeighborKeyLen]; keyFor(a, 0x10);
    uint8_t b[offband::kNeighborKeyLen]; keyFor(b, 0x20);
    t.put(a, 1, 100, 2.0f, -70);
    t.put(b, 1, 200, 3.0f, -80);
    ScopeQueryRun<1> run;
    run.start(t);
    EXPECT_EQ(run.totalCount(), 2);
    ASSERT_EQ(run.resultCount(), 1);
    EXPECT_EQ(run.result(0).row.snr, 8);
    EXPECT_EQ(run.result(0).row.rssi, -70);
    EXPECT_EQ(t.slotAt(-1), nullptr);
    EXPECT_EQ(t.slotAt(3), nullptr);
    EXPECT_EQ(t.slotAt(2), nullptr);   // empty slot
}

TEST(RegionsReply, EdgeLengthsAndCuts) {
    uint8_t buf[64];
    char out[16];
    // exactly the header: a valid, empty answer
    size_t len = makeReply(buf, 7, "", 0);
    ASSERT_EQ(len, 8u);
    ASSERT_TRUE(offband::parseRegionsResponse(buf, len, 7, out, sizeof(out)));
    EXPECT_STREQ(out, "");
    // names fill len exactly: no NUL, no padding
    len = makeReply(buf, 7, "hao", 0);
    ASSERT_TRUE(offband::parseRegionsResponse(buf, len, 7, out, sizeof(out)));
    EXPECT_STREQ(out, "hao");
    // tiny buffers
    char one[1], two[2];
    ASSERT_TRUE(offband::parseRegionsResponse(buf, len, 7, one, sizeof(one)));
    EXPECT_STREQ(one, "");
    ASSERT_TRUE(offband::parseRegionsResponse(buf, len, 7, two, sizeof(two)));
    EXPECT_STREQ(two, "");
    // a comma right at the last usable position, and the wildcard surviving a cut
    len = makeReply(buf, 7, "*,abc,defghij", 0);
    char six[6];   // index 5 (out_cap - 1) is the comma after "abc": "*,abc" fits exactly
    ASSERT_TRUE(offband::parseRegionsResponse(buf, len, 7, six, sizeof(six)));
    EXPECT_STREQ(six, "*,abc");
    char five[5];  // "abc" no longer fits whole: only the wildcard survives
    ASSERT_TRUE(offband::parseRegionsResponse(buf, len, 7, five, sizeof(five)));
    EXPECT_STREQ(five, "*");
}

TEST(ScopeQueryRun, StatusNamesAreTheContractStrings) {
    EXPECT_STREQ(offband::scopeStatusName(ScopeStatus::Responded), "responded");
    EXPECT_STREQ(offband::scopeStatusName(ScopeStatus::Timeout), "timeout");
    EXPECT_STREQ(offband::scopeStatusName(ScopeStatus::SendFailed), "send_failed");
    EXPECT_STREQ(offband::scopeStatusName(ScopeStatus::Pending), "pending");
}

// ---- node discovery (#1398 T5) --------------------------------------------

TEST(NodeDiscover, RequestIsWhatTheRepeaterParses) {
    // simple_repeater onControlDataRecv: payload[0] & 0xF0 == 0x80 (bit 0 = prefix only),
    // payload[1] = type filter, [2..5] = tag, [6..9] = since.
    uint8_t buf[offband::kNodeDiscoverReqLen + 2];
    memset(buf, 0xEE, sizeof(buf));
    ASSERT_EQ(offband::buildNodeDiscoverRequest(0xCAFEF00Du, buf), 10);
    EXPECT_EQ(buf[0], 0x80);              // full keys, not prefixes
    EXPECT_EQ(buf[1], 1u << 2);           // repeaters only (ADV_TYPE_REPEATER = 2)
    uint32_t tag, since;
    memcpy(&tag, buf + 2, 4);
    memcpy(&since, buf + 6, 4);
    EXPECT_EQ(tag, 0xCAFEF00Du);
    EXPECT_EQ(since, 0u);                 // every repeater, however long since it changed
    EXPECT_EQ(buf[10], 0xEE);
}

static size_t discoverReply(uint8_t* buf, uint8_t type_byte, uint32_t tag, uint8_t key_tag, size_t key_len) {
    buf[0] = type_byte;
    buf[1] = 0x14;   // the SNR the repeater heard us at
    memcpy(buf + 2, &tag, 4);
    uint8_t key[offband::kNeighborKeyLen]; keyFor(key, key_tag);
    memcpy(buf + 6, key, key_len);
    return 6 + key_len;
}

TEST(NodeDiscover, ResponseFromARepeaterWithOurTagGivesItsKey) {
    uint8_t buf[64];
    size_t len = discoverReply(buf, 0x92, 77, 0x40, 32);
    uint8_t key[offband::kNeighborKeyLen];
    ASSERT_TRUE(offband::parseNodeDiscoverResponse(buf, len, 77, key));
    EXPECT_TRUE(keyIs(key, 0x40));
}

TEST(NodeDiscover, ResponsesThatCannotBeUsedAreRejected) {
    uint8_t buf[64];
    uint8_t key[offband::kNeighborKeyLen];
    size_t len = discoverReply(buf, 0x92, 77, 0x40, 32);
    EXPECT_FALSE(offband::parseNodeDiscoverResponse(buf, len, 78, key));   // the client's tag
    len = discoverReply(buf, 0x91, 77, 0x40, 32);                          // a companion answering
    EXPECT_FALSE(offband::parseNodeDiscoverResponse(buf, len, 77, key));
    len = discoverReply(buf, 0x82, 77, 0x40, 32);                          // a request, not a response
    EXPECT_FALSE(offband::parseNodeDiscoverResponse(buf, len, 77, key));
    len = discoverReply(buf, 0x92, 77, 0x40, 8);                           // prefix-only answer
    EXPECT_FALSE(offband::parseNodeDiscoverResponse(buf, len, 77, key));
    EXPECT_FALSE(offband::parseNodeDiscoverResponse(nullptr, 40, 77, key));
    len = discoverReply(buf, 0x90, 77, 0x40, 32);                          // type nibble 0
    EXPECT_FALSE(offband::parseNodeDiscoverResponse(buf, len, 77, key));
}

TEST(NodeDiscover, LengthBoundaryIsAWholeKey) {
    uint8_t buf[64];
    uint8_t key[offband::kNeighborKeyLen];
    size_t len = discoverReply(buf, 0x92, 77, 0x40, 32);
    ASSERT_EQ(len, 38u);
    EXPECT_TRUE(offband::parseNodeDiscoverResponse(buf, 38, 77, key));    // header + whole key
    EXPECT_FALSE(offband::parseNodeDiscoverResponse(buf, 37, 77, key));   // one byte short
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
