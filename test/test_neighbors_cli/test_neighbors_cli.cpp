#include <gtest/gtest.h>
#include <cstring>
#include <string>

#include "helpers/wifi_observer/NeighborsCli.h"

using offband::DiscoverResult;
using offband::NeighborsCliHooks;
using offband::ScopeWalkSummary;

// A fake companion behind the hooks.
static int g_count = 0;
static DiscoverResult g_discover = DiscoverResult::Queued;
static int g_discover_calls = 0;
static int g_start_calls = 0;
static ScopeWalkSummary g_summary;

static int fakeCount() { return g_count; }
static DiscoverResult fakeDiscover(int* seconds_left) {
    g_discover_calls++;
    if (g_discover == DiscoverResult::Busy) *seconds_left = 42;
    return g_discover;
}
static void fakeSummary(ScopeWalkSummary* out) { *out = g_summary; }
static bool g_start_ok = true;
static bool fakeStart() { g_start_calls++; return g_start_ok; }

// The saved settings and the schedule behind them.
static offband::NeighborsSettings g_saved;
static bool g_save_ok = true;
static int g_save_calls = 0;
static offband::NeighborsScheduleStatus g_status;
static void fakeRead(offband::NeighborsSettings* out) { *out = g_saved; }
static bool fakeSave(const offband::NeighborsSettings& s) {
    g_save_calls++;
    if (!g_save_ok) return false;
    g_saved = s;
    return true;
}
static void fakeStatus(offband::NeighborsScheduleStatus* out) { *out = g_status; }

static const NeighborsCliHooks kHooks = { fakeCount, fakeDiscover, fakeSummary, fakeStart,
                                          fakeRead, fakeSave, fakeStatus };

class NeighborsCli : public ::testing::Test {
protected:
    void SetUp() override {
        g_count = 0;
        g_discover = DiscoverResult::Queued;
        g_discover_calls = 0;
        g_start_calls = 0;
        g_start_ok = true;
        memset(&g_summary, 0, sizeof(g_summary));
        g_saved = { false, 24 };
        g_save_ok = true;
        g_save_calls = 0;
        g_status = { false, 3600, false, 0 };
        offband::setNeighborsCliHooks(&kHooks);
    }
    void TearDown() override { offband::setNeighborsCliHooks(nullptr); }

    std::string call(const char* cmd) {
        char reply[256];
        memset(reply, 'X', sizeof(reply));
        handled_ = offband::handleNeighborsCli(cmd, reply, sizeof(reply));
        reply[sizeof(reply) - 1] = 0;
        return handled_ ? std::string(reply) : std::string();
    }
    bool handled_ = false;
};

TEST_F(NeighborsCli, OtherCommandsAreNotOurs) {
    call("mqtt status");
    EXPECT_FALSE(handled_);
    call("discover");
    EXPECT_FALSE(handled_);
    call("discover.neighborsX");
    EXPECT_FALSE(handled_);
    call("discover.scopes now");
    EXPECT_FALSE(handled_);
    EXPECT_EQ(g_discover_calls + g_start_calls, 0);
}

TEST_F(NeighborsCli, DiscoverNeighborsSendsAndSaysHowMany) {
    g_count = 3;
    std::string r = call("discover.neighbors");
    ASSERT_TRUE(handled_);
    EXPECT_EQ(g_discover_calls, 1);
    EXPECT_NE(r.find("request queued"), std::string::npos) << r;   // queued, not claimed on air
    EXPECT_NE(r.find("3 neighbors"), std::string::npos) << r;
}

TEST_F(NeighborsCli, DiscoverNeighborsReportsAFailedSend) {
    g_discover = DiscoverResult::Failed;
    std::string r = call("discover.neighbors\r\n");   // a serial line's tail is tolerated
    ASSERT_TRUE(handled_);
    EXPECT_EQ(r.rfind("ERROR:", 0), 0u) << r;
}

TEST_F(NeighborsCli, DiscoverNeighborsWhileListeningSaysSoAndQueuesNothing) {
    g_count = 2;
    g_discover = DiscoverResult::Busy;
    std::string r = call("discover.neighbors");
    ASSERT_TRUE(handled_);
    EXPECT_NE(r.find("still collecting"), std::string::npos) << r;
    EXPECT_NE(r.find("42 s left"), std::string::npos) << r;
    EXPECT_EQ(r.find("queued"), std::string::npos) << r;
}

TEST_F(NeighborsCli, DiscoverScopesWithNoNeighborsStartsNothing) {
    std::string r = call("discover.scopes");
    ASSERT_TRUE(handled_);
    EXPECT_EQ(g_start_calls, 0);
    EXPECT_NE(r.find("no neighbors"), std::string::npos) << r;
}

TEST_F(NeighborsCli, DiscoverScopesStartsAWalk) {
    g_count = 4;
    std::string r = call("discover.scopes");
    ASSERT_TRUE(handled_);
    EXPECT_EQ(g_start_calls, 1);
    EXPECT_NE(r.find("querying 4 neighbors"), std::string::npos) << r;
    EXPECT_EQ(r.find("last walk"), std::string::npos) << r;   // there was none
}

TEST_F(NeighborsCli, DiscoverScopesSaysSoWhenTheWalkCannotStart) {
    g_count = 4;
    g_start_ok = false;   // e.g. no PSRAM for the walk
    std::string r = call("discover.scopes");
    ASSERT_TRUE(handled_);
    EXPECT_EQ(r.rfind("ERROR:", 0), 0u) << r;
    EXPECT_EQ(r.find("querying"), std::string::npos) << r;   // never claims it started
}

TEST_F(NeighborsCli, DiscoverScopesReportsTheLastWalkWhenStartingAnother) {
    g_count = 4;
    g_summary = { false, 3, 3, 1, 1, 1 };
    std::string r = call("discover.scopes");
    EXPECT_EQ(g_start_calls, 1);
    EXPECT_NE(r.find("last walk: 1 responded, 1 timeout, 1 send_failed of 3"), std::string::npos) << r;
}

TEST_F(NeighborsCli, DiscoverScopesWhileRunningReportsAndDoesNotRestart) {
    g_count = 5;
    g_summary = { true, 5, 2, 1, 1, 0 };
    std::string r = call("discover.scopes");
    ASSERT_TRUE(handled_);
    EXPECT_EQ(g_start_calls, 0);   // a second command does not throw away the walk in progress
    EXPECT_NE(r.find("running, 2 of 5 done"), std::string::npos) << r;
}

TEST_F(NeighborsCli, WithoutHooksItIsAnErrorNotACrash) {
    offband::setNeighborsCliHooks(nullptr);
    std::string r = call("discover.scopes");
    ASSERT_TRUE(handled_);
    EXPECT_EQ(r.rfind("ERROR:", 0), 0u) << r;
    const NeighborsCliHooks partials[] = {
        { nullptr, fakeDiscover, fakeSummary, fakeStart, fakeRead, fakeSave, fakeStatus },
        { fakeCount, nullptr, fakeSummary, fakeStart, fakeRead, fakeSave, fakeStatus },
        { fakeCount, fakeDiscover, nullptr, fakeStart, fakeRead, fakeSave, fakeStatus },
        { fakeCount, fakeDiscover, fakeSummary, nullptr, fakeRead, fakeSave, fakeStatus },
        { fakeCount, fakeDiscover, fakeSummary, fakeStart, nullptr, fakeSave, fakeStatus },
        { fakeCount, fakeDiscover, fakeSummary, fakeStart, fakeRead, nullptr, fakeStatus },
        { fakeCount, fakeDiscover, fakeSummary, fakeStart, fakeRead, fakeSave, nullptr },
    };
    for (const NeighborsCliHooks& partial : partials) {
        offband::setNeighborsCliHooks(&partial);
        for (const char* cmd : { "discover.neighbors", "discover.scopes", "set mqtt.neighbors on",
                                 "set mqtt.neighbors.interval 48", "get mqtt.neighbors",
                                 "get mqtt.neighbors.interval" }) {
            r = call(cmd);
            EXPECT_TRUE(handled_) << cmd;
            EXPECT_EQ(r.rfind("ERROR:", 0), 0u) << cmd << ": " << r;
        }
        char line[64] = "X";
        EXPECT_EQ(offband::formatNeighborsStatusLine(line, sizeof(line)), 0u);
        EXPECT_STREQ(line, "");
    }
    EXPECT_EQ(g_discover_calls + g_start_calls + g_save_calls, 0);
}

// ---- #1399: mqtt.neighbors[.interval] ----

TEST_F(NeighborsCli, SetNeighborsOnAndOffSavesAndSaysSo) {
    std::string r = call("set mqtt.neighbors on");
    ASSERT_TRUE(handled_);
    EXPECT_EQ(r, "mqtt.neighbors = on, every 24 h\n");
    EXPECT_TRUE(g_saved.enabled);
    EXPECT_EQ(g_saved.interval_hours, 24);   // the other setting is kept
    r = call("set mqtt.neighbors off\r\n");
    EXPECT_EQ(r, "mqtt.neighbors = off\n");
    EXPECT_FALSE(g_saved.enabled);
    call("set mqtt.neighbors 1");
    EXPECT_TRUE(g_saved.enabled);
    call("set mqtt.neighbors 0");
    EXPECT_FALSE(g_saved.enabled);
    call("set mqtt.neighbors ON");   // the normalizer leaves the value's case alone
    EXPECT_TRUE(g_saved.enabled);
    call("set mqtt.neighbors Off");
    EXPECT_FALSE(g_saved.enabled);
    EXPECT_EQ(g_save_calls, 6);
}

static std::string statusLine();

// Review of #1880: each line of a reply is one _sys frame of at most 139 characters.
TEST_F(NeighborsCli, EveryReplyLineFitsOneSysFrame) {
    g_count = 64;
    g_summary = { false, 64, 64, 64, 0, 0 };
    g_saved = { true, 336 };
    g_status = { false, -1, true, 336u * 3600u };
    std::string all;
    all += call("discover.scopes");
    g_summary = { true, 64, 63, 21, 21, 21 };
    all += call("discover.scopes");
    g_discover = DiscoverResult::Busy;
    all += call("discover.neighbors");
    g_discover = DiscoverResult::Queued;
    all += call("discover.neighbors");
    for (const char* cmd : { "set mqtt.neighbors on", "set mqtt.neighbors.interval 3360",
                             "set mqtt.neighbors.interval x", "get mqtt.neighbors.interval" }) {
        all += call(cmd);
    }
    all += statusLine();
    size_t start = 0;
    int lines = 0;
    while (start < all.size()) {
        size_t nl = all.find('\n', start);
        ASSERT_NE(nl, std::string::npos) << "every reply ends in a newline";
        EXPECT_LT(nl - start, 140u) << all.substr(start, nl - start);
        start = nl + 1;
        lines++;
    }
    EXPECT_EQ(lines, 10);   // the walk report is the only two-line reply
}

TEST_F(NeighborsCli, SetNeighborsRefusesAnythingElse) {
    for (const char* cmd : { "set mqtt.neighbors", "set mqtt.neighbors yes", "set mqtt.neighbors true",
                             "set mqtt.neighbors on off", "set mqtt.neighbors onnnnnnnnnnnnnnnnnnnn" }) {
        std::string r = call(cmd);
        ASSERT_TRUE(handled_) << cmd;
        EXPECT_EQ(r.rfind("ERROR: usage: set mqtt.neighbors on|off", 0), 0u) << cmd << ": " << r;
    }
    EXPECT_EQ(g_save_calls, 0);
}

TEST_F(NeighborsCli, IntervalTakesTheWholeRangeAndKeepsOnOff) {
    g_saved = { true, 24 };
    std::string r = call("set mqtt.neighbors.interval 12");
    EXPECT_EQ(r, "mqtt.neighbors.interval = 12\n");
    EXPECT_EQ(g_saved.interval_hours, 12);
    EXPECT_TRUE(g_saved.enabled);
    call("set mqtt.neighbors.interval 336");
    EXPECT_EQ(g_saved.interval_hours, 336);
}

TEST_F(NeighborsCli, IntervalOutsideTheRangeOrNotANumberIsRefused) {
    for (const char* cmd : { "set mqtt.neighbors.interval 11", "set mqtt.neighbors.interval 337",
                             "set mqtt.neighbors.interval 0", "set mqtt.neighbors.interval -24" }) {
        std::string r = call(cmd);
        EXPECT_NE(r.find("out of range [12, 336] hours"), std::string::npos) << cmd << ": " << r;
    }
    for (const char* cmd : { "set mqtt.neighbors.interval", "set mqtt.neighbors.interval 24h",
                             "set mqtt.neighbors.interval twelve",
                             "set mqtt.neighbors.interval 000000000000000000024" }) {
        std::string r = call(cmd);
        EXPECT_EQ(r.rfind("ERROR: usage: set mqtt.neighbors.interval", 0), 0u) << cmd << ": " << r;
    }
    EXPECT_EQ(g_save_calls, 0);
    EXPECT_EQ(g_saved.interval_hours, 24);
}

TEST_F(NeighborsCli, AFailedSaveIsReportedNotClaimed) {
    g_save_ok = false;
    std::string r = call("set mqtt.neighbors on");
    EXPECT_EQ(r, "ERROR: failed to save mqtt.neighbors (NVS write failed)\n");
    r = call("set mqtt.neighbors.interval 48");
    EXPECT_EQ(r, "ERROR: failed to save mqtt.neighbors.interval (NVS write failed)\n");
    EXPECT_FALSE(g_saved.enabled);
}

TEST_F(NeighborsCli, GetReadsTheSavedSettings) {
    g_saved = { true, 72 };
    EXPECT_EQ(call("get mqtt.neighbors"), "mqtt.neighbors = on\n");
    EXPECT_EQ(call("get mqtt.neighbors.interval"), "mqtt.neighbors.interval = 72\n");
    g_saved = { false, 24 };
    EXPECT_EQ(call("get mqtt.neighbors"), "mqtt.neighbors = off\n");
    EXPECT_EQ(g_save_calls, 0);
}

TEST_F(NeighborsCli, NeighborKeysDoNotSwallowOtherSettings) {
    for (const char* cmd : { "set mqtt.neighborsx on", "get mqtt.neighbors.intervalx",
                             "set mqtt.status_interval 30", "get mqtt.iata", "setmqtt.neighbors on",
                             "mqtt.neighbors on" }) {
        call(cmd);
        EXPECT_FALSE(handled_) << cmd;
    }
}

static std::string statusLine() {
    char line[160];
    memset(line, 'X', sizeof(line));
    offband::formatNeighborsStatusLine(line, sizeof(line));
    return line;
}

TEST_F(NeighborsCli, StatusLineWhenOff) {
    EXPECT_EQ(statusLine(), "nbr: off, last never\n");
    g_status = { false, 0, true, 2 * 3600 + 5 * 60 };
    EXPECT_EQ(statusLine(), "nbr: off, last 2h05m ago\n");
    g_status.cycle_running = true;   // a discover.scopes walk
    EXPECT_EQ(statusLine(), "nbr: off, report running, last 2h05m ago\n");
}

TEST_F(NeighborsCli, StatusLineWhenOn) {
    g_saved = { true, 24 };
    g_status = { false, 3 * 3600 + 12 * 60, true, 20 * 3600 + 48 * 60 };
    EXPECT_EQ(statusLine(), "nbr: on, every 24 h, next in 3h12m, last 20h48m ago\n");
    g_status = { false, 45 * 60 + 59, false, 0 };
    EXPECT_EQ(statusLine(), "nbr: on, every 24 h, next in 45m, last never\n");
    g_status = { false, 59, true, 30 };
    EXPECT_EQ(statusLine(), "nbr: on, every 24 h, next in <1m, last <1m ago\n");
    g_status = { true, 0, false, 0 };
    EXPECT_EQ(statusLine(), "nbr: on, every 24 h, cycle running, last never\n");
    g_status = { false, -5, false, 0 };   // due, but no clock or broker yet
    EXPECT_EQ(statusLine(), "nbr: on, every 24 h, due (waiting for clock and a broker), last never\n");
}

TEST_F(NeighborsCli, StatusLineIsCutCleanly) {
    g_saved = { true, 336 };
    g_status = { false, 336 * 3600, true, 336 * 3600 };
    char line[16];
    EXPECT_EQ(offband::formatNeighborsStatusLine(line, sizeof(line)), sizeof(line) - 1);
    EXPECT_EQ(strlen(line), sizeof(line) - 1);
    EXPECT_EQ(offband::formatNeighborsStatusLine(line, 0), 0u);
    EXPECT_EQ(offband::formatNeighborsStatusLine(nullptr, 16), 0u);
}

TEST_F(NeighborsCli, TinyReplyBufferIsStillTerminated) {
    g_count = 4;
    char reply[8];
    ASSERT_TRUE(offband::handleNeighborsCli("discover.scopes", reply, sizeof(reply)));
    EXPECT_EQ(strlen(reply), sizeof(reply) - 1);
    EXPECT_FALSE(offband::handleNeighborsCli("discover.scopes", reply, 0));
    EXPECT_FALSE(offband::handleNeighborsCli(nullptr, reply, sizeof(reply)));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
