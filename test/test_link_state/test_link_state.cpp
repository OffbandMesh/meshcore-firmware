// Native unit tests for the WiFi+MQTT link state machine (#913, epic #911).
// Pure logic — no Arduino, no WiFi, no clock.
//
// These exist because the defect they replace was UNTESTABLE. The blocking
// spin lived inside `WifiMqttTransport`, which needs the real `WiFi` and
// `PubSubClient` classes and is absent from the native `build_src_filter` — so
// a 20-second stall on the loop task sat there unnoticed until it was caught on
// hardware with a serial capture (#910).
//
// Elapsed time is an INPUT, so a 15-second timeout is tested in microseconds.

#include <gtest/gtest.h>
#include "helpers/wifi_telemetry/LinkStateMachine.h"

using namespace offband;

namespace {
LinkInputs in(bool wifi, bool mqtt, uint32_t elapsed = 0, bool stop = false) {
    LinkInputs i;
    i.wifi_connected = wifi;
    i.mqtt_connected = mqtt;
    i.phase_elapsed_ms = elapsed;
    i.stop_requested = stop;
    return i;
}
}  // namespace

// ---------------------------------------------------------------- bring-up ---

TEST(LinkState, IdleStartsWifiWhenNothingIsUp) {
    auto t = linkTick(LinkState::Idle, in(false, false));
    EXPECT_EQ(LinkState::Connecting, t.state);
    EXPECT_EQ(LinkAction::StartWifi, t.action);
    EXPECT_TRUE(t.phase_changed);
}

TEST(LinkState, IdleSkipsStraightToMqttWhenWifiIsAlreadyUp) {
    auto t = linkTick(LinkState::Idle, in(true, false));
    EXPECT_EQ(LinkAction::StartMqtt, t.action);
}

TEST(LinkState, IdleAdoptsAWorkingLinkInsteadOfRebuildingIt) {
    // Persistent mode leaves WiFi+MQTT up between publish cycles. Tearing that
    // down and reconnecting would reintroduce the very stall being removed.
    auto t = linkTick(LinkState::Idle, in(true, true));
    EXPECT_EQ(LinkState::Ready, t.state);
    EXPECT_EQ(LinkAction::ReportReady, t.action);
}

TEST(LinkState, ConnectingWaitsWithoutActingWhileInsideTheBudget) {
    auto t = linkTick(LinkState::Connecting, in(false, false, 14999));
    EXPECT_EQ(LinkState::Connecting, t.state);
    EXPECT_EQ(LinkAction::None, t.action) << "must not block, retry or thrash";
    EXPECT_FALSE(t.phase_changed);
}

TEST(LinkState, ConnectingReachesReadyWhenBothLegsComeUp) {
    auto t = linkTick(LinkState::Connecting, in(true, true, 3000));
    EXPECT_EQ(LinkState::Ready, t.state);
    EXPECT_EQ(LinkAction::ReportReady, t.action);
}

// ----------------------------------------------------------------- timeouts ---
// Same budgets as the blocking implementation: only WHERE the waiting happens
// changes, never how long.

TEST(LinkState, WifiTimesOutAtTheOriginalFifteenSeconds) {
    EXPECT_EQ(15000u, kWifiConnectTimeoutMs);
    auto ok = linkTick(LinkState::Connecting, in(false, false, 15000));
    EXPECT_EQ(LinkState::Connecting, ok.state) << "must not fire early";
    auto late = linkTick(LinkState::Connecting, in(false, false, 15001));
    EXPECT_EQ(LinkState::Failed, late.state);
    EXPECT_EQ(LinkAction::Teardown, late.action);
}

TEST(LinkState, MqttTimesOutAtTheOriginalFiveSeconds) {
    EXPECT_EQ(5000u, kMqttConnectTimeoutMs);
    auto ok = linkTick(LinkState::Connecting, in(true, false, 5000));
    EXPECT_EQ(LinkState::Connecting, ok.state);
    auto late = linkTick(LinkState::Connecting, in(true, false, 5001));
    EXPECT_EQ(LinkState::Failed, late.state);
    EXPECT_EQ(LinkAction::Teardown, late.action);
}

TEST(LinkState, TheMqttBudgetIsNotChargedAgainstTheWifiBudget) {
    // WiFi up at 14s, MQTT still connecting 4s later: the WiFi budget is spent
    // but MQTT's is not, and the phase timer restarted at the transition.
    auto t = linkTick(LinkState::Connecting, in(true, false, 4000));
    EXPECT_EQ(LinkState::Connecting, t.state);
    EXPECT_EQ(LinkAction::None, t.action);
}

TEST(LinkState, FailedDoesNotSelfRetry) {
    // Retry cadence belongs to the telemetry schedule (5 min). Retrying here
    // would hammer a down access point.
    auto t = linkTick(LinkState::Failed, in(false, false, 60000));
    EXPECT_EQ(LinkState::Failed, t.state);
    EXPECT_EQ(LinkAction::None, t.action);
}

// -------------------------------------------------------------------- stop ---

TEST(LinkState, StopWinsFromEveryState) {
    for (auto s : {LinkState::Connecting, LinkState::Ready, LinkState::Failed}) {
        auto t = linkTick(s, in(true, true, 100, /*stop=*/true));
        EXPECT_EQ(LinkState::Idle, t.state);
        EXPECT_EQ(LinkAction::Teardown, t.action);
    }
}

TEST(LinkState, StopBeatsAConnectionCompletingInTheSameTick) {
    // Ordering guard. If the connected-check ran first, a link that came up on
    // the same tick as a stop request would be left UP after the caller asked
    // for it to go down — which in persistent/OTA terms means WiFi staying on
    // when it should not.
    auto t = linkTick(LinkState::Connecting, in(true, true, 10, /*stop=*/true));
    EXPECT_EQ(LinkState::Idle, t.state);
    EXPECT_EQ(LinkAction::Teardown, t.action);
}

TEST(LinkState, StopOnAnAlreadyIdleLinkDoesNothing) {
    auto t = linkTick(LinkState::Idle, in(false, false, 0, /*stop=*/true));
    EXPECT_EQ(LinkState::Idle, t.state);
    EXPECT_EQ(LinkAction::None, t.action) << "no redundant teardown";
}

// ------------------------------------------------------------ loss of link ---

TEST(LinkState, ReadyDropsToConnectingIfWifiGoesAway) {
    auto t = linkTick(LinkState::Ready, in(false, false));
    EXPECT_EQ(LinkState::Connecting, t.state);
    EXPECT_EQ(LinkAction::StartWifi, t.action);
}

TEST(LinkState, ReadyDropsToConnectingIfOnlyMqttGoesAway) {
    // A caller guarded by isReady() must not publish into a dead socket.
    auto t = linkTick(LinkState::Ready, in(true, false));
    EXPECT_EQ(LinkState::Connecting, t.state);
    EXPECT_EQ(LinkAction::StartMqtt, t.action);
}

TEST(LinkState, ReadyStaysReadyWhileBothLegsHold) {
    auto t = linkTick(LinkState::Ready, in(true, true, 999999));
    EXPECT_EQ(LinkState::Ready, t.state);
    EXPECT_EQ(LinkAction::None, t.action);
    EXPECT_FALSE(t.phase_changed);
}

// ----------------------------------------------------------- attribution ----

TEST(LinkState, WifiPhaseExpiredDistinguishesAWifiFailureFromAnMqttOne) {
    // g_tel_wifi_fails counts WiFi failures specifically.
    EXPECT_TRUE(wifiPhaseExpired(in(false, false, 15001)));
    EXPECT_FALSE(wifiPhaseExpired(in(false, false, 14000)));
    EXPECT_FALSE(wifiPhaseExpired(in(true, false, 999999)))
        << "WiFi is up; a stall here is MQTT's, not WiFi's";
}

// ---------------------------------------------------------------- purity -----

TEST(LinkState, TickIsPureAndRepeatable) {
    // No hidden state, no clock read: the same inputs must always give the same
    // answer. That property is what lets a 15-second timeout be tested
    // instantly, and it is why this logic lives outside the transport.
    const auto i = in(false, false, 15001);
    auto a = linkTick(LinkState::Connecting, i);
    auto b = linkTick(LinkState::Connecting, i);
    EXPECT_EQ(a.state, b.state);
    EXPECT_EQ(a.action, b.action);
    EXPECT_EQ(a.phase_changed, b.phase_changed);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
