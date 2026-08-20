# WiFi telemetry lifecycle — design record

**Issue:** #912 (design) under epic #911. Defect record: #910. Answers #893.
**Date:** 2026-08-20
**Status:** awaiting human AGREE. **No implementation until agreed.**

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
