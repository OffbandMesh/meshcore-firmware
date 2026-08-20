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
// but it is the LOOP TASK that yields, and while it sleeps in here it is not
// running `mesh::Mesh::loop()`, so `Dispatcher::checkRecv()` never runs and the
// LoRa receiver is unserviced. Measured on ST-P: a 28.2 s hole in received
// traffic containing two WiFi connect/teardown cycles (#910), which is what a
// client sees as a command that never gets a reply (#893).
//
// WHY IT IS A SEPARATE, ARDUINO-FREE TRANSLATION UNIT
//
// `WifiMqttTransport` needs the real `WiFi` and `PubSubClient` classes and is
// absent from the native `build_src_filter`, so none of this logic could be
// exercised off-device. That is precisely why a 20-second stall lived in it
// unnoticed. Everything decidable WITHOUT hardware lives here and is tested on
// the host; the transport keeps only the calls that genuinely need a radio.
//
// The machine takes observations -- "is WiFi up?", "is MQTT up?", "how long has
// this phase been running?" -- and returns the next state plus the action to
// perform. It never calls WiFi, never sleeps, and never reads a clock: the
// caller passes elapsed time in, so tests drive it with a fake clock.

#pragma once

#include <stdint.h>

namespace offband {

// Where the link is right now. Exposed through the transport so callers can
// tell "still connecting" from "failed" -- a bare bool cannot, and
// `main.cpp:311` treats false as failure (increments g_tel_wifi_fails and tears
// down), so every cycle would abort on its first pass.
enum class LinkState : uint8_t {
    Idle = 0,     // nothing running; nothing attempted yet, or torn down
    Connecting,   // WiFi and/or MQTT bring-up in progress -- NOT a failure
    Ready,        // WiFi + MQTT both up; publish() may be called
    Failed,       // an attempt ran out of time; caller decides whether to retry
};

// What the transport should DO on this tick. Returned rather than performed, so
// the decision logic stays testable and the side effects stay in one place.
enum class LinkAction : uint8_t {
    None = 0,        // nothing to do; keep waiting
    StartWifi,       // call WiFi.begin()
    StartMqtt,       // WiFi is up; connect the MQTT client
    ReportReady,     // transitioned into Ready this tick
    Teardown,        // give up / shut down (timeout, or an explicit stop)
};

// One observation of the world, gathered by the caller immediately before the
// tick. Deliberately plain data: no pointers, no Arduino types, so the whole
// machine is host-testable.
struct LinkInputs {
    bool wifi_connected = false;
    bool mqtt_connected = false;
    uint32_t phase_elapsed_ms = 0;   // ms since the CURRENT phase began
    bool stop_requested = false;     // caller wants the link down
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

// Advance one tick. Pure: same inputs always give the same output.
//
// The caller owns the phase timer because it owns the clock; `phase_changed`
// tells it when to restart it. Passing elapsed time IN rather than reading
// millis() here is what makes the timeout paths testable without waiting 15
// real seconds.
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
            // Nothing attempted yet. If the world already shows both up (a
            // reconnect, or someone else brought WiFi up), adopt Ready rather
            // than tearing a working link down and rebuilding it.
            if (in.wifi_connected && in.mqtt_connected) {
                out.state = LinkState::Ready;
                out.action = LinkAction::ReportReady;
                out.phase_changed = true;
            } else {
                out.state = LinkState::Connecting;
                out.action = in.wifi_connected ? LinkAction::StartMqtt
                                               : LinkAction::StartWifi;
                out.phase_changed = true;
            }
            return out;

        case LinkState::Connecting:
            if (in.wifi_connected && in.mqtt_connected) {
                out.state = LinkState::Ready;
                out.action = LinkAction::ReportReady;
                out.phase_changed = true;
                return out;
            }
            if (!in.wifi_connected) {
                // Still waiting on WiFi.
                if (in.phase_elapsed_ms > kWifiConnectTimeoutMs) {
                    out.state = LinkState::Failed;
                    out.action = LinkAction::Teardown;
                    out.phase_changed = true;
                }
                return out;   // otherwise: keep waiting, do nothing
            }
            // WiFi is up, MQTT is not.
            if (in.phase_elapsed_ms > kMqttConnectTimeoutMs) {
                out.state = LinkState::Failed;
                out.action = LinkAction::Teardown;
                out.phase_changed = true;
            }
            return out;

        case LinkState::Ready:
            // Drop out of Ready the moment either leg goes away, so a caller
            // guarded by isReady() cannot publish into a dead socket.
            if (!in.wifi_connected || !in.mqtt_connected) {
                out.state = LinkState::Connecting;
                out.action = in.wifi_connected ? LinkAction::StartMqtt
                                               : LinkAction::StartWifi;
                out.phase_changed = true;
            }
            return out;

        case LinkState::Failed:
            // Terminal until the caller acts. It does NOT self-retry: the retry
            // cadence belongs to the telemetry schedule (5 min), and retrying
            // from in here would hammer a down access point.
            return out;
    }
    return out;
}

// True once the WiFi phase has been given its full budget. Used by the caller
// to attribute a failure to WiFi rather than MQTT, which is what
// `g_tel_wifi_fails` counts.
inline bool wifiPhaseExpired(const LinkInputs& in) {
    return !in.wifi_connected && in.phase_elapsed_ms > kWifiConnectTimeoutMs;
}

}  // namespace offband
