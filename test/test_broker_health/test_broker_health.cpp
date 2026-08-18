#include <gtest/gtest.h>
#include "helpers/wifi_observer/BrokerHealth.h"

using offband::BrokerHealth;

// A broker that never fails is never penalised, and a clean dwell on a healthy
// broker is a no-op (nothing to earn).
TEST(BrokerHealth, HealthyStaysHealthy) {
    BrokerHealth h;
    EXPECT_TRUE(h.isHealthy());
    EXPECT_FALSE(h.isTerminal());
    for (int i = 0; i < 10; i++) h.onCleanDwell();
    EXPECT_EQ(h.fail_penalty, 0);
    EXPECT_TRUE(h.isHealthy());
}

// A failure escalates the penalty and discards clean-streak progress.
TEST(BrokerHealth, FailureEscalatesAndZeroesStreak) {
    BrokerHealth h;
    h.onFailure();
    EXPECT_EQ(h.fail_penalty, 1);
    EXPECT_EQ(h.clean_streak, 0);
    h.onCleanDwell();
    EXPECT_EQ(h.clean_streak, 1);   // progress toward the bar
    h.onFailure();
    EXPECT_EQ(h.fail_penalty, 2);
    EXPECT_EQ(h.clean_streak, 0);   // failure wiped the progress
}

// THE CORE #739 PROPERTY: a single success does NOT clear the penalty. This is
// what the old retry_count=0-on-connect did, and why a flaky broker never got
// penalised.
TEST(BrokerHealth, SingleSuccessDoesNotRehabilitate) {
    BrokerHealth h;
    h.onFailure(); h.onFailure(); h.onFailure();
    EXPECT_EQ(h.fail_penalty, 3);
    h.onCleanDwell();               // one good dwell
    EXPECT_EQ(h.fail_penalty, 3) << "one success must NOT clear the penalty";
    EXPECT_FALSE(h.isHealthy());
}

// A broker earns its way back with SUCCESS_BAR consecutive clean dwells.
TEST(BrokerHealth, SuccessBarRehabilitates) {
    BrokerHealth h;
    h.onFailure(); h.onFailure();
    EXPECT_EQ(h.fail_penalty, 2);
    for (unsigned i = 0; i < OFFBAND_BROKER_SUCCESS_BAR; i++) {
        EXPECT_FALSE(h.isHealthy()) << "must not rehabilitate before the bar";
        h.onCleanDwell();
    }
    EXPECT_TRUE(h.isHealthy()) << "clears after the bar is met";
    EXPECT_EQ(h.fail_penalty, 0);
    EXPECT_EQ(h.clean_streak, 0);
}

// A failure part-way through the clean streak makes the broker start the bar over.
TEST(BrokerHealth, FailureMidStreakRestartsTheBar) {
    BrokerHealth h;
    h.onFailure();
    h.onCleanDwell();               // 1 of the bar
    ASSERT_GE(OFFBAND_BROKER_SUCCESS_BAR, 2u);
    h.onFailure();                  // back to square one, penalty up
    EXPECT_EQ(h.clean_streak, 0);
    EXPECT_EQ(h.fail_penalty, 2);
    // now it must serve the FULL bar again
    for (unsigned i = 0; i < OFFBAND_BROKER_SUCCESS_BAR - 1; i++) h.onCleanDwell();
    EXPECT_FALSE(h.isHealthy());
    h.onCleanDwell();
    EXPECT_TRUE(h.isHealthy());
}

// A persistently-dead broker escalates to a terminal give-up.
TEST(BrokerHealth, PersistentFailureBecomesTerminal) {
    BrokerHealth h;
    for (unsigned i = 0; i < OFFBAND_BROKER_TERMINAL_PENALTY; i++) {
        EXPECT_FALSE(h.isTerminal());
        h.onFailure();
    }
    EXPECT_TRUE(h.isTerminal()) << "gives up after the terminal threshold";
}

// The scenario that motivated the issue: a ~50% flaky broker that alternates
// fail/clean never rehabilitates (the bar needs CONSECUTIVE cleans) and its
// penalty ratchets toward terminal instead of oscillating at the floor.
TEST(BrokerHealth, FlakyAlternatingNeverRehabilitatesAndEscalates) {
    BrokerHealth h;
    ASSERT_GE(OFFBAND_BROKER_SUCCESS_BAR, 2u);   // else a single clean would pass
    uint16_t prev = 0;
    for (int i = 0; i < 20; i++) {
        h.onFailure();
        h.onCleanDwell();            // one clean, not enough for the bar
        EXPECT_FALSE(h.isHealthy()) << "alternating flaky must never look healthy";
        EXPECT_GT(h.fail_penalty, prev) << "penalty must keep ratcheting up";
        prev = h.fail_penalty;
        if (h.isTerminal()) break;
    }
    EXPECT_TRUE(h.isTerminal()) << "a sustained-flaky broker eventually gives up";
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
