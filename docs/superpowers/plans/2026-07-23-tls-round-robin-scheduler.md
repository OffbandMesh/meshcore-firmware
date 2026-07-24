# Round-Robin TLS Broker Scheduler — Implementation Plan (#175)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the observer service more enabled TLS (`wss`) broker feeds than fit in heap at once, by time-sharing the TLS budget instead of hard-capping it.

**Architecture:** A single append-only ring log holds each published payload **once**; every broker keeps a ~4-byte read cursor into it (Kafka-offset pattern), so memory is O(ring), not O(brokers × ring). A dwell-timer scheduler rotates which TLS brokers are live, reusing the existing async reconcile worker for the blocking teardown/bring-up. Live count stays heap-derived, so a board with enough heap for all feeds degenerates to "all live, no rotation" through the same code path.

**Tech Stack:** C++17, ESP32 (esp-mqtt / esp-tls), PlatformIO `[env:native]` googletest for the ring log.

**Issue:** [#175](https://github.com/OffbandMesh/meshcore-firmware/issues/175) · **Epic:** #177 · **Depends on:** #171 (shipped — `OFFBAND_MAX_LIVE_TLS`, `tlsHeapBudgetOk()`, `HeldNoHeap`)

---

## Existing building blocks (verified in `src/helpers/wifi_observer`, 2026-07-23)

Confirmed present, so this plan **extends** rather than invents:

| Piece | Where | Use |
|---|---|---|
| Single fan-out point | `MqttBrokerPool::publishPacket()` (`MqttBrokerPool.cpp:141`) | The one place the ring intercepts |
| TLS live count + budget | `MqttBrokerPool::loop()` (`.cpp:391-413`), `tlsHeapBudgetOk()` (`MqttBroker.cpp:56`) | Derive `live` budget; already counts `Up`/`Connecting` TLS slots |
| "Deferred, waiting for budget" state | `BrokerState::HeldNoHeap` (`MqttBroker.h:31`) | Already the parked state; rotation promotes/demotes into it |
| Async blocking lifecycle worker | `worker_task_` / `reconcile_q_` / `reconcileSlot()` (`MqttBrokerPool.h:125-130`) | Rotation reuses this — never block loopTask (#53) |
| Per-slot reconcile guard | `reconciling_[]` (`MqttBrokerPool.h:120`) | Skip slots mid-rotation |
| TLS-vs-plaintext test | `isTlsTransport()` (`MqttBrokerPool.cpp:363`) | Plaintext brokers exempt from the budget |

---

## File Structure

- **Create** `src/helpers/wifi_observer/MqttRingLog.h` — pure, dependency-free append-only ring + per-reader cursors. **No Arduino/ESP includes**, so it builds in `[env:native]` and is unit-testable in isolation (same pattern as `src/helpers/BlockStore.h`). One responsibility: retain payloads and track per-reader positions.
- **Create** `test/test_mqtt_ring/test_mqtt_ring.cpp` — native googletest for the ring.
- **Modify** `src/helpers/wifi_observer/MqttBrokerPool.h/.cpp` — own the ring; `publishPacket` appends instead of fanning out; drain per broker; dwell-timer rotation.
- **Modify** `src/helpers/wifi_observer/MqttBroker.h` — expose `wentUpAtMs()` so the scheduler can pick the oldest live TLS slot.

---

## Decisions locked before coding

- `MQTT_RING_SLOTS = 16`, `MQTT_RING_MSG_MAX = 512` → **8 KB static**. Tunable via `-D`. Rationale: observed `/packets` JSON runs 200–400 B; 16 deep covers a rotation dwell at typical arrival rates.
- `MQTT_ROTATE_DWELL_MS = 60000` (60 s), tunable.
- Overflow = **oldest-unsent evicted**, per the issue. Lossless only while `(downtime × arrival-rate) ≤ ring depth`. A lapped reader is *reported*, never a crash.
- **Plaintext brokers bypass rotation entirely** (no mbedTLS context) but still read through the ring, so there is one publish path, not two.

---

## Task 1: `MqttRingLog` core (pure, native-unit-tested)

**Files:**
- Create: `src/helpers/wifi_observer/MqttRingLog.h`
- Test: `test/test_mqtt_ring/test_mqtt_ring.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <gtest/gtest.h>
#include <cstring>
#include "helpers/wifi_observer/MqttRingLog.h"

static void fill(uint8_t* b, size_t n, uint8_t v) { memset(b, v, n); }

TEST(MqttRingLog, AppendAndReadInOrder) {
    MqttRingLog ring;
    uint8_t a[8]; fill(a, sizeof(a), 0xA1);
    uint8_t b[8]; fill(b, sizeof(b), 0xB2);
    EXPECT_EQ(ring.append(a, sizeof(a)), 1u);   // seq starts at 1
    EXPECT_EQ(ring.append(b, sizeof(b)), 2u);
    EXPECT_EQ(ring.head(), 2u);

    uint8_t out[MQTT_RING_MSG_MAX]; size_t out_len = 0; uint32_t seq = 0;
    ASSERT_TRUE(ring.peek(0, out, sizeof(out), out_len, seq));
    EXPECT_EQ(seq, 1u);
    EXPECT_EQ(out_len, sizeof(a));
    EXPECT_EQ(memcmp(out, a, sizeof(a)), 0);
    ring.commit(0);
    ASSERT_TRUE(ring.peek(0, out, sizeof(out), out_len, seq));
    EXPECT_EQ(seq, 2u);
    ring.commit(0);
    EXPECT_FALSE(ring.peek(0, out, sizeof(out), out_len, seq));  // drained
}

TEST(MqttRingLog, ReadersAreIndependent) {
    MqttRingLog ring;
    uint8_t m[4]; fill(m, sizeof(m), 0x5A);
    ring.append(m, sizeof(m));
    ring.append(m, sizeof(m));
    uint8_t out[MQTT_RING_MSG_MAX]; size_t n = 0; uint32_t seq = 0;

    ASSERT_TRUE(ring.peek(0, out, sizeof(out), n, seq));
    ring.commit(0);                       // reader 0 consumed one
    EXPECT_EQ(ring.lag(0), 1u);
    EXPECT_EQ(ring.lag(1), 2u);           // reader 1 untouched
}

TEST(MqttRingLog, LapDetectedAndReaderResyncs) {
    MqttRingLog ring;
    uint8_t m[4]; fill(m, sizeof(m), 0x77);
    for (int i = 0; i < MQTT_RING_SLOTS + 3; i++) ring.append(m, sizeof(m));
    EXPECT_TRUE(ring.lapped(0));                       // reader 0 never read
    EXPECT_EQ(ring.lag(0), (uint32_t)MQTT_RING_SLOTS); // clamped to retained
    ring.resync(0);
    EXPECT_FALSE(ring.lapped(0));
    EXPECT_EQ(ring.lag(0), 0u);
}

TEST(MqttRingLog, OversizeRejected) {
    MqttRingLog ring;
    uint8_t big[MQTT_RING_MSG_MAX + 1] = {0};
    EXPECT_EQ(ring.append(big, sizeof(big)), 0u);  // 0 = rejected
    EXPECT_EQ(ring.head(), 0u);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_mqtt_ring`
Expected: FAIL — `helpers/wifi_observer/MqttRingLog.h: No such file or directory`

> Note: this host needs MinGW gcc on PATH first:
> `export PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH"`

- [ ] **Step 3: Write minimal implementation**

```cpp
#pragma once

#include <cstdint>
#include <cstring>

// Append-only ring of published MQTT payloads with per-reader cursors (#175).
//
// One copy of each message is retained; each broker ("reader") holds only a
// 4-byte cursor, so memory is O(ring), not O(brokers x ring). This is what lets
// a broker go offline during TLS rotation and resume where it left off.
//
// Deliberately dependency-free (no Arduino/ESP headers) so it builds in
// [env:native] and is unit-testable in isolation -- same pattern as BlockStore.h.

#ifndef MQTT_RING_SLOTS
  #define MQTT_RING_SLOTS 16
#endif
#ifndef MQTT_RING_MSG_MAX
  #define MQTT_RING_MSG_MAX 512
#endif
#ifndef MQTT_RING_MAX_READERS
  #define MQTT_RING_MAX_READERS 10
#endif

class MqttRingLog {
public:
    MqttRingLog() { memset(len_, 0, sizeof(len_)); memset(seq_, 0, sizeof(seq_));
                    for (int i = 0; i < MQTT_RING_MAX_READERS; i++) cursor_[i] = 0; }

    // Append a payload. Returns its sequence number (1-based), or 0 if rejected
    // (empty or larger than MQTT_RING_MSG_MAX). Overwrites the oldest slot when
    // full -- a reader that has not kept up loses the overrun (documented,
    // best-effort; see lapped()).
    uint32_t append(const uint8_t* payload, size_t len) {
        if (payload == nullptr || len == 0 || len > MQTT_RING_MSG_MAX) return 0;
        uint32_t s = ++head_;
        uint32_t idx = (s - 1) % MQTT_RING_SLOTS;
        memcpy(buf_[idx], payload, len);
        len_[idx] = static_cast<uint16_t>(len);
        seq_[idx] = s;
        return s;
    }

    uint32_t head() const { return head_; }

    // Oldest sequence still retained (1-based); 0 when empty.
    uint32_t tail() const {
        if (head_ == 0) return 0;
        return (head_ > MQTT_RING_SLOTS) ? (head_ - MQTT_RING_SLOTS + 1) : 1;
    }

    // Messages this reader has not yet consumed, clamped to what is retained.
    uint32_t lag(uint8_t reader) const {
        if (reader >= MQTT_RING_MAX_READERS) return 0;
        uint32_t c = cursor_[reader];
        if (c < tailMinus1()) c = tailMinus1();      // lapped: clamp
        return (head_ > c) ? (head_ - c) : 0;
    }

    // True when the writer overran this reader's cursor (data was lost).
    bool lapped(uint8_t reader) const {
        if (reader >= MQTT_RING_MAX_READERS) return false;
        return head_ > MQTT_RING_SLOTS && cursor_[reader] < tailMinus1();
    }

    // Copy this reader's next unread message out. Returns false when caught up.
    // Does NOT advance -- call commit() only after a successful publish, so a
    // failed publish is retried rather than dropped.
    bool peek(uint8_t reader, uint8_t* out, size_t out_cap,
              size_t& out_len, uint32_t& out_seq) const {
        if (reader >= MQTT_RING_MAX_READERS || out == nullptr) return false;
        uint32_t c = cursor_[reader];
        if (c < tailMinus1()) c = tailMinus1();      // lapped: jump to oldest kept
        if (c >= head_) return false;                // caught up
        uint32_t want = c + 1;
        uint32_t idx = (want - 1) % MQTT_RING_SLOTS;
        if (seq_[idx] != want) return false;         // slot recycled under us
        if (len_[idx] > out_cap) return false;
        memcpy(out, buf_[idx], len_[idx]);
        out_len = len_[idx];
        out_seq = want;
        return true;
    }

    // Advance past the message peek() just returned.
    void commit(uint8_t reader) {
        if (reader >= MQTT_RING_MAX_READERS) return;
        uint32_t c = cursor_[reader];
        if (c < tailMinus1()) c = tailMinus1();
        if (c < head_) cursor_[reader] = c + 1;
    }

    // Abandon the backlog and jump to the head (used when a reader is hopelessly
    // lapped, or a broker is (re)attached and should not replay stale traffic).
    void resync(uint8_t reader) {
        if (reader >= MQTT_RING_MAX_READERS) return;
        cursor_[reader] = head_;
    }

private:
    // Cursor value meaning "has consumed everything before the oldest retained".
    uint32_t tailMinus1() const {
        uint32_t t = tail();
        return (t == 0) ? 0 : (t - 1);
    }

    uint8_t  buf_[MQTT_RING_SLOTS][MQTT_RING_MSG_MAX];
    uint16_t len_[MQTT_RING_SLOTS];
    uint32_t seq_[MQTT_RING_SLOTS];
    uint32_t cursor_[MQTT_RING_MAX_READERS];
    uint32_t head_ = 0;
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_mqtt_ring`
Expected: PASS — 4 tests. (PlatformIO prints `0 test cases` in the summary for a custom-`main` suite; trust the per-suite `[PASSED]` line.)

- [ ] **Step 5: Commit**

```bash
git add src/helpers/wifi_observer/MqttRingLog.h test/test_mqtt_ring/test_mqtt_ring.cpp
git commit -m "feat(#175): MqttRingLog - append-only ring + per-reader cursors, native-tested"
```

---

## Task 2: Route `publishPacket` through the ring

**Files:**
- Modify: `src/helpers/wifi_observer/MqttBrokerPool.h` (add member + drain decl)
- Modify: `src/helpers/wifi_observer/MqttBrokerPool.cpp:141-164` (`publishPacket`)

- [ ] **Step 1: Add the ring member (MqttBrokerPool.h)**

Add the include beside the other wifi_observer headers:

```cpp
#include "MqttRingLog.h"
```

In the private section, beside `brokers_`:

```cpp
    // #175: one retained copy of each published payload; each broker reads at
    // its own cursor. Decouples "message published" from "broker currently live",
    // which is what makes TLS rotation lossless within the ring depth.
    MqttRingLog ring_;

    // Drain one broker's backlog to its transport. Returns messages published.
    uint8_t drainBroker(uint8_t slot);
```

- [ ] **Step 2: Replace the immediate fan-out (MqttBrokerPool.cpp)**

`publishPacket` currently loops brokers and publishes inline. Replace its body with an append:

```cpp
uint8_t MqttBrokerPool::publishPacket(const uint8_t* payload, size_t payload_len) {
    if (payload == nullptr || payload_len == 0) return 0;
    // #175: append ONCE. Brokers pull from their own cursor in drainBroker(),
    // so a broker that is down (rotated out, backoff, HeldNoHeap) resumes where
    // it left off instead of losing the traffic.
    if (ring_.append(payload, payload_len) == 0) return 0;  // oversize/rejected
    uint8_t drained = 0;
    for (uint8_t slot = 0; slot < OFFBAND_MAX_BROKERS; ++slot) {
        drained += drainBroker(slot) ? 1 : 0;
    }
    return drained;
}
```

- [ ] **Step 3: Implement `drainBroker` (MqttBrokerPool.cpp, beside publishPacket)**

This carries over the per-broker topic formatting that used to live inline:

```cpp
// #175: publish this broker's unread backlog. Bounded per call so one lagging
// broker cannot monopolise a loop pass. Only commits the cursor on a SUCCESSFUL
// publish, so a refused enqueue is retried next pass rather than dropped.
uint8_t MqttBrokerPool::drainBroker(uint8_t slot) {
    if (slot >= OFFBAND_MAX_BROKERS) return 0;
    if (reconciling_[slot]) return 0;              // #53: mid-reconcile
    MqttBroker& b = brokers_[slot];
    if (!b.isConfigured() || b.runtime().state != BrokerState::Up) return 0;

    MqttPayloadCtx ctx;
    b.fillPayloadCtx(ctx, global_iata_, device_id_, node_name_,
                     client_version_, firmware_version_, model_);
    if (ctx.iata == nullptr || ctx.iata[0] == '\0') return 0;  // HARD RULE: no garbage topics
    char topic[160];
    formatTopic(topic, sizeof(topic), "packets", ctx);
    if (topic[0] == '\0') return 0;

    uint8_t sent = 0;
    uint8_t buf[MQTT_RING_MSG_MAX];
    size_t  len = 0;
    uint32_t seq = 0;
    // Bound the burst: at most MQTT_RING_SLOTS messages per pass.
    for (uint8_t i = 0; i < MQTT_RING_SLOTS; ++i) {
        if (!ring_.peek(slot, buf, sizeof(buf), len, seq)) break;
        if (!b.publish(topic, buf, len, /*retain=*/false)) break;  // retry next pass
        ring_.commit(slot);
        sent++;
    }
    return sent;
}
```

- [ ] **Step 4: Drain every loop pass, not only on publish (MqttBrokerPool.cpp, in `loop()`)**

Inside the existing per-slot loop in `MqttBrokerPool::loop()`, right after `b.loop(now);`, add:

```cpp
            // #175: a broker that just came Up (or was rotated back in) drains
            // its backlog here, not only when a new packet arrives.
            if (b.runtime().state == BrokerState::Up) (void)drainBroker(s);
```

- [ ] **Step 5: Build-verify**

Run: `pio run -e heltec_v4_companion_observer_wifi`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/helpers/wifi_observer/MqttBrokerPool.h src/helpers/wifi_observer/MqttBrokerPool.cpp
git commit -m "feat(#175): route publishPacket through the ring log with per-broker drain"
```

---

## Task 3: Dwell-timer rotation

**Files:**
- Modify: `src/helpers/wifi_observer/MqttBroker.h` (expose when the broker came Up)
- Modify: `src/helpers/wifi_observer/MqttBrokerPool.h/.cpp` (rotation state + scheduler)

- [ ] **Step 1: Expose the up-timestamp (MqttBroker.h)**

`BrokerRuntimeState` already carries `last_publish_ms`. Add beside it:

```cpp
    uint32_t         went_up_ms        = 0;  // #175: set when state -> Up; picks the oldest live TLS slot
```

Set it where the broker transitions to `Up` in `MqttBroker.cpp` (the same place `rt_.state = BrokerState::Up;` is assigned):

```cpp
    rt_.went_up_ms = now_ms;
```

- [ ] **Step 2: Add rotation state (MqttBrokerPool.h private section)**

```cpp
    // #175: TLS rotation. When more TLS brokers are enabled than the heap-derived
    // budget allows, the live set is rotated on a dwell timer so every feed gets
    // serviced. Degenerates to no-op when budget >= enabled TLS count.
    uint32_t last_rotate_ms_ = 0;
    void rotateTlsIfDue(uint32_t now_ms, uint8_t tls_live, uint8_t tls_enabled);
```

Add the tunable near the other constants at the top of `MqttBrokerPool.cpp`:

```cpp
#ifndef MQTT_ROTATE_DWELL_MS
  #define MQTT_ROTATE_DWELL_MS 60000U   // #175: how long a TLS slot holds the budget
#endif
```

- [ ] **Step 3: Implement the scheduler (MqttBrokerPool.cpp)**

```cpp
// #175: rotate the live TLS set on a dwell timer. Demotes the OLDEST live TLS
// broker so a parked (HeldNoHeap) one can take the freed budget slot on the next
// tick -- the existing loop() re-drive already promotes held slots. Teardown goes
// through the async reconcile worker so loopTask never blocks (#53), and that
// path performs a full esp_mqtt_client_destroy (skipping it leaks ~68 KB, #327).
void MqttBrokerPool::rotateTlsIfDue(uint32_t now_ms, uint8_t tls_live,
                                    uint8_t tls_enabled) {
    // Nothing to share: every enabled TLS feed already fits in the budget.
    if (tls_enabled <= OFFBAND_MAX_LIVE_TLS) return;
    if (tls_live == 0) return;
    if (now_ms - last_rotate_ms_ < MQTT_ROTATE_DWELL_MS) return;

    // Pick the oldest live TLS slot (smallest went_up_ms).
    uint8_t  victim = 0xFF;
    uint32_t oldest = 0xFFFFFFFFU;
    for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
        if (reconciling_[s]) continue;
        MqttBroker& b = brokers_[s];
        if (!b.isConfigured() || !isTlsTransport(b.config())) continue;
        if (b.runtime().state != BrokerState::Up) continue;
        if (b.runtime().went_up_ms <= oldest) { oldest = b.runtime().went_up_ms; victim = s; }
    }
    if (victim == 0xFF) return;

    // Only rotate if someone is actually waiting for the budget.
    bool waiting = false;
    for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
        MqttBroker& b = brokers_[s];
        if (!b.isConfigured() || !isTlsTransport(b.config())) continue;
        if (b.runtime().state == BrokerState::HeldNoHeap) { waiting = true; break; }
    }
    if (!waiting) return;

    last_rotate_ms_ = now_ms;
    // Async teardown; the slot's cursor is retained, so it resumes on its next window.
    (void)reloadSlot(victim);
}
```

- [ ] **Step 4: Call it from `loop()` (MqttBrokerPool.cpp)**

The loop already computes `tls_live`. Add an enabled-TLS count beside it and call the scheduler after the per-slot loop:

```cpp
        uint8_t tls_enabled = 0;
        for (uint8_t s = 0; s < OFFBAND_MAX_BROKERS; ++s) {
            const MqttBroker& b = brokers_[s];
            if (b.isConfigured() && isTlsTransport(b.config())) tls_enabled++;
        }
        rotateTlsIfDue(now, tls_live, tls_enabled);
```

- [ ] **Step 5: Build-verify**

Run: `pio run -e heltec_v4_companion_observer_wifi -e Heltec_v3_companion_observer_wifi`
Expected: both SUCCESS. (HV3 is the heap-constrained target this feature exists for.)

- [ ] **Step 6: Commit**

```bash
git add src/helpers/wifi_observer/MqttBroker.h src/helpers/wifi_observer/MqttBroker.cpp \
        src/helpers/wifi_observer/MqttBrokerPool.h src/helpers/wifi_observer/MqttBrokerPool.cpp
git commit -m "feat(#175): dwell-timer TLS rotation via the async reconcile worker"
```

---

## Task 4: Observability — surface lag + rotation in the broker dump

**Files:**
- Modify: `src/helpers/wifi_observer/MqttBrokerPool.cpp` (expose lag), `ObserverCli` broker render

- [ ] **Step 1: Expose per-slot lag (MqttBrokerPool.h public section)**

```cpp
    // #175: how many published messages this broker has not yet sent (0 = caught
    // up). Non-zero on a rotated-out or backlogged feed; a persistently large
    // value means that broker is misbehaving -- see the issue's overflow note.
    uint32_t brokerLag(uint8_t slot) const { return ring_.lag(slot); }
    bool     brokerLapped(uint8_t slot) const { return ring_.lapped(slot); }
```

- [ ] **Step 2: Render it in the existing broker dump**

`configRenderBrokerSlot` already renders per-broker runtime state (`#172`). Add two lines to its output, following the existing `key=value` line format used there:

```
mqtt.broker.<N>.lag=<n>
mqtt.broker.<N>.lapped=<yes|no>
```

> Keep the line budget in mind: the `_sys` outgoing ring drops oldest when the reply exceeds ~8 lines (root cause of the earlier `mqtt view` truncation). Two extra lines is within budget for a single-slot render; re-check `mqtt view` output after this change.

- [ ] **Step 3: Build-verify**

Run: `pio run -e heltec_v4_companion_observer_wifi`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/helpers/wifi_observer/
git commit -m "feat(#175): surface per-broker ring lag + lap flag in the broker dump"
```

---

## Task 5: Full verification gate

**Files:** none (verification only)

- [ ] **Step 1: Native tests**

Run: `export PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH" && pio test -e native`
Expected: PASS — `test_mqtt_ring`, `test_block`, `test_utils`.

- [ ] **Step 2: Representative device builds**

Run: `pio run -e heltec_v4_companion_observer_wifi -e Heltec_v3_companion_observer_wifi -e Xiao_S3_WIO_companion_observer_wifi`
Expected: all SUCCESS.

- [ ] **Step 3: Confirm no non-observer role changed**

Run: `git diff origin/firmware-base --stat -- src examples`
Expected: changes confined to `src/helpers/wifi_observer/` (+ the new test). The companion/repeater roles must be untouched.

- [ ] **Step 4: Gemini adversarial review (REQUIRED before PR)**

Run `scripts/llm-consult.py --backend gemini --model gemini-2.5-pro` against the consolidated diff. Focus questions: cursor arithmetic on `head_` wraparound; the lapped-reader clamp; rotation starving a feed that never reaches `Up`; whether `reloadSlot` on the victim can race a `drainBroker` on the same slot (should be covered by `reconciling_[]`).

---

## Hardware verification (owner-gated, before merge)

Per the issue: **continuous rotation must run without OOM/leak on HV3** — this stresses the exact teardown path that crashed in #171.

1. Configure **3+ enabled `wss` brokers** on an HV3 observer (budget is ~2).
2. Run for **≥ 30 rotation cycles** (~30 min at the 60 s dwell).
3. Watch: free heap must not trend downward across cycles (the #327 `esp_mqtt_client_destroy` leak signature), no reboots.
4. Confirm every configured feed receives traffic over the run (check each broker's ingest, not just the device).
5. Record heap-over-time + a rotation log on the issue.

---

## Deferred to a separate issue: TLS session resumption

The issue lists session resumption as the key optimization — park the ~1 KB session ticket instead of the 60 KB context so a re-admitted broker gets an abbreviated handshake.

**Deliberately not in this plan.** Its own text flags the feasibility as *unverified*: it requires `esp_tls_cfg.client_session` / `esp_tls_get_client_session` to be plumbed through `esp-mqtt`, "else manage TLS a layer down" — which would be a much larger change. Rotation is correct and useful without it (brokers simply pay a full handshake, "no worse than today").

**Recommended:** land Tasks 1–5, then open a follow-up issue under #177 that starts with a spike answering *"does esp-mqtt expose the client-session hook?"* before committing to an approach.

---

## Self-Review

**Spec coverage:** ring log + per-broker cursors → Task 1–2. Heap-derived live budget → reuses #171's `tls_live`/`OFFBAND_MAX_LIVE_TLS` (Task 3), degenerating to no-op when `tls_enabled <= budget` as the spec requires. Dwell rotation via the existing async reconcile worker with full destroy → Task 3. Plaintext exempt from the budget → inherited from `isTlsTransport` (Task 3 skips non-TLS). Overflow = oldest-unsent evicted, best-effort, reported → Task 1 (`lapped`) + Task 4 (surfaced). Hardware stress test → the hardware section. Session resumption → explicitly deferred with rationale.

**Placeholder scan:** none — every code step carries full code; the one prose step (Task 4 Step 2) specifies the exact output lines and the constraint to respect.

**Type consistency:** `append/head/tail/lag/lapped/peek/commit/resync`, `MQTT_RING_SLOTS`, `MQTT_RING_MSG_MAX`, `MQTT_RING_MAX_READERS`, `drainBroker`, `rotateTlsIfDue`, `went_up_ms`, `brokerLag`, `brokerLapped` are used identically across Tasks 1–5.

**Known gap:** `MQTT_RING_MAX_READERS` must be `>= OFFBAND_MAX_BROKERS`; Task 1's default (10) matches today's pool size. If `OFFBAND_MAX_BROKERS` ever grows, the ring must grow with it — worth a `static_assert` in Task 2.
