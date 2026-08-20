#include <gtest/gtest.h>
#include "helpers/wifi_observer/FailEscalateWindow.h"

using offband::FailEscalateWindow;

// Baseline: the first failure of an attempt escalates.
TEST(FailEscalateWindow, FirstFailureEscalates) {
    FailEscalateWindow w;
    EXPECT_TRUE(w.shouldEscalate(1000));
}

// The up-to-three events of ONE attempt (ERROR / ERROR+DISCONNECTED /
// DISCONNECTED), arriving within milliseconds, collapse to a single escalation.
TEST(FailEscalateWindow, PairedEventsCollapseToOne) {
    FailEscalateWindow w;
    EXPECT_TRUE(w.shouldEscalate(1000));    // ERROR
    EXPECT_FALSE(w.shouldEscalate(1003));   // DISCONNECTED ms later -- same attempt
    EXPECT_FALSE(w.shouldEscalate(1050));   // still inside the window
}

// Genuinely separate attempts (seconds+ apart) each escalate. The window slides
// forward to each escalation, so it is measured from the LAST count, not the first.
TEST(FailEscalateWindow, SeparateAttemptsEachEscalate) {
    FailEscalateWindow w;
    EXPECT_TRUE(w.shouldEscalate(0));                                  // attempt 1
    EXPECT_FALSE(w.shouldEscalate(OFFBAND_FAIL_ESCALATE_WINDOW_MS - 1));
    EXPECT_TRUE(w.shouldEscalate(OFFBAND_FAIL_ESCALATE_WINDOW_MS + 100)); // attempt 2
    // window now anchored at +5100; a hit just after it collapses again
    EXPECT_FALSE(w.shouldEscalate(OFFBAND_FAIL_ESCALATE_WINDOW_MS + 200));
}

// #906 finding 3: a real escalation at millis()==0 must not read as "no window".
// The paired event a few ms later must still collapse (no double-count).
TEST(FailEscalateWindow, EscalationAtZeroIsNotDoubleCounted) {
    FailEscalateWindow w;
    EXPECT_TRUE(w.shouldEscalate(0));    // first event at t=0
    EXPECT_FALSE(w.shouldEscalate(3));   // paired event -- collapses, not double
    EXPECT_FALSE(w.shouldEscalate(50));
}

// #906 finding 2: a clean connect ends the window, so a failure after a genuine
// reconnect escalates immediately instead of being masked by the pre-success
// window. Without reset(), the t=101000 failure would collapse into t=100000.
TEST(FailEscalateWindow, ResetOnConnectUnmasksNextFailure) {
    FailEscalateWindow w;
    EXPECT_TRUE(w.shouldEscalate(100000));   // failure -> window @100000
    w.reset();                               // clean connect
    EXPECT_TRUE(w.shouldEscalate(101000));   // 1s later, still <5s: escalates ONLY because reset
}

// A flapping broker (fail, briefly up, fail...) keeps escalating across resets,
// so it can still climb to terminal rather than sticking at penalty 1.
TEST(FailEscalateWindow, FlappingKeepsEscalating) {
    FailEscalateWindow w;
    int escalations = 0;
    uint32_t t = 0;
    for (int i = 0; i < 8; i++) {
        if (w.shouldEscalate(t)) escalations++;  // failure
        t += 500;
        w.reset();                                // brief successful connect
        t += 500;
    }
    EXPECT_EQ(escalations, 8);   // every flap counts -- reaches terminal territory
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
