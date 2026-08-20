# WiFi telemetry lifecycle — design record

**Issue:** #912 (design) under epic #911. Defect record: #910. Answers #893.
**Date:** 2026-08-20
**Status:** **AGREED 2026-08-20** — owner selected **Option 1** (lifecycle worker), with **Option 2 planned as a follow-up**.

---

## 1. The defect

`WifiMqttTransport::connectWifi()` and `connectMqtt()` spin-wait **on the Arduino loop task**:

```cpp
// src/helpers/wifi_telemetry/WifiMqttTransport.cpp:66-77
uint32_t start = millis();
while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - start) > timeout_ms) { ...; return false; }
    delay(100);
}
```

| Call | Wait | Default timeout |
|---|---|---|
| `connectWifi()` | `while` + `delay(100)` | **15,000 ms** |
| `connectMqtt()` | `while` + `delay(500)` | **5,000 ms** |
| `end()` | `WiFi.disconnect(true)` + `WiFi.mode(WIFI_OFF)` | blocking |

While these spin, `mesh::Mesh::loop()` never runs, so `Dispatcher::checkRecv()` never runs, and **the LoRa receiver is unserviced**. The radio is not congested — it is deaf.

### Evidence

`[verified: live caplog capture, ST-P (heltec_v4_repeater_telemetry), level=packet, 2026-08-20]`

Every CLI exchange that completed took **exactly 0.6 s** (ten consecutive pairs, matching `CLI_REPLY_DELAY_MILLIS = 600`), then:

```
  302.4s  RX len=22
  303.0s  TX len=22  (+0.6s)
     ---- [WTEL] begin: connecting WiFi... / connected / MQTT connected / torn down
     ---- [WTEL] begin: connecting WiFi... / connected / MQTT connected / subscribe ok / torn down
  331.2s  RX len=38  (+28.2s)   <-- 28.2 s with NO received traffic
```

Owner independently observed a client-side failure at **28.7 s**. `packet pool empty`: **0 occurrences**.

**Ruled out with evidence:** the CLI handler (0.6 s, ten for ten), packet-pool starvation, RF marginality (replies that were sent arrived — the *requests* went missing), radio congestion.

---

## 2. Why two cycles landed together

`[verified: main.cpp:78-83, 495, 533]` — there are **two independent triggers**, each doing its own full WiFi up/down:

| Trigger | Function | Interval |
|---|---|---|
| Telemetry publish | `wifi_telemetry_collect_and_publish()` | `WIFI_TELEMETRY_INTERVAL_MS` = **5 min** |
| Command poll | `wifi_telemetry_http_cmd_poll()` | `g_cmd_poll_burst_interval_ms`, **default = the same 5 min** |

Source comment: *"its own WiFi up-and-down (independent of telemetry publish cycle)"*.

Because both default to the same interval they fire together, so the deaf window is **roughly double** a single cycle. Any fix must cover **both** triggers; fixing only the publish path leaves half the defect.

---

## 3. Blast radius — enumerated, not assumed

`[verified: uncapped searches, 2026-08-20]`

- **`TelemetryTransport` implementations: 1** (`WifiMqttTransport`). A contract change affects exactly one implementer.
- **Lifecycle call sites in `main.cpp`: 7** (`begin()` ×2, `end()` ×5).
- **Distinct caller shapes: 2**, and they are *not* compatible:

| Caller | Shape | Tolerates async `begin()`? |
|---|---|---|
| `WifiTelemetry.cpp:75-79` | discards `begin()`'s return, re-checks `isReady()` | **yes** |
| `main.cpp:308-316` | `bool up = begin();` then publishes **in the same call** | **no** |

```cpp
// main.cpp:308-316 — the constraint
bool transport_up = g_tel_transport->begin();
g_tel_last_mqtt_state = g_tel_transport->getMqttState();
if (!transport_up) { g_tel_wifi_fails++; g_tel_transport->end(); return; }
```

### The declared contract

`[verified: WifiTelemetry.h:138-160]`

```
begin()   — "Bring up the transport (connect WiFi, connect MQTT).
             May be called multiple times; idempotent if already connected.
             Returns true on success."
end()     — "Tear down... Used to drop into low-power state between publish cycles."
isReady() — "Is the transport currently ready to accept publish() calls?"
```

`begin()`'s **synchronous success semantics are documented**, not incidental. `end()` exists deliberately for **power**, which any design must preserve.

---

## 4. An existing mode that already avoids this

`[verified: main.cpp:64-73]` — persistent WiFi mode (#56 / D2):

```c
// When non-zero, marks the millis() deadline at which to auto-revert to BURST.
// Zero = BURST mode (default). When persistent, collect_and_publish() skips the
// final transport.end() so WiFi stays up between publish cycles.
```

Because `begin()` early-returns when `isReady()`, **the blocking is specific to BURST mode** — the default. In persistent mode only the *first* connect blocks.

This is **not** a fix (persistent mode costs power, is capped at 60 min by `WIFI_PERSISTENT_MAX_MS`, and still blocks on first connect), but it bounds the problem and is a useful diagnostic lever.

---

## 5. Options

### Option 1 — move both triggers onto a lifecycle worker task

Precedent in-tree: `MqttBrokerPool` (#53) — *"Lifecycle worker (#53): owns ALL blocking esp_mqtt ops off loopTask"*, `xTaskCreatePinnedToCore(..., 6144, this, 5, &worker_task_, tskNO_AFFINITY)`.

- **Interface change:** none. `begin()` keeps its documented synchronous semantics.
- **Caller redesign:** none — the two trigger functions move wholesale; their internal logic is untouched.
- **Concurrency introduced:** yes. `g_tel_*` counters and transport state become cross-task. **Open risk:** the CLI reads telemetry stats from the loop side; those reads need auditing.
- **Cost:** one task + stack (~6 KB by the #53 precedent), and WiFi bring-up's heap spike moves onto that stack.
- **Blast radius:** small and contained; blocking still exists but off the critical path.

### Option 2 — non-blocking state machine in the transport

- **Interface change:** yes. `begin()` can no longer mean "connected on return"; the documented contract changes for the one implementation.
- **Caller redesign:** yes, and this is the real cost — **both** triggers become multi-`loop()`-pass state machines, preserving counters and guaranteed teardown across passes.
- **Concurrency introduced:** none. Single-threaded reasoning throughout.
- **Cost:** no task, no stack; larger diff across 7 call sites and 2 trigger functions.
- **Blast radius:** wider, but the blocking **ceases to exist** rather than relocating.

### Option 3 — shrink the timeouts

Mitigation only. Narrows the deaf window; does not remove it. **Rejected as the fix.** Worth keeping as a defensive follow-up regardless of which option lands.

---

## 6. Recommendation

**Option 1 — the lifecycle worker.**

Reasoning, and the counter-argument it has to survive:

The strongest case for Option 2 is that it *eliminates* blocking rather than relocating it, and introduces no concurrency. That is a genuine architectural advantage and it is why this record does not simply default to precedent.

It loses on **blast radius against a live defect**. Option 2 changes a documented interface contract, then requires **both** trigger functions to be re-expressed as state machines that maintain `g_tel_wifi_attempts` / `g_tel_wifi_fails` / `g_tel_publish_attempts` correctly across multiple `loop()` passes, and preserve guaranteed teardown on failure. That is a redesign of the telemetry cycle's control flow, in a role that is deployed, to fix a bug whose impact is a deaf radio.

Option 1 gets the radio listening again while changing **no contract and no caller logic**, and follows a pattern already proven in this tree for exactly this problem.

**This is a sequencing judgement, not a claim that Option 1 is the better end state.** If the concurrency audit in §7 turns up shared state that cannot be cleanly isolated, Option 2 becomes correct and this recommendation should be revisited rather than forced.

### Decision — owner, 2026-08-20

**Option 1 agreed for this epic. Option 2 planned as a follow-up**, tracked separately so it does not gate #911's completion.

That makes Option 1 explicitly a **staging step**, not the end state. Two consequences bind the implementation:

- The worker must not entrench assumptions that make Option 2 harder later. Concretely: keep the blocking confined to `WifiMqttTransport`, and do **not** spread `begin()`-is-synchronous assumptions into new code.
- The shared-state audit (§7.1) is still blocking. It is now doubly load-bearing: it decides whether Option 1 is safe, and its findings are the main input to scoping Option 2.

---

## 7. Open risks to resolve during implementation

1. **Shared-state audit (blocking).** Enumerate every reader of `g_tel_wifi_attempts`, `g_tel_wifi_fails`, `g_tel_publish_attempts`, `g_tel_last_mqtt_state`, `g_tel_persistent_until_ms`. If the CLI reads them from the loop task while the worker writes, they need protection or single-owner discipline.
2. **Heap during WiFi bring-up.** Bring-up is a heap spike; on a stack that is not the loop task's. Baseline free heap on ST-P is ~204 KB.
3. **Both triggers must move.** Fixing only the publish path leaves the cmd-poll path blocking and the defect half-present.
4. **Persistent mode interaction.** `end()` is skipped in persistent mode; the worker must honour that or it will tear down WiFi under OTA (`OTA_EXTEND_MS` at main.cpp:428).
5. **Ordering.** The worker must not publish after a teardown request has been queued.

---

## 8. What the integration test must show (#915)

- **No RX gap** coinciding with `[WTEL] begin:` / `end:` — the defect signature is a 28.2 s hole containing two cycles
- CLI round-trip stays **~0.6 s** across a full telemetry cycle
- Telemetry still publishes; MQTT still connects
- No new `packet pool empty`
- Free heap not materially below the ~204 KB baseline
- **Both** triggers exercised — publish and cmd-poll

---

## 9. Adversarial review response (gemini-2.5-pro, 2026-08-20)

Two CRITICALs and four HIGH/MEDIUMs. Each verified rather than accepted or dismissed.

### CRITICAL — "Option 1 may not fix the defect" (priority inversion / shared LwIP locks)

**Reduced to LOW, with evidence.**

`[verified: framework-arduinoespressif32/cores/esp32/esp32-hal-misc.c:176-178]`

```c
void delay(uint32_t ms) { vTaskDelay(ms / portTICK_PERIOD_MS); }
```

`delay()` **yields**. During the spin, the task is BLOCKED, not spinning, and holds no CPU. That is exactly why the defect presents as it does today: `loopTask` is not burning CPU for 20 s, it is *sleeping inside `connectWifi()`* and therefore not running `mesh::Mesh::loop()`. Move that call to a worker and `loopTask` is free to run — the worker sleeps instead.

The residual concern — a mutex held *inside* the WiFi/LwIP stack that `loopTask` also needs — is real but narrow: `mesh::Mesh::loop()` services the LoRa radio over SPI and packet queues, and does not call into LwIP. **Open item:** confirm during implementation that nothing on the mesh loop path takes a WiFi/LwIP lock.

### CRITICAL — missing operational requirements

**Watchdog: reduced to LOW.** `[verified: sdkconfig:1197-1200]` `CONFIG_ESP_TASK_WDT_PANIC=y`, `TIMEOUT_S=5`, `CHECK_IDLE_TASK_CPU0=y`. A 20 s block does not panic **because `delay()` yields and the idle task keeps being fed** — consistent with the field behaviour (no reboots observed). A worker using the same yielding wait is equally safe. **Recorded because it was an undocumented assumption, not because it is a defect.**

**Stack sizing: ACCEPTED.** 6 KB was cited from precedent, which is a guess. Implementation must **measure** `uxTaskGetStackHighWaterMark()` under a real connect+TLS cycle and size from the measurement.

**Worker failure semantics: ACCEPTED.** Undefined today. Must specify behaviour on worker death and who releases WiFi if it dies mid-cycle.

### HIGH — the shared-state audit should GATE the decision, not follow it

**ACCEPTED, and this is the sharpest point in the review.** §7.1 defers the audit to implementation, but its outcome is a primary input to choosing Option 1 at all. Deferring it means discovering infeasibility under schedule pressure.

**Change:** the audit runs **first**, as the opening step of #913, and its findings are recorded here. If it finds state that cannot be cleanly isolated, that is a stop-and-return-to-the-owner condition, not something to engineer around.

### HIGH — persistent-mode TOCTOU (worker tears down WiFi mid-OTA)

**ACCEPTED. Real and specific.** `loopTask` extends `g_tel_persistent_until_ms` on OTA activity (`main.cpp:419-428`); a worker reading that value can decide to tear down, be preempted while the deadline is extended, then resume and tear down **mid-OTA download**.

**Change:** teardown must not be decided from an unprotected shared deadline. Use an explicit command to the worker, or a mutex held across check-and-act.

### MEDIUM — counters are read-modify-write, not atomic

**ACCEPTED.** `g_tel_wifi_fails++` is not atomic on a 32-bit MCU even though a `uint32_t` load/store is. Increments crossing the task boundary need `std::atomic` or a critical section. Where a variable is written only by the worker and read for display, stale reads are acceptable — but that must be stated, not assumed.

### MEDIUM — two triggers on one worker: starvation and incoherent teardown

**ACCEPTED, and the suggested shape is better than a plain queue.** A command queue lets a publish's `end()` tear down a connection the cmd-poll is still using. Model it as **reference-counted demand** — "N users want WiFi up" — with teardown only when the count reaches zero and no deadline holds it open.

### Not accepted

The framing that Option 1 is "shipping technical debt" and that the follow-up "will never be prioritised". That is a general organisational claim, not a property of this change; the owner has explicitly scheduled Option 2 as a follow-up. Recorded so the disagreement is visible rather than silently dropped.

---

## 10. Revised plan for #913

The review changes the **order** of work, not the option:

1. **Shared-state audit — first, and it is a gate.** Enumerate every reader/writer of `g_tel_*` and the transport across the task boundary. Stop and report if isolation is not clean.
2. Reference-counted WiFi demand, replacing the implicit "each trigger owns the lifecycle" model.
3. Explicit teardown command (or held mutex) so persistent/OTA cannot race.
4. `std::atomic` for cross-task counters subject to increment.
5. Measure stack high-water mark; size from measurement.
6. Define worker failure behaviour.
7. Confirm nothing on the mesh loop path takes a WiFi/LwIP lock.

---

## 11. Shared-state audit results (#913 gate) — 2026-08-20

The audit was made a gate by §9. It has fired.

### Finding A — the transport is used from `loopTask` on every iteration

`[verified: main.cpp:386, and wifi_telemetry_loop() +6/+75/+94/+115-132]`

```c
if (g_tel_transport) g_tel_transport->loop();     // MQTT keepalive, socket I/O
```

`wifi_telemetry_loop()` also calls `end()`, `isReady()` and `begin()` — **the cmd-poll trigger is embedded inside it**, not a separate movable function.

`->loop()` is PubSubClient keepalive over the shared `WiFiClient`. Neither is thread-safe. A worker calling `begin()`/`publish()`/`end()` concurrently is a data race on a live socket.

**A mutex is disqualified**, not merely undesirable: `loopTask` would block on it while the worker holds it through a 15 s connect — **recreating the exact defect**, with extra steps.

Therefore the only viable Option 1 shape is **worker owns the transport exclusively**, including keepalive. That is materially larger than "move the two trigger functions".

### Finding B — OTA depends on WiFi staying up, across THREE tasks

`[verified: main.cpp:418-447]` — OTA is served by AsyncElegantOTA, i.e. the **AsyncTCP task**. `loopTask` observes `Update.isRunning()` and extends `g_tel_persistent_until_ms` to hold WiFi up.

Under Option 1 the teardown decision moves to a **third** task. A worker that reads an expired deadline, is preempted while `loopTask` extends it, then resumes and tears down, **kills a firmware update in flight** — the highest-consequence operation on the device.

Under Option 2 check-and-act stay on one task and the race cannot exist.

### Scope comparison

| | **Option 1 (revised)** | **Option 2** |
|---|---|---|
| Code relocated / restructured | ~230 lines across 5 functions, **plus splitting a 156-line function by concern** | transport internals (186-line file) + 2 trigger sites |
| Lifecycle call sites touched | 7 | 7 |
| Interface contract change | none | `begin()`, **1 implementation** |
| New cross-task state | transport object, 5 counters, persistent deadline, refcounted demand | **none** |
| Synchronisation required | atomics + explicit teardown command + refcount | **none** |
| OTA | teardown decision crosses to a third task | **untouched** |
| Blocking | still exists, relocated | **eliminated** |

### Conclusion

**Option 1's original justification does not survive the audit.** It was chosen for smaller blast radius and no caller redesign. The audit shows it needs the transport moved wholesale, a 156-line function split, a cross-task OTA protocol, atomics and a refcount — while Option 2 needs none of those and leaves OTA alone.

§6 pre-registered this outcome: *"If the concurrency audit in §7 turns up shared state that cannot be cleanly isolated, Option 2 becomes correct and this recommendation should be revisited rather than forced."*

**Recommendation: switch to Option 2.** Returned to the owner rather than engineered around.
