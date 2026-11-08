#include <gtest/gtest.h>

#include "helpers/wifi_observer/RegionsPeer.h"

using offband::PeerRoute;
using offband::ScopeReply;
using offband::kRegionsPeerIdx;
using offband::routePeerData;

static const int kClients = 3;

// Review #1: a neighbor matched only as the regions peer never reaches a client path.
TEST(RegionsPeer, TheWalksReplyFromANonClientGoesToTheWalk) {
    EXPECT_EQ(routePeerData(kRegionsPeerIdx, kClients, true, ScopeReply::Current), PeerRoute::Regions);
    EXPECT_EQ(routePeerData(kRegionsPeerIdx, kClients, true, ScopeReply::Retired), PeerRoute::Regions);
}

TEST(RegionsPeer, ANonClientsOtherResponseIsDropped) {
    EXPECT_EQ(routePeerData(kRegionsPeerIdx, kClients, true, ScopeReply::None), PeerRoute::Drop);
}

TEST(RegionsPeer, ANonClientsRequestOrCommandIsDroppedEvenWhileItsQueryIsOut) {
    // A request, a login or a CLI text is not a response: whatever the walk thinks of the
    // sender, the regions peer never gets near handleRequest or the CLI.
    for (ScopeReply r : {ScopeReply::None, ScopeReply::Current, ScopeReply::Retired}) {
        EXPECT_EQ(routePeerData(kRegionsPeerIdx, kClients, false, r), PeerRoute::Drop);
    }
}

TEST(RegionsPeer, ThePeerIndexIsNeverAValidAclIndex) {
    EXPECT_LT(kRegionsPeerIdx, 0);
    EXPECT_EQ(routePeerData(kRegionsPeerIdx, 1000, false, ScopeReply::None), PeerRoute::Drop);
}

// Review #2: a neighbor that is also an ACL client keeps its client paths.
TEST(RegionsPeer, AClientsRequestsStillReachTheClientPaths) {
    for (ScopeReply r : {ScopeReply::None, ScopeReply::Current, ScopeReply::Retired}) {
        EXPECT_EQ(routePeerData(0, kClients, false, r), PeerRoute::Client);
        EXPECT_EQ(routePeerData(kClients - 1, kClients, false, r), PeerRoute::Client);
    }
}

TEST(RegionsPeer, AClientsRegionsReplyGoesToTheWalk) {
    // Matched as the client first (same keys, same secret), but it answers the walk.
    EXPECT_EQ(routePeerData(1, kClients, true, ScopeReply::Current), PeerRoute::Regions);
    EXPECT_EQ(routePeerData(1, kClients, true, ScopeReply::Retired), PeerRoute::Regions);
}

TEST(RegionsPeer, AClientsOtherResponseStaysWithTheClient) {
    EXPECT_EQ(routePeerData(1, kClients, true, ScopeReply::None), PeerRoute::Client);
}

TEST(RegionsPeer, AnOutOfRangeIndexIsDropped) {
    EXPECT_EQ(routePeerData(kClients, kClients, false, ScopeReply::None), PeerRoute::Drop);
    EXPECT_EQ(routePeerData(-1, kClients, false, ScopeReply::None), PeerRoute::Drop);
    EXPECT_EQ(routePeerData(0, 0, false, ScopeReply::None), PeerRoute::Drop);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
