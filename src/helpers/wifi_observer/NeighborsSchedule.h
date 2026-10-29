// src/helpers/wifi_observer/NeighborsSchedule.h
//
// #1399: when the observer refreshes its neighbor table, queries it and publishes the
// CoreScope /neighbors report (Feature #1385). Header-only and Arduino-free; the
// companion's loop() calls tick() every pass and acts on what it returns, so nothing
// here waits or sends.
//
// One cycle, while mqtt.neighbors is on: ask repeaters in range to identify themselves
// (Discover), give them the discover answer window, query every neighbor's regions
// (StartWalk), and when the walk ends publish (Publish). The next cycle starts one
// interval after this one started. The first waits kNeighborsFirstCycleMs from boot, so
// adverts have arrived and brokers have connected.
//
// "ready" is whether a report could go out now (clock set, a broker up, no report still
// being handed out). A due cycle waits for it rather than spend the walk's airtime on a
// report nobody can receive. A report that could not go out is offered again every
// kNeighborsPublishRetryMs while ready, for up to kNeighborsPublishGiveUpMs.
//
// discover.scopes starts a walk by hand (manualWalkStarted); its result is published
// when it ends whether or not periodic publishing is on.

#pragma once
#include <stdint.h>

namespace offband {

constexpr uint32_t kNeighborsFirstCycleMs = 10u * 60u * 1000u;
constexpr uint32_t kNeighborsRefreshMs = 60000u;   // the discover answer window
constexpr uint32_t kNeighborsPublishRetryMs = 60000u;
constexpr uint32_t kNeighborsPublishGiveUpMs = 30u * 60u * 1000u;

// mqtt.neighbors.interval, in hours.
constexpr uint16_t kNeighborsIntervalMinHours = 12;
constexpr uint16_t kNeighborsIntervalMaxHours = 336;
constexpr uint16_t kNeighborsIntervalDefaultHours = 24;

constexpr uint32_t neighborsIntervalMs(uint16_t hours) { return (uint32_t)hours * 3600u * 1000u; }
static_assert(neighborsIntervalMs(kNeighborsIntervalMaxHours) / 3600000u == kNeighborsIntervalMaxHours,
              "the longest interval fits millis()");

enum class NeighborsAction { None, Discover, StartWalk, Publish };

class NeighborsSchedule {
public:
    // Live: a change applies from the next tick. Turning off mid-cycle stops it before
    // its next step; a walk already running still finishes and is published. The
    // interval is held to the setting's range whatever the caller passes.
    void configure(bool enabled, uint32_t interval_ms) {
        const uint32_t lo = neighborsIntervalMs(kNeighborsIntervalMinHours);
        const uint32_t hi = neighborsIntervalMs(kNeighborsIntervalMaxHours);
        enabled_ = enabled;
        interval_ms_ = interval_ms < lo ? lo : interval_ms > hi ? hi : interval_ms;
    }

    NeighborsAction tick(uint32_t now_ms, bool walk_running, bool ready) {
        switch (phase_) {
            case Phase::Querying:
                if (walk_running) return NeighborsAction::None;
                phase_ = Phase::Publishing;
                publish_since_ms_ = now_ms;
                checked_ = false;
                return publishStep(now_ms, ready);
            case Phase::Publishing:
                return publishStep(now_ms, ready);
            case Phase::Refreshing:
                if (!enabled_) {
                    phase_ = Phase::Idle;
                    return NeighborsAction::None;
                }
                if ((uint32_t)(now_ms - refresh_since_ms_) < kNeighborsRefreshMs) return NeighborsAction::None;
                return NeighborsAction::StartWalk;   // until walkStarted() reports back
            case Phase::Idle:
            default:
                if (!enabled_ || !due(now_ms) || !readyCheck(now_ms, ready)) return NeighborsAction::None;
                checked_ = false;
                have_cycle_ = true;
                cycle_start_ms_ = now_ms;
                phase_ = Phase::Refreshing;
                refresh_since_ms_ = now_ms;
                return NeighborsAction::Discover;
        }
    }

    // Whether this tick would look at "ready", so the caller can skip working it out on
    // the passes that do not (most of them).
    bool needsReady(uint32_t now_ms, bool walk_running) const {
        switch (phase_) {
            case Phase::Publishing: return checkDue(now_ms);
            case Phase::Querying:   return !walk_running;
            case Phase::Idle:       return enabled_ && due(now_ms) && checkDue(now_ms);
            default:                return false;
        }
    }

    // After a StartWalk: false (no walk could start) ends this cycle unpublished.
    void walkStarted(bool ok) {
        if (phase_ == Phase::Refreshing) phase_ = ok ? Phase::Querying : Phase::Idle;
    }

    // After a Publish: whether the report went out. A "no" is offered again later.
    void published(bool ok, uint32_t now_ms) {
        if (phase_ != Phase::Publishing || !ok) return;
        phase_ = Phase::Idle;
        checked_ = false;
        published_ = true;
        last_publish_ms_ = now_ms;
    }

    // discover.scopes started a walk: publish when it ends. It takes over a cycle that
    // is still refreshing, so no second walk starts on top of it, and replaces a report
    // still waiting to go out with the fresher one.
    void manualWalkStarted() { phase_ = Phase::Querying; }

    // When the next cycle is due, in millis(); the first-cycle point before any ran.
    uint32_t nextDueMs() const { return have_cycle_ ? cycle_start_ms_ + interval_ms_ : kNeighborsFirstCycleMs; }
    bool enabled() const { return enabled_; }
    uint32_t intervalMs() const { return interval_ms_; }
    bool cycleRunning() const { return phase_ != Phase::Idle; }
    bool hasPublished() const { return published_; }
    uint32_t lastPublishMs() const { return last_publish_ms_; }

private:
    enum class Phase { Idle, Refreshing, Querying, Publishing };

    bool due(uint32_t now_ms) const {
        if (!have_cycle_) return now_ms >= kNeighborsFirstCycleMs;
        return (uint32_t)(now_ms - cycle_start_ms_) >= interval_ms_;
    }

    // "ready" is looked at once per kNeighborsPublishRetryMs, not every pass.
    bool checkDue(uint32_t now_ms) const {
        return !checked_ || (uint32_t)(now_ms - last_check_ms_) >= kNeighborsPublishRetryMs;
    }
    bool readyCheck(uint32_t now_ms, bool ready) {
        if (!checkDue(now_ms)) return false;
        checked_ = true;
        last_check_ms_ = now_ms;
        return ready;
    }

    NeighborsAction publishStep(uint32_t now_ms, bool ready) {
        if ((uint32_t)(now_ms - publish_since_ms_) >= kNeighborsPublishGiveUpMs) {
            phase_ = Phase::Idle;   // this report is dropped; the next cycle runs on time
            checked_ = false;
            return NeighborsAction::None;
        }
        return readyCheck(now_ms, ready) ? NeighborsAction::Publish : NeighborsAction::None;
    }

    bool enabled_ = false;
    uint32_t interval_ms_ = neighborsIntervalMs(kNeighborsIntervalDefaultHours);
    Phase phase_ = Phase::Idle;
    bool have_cycle_ = false;
    uint32_t cycle_start_ms_ = 0;
    uint32_t refresh_since_ms_ = 0;
    uint32_t publish_since_ms_ = 0;
    bool checked_ = false;
    uint32_t last_check_ms_ = 0;
    bool published_ = false;
    uint32_t last_publish_ms_ = 0;
};

}  // namespace offband
