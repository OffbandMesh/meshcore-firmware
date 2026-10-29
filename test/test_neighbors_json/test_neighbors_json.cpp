#include <gtest/gtest.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "helpers/wifi_observer/NeighborsReport.h"

using offband::NeighborsReportInput;
using offband::ScopeResult;
using offband::ScopeStatus;

static void keyFor(uint8_t* key, uint8_t tag) {
    for (int i = 0; i < (int)offband::kNeighborKeyLen; i++) key[i] = (uint8_t)(tag + i);
}

static ScopeResult row(uint8_t tag, ScopeStatus st, uint32_t heard, int8_t snr4, int8_t rssi, const char* scopes) {
    ScopeResult r;
    memset(&r, 0, sizeof(r));
    keyFor(r.row.pub_key, tag);
    r.row.heard_timestamp = heard;
    r.row.snr = snr4;
    r.row.rssi = rssi;
    r.status = st;
    strncpy(r.scopes, scopes, sizeof(r.scopes) - 1);
    return r;
}

static bool plausibleFrom1000(uint32_t t) { return t >= 1000; }

static NeighborsReportInput baseInput(const ScopeResult* rows, int n) {
    NeighborsReportInput in;
    memset(&in, 0, sizeof(in));
    in.timestamp = "2026-10-28T13:00:00Z";
    in.origin = "Obs \"1\"";
    in.origin_id = "0011aabb";
    in.self_scopes = "hao";
    in.default_scope = "#hao";
    in.rows = rows;
    in.n_rows = n;
    in.total_neighbors = 5;
    in.now = 1200;
    in.plausible = plausibleFrom1000;
    return in;
}

static std::string golden() {
    std::string path(__FILE__);
    path = path.substr(0, path.find_last_of("/\\") + 1) + "golden_neighbors.json";
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

static std::string build(const NeighborsReportInput& in, size_t cap = offband::kNeighborsReportMax) {
    std::string buf(cap, 'X');
    int n = offband::buildNeighborsJson(&buf[0], cap, in);
    if (n < 0) return std::string("<fail>");
    EXPECT_EQ(buf[n], '\0');
    return buf.substr(0, (size_t)n);
}

// The golden file is also parsed with a real JSON parser and checked against the
// CoreScope ingestor's handleNeighborsReport rules by scripts/test_neighbors_golden.py.
TEST(NeighborsJson, MatchesTheGoldenReport) {
    const ScopeResult rows[] = {
        row(0x10, ScopeStatus::Responded, 1000, 8, -70, "*,hao"),
        row(0x20, ScopeStatus::Timeout, 1100, -29, 0, "stale"),   // scopes only count on responded
        row(0x30, ScopeStatus::SendFailed, 5, 2, -95, ""),        // heard on an unset clock
        row(0x40, ScopeStatus::Pending, 1150, 0, -60, ""),         // never queried: not reported
    };
    std::string g = golden();
    ASSERT_FALSE(g.empty()) << "golden_neighbors.json not found";
    EXPECT_EQ(build(baseInput(rows, 4)), g);
}

TEST(NeighborsJson, AgesAreNullWhenTheClockIsNotSet) {
    // plan Review #4: never a bogus zero or garbage age
    const ScopeResult rows[] = { row(0x10, ScopeStatus::Responded, 1100, 0, -70, "hao") };
    NeighborsReportInput in = baseInput(rows, 1);
    in.now = 900;   // our clock itself is implausible
    EXPECT_NE(build(in).find("\"heard_secs_ago\":null"), std::string::npos);
    in.now = 1200;
    in.plausible = nullptr;   // no way to judge the clock
    EXPECT_NE(build(in).find("\"heard_secs_ago\":null"), std::string::npos);
    in.plausible = plausibleFrom1000;
    const ScopeResult future[] = { row(0x10, ScopeStatus::Responded, 1300, 0, -70, "hao") };
    in.rows = future;   // heard "after" now: the clock was set back
    EXPECT_NE(build(in).find("\"heard_secs_ago\":null"), std::string::npos);
}

TEST(NeighborsJson, SortIsUsableAgeThenRecencyThenSnrThenKey) {
    const ScopeResult rows[] = {
        row(0x10, ScopeStatus::Responded, 5, 40, -70, ""),      // best SNR but unusable age: last
        row(0x20, ScopeStatus::Responded, 1100, 4, -70, ""),    // same recency, lower SNR
        row(0x30, ScopeStatus::Responded, 1100, 8, -70, ""),    // same recency, higher SNR: first
        row(0x40, ScopeStatus::Responded, 1000, 8, -70, ""),    // older
    };
    std::string j = build(baseInput(rows, 4));
    size_t p30 = j.find("\"pubkey\":\"3031"), p20 = j.find("\"pubkey\":\"2021");
    size_t p40 = j.find("\"pubkey\":\"4041"), p10 = j.find("\"pubkey\":\"1011");
    ASSERT_NE(p10, std::string::npos);
    EXPECT_LT(p30, p20);
    EXPECT_LT(p20, p40);
    EXPECT_LT(p40, p10);
}

TEST(NeighborsJson, OverTheCapIsTruncatedInSortOrderAndStillValid) {
    // plan Review #2: never overflow, cut the least useful rows, say so
    ScopeResult rows[50];
    char scopes[90];
    memset(scopes, 'a', sizeof(scopes) - 1);
    scopes[sizeof(scopes) - 1] = 0;
    for (int i = 0; i < 50; i++) {
        rows[i] = row((uint8_t)(i * 3), ScopeStatus::Responded, (uint32_t)(1000 + i), 0, -70, scopes);
    }
    NeighborsReportInput in = baseInput(rows, 50);
    in.total_neighbors = 50;
    const size_t cap = 4096;
    std::string j = build(in, cap);
    ASSERT_NE(j, "<fail>");
    EXPECT_LT(j.size(), cap);
    EXPECT_NE(j.find("\"truncated\":true"), std::string::npos);
    EXPECT_NE(j.find("\"queried_neighbors\":50"), std::string::npos);   // the walk's count, not the emitted one
    EXPECT_EQ(j.back(), '}');
    // the newest neighbors (highest heard time) are the ones kept
    uint8_t k[offband::kNeighborKeyLen];
    keyFor(k, (uint8_t)(49 * 3));
    char hex[8];
    snprintf(hex, sizeof(hex), "%02X%02X", k[0], k[1]);
    EXPECT_NE(j.find(std::string("\"pubkey\":\"") + hex), std::string::npos);
}

TEST(NeighborsJson, NoNeighborsIsAnEmptyListNotAFailure) {
    std::string j = build(baseInput(nullptr, 0));
    EXPECT_NE(j.find("\"neighbors\":[]"), std::string::npos) << j;
    EXPECT_NE(j.find("\"truncated\":false"), std::string::npos) << j;
}

TEST(NeighborsJson, StringsAreEscapedAndControlBytesReplaced) {
    // A repeater's region list arrives raw off the air.
    const ScopeResult rows[] = { row(0x10, ScopeStatus::Responded, 1100, 0, -70, "a\"b\\c\x01" "d") };
    NeighborsReportInput in = baseInput(rows, 1);
    in.self_scopes = "x\ny";
    std::string j = build(in);
    EXPECT_NE(j.find("\"scopes\":\"a\\\"b\\\\c_d\""), std::string::npos) << j;
    EXPECT_NE(j.find("\"scopes\":\"x\\ny\""), std::string::npos) << j;
}

TEST(NeighborsJson, TooSmallABufferFailsCleanly) {
    std::string buf(40, 'X');
    EXPECT_EQ(offband::buildNeighborsJson(&buf[0], buf.size(), baseInput(nullptr, 0)), -1);
    EXPECT_EQ(offband::buildNeighborsJson(nullptr, 100, baseInput(nullptr, 0)), -1);
    EXPECT_EQ(offband::buildNeighborsJson(&buf[0], 0, baseInput(nullptr, 0)), -1);
}

TEST(NeighborsJson, NullStringsAreEmpty) {
    NeighborsReportInput in = baseInput(nullptr, 0);
    in.origin = nullptr;
    in.self_scopes = nullptr;
    in.default_scope = nullptr;
    in.timestamp = nullptr;
    in.origin_id = nullptr;
    std::string j = build(in);
    EXPECT_NE(j.find("\"self\":{\"scopes\":\"\",\"default_scope\":\"\"}"), std::string::npos) << j;
}

TEST(NeighborsJson, TimestampIsUtcRfc3339WithAZone) {
    // T6 review: CoreScope parses only RFC 3339 with a zone; anything else bypasses
    // its newest-report-wins guard.
    char ts[32];
    ASSERT_EQ(offband::formatReportTimestamp(1793192400u, ts, sizeof(ts)), 20);   // 2026-10-28 13:00:00 UTC
    EXPECT_STREQ(ts, "2026-10-28T13:00:00Z");
    ASSERT_EQ(offband::formatReportTimestamp(0, ts, sizeof(ts)), 20);
    EXPECT_STREQ(ts, "1970-01-01T00:00:00Z");
    ASSERT_EQ(offband::formatReportTimestamp(951868799u, ts, sizeof(ts)), 20);    // leap day, last second
    EXPECT_STREQ(ts, "2000-02-29T23:59:59Z");
    ASSERT_EQ(offband::formatReportTimestamp(4294967295u, ts, sizeof(ts)), 20);   // uint32 max
    EXPECT_STREQ(ts, "2106-02-07T06:28:15Z");
    EXPECT_EQ(offband::formatReportTimestamp(0, ts, 20), -1);   // no room for the NUL
    EXPECT_EQ(offband::formatReportTimestamp(0, nullptr, 32), -1);
}

TEST(NeighborsJson, RowsBeyondTheSortLimitStillCountAndTruncate) {
    static ScopeResult rows[70];
    for (int i = 0; i < 70; i++) rows[i] = row((uint8_t)i, ScopeStatus::Timeout, 1100, 0, -70, "");
    NeighborsReportInput in = baseInput(rows, 70);
    in.total_neighbors = 70;
    std::string j = build(in, 64 * 1024);
    EXPECT_NE(j.find("\"queried_neighbors\":70"), std::string::npos);
    EXPECT_NE(j.find("\"truncated\":true"), std::string::npos);   // 70 queried, 64 listed
}

TEST(NeighborsJson, SelfScopesAreCutAtAWholeName) {
    std::string longlist;
    for (int i = 0; i < 20; i++) longlist += (i ? ",region" : "region") + std::to_string(i);
    NeighborsReportInput in = baseInput(nullptr, 0);
    in.self_scopes = longlist.c_str();
    std::string j = build(in);
    size_t a = j.find("\"self\":{\"scopes\":\"") + 18;
    std::string self = j.substr(a, j.find('"', a) - a);
    EXPECT_LT(self.size(), offband::kScopesLen);
    EXPECT_EQ(longlist.compare(0, self.size(), self), 0);   // a prefix of the list...
    EXPECT_EQ(longlist[self.size()], ',');                   // ...ending at a whole name
}

TEST(NeighborsReportLimits, CapFollowsPsram) {
    EXPECT_EQ(offband::neighborsReportCap(true), offband::kNeighborsReportMax);
    EXPECT_EQ(offband::neighborsReportCap(false), offband::kNeighborsReportMaxNoPsram);
    EXPECT_LT(offband::kNeighborsReportMaxNoPsram, offband::kNeighborsReportMax);
}

TEST(NeighborsReportLimits, HeapFitKeepsTheFloorAndMargin) {
    const uint32_t floor = 80u * 1024u;
    const size_t len = 4096;
    const uint32_t exact = floor + (uint32_t)len + offband::kNeighborsHeapMargin;
    EXPECT_TRUE(offband::neighborsHeapFits(exact, floor, len));
    EXPECT_FALSE(offband::neighborsHeapFits(exact - 1, floor, len));
    EXPECT_FALSE(offband::neighborsHeapFits(0, floor, len));
    EXPECT_FALSE(offband::neighborsHeapFits(0xFFFFFFFFu, 0xFFFFFFFFu, 1));   // no wrap-around "fit"
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
