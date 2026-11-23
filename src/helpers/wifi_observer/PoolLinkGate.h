#pragma once

// #2232: the broker pool's link gate.
//
// The pool's mqtt_worker task drives every enabled slot's connect on its own
// 500 ms timer. A repeater starts the pool in setup(), before WiFi, and drops
// WiFi between telemetry bursts; a connect attempt with no network either
// asserts inside lwIP (no tcpip thread yet: "tcpip_send_msg_wait_sem ...
// Invalid mbox", a crash loop at boot) or fails and counts toward the slot's
// terminal-failure penalty. So the owner of the link tells the pool whether
// it is up, and the worker acts on this decision every pass.
//
// Pure: no Arduino, no esp-mqtt. Host-tested in test/test_pool_link_gate.

#include <stdint.h>

namespace offband {

enum class LinkAction : uint8_t {
  Drive,     // link up: run the normal connect / backoff / rotation drive
  Hold,      // link down and was down: start nothing
  PauseAll,  // link just went down: stop every running client, start nothing
};

inline LinkAction linkAction(bool link_up, bool link_was_up) {
  if (link_up) return LinkAction::Drive;
  return link_was_up ? LinkAction::PauseAll : LinkAction::Hold;
}

// A disconnect the pool caused by pausing a client is not a failure: it must
// not raise the slot's penalty, or a repeater would fail its own slot after a
// handful of WiFi bursts.
inline bool disconnectCountsAsFailure(bool paused_by_pool) {
  return !paused_by_pool;
}

}  // namespace offband
