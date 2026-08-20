// src/helpers/wifi_telemetry/LinkStateMachine.h
//
// #913 / epic #911: the WiFi+MQTT bring-up sequence as a PURE state machine.
//
// WHY THIS EXISTS
//
// `WifiMqttTransport::connectWifi()` and `connectMqtt()` used to spin:
//
//     while (WiFi.status() != WL_CONNECTED) {
//         if ((millis() - start) > timeout_ms) return false;
//         delay(100);
//     }
//
// `delay()` is `vTaskDelay()`, so the task YIELDS rather than burning CPU --
// but it is the LOOP TASK that yields, and while it slept in here
// `mesh::Mesh::loop()` did not run, so `Dispatcher::checkRecv()` did not run
// and the LoRa receiver was unserviced. Measured on ST-P: a 28.2 s hole in
// received traffic containing two WiFi connect/teardown cycles (#910), which is
// what a client sees as a command that never gets a reply (#893).
//
// WHY IT IS A SEPARATE, ARDUINO-FREE TRANSLATION UNIT
//
// `WifiMqttTransport` needs the real `WiFi` and `PubSubClient` classes and is
// absent from the native `build_src_filter`, so none of its logic could be
// exercised off-device. That is exactly why a 20-second stall lived in it
// unnoticed. Everything decidable WITHOUT hardware lives here and is tested on
// the host; the transport keeps only the calls that genuinely need a radio.
//
// The machine takes observations and returns the next state plus the action to
// perform. It never calls WiFi, never sleeps, and never reads a clock: elapsed
// time is an INPUT, so timeout paths are tested instantly instead of by waiting.

#pragma once

#include <stdint.h>

namespace offband {

// Where the link is right now. Exposed through the transport so callers can
// tell "still connecting" from "failed" -- a bare bool cannot, and
// `main.cpp:311` treats false as failure (increments g_tel_wifi_fails and tears
// down), so every cycle would abort on its first pass.
//
// WiFi and MQTT are SEPARATE states, each with its own budget and its own phase
// timer. An earlier version collapsed them into one `Connecting` state sharing
// a single timer, which silently charged WiFi's elapsed time against MQTT's
// budget: a 6 s WiFi connect exhausted the 5 s MQTT budget and failed before a
// single attempt was made. Found by adversarial review.
//
// Worse, the test written for that property PASSED. It was hand-fed the
// post-reset elapsed value -- asserting the intent rather than driving the
// transition that was supposed to produce it. The tests now drive a simulated
// caller with a monotonic fake clock, so the reset has to actually happen.
enum class LinkState : uint8_t {
    Idle = 0,       // nothing running; nothing attempted yet, or torn down
    AwaitingWifi,   // WiFi bring-up in progress -- NOT a failure
    AwaitingMqtt,   // WiFi up, MQTT bring-up in progress -- NOT a failure
    Ready,          // WiFi + MQTT both up; publish() may be called
    Failed,         // an attempt ran out of time; caller decides whether to retry
};

// True while bring-up is in progress.
inline bool isConnecting(LinkState s) {
    return s == LinkState::AwaitingWifi || s == LinkState::AwaitingMqtt;
}

// What the transport should DO on this tick. Returned rather than performed, so
// the decision logic stays testable and the side effects stay in one place.
enum class LinkAction : uint8_t {
    None = 0,        // nothing to do; keep waiting
    StartWifi,       // call WiFi.begin()
    StartMqtt,       // attempt one MQTT connect
    ReportReady,     // transitioned into Ready this tick
    Teardown,        // give up / shut down (timeout, or an explicit stop)
};

// One observation of the world, gathered by the caller immediately before the
// tick. Deliberately plain data: no pointers, no Arduino types.
struct LinkInputs {
    bool wifi_connected = false;
    bool mqtt_connected = false;
    uint32_t phase_elapsed_ms = 0;            // ms since the CURRENT phase began
    uint32_t ms_since_last_mqtt_attempt = 0;  // rate-limits MQTT retries
    bool stop_requested = false;              // caller wants the link down
};

struct LinkTick {
    LinkState state = LinkState::Idle;
    LinkAction action = LinkAction::None;
    bool phase_changed = false;      // caller must reset its phase timer
};

// Timeouts preserved from the original blocking implementation so behaviour is
// unchanged in every respect except WHERE the waiting happens.
constexpr uint32_t kWifiConnectTimeoutMs = 15000;   // was connectWifi(15000)
constexpr uint32_t kMqttConnectTimeoutMs = 5000;    // was connectMqtt(5000)

// Minimum gap between MQTT connect attempts. The old code retried on a
// `delay(500)` cadence; without an equivalent the transport would re-attempt on
// EVERY loop pass -- thousands of TCP connects per second against a broker that
// is down. Preserves the original cadence without the blocking sleep.
constexpr uint32_t kMqttRetryIntervalMs = 500;

// Advance one tick. Pure: same inputs always give the same output.
inline LinkTick linkTick(LinkState current, const LinkInputs& in) {
    LinkTick out;
    out.state = current;

    // A stop request wins from any state. Checked FIRST so a teardown can never
    // be overtaken by a connection that completes in the same tick -- that
    // ordering is what stops a just-connected link from being left up after the
    // caller asked for it to go down.
    if (in.stop_requested) {
        if (current == LinkState::Idle) return out;          // already down
        out.state = LinkState::Idle;
        out.action = LinkAction::Teardown;
        out.phase_changed = true;
        return out;
    }

    switch (current) {
        case LinkState::Idle:
            // If the world already shows both up (a reconnect, or persistent
            // mode holding the link), adopt Ready rather than tearing a working
            // link down and rebuilding it.
            if (in.wifi_connected && in.mqtt_connected) {
                out.state = LinkState::Ready;
                out.action = LinkAction::ReportReady;
            } else if (in.wifi_connected) {
                out.state = LinkState::AwaitingMqtt;
                out.action = LinkAction::StartMqtt;
            } else {
                out.state = LinkState::AwaitingWifi;
                out.action = LinkAction::StartWifi;
            }
            out.phase_changed = true;
            return out;

        case LinkState::AwaitingWifi:
            if (in.wifi_connected) {
                // Hand over to the MQTT phase and RESTART the timer. This reset
                // is the entire point of splitting the states: MQTT gets its
                // own full budget regardless of how long WiFi took.
                out.state = in.mqtt_connected ? LinkState::Ready
                                              : LinkState::AwaitingMqtt;
                out.action = in.mqtt_connected ? LinkAction::ReportReady
                                               : LinkAction::StartMqtt;
                out.phase_changed = true;
                return out;
            }
            if (in.phase_elapsed_ms > kWifiConnectTimeoutMs) {
                out.state = LinkState::Failed;
                out.action = LinkAction::Teardown;
                out.phase_changed = true;
            }
            return out;   // otherwise keep waiting, do nothing

        case LinkState::AwaitingMqtt:
            if (!in.wifi_connected) {
                // WiFi dropped underneath us; go back and re-establish it.
                out.state = LinkState::AwaitingWifi;
                out.action = LinkAction::StartWifi;
                out.phase_changed = true;
                return out;
            }
            if (in.mqtt_connected) {
                out.state = LinkState::Ready;
                out.action = LinkAction::ReportReady;
                out.phase_changed = true;
                return out;
            }
            if (in.phase_elapsed_ms > kMqttConnectTimeoutMs) {
                out.state = LinkState::Failed;
                out.action = LinkAction::Teardown;
                out.phase_changed = true;
                return out;
            }
            // Retry on a CADENCE, never on every tick.
            if (in.ms_since_last_mqtt_attempt >= kMqttRetryIntervalMs) {
                out.action = LinkAction::StartMqtt;
            }
            return out;

        case LinkState::Ready:
            // Drop out of Ready the moment either leg goes away, so a caller
            // guarded by isReady() cannot publish into a dead socket.
            if (!in.wifi_connected) {
                out.state = LinkState::AwaitingWifi;
                out.action = LinkAction::StartWifi;
                out.phase_changed = true;
            } else if (!in.mqtt_connected) {
                out.state = LinkState::AwaitingMqtt;
                out.action = LinkAction::StartMqtt;
                out.phase_changed = true;
            }
            return out;

        case LinkState::Failed:
            // Terminal WITHIN the machine: it does not self-retry, because the
            // retry cadence belongs to the 5-minute telemetry schedule and
            // retrying here would hammer a down access point.
            //
            // The CALLER clears it by tearing down -- WifiMqttTransport::end()
            // resets _link to Idle. Without that reset this is a permanent
            // hang: end() disconnects the radio but the machine stays Failed
            // and every later begin() does nothing. Exactly what review found.
            return out;
    }
    return out;
}

// True once the WiFi phase has been given its full budget. Lets the caller
// attribute a failure to WiFi rather than MQTT, which is what g_tel_wifi_fails
// counts.
inline bool wifiPhaseExpired(const LinkInputs& in) {
    return !in.wifi_connected && in.phase_elapsed_ms > kWifiConnectTimeoutMs;
}

// NOTE ON millis() ROLLOVER, since review raised it: no guard is needed.
// Callers compute `phase_elapsed_ms` as `millis() - _phase_started_ms` in
// uint32 arithmetic, which wraps correctly. With millis()==10 and
// _phase_started_ms==4294967290, (10 - 4294967290) mod 2^32 == 16 -- the true
// elapsed time. The standard idiom is right; a rollover "fix" would add risk
// rather than remove it.

}  // namespace offband
