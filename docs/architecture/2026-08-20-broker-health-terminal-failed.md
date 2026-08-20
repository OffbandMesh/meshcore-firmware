# Broker Health, Backoff Escalation, and the Terminal `Failed` State

**Date:** 2026-08-20
**Status:** As-built (lands with #838 + #848)
**Area:** `src/helpers/wifi_observer/` — `MqttBroker`, `MqttBrokerPool`, `BrokerHealth`
**Issues:** #739 (penalty + terminal state), #838 (connect-layer escalation), #848 (Failed sticks + no crash; subsumes #823), #746 (auto-reconnect trade-off)

## Why this exists

The observer keeps a rotating set of TLS/wss MQTT broker connections, at most
`OFFBAND_MAX_LIVE_TLS = 1` live at a time (heap on the ESP32 fits ~2 mbedTLS
contexts before an OOM reboot). A configured broker can be healthy, flaky, or
dead. This document is the design-of-record for how the pool tracks a broker's
reliability, how it escalates backoff, and how a persistently-dead broker is
given up on **without** starving the healthy ones, crashing the device, or
churning a rotation slot forever.

## Broker states (`BrokerState`)

| State | Meaning |
|---|---|
| `Down` | disabled, or not yet attempted |
| `Connecting` | TCP connect / TLS handshake in progress |
| `Up` | connected and serving |
| `Backoff` | failed; waiting out the (penalty-scaled) backoff window |
| `HeldNoClock` | wss/TLS deferred until the wall clock is sane (NTP/GPS). Not a failure |
| `HeldNoHeap` | TLS bring-up deferred: free heap below `OFFBAND_TLS_HEAP_FLOOR_BYTES`. Not a failure |
| `HeldBudget` | TLS bring-up deferred: the pool already holds `OFFBAND_MAX_LIVE_TLS` live contexts — normal rotation waiting its turn. Not a failure |
| `Failed` | **terminal** — given up on after sustained failure. Dropped from rotation, client reaped, logged loudly. Cleared only by operator reconfigure |

## Health model — the sticky penalty (`BrokerHealth`)

`BrokerHealth` is a dependency-free, host-testable policy object
(`test/test_broker_health/`). It owns *how a broker's reliability maps to
backoff, rehabilitation, and a terminal give-up*.

- `fail_penalty` escalates on each failed connection attempt and is **sticky in
  both directions**: a single success does **not** clear it (a flaky broker
  cannot cheaply reset), and the broker must earn its way back with
  `OFFBAND_BROKER_SUCCESS_BAR` (3) **consecutive** clean full-dwells.
- Backoff length is `brokerBackoffMs(fail_penalty)` (5 / 15 / 30 / 60 / 120 s,
  then 120 s capped) — it tracks the sticky penalty, **not** a per-attempt
  counter that a lone success would zero.
- Past `OFFBAND_BROKER_TERMINAL_PENALTY` (8) the broker is declared `Failed`.

### Why a sticky penalty (the #739 problem)

Before #739 the only backoff state was `retry_count`, reset to 0 on **any**
successful connect. That is fine for a *dead* broker but wrong for a *flaky*
one: an intermittent broker connects just often enough to zero its penalty, so
it oscillates at the 5 s floor forever and keeps claiming a rotation turn equal
to a reliable broker. Measured on hv3-bench: two ~50%-failing remote JWT brokers
held 46 % of a soak with **no** TLS broker up.

## Escalation — one choke point, time-window deduped (#838)

Every failure routes through a single `MqttBroker::escalateFailureOnce(now_ms)`,
which calls `BrokerHealth::onFailure()` at most once per
`OFFBAND_FAIL_ESCALATE_WINDOW_MS` (5 s). The dedup decision itself lives in a
dependency-free, host-tested seam — `offband::FailEscalateWindow`
(`FailEscalateWindow.h`, `test/test_fail_escalate_window/`) — so the escalation
logic that was hardware-only in #838 now has unit coverage. Callers:

- `onDisconnected` — an established connection dropped.
- `onError` — a connection error (**including a connect-layer refusal /
  unreachable / timeout that fires `MQTT_EVENT_ERROR` with no following
  `DISCONNECTED`**).
- the two synchronous `tryConnect` failures (JWT re-apply, `client_start`).

### Why a *time-window* dedupe

A single failed attempt can surface three ways: `ERROR` alone (connect refused/
unreachable — no `DISCONNECTED` follows), `ERROR`+`DISCONNECTED` (TLS/auth
failure after the TCP connect), or `DISCONNECTED` alone (a clean drop). The 5 s
window collapses the near-simultaneous ERROR+DISCONNECTED of one attempt to a
single escalation, while genuinely separate attempts (esp-mqtt's reconnect
cadence, or the pool's backoff-throttled retries — seconds+ apart) each count.

The seam guards the window with an explicit `active_` flag rather than an
`== 0` timestamp sentinel (so a real escalation at `millis() == 0` is not read
as "no window", which would double-count its paired event), and `onConnected()`
calls `reset()` so a failure after a genuine reconnect escalates immediately
instead of being masked by the pre-success window. Both were Gemini-review
findings (#906).

**#838's core fix:** before it, escalation lived only in `onDisconnected`, so a
**connect-layer** failure (ERROR with no DISCONNECTED — the common "server
down" case) never escalated at all, and a dead broker never reached terminal.
An earlier boolean-guard-reset-on-`MQTT_EVENT_BEFORE_CONNECT` design failed on
hardware because connect-refused retries fire repeated ERRORs with no
`BEFORE_CONNECT` between them; the time-window replaced it.

## The terminal `Failed` state (#848, subsumes #823)

When `fail_penalty` crosses the terminal threshold, `noteFailure()` sets
`BrokerState::Failed`. Making that *stick* requires three things to all hold:

1. **Reap the client, keep the state.** The pool's worker reaps a `Failed`
   broker's client via `releaseClient(keep_failed = true)`. This destroys the
   ~60 KB esp-mqtt/mbedTLS client (so esp-mqtt cannot auto-reconnect it) **but
   preserves `state = Failed`**. A plain `releaseClient()` resets `rt_` to
   `Down` — which was the real cause of #823: the broker looked `Down`, got
   re-promoted, re-failed, and was reaped again in a churn loop.
2. **Guards on every revive path.** `tryConnect()` and `reconcileSlot()`
   early-return on `Failed`; `onConnected()` ignores a late CONNECTED for a
   `Failed` broker (belt-and-suspenders against a racing auto-reconnect).
3. **Excluded from the candidate set.** The worker's re-drive loop omits
   `Failed` from the promotable states, so it never competes for the TLS budget.

A `Failed` broker therefore holds no client, consumes no rotation turn, and
stays out until an operator reconfigures it.

### Worker stack sizing (the #848 crash)

`esp_mqtt_client_stop` + `esp_mqtt_client_destroy` on a client that is
**mid-connect-failure** is a deeper esp-tls/transport teardown than the clean
rotated-out clients the pool destroys thousands of times during normal
rotation — and it blocks the worker ~3–4 s while it joins the stuck mqtt task.
Instrumentation measured the `mqtt_worker` stack high-water fall to **340 bytes
free of 6144** during a terminal reap; it intermittently overflowed, tripping
the stack canary on the adjacent IDLE0 task and panicking the device (the
coredump writer then double-faulted on the exhausted stack, which is why the
raw backtrace was corrupted). The worker stack is therefore **10240** bytes
(+4 KB → ~4.4 KB worst-case headroom). This crash was latent since #739's
reaper landed — it was never triggered because escalation never reached
terminal until #838.

## The auto-reconnect trade-off (#746)

esp-mqtt's client-level auto-reconnect stays **ON**. Disabling it globally
(reverted commit `cc8fec4a`) was catastrophic — a healthy broker terminally
failed and rotation collapsed (`live=0/1` up to 96 %), because the pool's own
reconnect path is not robust enough to be the sole connect driver. Auto-reconnect
is kept as the resilience safety net. Its downside — reconnecting a still-*trying*
(non-terminal) broker on its own ~10 s cadence, bypassing the pool's backoff — is
an accepted trade-off: failures still escalate the sticky penalty through
`onError`/`onDisconnected` regardless of who drives the connect, and a broker
that reaches terminal has its client destroyed so auto-reconnect has nothing to
resurrect.

## Operator escape

The only way out of `Failed` is an operator reconfigure — `mqtt enable/set/clear`
→ `reloadSlot()`, which calls `MqttBroker::clearTerminalFailure()` (resets the
penalty and `Failed → Down`) before the worker reconciles the slot, so the
Failed-guards let it come back up with a clean slate.

## Tunable constants

| Constant | Value | Where |
|---|---|---|
| `OFFBAND_BROKER_SUCCESS_BAR` | 3 consecutive clean dwells to rehab | `BrokerHealth.h` |
| `OFFBAND_BROKER_TERMINAL_PENALTY` | 8 | `BrokerHealth.h` |
| `OFFBAND_FAIL_ESCALATE_WINDOW_MS` | 5000 | `FailEscalateWindow.h` |
| backoff schedule | 5 / 15 / 30 / 60 / 120 s (capped) | `brokerBackoffMs()` |
| `OFFBAND_MAX_LIVE_TLS` | 1 | build flag |
| `mqtt_worker` stack | 10240 bytes | `MqttBrokerPool.cpp` |

## Validation

- `test/test_broker_health/` — 7 host tests of the sticky-penalty policy
  (single-success-does-not-rehab, success-bar, flaky-alternating-goes-terminal,
  etc.).
- `test/test_fail_escalate_window/` — 6 host tests of the escalation dedup
  (paired-collapse, separate-attempts-each-escalate, `millis()==0` no-double-count,
  reset-unmasks-next-failure, flapping-keeps-escalating).
- Hardware (hv3-bench): a broker's IP blocked at the router (REJECT tcp-reset)
  drives it `p1→p8`, it reaches terminal `Failed`, **holds** (0 revivals), the
  device does **not** panic, and the healthy broker keeps `live=1/1`.
