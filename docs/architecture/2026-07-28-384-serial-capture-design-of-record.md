# Design-of-Record — On-demand full serial-monitor capture + download-to-app

**Epic:** [#384](https://github.com/OffbandMesh/meshcore-firmware/issues/384) ·
**Design task:** [#385](https://github.com/OffbandMesh/meshcore-firmware/issues/385) ·
**Status:** DRAFT — pending Gemini adversarial review + Ben sign-off ·
**Author:** TopazHill (session `ce5045af`) · **Date:** 2026-07-28

> SAFELANE PLAN phase. This document is design-of-record only — **no firmware code lands
> under #385.** Implementation + hardware-test child issues are created off this doc *after*
> sign-off. All code claims below are tagged `[verified: …]` (checked against `firmware-base`
> this session) or `[hypothesis: …]`.

---

## 1. Purpose (plain terms)

The user wants to turn on a capture, reproduce a problem on a connected device, and
**download the full serial-console log as a file** to share (email / Discord / WhatsApp).
Today the complete console stream is captured **nowhere the app can retrieve**, so there is
no download path. This epic adds two genuinely new pieces:

1. A **tee** that copies what the firmware writes to the serial console into a retrievable buffer.
2. A **download-to-app command** that streams that buffer to the Offband app, reassembled
   client-side into a shareable file.

Must work across **all roles** (companion, repeater, room-server, sensor) and **both MCU
families** (nRF52 + ESP32). Primary bench: **RAK4631 companion (nRF52)** + one repeater.

## 2. Why existing mechanisms are not this (do NOT reinvent)

| Mechanism | What it captures | Why it's not #384 |
|---|---|---|
| `/packet_log` (`log start/stop/erase/dump`) | RX/TX **packet-event lines only** | Not the console; gated by `MESH_PACKET_LOGGING`. `[verified: Dispatcher.cpp:219-236, 340-350; CommonCLI.cpp:688-699]` |
| `CrashLog` (#350) | ESP-IDF `ESP_LOG*` + breadcrumbs into a **4 KB retained ring** | ESP32-focused, tiny, **reboot-survival** oriented; does **not** capture `MESH_DEBUG_PRINTLN` / raw `Serial.print`. `[verified: CrashLog.h; MeshCore.h:25-32]` |
| `#379`/`#380` redacting serial-capture tool | Host-side **Python** capture+redact for an MQTT-observer diagnosis | Runs on the PC, not on-device; different goal. Naming collision only. `[verified: scripts/_cap_serial.py on branch task/379-serial-capture]` |

**Conclusion:** #384 is distinct from crash/boot-survival work but reuses its *plumbing*. The
new work is (a) the **tee** into a capture sink, and (b) the **download-to-app** path.

## 3. Verified groundwork (interception points + shared-line reality)

### 3.1 Where console output is produced

- `MESH_DEBUG_PRINTLN(F, …)` → `Serial.printf("DEBUG: " F "\n", …)`, compile-gated by
  `MESH_DEBUG && ARDUINO`. Shared by **all roles** via core `src/`. `[verified: MeshCore.h:25-32]`
- Raw packet lines: `Serial.print`/`printf` in `Dispatcher::checkRecv()` (RX) and
  `checkSend()` (TX), gated by `#if MESH_PACKET_LOGGING`. The custom-logging hook is
  **`logRx(pkt, len, score)`** at `Dispatcher.cpp:237` (the epic body called it `logRxRaw` —
  corrected here). `[verified: Dispatcher.cpp:219-237, 340-350]`

### 3.2 `MESH_DEBUG` is OFF everywhere by default

Every variant ships `MESH_DEBUG` / `MESH_PACKET_LOGGING` **commented out**; several carry an
explicit `; NOTE: DO NOT ENABLE`. `[verified: grep across all variants/*/platformio.ini —
every occurrence is a comment]` Implication: on a stock build the tee has little to capture
**unless** capture also implies enabling the debug macros, or we route boot/error lines
through the sink unconditionally. See §5.4.

### 3.3 The shared-line reality (per-env, mechanical)

The only corruption vector is writing raw human text **live** onto a line that also carries the
COBS/frame protocol. Whether Serial is free depends on the transport, which is compile-time:

| Companion transport | Compile signal | Serial (USB-CDC) is… | Live mirror |
|---|---|---|---|
| BLE (e.g. RAK4631 companion) | `defined(BLE_PIN_CODE)` → `SerialBLEInterface` | **free** (protocol on BLE) | allowed |
| Dedicated UART bridge | `defined(SERIAL_RX)` → `HardwareSerial(1)` | **free** (protocol on UART1) | allowed |
| USB-serial companion | neither defined → `ArduinoSerialInterface` on `Serial` | **carries the protocol** | **forbidden** |

`[verified: examples/companion_radio/main.cpp:85-100; variants/rak4631/platformio.ini:169
BLE_PIN_CODE=123456]`

**Mechanical rule:** live serial mirror is permitted **iff** `defined(BLE_PIN_CODE) ||
defined(SERIAL_RX)` (protocol is on a different channel). Otherwise capture-to-buffer +
**framed** download only — never raw text on the wire.

### 3.4 Download gating precedent

- Bare `log` → `dumpLogFile()` is gated to the **local console** (`sender_timestamp == 0`);
  same for `stats-*`. `[verified: CommonCLI.cpp:697-705]`
- But `safety log` / `safety state` / `safety partitions` are **already un-gated** and answer
  over the companion/authenticated channel. `[verified: CommonCLI.cpp:678-687]` → precedent
  that un-gating a diagnostic dump to the companion channel is established practice.
- `dumpLogFile()` on repeater/room-server reads a file and `Serial.print`s it char-by-char —
  safe there because Serial *is* the console. `[verified: simple_repeater/MyMesh.cpp:1050-1064]`
  On a companion this must instead be **framed** (§7).

## 4. Architecture

```
 ┌──────────────────────────────────────────────┐
 │  console producers (all roles, core src/)     │
 │   MESH_DEBUG_PRINTLN(...)   raw Serial.print  │
 │            boot lines / errors                │
 └───────────────┬──────────────────────────────┘
                 │  route through one sink
                 ▼
        ┌────────────────────┐        ┌───────────────────────────┐
        │  mesh_log_line()   │──────► │ live Serial (mirror)      │  ← only if BLE_PIN_CODE
        │  (the tee sink)    │        │                           │     or SERIAL_RX (§3.3)
        └─────────┬──────────┘        └───────────────────────────┘
                  │
                  ▼
        ┌────────────────────────────────────┐
        │  CaptureBuffer (plain RAM ring)     │  ← NEW, separate from CrashLog #350
        │  bounded, per-platform size, toggle │
        └─────────┬──────────────────────────┘
                  │  on `caplog dump` / CMD_GET_CAPLOG
                  ▼
        ┌────────────────────────────────────┐
        │  framed download (START/CHUNK*/END) │  → app reassembles → file → OS share
        └────────────────────────────────────┘
```

### 4.1 The tee sink — `mesh_log_line()`

Introduce **one** sink function in core `src/`. Route `MESH_DEBUG_PRINTLN`, the raw packet
`Serial.print` lines, and boot/error lines through it. The sink: (a) optionally mirrors to
live `Serial` per §3.3, and (b) appends to `CaptureBuffer` when capture is enabled. This is
the single new interception point the epic calls for. Keeping it in core `src/` means every
role is covered by construction.

**Hard implementation constraints (from Gemini review — see §12):**

- **ISR-safe guard, not just a mutex.** The sink may be reached from the main loop
  (`Dispatcher`), BLE callbacks, and timers — on nRF52 some at high interrupt priority. A plain
  mutex taken in an ISR while the loop holds it hard-faults/deadlocks. The buffer mutation must
  use an **ISR-safe critical section** (`taskENTER_CRITICAL[_FROM_ISR]` / `portDISABLE_INTERRUPTS`
  or the platform equivalent), kept to the shortest possible span. `[F-A1, HIGH]`
- **Stack-frugal hot path.** Do **not** `sprintf`/`vsnprintf` inside the guarded section. Format
  the line (timestamp prefix + text) into a small caller/stack scratch *before* the critical
  section, then `memcpy` the finished bytes into the ring. Long lines are truncated to a fixed
  cap (mirror CrashLog's ~240-char truncation). `[F-A2, MEDIUM]`
- **Timestamp every line.** Prepend `[<millis>] ` (as CrashLog does) so captured output has
  timing context — raw console order alone is not enough for diagnosis. `[F-A7]`
- **Robust console-sharing detection.** Prefer a `RadioInterface` trait
  `isConsoleSharedWithProtocol()` over the compile-macro proxy `BLE_PIN_CODE || SERIAL_RX`
  (§3.3). The macro test is a correct *first cut* but brittle — a future variant could put the
  protocol on `Serial` without either macro and silently re-introduce corruption. The trait
  makes the live-mirror decision authoritative at the transport layer. `[F-A3, MEDIUM]`

> Perf target ~5–15 µs/line (mirror CrashLog's guard cost). `[hypothesis: validate on bench]`
> A non-reentrant sink is a non-starter (SAFELANE §11 rules 8 + 10) — reentrancy is the
> acceptance bar, not the perf number.

### 4.2 The capture buffer — separate plain-RAM ring (Q1/Q6 decision)

**Decision (Ben-approved 2026-07-28):** the capture buffer is a **dedicated, plain-RAM,
live-only (non-retained) ring** — NOT `CrashLog`'s 4 KB retained buffer.

Rationale:
- **Sizing diverges hard.** CrashLog's ring is `OFFBAND_CRASHLOG_RING_BYTES` = **4 KB** (~50
  lines) for a crash breadcrumb. A serial *session* capture is much larger (the operator
  reproduces a multi-second/minute problem). `[verified: #350 branch contract, per RubyDog]`
- **Retention diverges hard.** Serial capture has a **present operator** — it does not need to
  survive a reset. Making it non-retained plain RAM:
  - sidesteps the **nRF52 retention failure discovered on hardware today** (#378): the
    `.noinit` SRAM ring does **not** survive a watchdog reset on nRF52 (bootloader clears SRAM
    pre-`SystemInit`). `[verified: RubyDog hardware test, RAK3401, 2026-07-28]`
  - avoids all contention with **SafeBoot's retained region** (`GPREGRET2` bit-7 owned +
    `SHUTDOWN_REASON_*`; RTC_NOINIT) — zero collision by construction (Q6). `[verified: #255/#257
    + RubyDog]`

**What we DO reuse from #350:** the *pattern* and *plumbing* — the cross-MCU sink discipline,
the critical-section guard model, and the tee **also** feeding `crashLogf()` (see cohesion
note below). We do **not** reuse the 4 KB retained allocation as the capture store.

**Crash-context cohesion (Gemini §4 counter-argument, mitigated).** The strongest case against
the split is that separating live capture from crash breadcrumbs fractures the "events leading
up to the crash" timeline. We keep the split (sizing + retention genuinely diverge; nRF52
retention is currently broken), but **mitigate** by having `mesh_log_line()` **also** call
`crashLogf()` for the retained ring. Result: the large live buffer serves the download use
case, and the 4 KB retained ring still holds the last-N lines before a reset — the crash
timeline is preserved without forcing the whole session capture into retained RAM. `[F-A4]`

**Static allocation (BSS), not dynamic.** The capture ring is a **statically-allocated** byte
array, not `malloc`/`new`. This resolves two review findings at once: (a) the **boot-time race**
— early boot/constructor lines can call the sink before `setup()` runs; a static buffer always
exists, so the sink never dereferences a null buffer `[F-A/boot, HIGH]`; (b) **nRF52 heap
fragmentation** — a multi-KB dynamic alloc on nRF52 (SoftDevice leaves <64 KB heap) is a
fragmentation/OOM risk; static removes it from the heap deterministically `[F-A5, HIGH]`. The
cost is that the RAM is permanently reserved — so sizes must be conservative and validated
against a per-role heap-headroom check before merge.

Buffer sizing (per-platform budget; final numbers set at implementation after a heap check):
- **ESP32-S3:** larger ring feasible (PSRAM/heap headroom) — `[hypothesis: 32–64 KB]`.
- **nRF52 (RAK4631):** tighter — `[hypothesis: 8–16 KB, conservative, static]`; **file spill**
  to the filesystem is a **separate follow-up child issue**, not a simple fallback — on-device
  FS adds flash-wear + perf + complexity, so the first cut is memory-only. `[verified: companion
  has FS — dumpLogFile reads _fs; F-A/spill]`

### 4.3 Per-role / per-platform matrix

| Role | ESP32 | nRF52 | Live mirror | Download channel |
|---|---|---|---|---|
| Companion (BLE) | ✓ | ✓ (bench) | allowed (Serial free) | framed app frames (§7) |
| Companion (USB-serial) | ✓ | ✓ | **forbidden** (protocol on Serial) | framed app frames only |
| Repeater | ✓ | ✓ | allowed (console = Serial) | local console dump + authenticated-CLI framed |
| Room-server | ✓ | — | allowed | as repeater |
| Sensor (#349) | ✓ | ✓ | field node, usually headless | framed / on-connect |

## 5. Resolved open questions (epic Q1–Q6)

1. **Reuse #350 store vs dedicated?** → **Dedicated** plain-RAM live ring; reuse #350's
   tee/guard *pattern* and optionally cross-feed `crashLogf`. Justified divergence from the
   issue's "default to reuse" — sizing + retention needs differ, and nRF52 retention is
   currently broken (§4.2). *Independently recommended by #350's owner (RubyDog).*
2. **`MESH_DEBUG` on companion + UART topology per env?** → `MESH_DEBUG`/`MESH_PACKET_LOGGING`
   are **off by default in all envs** (§3.2). Transport per env is compile-time detectable
   (§3.3). Capture must therefore route **boot + error + explicitly-enabled debug** lines
   through the sink; turning on capture may optionally imply enabling the debug macros for the
   session (see §5.4 open decision).
3. **New CLI verbs vs reuse `log …`?** → **New distinct verbs** to avoid overloading the
   packet-log semantics. Proposed: **`caplog start | stop | erase | dump | status`** (name is
   a proposal — Ben to confirm). Mirrors the `log start/stop/erase` shape for familiarity.
4. **CLI-text stream vs binary push-code?** → **Framed binary** (§7), reusing the companion's
   existing streamed-frame protocol (`RESP_CODE_CONTACTS_START` model). Cleaner client
   reassembly; avoids USB-CDC shared-line corruption. `[verified: companion frame protocol
   exists — MyMesh.cpp:111,162,204,1736]`
5. **Upstream PR vs fork-private?** → **Defer.** Build fork-private first (Offband policy is
   no-upstream-*merge*; this would be an outbound contribution decision). The tee at
   `MESH_DEBUG_PRINTLN` is a general improvement and a reasonable future outbound PR — decide
   after it's proven. Does not gate the design.
6. **Reconcile with #255/#257 SafeBoot retention.** → **No collision by construction** — the
   capture ring is non-retained plain RAM (§4.2). SafeBoot keeps sole ownership of the
   retained region.

### 5.4 Decisions (Ben, signed off 2026-07-28)

- **D1 — CLI verb name → `caplog`** (`caplog start/stop/erase/dump/status`). ✅ decided.
- **D2 — default verbosity → `debug`** (`caplog start` captures boot/error/debug; `caplog start
  error` narrows to errors-only). ✅ decided. Level names: `boot < error < debug < packet`.
- **D3 — nRF52 buffer size + file-spill** → deferred to implementation after a heap-headroom
  check (not blocking design). ✅ decided (defer).

*(Superseded proposal text retained below for provenance.)*

- **D1 (proposed) — CLI verb name.** `caplog …` proposed. OK, or prefer `serial-cap` / `console …` / other?
- **D2 — Runtime verbosity is now a CORE requirement (Gemini §7), not an open question.**
  Because `MESH_DEBUG`/`MESH_PACKET_LOGGING` ship OFF (§3.2), a pure "capture what's already
  emitted" design would show the user almost nothing — perceived as broken. So the sink gains a
  **runtime verbosity level/bitmask**: each routed line is tagged (boot / error / debug /
  packet) and the sink drops lines above the active level via a **cheap integer check in the hot
  path** (no string parsing). `caplog start [level]` sets it. The remaining question for Ben is
  only the **default level** and the exact level names — not whether to build it. `[F-A6,
  decided-core]`
- **D3 — nRF52 buffer size + file-spill** default (affects heap). Recommend deciding the number
  at implementation after a heap-headroom check, not now.

## 6. Enable / disable / erase / sizing

- **Toggle, default OFF.** User enables before reproducing. `caplog start` / `caplog stop`.
- **`caplog erase`** clears the ring; **`caplog status`** reports enabled + bytes used/size.
- **Bounded ring** (§4.2 sizes, static). On overflow, evict **as many oldest lines as needed**
  to fit the new line — a single line can be larger than the single oldest entry, so eviction
  loops until there is room (never blocks the writer, never partially-overwrites). `[F-A/evict,
  LOW]`
- **File spill** where a filesystem exists (companion) — **separate follow-up child issue**, not
  the first cut (§4.2).

## 7. Download contract

Reassembled client-side into a file → OS share sheet. Client reuses ScarletForest's
`LogExport.shareFile` (meshcore-open #393 / client #430). Three channel cases:

1. **Companion (BLE or USB-serial):** a new framed exchange modelled on the contact-list
   stream — `CMD_GET_CAPLOG` → `RESP_CODE_CAPLOG_START` → `…CHUNK` frames → `…END`, or reuse
   `PUSH_CODE_BINARY_RESPONSE (0x8C)` for the chunk payloads. Never raw `Serial.print` of
   captured text on a USB-serial companion. `[verified: frame codes exist — MyMesh.cpp:111-164,204]`
2. **Local console (`sender_timestamp == 0`):** `caplog dump` may `Serial.print` the buffer
   directly (console = Serial), mirroring `dumpLogFile()`.
3. **Authenticated remote CLI (repeater/room-server):** un-gate the dump to the
   authenticated channel, following the `safety log` precedent (§3.4) — framed, not raw.

**Active-write race (Gemini §3, HIGH).** New lines may arrive *during* a download. Reading
head/tail while a writer mutates them yields duplicated/truncated/corrupt output. Resolution:
on `caplog dump`/`CMD_GET_CAPLOG`, **auto-stop capture for the duration of the download**
(simplest correct behaviour — the operator has finished reproducing before they download), and
snapshot the valid byte range `[tail, head)` once under the guard, then stream from the frozen
range. If concurrent capture-during-download is later required, upgrade to a copied snapshot
buffer or a monotonic sequence-number chunk protocol — noted, not built in the first cut.
`[F-A/dl-race]`

**Flow control (Gemini §3, MEDIUM).** START/CHUNK/END needs pacing — a fast MCU can overrun the
app's BLE/serial receive buffers on a noisy link. First cut: bounded chunk size + a small
inter-chunk delay; if loss persists, a windowed ACK from the client. The client (#430) and this
contract agree the pacing scheme. `[F-A/dl-flow]`

Frame code numbers, chunk size, and CRC/END-marker are specified in the implementation child
issue, not here.

## 8. Security / redaction

- **Never capture secrets.** WiFi PSK / MQTT creds / OTA tokens must never reach the sink; the
  sink inherits the existing "log only derived properties" discipline (CLAUDE.md Security).
- The **download is user-initiated and shareable** — treat the buffer as
  potentially-public. Reuse the redaction intent from #379/#380 (host-side redaction of
  observer logs) as guidance for what must never be printed. `[verified: #379 redaction tests]`
- No LAN IPs / device identifiers echoed into captured lines beyond what the console already
  prints; audit the sink's inputs.
- **Mechanical guardrail, not just discipline (Gemini §6, MEDIUM).** A single
  `MESH_DEBUG_PRINTLN("PSK: %s", key)` slip would persist a secret into a user-shareable file.
  Provide a mechanism, not only a policy: (a) the sink strips/masks lines tagged sensitive, and
  (b) audit the existing console producers so no secret is printed at all. `[F-A/sec]`

### 8.1 UX limitation the app must surface (Gemini §7)

The capture ring is **live-only** — any reset (crash, watchdog, power-cycle) loses it (that is
the deliberate non-retention trade, §4.2). The Offband app's capture screen must state this
plainly ("capture is cleared if the device restarts") so the limitation reads as designed, not
a bug. Tracked on the client issue (#430).

## 9. Reconciliation & sequencing

- **NOT blocked-by #350** (corrected 2026-07-28). The capture ring is a **separate, non-retained
  plain-RAM buffer**, so serial-capture does *not* depend on #350's cross-role store nor its
  (currently-unsolved, #378) nRF52 retention. The only tie is a **soft/optional** `crashLogf`
  cross-feed that degrades gracefully. Implementation can — and does (#393) — proceed
  independently of #350; coordinate only on file overlap in core `src/` / `CommonCLI`. *(The
  earlier "blocked-by #350" framing over-coupled the two and was removed.)*
- **#255 / #257:** no retained-region collision (§4.2/Q6). Coordinate the optional
  `crashLogf` cross-feed with RubyDog before that child issue lands.
- **#325:** the companion download transport aligns with the Serial+WiFi coexistence work; the
  framed contract is transport-agnostic.

## 10. Proposed implementation breakdown (child issues — created AFTER sign-off)

> Each a single-PR task (CLAUDE-BASE sizing). Created + parent-linked to #384 on 2026-07-28.
> **Not blocked-by #350** — soft coordination only (see §9). #394 was folded into #393.

1. **[#393](https://github.com/OffbandMesh/meshcore-firmware/issues/393) — Core tee sink**
   `mesh_log_line()` in `src/`; route `MESH_DEBUG_PRINTLN` + raw packet prints + boot/error
   lines (ISR-safe, stack-frugal, timestamped, `isConsoleSharedWithProtocol()` mirror gate,
   `crashLogf` cross-feed).
2. **[#394](https://github.com/OffbandMesh/meshcore-firmware/issues/394) — CaptureBuffer**
   static plain-RAM ring + multi-line eviction + runtime verbosity level + toggle/erase/status.
3. **[#395](https://github.com/OffbandMesh/meshcore-firmware/issues/395) — CLI verbs**
   `caplog start[level]/stop/erase/dump/status` (default `debug`) + un-gate remote dump.
4. **[#396](https://github.com/OffbandMesh/meshcore-firmware/issues/396) — Companion download
   frames** `CMD_GET_CAPLOG` + START/CHUNK/END; auto-stop+snapshot + pacing; align with client #430.
5. **[#397](https://github.com/OffbandMesh/meshcore-firmware/issues/397) — File spill**
   (FS-bearing roles; after the memory path is proven).
6. **[#398](https://github.com/OffbandMesh/meshcore-firmware/issues/398) — Epic integration
   test** RAK4631 companion + one repeater: capture → download → verify; frame-corruption guard
   on a USB-serial companion build. (Final task; blocks epic close.)

## 11. Verification plan (for the eventual integration test)

- **Bench:** RAK4631 companion (nRF52) + one repeater. `[verified: HARDWARE.local.md bench]`
- Enable `caplog`, generate console traffic (boot, errors, `--verbose` if D2=b), download to
  the Offband app, open the shared file, confirm completeness + ordering.
- **Corruption guard:** on a **USB-serial companion** build (no `BLE_PIN_CODE`), confirm the
  protocol frames are intact during/after capture + download (the core risk in §3.3).
- Confirm capture OFF by default and that `stop`/`erase` behave.

## 12. Gemini adversarial review — findings & dispositions

Review log: [`docs/llm-consultations/2026-07-28-384-serial-capture-design-review-gemini-gemini-2.5-pro.log`](../llm-consultations/2026-07-28-384-serial-capture-design-review-gemini-gemini-2.5-pro.log).
Every finding is dispositioned (standards#145 — fix or justify):

| # | Sev | Finding | Disposition |
|---|---|---|---|
| A1 | HIGH | Sink not proven ISR-safe (deadlock risk) | **Accepted** — ISR-safe critical section mandated (§4.1) |
| boot | HIGH | Dynamic buffer + early boot lines → null deref | **Accepted** — static allocation (§4.2) |
| A5 | HIGH | 8–16 KB dynamic on nRF52 → fragmentation/OOM | **Accepted** — static BSS, conservative + heap-checked (§4.2) |
| dl-race | HIGH | Read-during-write corrupts download | **Accepted** — auto-stop + snapshot range on dump (§7) |
| A2 | MED | `sprintf` in sink → stack overflow | **Accepted** — format outside guard, `memcpy` in (§4.1) |
| A3 | MED | `BLE_PIN_CODE||SERIAL_RX` guard brittle | **Accepted** — `isConsoleSharedWithProtocol()` trait (§4.1) |
| dl-flow | MED | No flow control → BLE overrun | **Accepted** — chunk pacing / windowed ACK (§7) |
| sec | MED | Redaction is policy not mechanism | **Accepted** — sink masks + producer audit (§8) |
| spill | MED | File spill under-scoped | **Accepted** — explicit separate follow-up, memory-only first (§4.2) |
| A6 | — | Runtime verbosity is core, not optional | **Accepted** — promoted to core requirement (§5.4 D2) |
| A7 | — | No timestamps | **Accepted** — `[<millis>]` prefix (§4.1) |
| evict | LOW | Single-line eviction insufficient | **Accepted** — multi-line eviction loop (§6) |
| A4 | — | Store split fractures crash timeline | **Kept split, mitigated** — tee cross-feeds `crashLogf` so retained ring keeps pre-crash lines (§4.2); split is Ben-approved + #350-owner-endorsed + nRF52 retention broken |

No findings were dismissed. The one architectural push-back (A4, fold into #350) is answered by
keeping the split *and* cross-feeding the retained ring, giving the crash-context cohesion
Gemini wanted without forcing session capture into (currently-broken, size-constrained) nRF52
retained RAM.

---

## Sign-off

- [x] Gemini adversarial review (standards#145) — [log](../llm-consultations/2026-07-28-384-serial-capture-design-review-gemini-gemini-2.5-pro.log); 13 findings, all dispositioned (§12).
- [x] **Ben sign-off** — 2026-07-28: D1 `caplog`, D2 default `debug`, D3 defer (§5.4).
- [x] Implementation + integration-test child issues created (#393–#398), parent-linked to #384. **Not** blocked-by #350 (§9). #393 in progress.
