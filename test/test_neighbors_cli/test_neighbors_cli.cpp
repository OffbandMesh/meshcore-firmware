#include <gtest/gtest.h>
#include <cstring>
#include <string>

#include "helpers/wifi_observer/NeighborsCli.h"

using offband::NeighborsCliHooks;
using offband::ScopeWalkSummary;

// A fake companion behind the hooks.
static int g_count = 0;
static bool g_discover_ok = true;
static int g_discover_calls = 0;
static int g_start_calls = 0;
static ScopeWalkSummary g_summary;

static int fakeCount() { return g_count; }
static bool fakeDiscover() { g_discover_calls++; return g_discover_ok; }
static void fakeSummary(ScopeWalkSummary* out) { *out = g_summary; }
static bool g_start_ok = true;
static bool fakeStart() { g_start_calls++; return g_start_ok; }

static const NeighborsCliHooks kHooks = { fakeCount, fakeDiscover, fakeSummary, fakeStart };

class NeighborsCli : public ::testing::Test {
protected:
    void SetUp() override {
        g_count = 0;
        g_discover_ok = true;
        g_discover_calls = 0;
        g_start_calls = 0;
        g_start_ok = true;
        memset(&g_summary, 0, sizeof(g_summary));
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
    EXPECT_NE(r.find("request sent"), std::string::npos) << r;
    EXPECT_NE(r.find("3 neighbors"), std::string::npos) << r;
}

TEST_F(NeighborsCli, DiscoverNeighborsReportsAFailedSend) {
    g_discover_ok = false;
    std::string r = call("discover.neighbors\r\n");   // a serial line's tail is tolerated
    ASSERT_TRUE(handled_);
    EXPECT_EQ(r.rfind("ERROR:", 0), 0u) << r;
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
    NeighborsCliHooks partial = { fakeCount, nullptr, fakeSummary, fakeStart };
    offband::setNeighborsCliHooks(&partial);
    r = call("discover.neighbors");
    EXPECT_EQ(r.rfind("ERROR:", 0), 0u) << r;
    EXPECT_EQ(g_discover_calls, 0);
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
