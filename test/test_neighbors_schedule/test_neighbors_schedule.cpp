#include <gtest/gtest.h>

#include "helpers/wifi_observer/NeighborsSchedule.h"

using offband::NeighborsAction;
using offband::NeighborsSchedule;

static const uint32_t H = 3600u * 1000u;
static const uint32_t M = 60u * 1000u;

// Runs one whole cycle from its Discover at t, ready throughout, and reports the
// publish as sent. Returns the time of the publish.
static uint32_t runCycle(NeighborsSchedule& s, uint32_t t) {
    EXPECT_EQ(s.tick(t, false, true), NeighborsAction::Discover);
    t += offband::kNeighborsRefreshMs;
    EXPECT_EQ(s.tick(t, false, true), NeighborsAction::StartWalk);
    s.walkStarted(true);
    t += 1;
    EXPECT_EQ(s.tick(t, false, true), NeighborsAction::Publish);
    s.published(true, t);
    return t;
}

TEST(NeighborsSchedule, OffDoesNothingOnItsOwn) {
    NeighborsSchedule s;
    s.configure(false, 24 * H);
    for (uint32_t t = 0; t < 100 * H; t += H / 2) EXPECT_EQ(s.tick(t, false, true), NeighborsAction::None) << t;
}

TEST(NeighborsSchedule, CycleOrderIsRefreshThenQueryThenPublish) {
    NeighborsSchedule s;
    s.configure(true, 24 * H);
    uint32_t t = 0;
    // nothing before the first-cycle delay
    EXPECT_EQ(s.tick(t, false, true), NeighborsAction::None);
    t = offband::kNeighborsFirstCycleMs - 1;
    EXPECT_EQ(s.tick(t, false, true), NeighborsAction::None);
    t = offband::kNeighborsFirstCycleMs;
    EXPECT_EQ(s.tick(t, false, true), NeighborsAction::Discover);
    // the discover answer window runs before the walk
    EXPECT_EQ(s.tick(t + offband::kNeighborsRefreshMs - 1, false, true), NeighborsAction::None);
    t += offband::kNeighborsRefreshMs;
    EXPECT_EQ(s.tick(t, false, true), NeighborsAction::StartWalk);
    s.walkStarted(true);
    // the walk runs; nothing else happens meanwhile
    EXPECT_EQ(s.tick(t + 1000, true, true), NeighborsAction::None);
    EXPECT_EQ(s.tick(t + 2000, true, true), NeighborsAction::None);
    // the walk ends: publish once
    EXPECT_EQ(s.tick(t + 3000, false, true), NeighborsAction::Publish);
    s.published(true, t + 3000);
    EXPECT_EQ(s.tick(t + 4000, false, true), NeighborsAction::None);
    EXPECT_FALSE(s.cycleRunning());
}

TEST(NeighborsSchedule, NextCycleIsOneIntervalAfterTheLastStarted) {
    NeighborsSchedule s;
    s.configure(true, 12 * H);
    uint32_t start = offband::kNeighborsFirstCycleMs;
    runCycle(s, start);
    EXPECT_EQ(s.nextDueMs(), start + 12 * H);
    EXPECT_EQ(s.tick(start + 12 * H - 1, false, true), NeighborsAction::None);
    EXPECT_EQ(s.tick(start + 12 * H, false, true), NeighborsAction::Discover);
}

TEST(NeighborsSchedule, DueTimingSurvivesMillisWrap) {
    NeighborsSchedule s;
    s.configure(true, 12 * H);
    const uint32_t start = 0xFFFFFFFFu - 1000u;   // the cycle starts just before the wrap
    runCycle(s, start);                           // its walk and publish land after the wrap
    EXPECT_EQ(s.tick(start + 12 * H - 1, false, true), NeighborsAction::None);
    EXPECT_EQ(s.tick(start + 12 * H, false, true), NeighborsAction::Discover);
}

TEST(NeighborsSchedule, AWalkThatCannotStartEndsTheCycleWithoutPublishing) {
    NeighborsSchedule s;
    s.configure(true, 24 * H);
    uint32_t t = offband::kNeighborsFirstCycleMs;
    ASSERT_EQ(s.tick(t, false, true), NeighborsAction::Discover);
    t += offband::kNeighborsRefreshMs;
    ASSERT_EQ(s.tick(t, false, true), NeighborsAction::StartWalk);
    s.walkStarted(false);   // e.g. no PSRAM for the walk
    EXPECT_EQ(s.tick(t + 1, false, true), NeighborsAction::None);
    EXPECT_FALSE(s.hasPublished());
    EXPECT_EQ(s.nextDueMs(), offband::kNeighborsFirstCycleMs + 24 * H);   // try again next interval
}

TEST(NeighborsSchedule, ManualWalkPublishesWhenDoneEvenWhenOff) {
    // plan: discover.scopes = query the table and publish now
    NeighborsSchedule s;
    s.configure(false, 24 * H);
    s.manualWalkStarted();
    EXPECT_EQ(s.tick(1000, true, true), NeighborsAction::None);
    EXPECT_EQ(s.tick(2000, false, true), NeighborsAction::Publish);
    s.published(true, 2000);
    EXPECT_EQ(s.tick(3000, false, true), NeighborsAction::None);
}

TEST(NeighborsSchedule, ManualWalkDuringRefreshTakesOverTheCycle) {
    NeighborsSchedule s;
    s.configure(true, 24 * H);
    uint32_t t = offband::kNeighborsFirstCycleMs;
    ASSERT_EQ(s.tick(t, false, true), NeighborsAction::Discover);
    s.manualWalkStarted();                                         // the user asked first
    EXPECT_EQ(s.tick(t + 10, true, true), NeighborsAction::None);   // no second walk on top
    EXPECT_EQ(s.tick(t + offband::kNeighborsRefreshMs, true, true), NeighborsAction::None);
    EXPECT_EQ(s.tick(t + offband::kNeighborsRefreshMs + 1, false, true), NeighborsAction::Publish);
}

TEST(NeighborsSchedule, TurningOffMidCycleStopsAtTheNextStep) {
    NeighborsSchedule s;
    s.configure(true, 24 * H);
    uint32_t t = offband::kNeighborsFirstCycleMs;
    ASSERT_EQ(s.tick(t, false, true), NeighborsAction::Discover);
    s.configure(false, 24 * H);
    EXPECT_EQ(s.tick(t + offband::kNeighborsRefreshMs, false, true), NeighborsAction::None);   // no walk
    EXPECT_FALSE(s.cycleRunning());
}

TEST(NeighborsSchedule, ChangingTheIntervalMovesTheNextDue) {
    NeighborsSchedule s;
    s.configure(true, 24 * H);
    uint32_t t = offband::kNeighborsFirstCycleMs;
    runCycle(s, t);
    s.configure(true, 12 * H);
    EXPECT_EQ(s.nextDueMs(), t + 12 * H);
    EXPECT_EQ(s.tick(t + 12 * H, false, true), NeighborsAction::Discover);
}

TEST(NeighborsSchedule, LastPublishIsRecordedOnlyWhenItWentOut) {
    NeighborsSchedule s;
    s.configure(false, 24 * H);
    EXPECT_FALSE(s.hasPublished());
    s.manualWalkStarted();
    ASSERT_EQ(s.tick(5000, false, true), NeighborsAction::Publish);
    s.published(false, 5000);
    EXPECT_FALSE(s.hasPublished());
    ASSERT_EQ(s.tick(5000 + M, false, true), NeighborsAction::Publish);
    s.published(true, 5000 + M);
    EXPECT_TRUE(s.hasPublished());
    EXPECT_EQ(s.lastPublishMs(), 5000u + M);
}

// Review of #1879: a due cycle used to run its discover and walk with nothing able to
// receive the report, then wait a whole interval.
TEST(NeighborsSchedule, NotReadyDefersTheCycleWithoutSpendingIt) {
    NeighborsSchedule s;
    s.configure(true, 24 * H);
    const uint32_t t = offband::kNeighborsFirstCycleMs;
    EXPECT_TRUE(s.needsReady(t, false));
    EXPECT_EQ(s.tick(t, false, false), NeighborsAction::None);
    EXPECT_FALSE(s.cycleRunning());
    EXPECT_EQ(s.nextDueMs(), offband::kNeighborsFirstCycleMs);   // still due, not pushed out
    // looked at again once a minute, not every pass
    EXPECT_FALSE(s.needsReady(t + M - 1, false));
    EXPECT_EQ(s.tick(t + M - 1, false, true), NeighborsAction::None);
    EXPECT_TRUE(s.needsReady(t + M, false));
    EXPECT_EQ(s.tick(t + M, false, true), NeighborsAction::Discover);
    EXPECT_EQ(s.nextDueMs(), t + M + 24 * H);
}

TEST(NeighborsSchedule, AReportThatCannotGoOutIsRetriedThenDropped) {
    NeighborsSchedule s;
    s.configure(true, 24 * H);
    uint32_t t = offband::kNeighborsFirstCycleMs;
    ASSERT_EQ(s.tick(t, false, true), NeighborsAction::Discover);
    t += offband::kNeighborsRefreshMs;
    ASSERT_EQ(s.tick(t, false, true), NeighborsAction::StartWalk);
    s.walkStarted(true);
    const uint32_t end = t + 5 * M;   // the walk ends while the broker is down
    EXPECT_FALSE(s.needsReady(end - 1, true));   // a running walk needs no answer
    EXPECT_EQ(s.tick(end, false, false), NeighborsAction::None);
    EXPECT_TRUE(s.cycleRunning());
    EXPECT_EQ(s.tick(end + M - 1, false, true), NeighborsAction::None);   // not yet
    ASSERT_EQ(s.tick(end + M, false, true), NeighborsAction::Publish);
    s.published(false, end + M);   // e.g. the broker dropped before the build
    EXPECT_TRUE(s.cycleRunning());
    // never ready again: dropped after the give-up time, and the next cycle stays on time
    for (uint32_t u = end + 2 * M; u < end + offband::kNeighborsPublishGiveUpMs; u += M) {
        EXPECT_EQ(s.tick(u, false, false), NeighborsAction::None) << u;
    }
    EXPECT_EQ(s.tick(end + offband::kNeighborsPublishGiveUpMs, false, true), NeighborsAction::None);
    EXPECT_FALSE(s.cycleRunning());
    EXPECT_FALSE(s.hasPublished());
    EXPECT_EQ(s.nextDueMs(), offband::kNeighborsFirstCycleMs + 24 * H);
}

TEST(NeighborsSchedule, AManualWalkReplacesAReportStillWaiting) {
    NeighborsSchedule s;
    s.configure(false, 24 * H);
    s.manualWalkStarted();
    EXPECT_EQ(s.tick(1000, false, false), NeighborsAction::None);   // waiting to go out
    s.manualWalkStarted();                                           // a fresh walk by hand
    EXPECT_EQ(s.tick(2000, true, true), NeighborsAction::None);
    EXPECT_EQ(s.tick(3000, false, true), NeighborsAction::Publish);   // offered at once
}

TEST(NeighborsSchedule, IntervalIsHeldToTheSettingRange) {
    NeighborsSchedule s;
    s.configure(true, 0);
    EXPECT_EQ(s.intervalMs(), offband::neighborsIntervalMs(offband::kNeighborsIntervalMinHours));
    s.configure(true, 0xFFFFFFFFu);
    EXPECT_EQ(s.intervalMs(), offband::neighborsIntervalMs(offband::kNeighborsIntervalMaxHours));
    s.configure(true, 48 * H);
    EXPECT_EQ(s.intervalMs(), 48 * H);
    NeighborsSchedule fresh;
    EXPECT_EQ(fresh.intervalMs(), offband::neighborsIntervalMs(offband::kNeighborsIntervalDefaultHours));
    EXPECT_FALSE(fresh.enabled());   // off until mqtt.neighbors on
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
