# Companion-Observer CoreScope `/neighbors` — Feature Plan of Record

> **For agentic workers:** REQUIRED SUB-SKILL: use `superpowers:executing-plans` (native) to implement task-by-task. Steps use `- [ ]` checkboxes. This plan is the argument for the grant minted from the `grant-terms` block below.

| | |
|---|---|
| **Initiative** | [#1384](https://github.com/OffbandMesh/meshcore-firmware/issues/1384) — Regional operation |
| **Feature** | [#1385](https://github.com/OffbandMesh/meshcore-firmware/issues/1385) — observer neighbor + region-scope reporting |
| **Diagnosis Epic** | [#1387](https://github.com/OffbandMesh/meshcore-firmware/issues/1387) — DIAGNOSE (done; outside the chain) |
| **Plan Epic** | this plan (outside the chain) |
| **Build chain** | [#1386](https://github.com/OffbandMesh/meshcore-firmware/issues/1386) table → [#1398](https://github.com/OffbandMesh/meshcore-firmware/issues/1398) client → [#1399](https://github.com/OffbandMesh/meshcore-firmware/issues/1399) publish (stacked PRs) |
| **Spec** | DIAGNOSE #1387 (per-role evidence map) + this file |

**Goal:** Give the Offband **companion** observer the CoreScope `/neighbors` capability — publish its own scope plus each directly-heard repeater's flood regions (fetched via the OTA anon-regions query), CLI-controlled.

**Architecture:** All new code on the **companion** (`companion_radio`) and its observer subsystem (`wifi_observer`). Three layers: (1) a zero-hop **neighbour table** fed by adverts the observer already hears; (2) an **anon-regions scope-query client** that walks the table — one query in flight, 20 s timeout — recording `responded`/`timeout`/`send_failed`; (3) a CoreScope-format **`/neighbors` publisher** + scheduler + CLI. The repeaters queried already answer (stock MeshCore `handleAnonRegionsReq`) — **no responder work.**

**Tech Stack:** C++ (Arduino/ESP32-S3/C6), MeshCore 1.17.0 base, PlatformIO, native unit tests (`test/`). Reuses the base anon-req transport (`createAnonDatagram`/`onAnonDataRecv`/`PAYLOAD_TYPE_ANON_REQ`/`ANON_REQ_TYPE_REGIONS 0x01`), `RegionMap`, `MqttBrokerPool`/`MqttPayload`.

## Grant terms (owner mints from this block; one block only)

```grant-terms
{
  "epic_chain": [1386, 1398, 1399],
  "chain": true,
  "verification": { "mode": "owner" },
  "merge": { "budget": 3, "basis": "one merge per build Epic: #1386 neighbour table, #1398 scope-query client, #1399 publish" },
  "expires_after_hours": 168
}
```

> **Structure (owner-locked 2026-10-07):** three build Epics as the build chain describes — [#1386](https://github.com/OffbandMesh/meshcore-firmware/issues/1386) (neighbour table) → [#1398](https://github.com/OffbandMesh/meshcore-firmware/issues/1398) (scope-query client) → [#1399](https://github.com/OffbandMesh/meshcore-firmware/issues/1399) (publish). **Chain mechanics:** work the chain **non-stop**; each Epic ends with a Gemini-reviewed push + PR (stacked on the previous Epic's branch); **all merges are held to the end**. At the end of the chain I hand you the whole thing for review → you agree → you mint a merge token per Epic → I merge all three in order → **then** the hardware/testing step. No mid-chain merge, no per-PR approval radio. Hardware flash is **not** in this grant — separate per-flash token at the bench step (your explicit go). `expires_after_hours: 168` covers non-stop build + your end-of-chain review without a mid-flight re-mint; lower it at mint if you prefer.

## Global Constraints
- Companion-only (`OFFBAND_OBSERVER` in `companion_radio`; all 6 `*_companion_observer_wifi` envs). No repeater/room-server changes.
- **CLI-first, no client code to start.** New CLI commands reach the client via the existing companion-API CLI passthrough. GUI is a later, separate effort.
- **Match the CoreScope contract exactly** (`CoreScope-OKI cmd/ingestor/main.go handleNeighborsReport`): `self:{scopes,default_scope}` + `neighbors:[{pubkey UPPER hex, snr, rssi, heard_secs_ago, scopes, status}]` + `total_neighbors`/`queried_neighbors`/`truncated`; `status ∈ {responded,timeout,send_failed}`; a neighbour's scopes apply **only** on `status=="responded"`.
- Reuse the existing `ANON_REQ_TYPE_REGIONS (0x01)` — **no new enum/cap allocation.**
- Non-blocking on the companion: the query path must not stall `loop()`/BLE servicing (#149) — one request in flight, timeout-driven.
- Keep MeshCore nomenclature for clean cherry-picking (#197). US English. Diag builds until beta. Every epic ends with its TEST task.
- **Gemini 2.5 review runs on every code task BEFORE anything is handed to the owner** — never hand first and review after. Findings are resolved inside the chain; what the owner reviews at the end is already Gemini-clean.
- **Clean-room:** no code copied from `agessaman/MeshCore` (a competitor). The scope-query client is written fresh against **our own repeater's** anon-regions request/response (base MeshCore in our tree). Gessaman is prior-art confirmation only.

## Control Surface (CLI — what the client drives)
| Command | Effect |
|---|---|
| `set mqtt.neighbors on\|off` | enable/disable periodic publishing (persisted, live) |
| `set mqtt.neighbors.interval <hours>` | 12–336, default 24 (persisted) |
| `get mqtt.neighbors` / `get mqtt.neighbors.interval` | read current settings |
| `get mqtt.status` | show `nbr: <next>/<last>` schedule when on |
| `discover.neighbors` | refresh the zero-hop neighbour table now |
| `discover.scopes` | one-shot: query the table + publish `/neighbors` now (test/on-demand) |

## Review Focus (failure modes pinned by tests)
1. A neighbour that never replies → `status:"timeout"`, never dropped, never hangs the cycle. → **T4**
2. Payload would exceed 10 KB → truncate by sort order (usable-age → recency → SNR), set `truncated:true`, never overflow the buffer. → **T6**
3. Scope-query must not disrupt protocol/BLE traffic — one request in flight, non-blocking, bounded timeout. → **T4/T8**
4. `heard_secs_ago` when the clock is unsynced → `null`, never a bogus zero/garbage age. → **T6**
5. Querying a node that is also a contact/client must not swallow that client's traffic (peer-table match order). → **T3**

## File Map
- `examples/companion_radio/MyMesh.{h,cpp}` — `NeighbourInfo[]` (+rssi), `onAdvertRecv` population, scope-query client (`sendRegionsReq`, `handleRegionsResponse`, discover orchestration), `onAnonDataRecv` response hook, `discover.*` CLI.
- `src/helpers/wifi_observer/MqttPayload.{h,cpp}` — `buildNeighborsJson` (CoreScope format).
- `src/helpers/wifi_observer/MqttBrokerPool.{h,cpp}` — `/neighbors` leaf publish + `publishNeighborsIfDue` scheduler.
- `src/helpers/wifi_observer/ObserverNeighbors.{h,cpp}` (new) — refresh→query→publish cycle + per-neighbour status table.
- `src/helpers/wifi_observer/ConfigSchema` + observer CLI — `mqtt.neighbors[.interval]` config + `set/get`.
- `test/test_observer_neighbors/` (new) — table, query state machine, golden-JSON.

## Build chain (tasks = commits under the Epic(s))

### Epic #1386 — Zero-hop neighbour table on the companion  *(build 1/3)*
- **T1. Neighbour table + advert population.** Port the repeater's `NeighbourInfo{Identity id; uint32 advert_ts; uint32 heard_ts; int8 snr(×4)}` + add `int8 rssi`. Populate in the companion's `onAdvertRecv` (match id, else evict oldest). Accessors `getNeighbourCount()`/`getNeighbours(out,max)`. Gate on `MAX_NEIGHBORS`. Native tests: populate-new; match-existing updates ts/snr/rssi; evict-oldest when full; count ignores empty slots. Gemini review.

### Epic #1398 — Anon-regions scope-query client  *(build 2/3; stacked on #1386)*
- **T2. `sendRegionsReq(target)`** — direct `ANON_REQ_TYPE_REGIONS` anon datagram via `createAnonDatagram`; record the in-flight peer. Test: emitted type/sub-type/target.
- **T3. `handleRegionsResponse` + `onAnonDataRecv` hook** — on `PAYLOAD_TYPE_RESPONSE` from the in-flight peer (matched after ACL clients), parse the region-name CSV. Test: parse names; ignore non-matching peers; a node that's also a client isn't swallowed (Review #5).
- **T4. Discover orchestration** — iterate the table, one query in flight, `REGION_QUERY_TIMEOUT=20 s`; per-neighbour `responded`/`timeout`/`send_failed`; results table. Test: state machine across all three + advance-to-next + no-hang on all-timeout (Review #1/#3).
- **T5. `discover.neighbors` / `discover.scopes` CLI.** Test: dispatch + replies. Epic TEST = the T4 state-machine suite. Gemini review per task.

### Epic #1399 — CoreScope `/neighbors` publish  *(build 3/3; stacked on #1398)*
- **T6. `buildNeighborsJson`** — CoreScope format; sort usable-age→recency→SNR; 10 KB cap. Golden-JSON test fed through `handleNeighborsReport` semantics. Reviews #2/#4 pinned here.
- **T7. `/neighbors` leaf + fan-out** in `MqttBrokerPool` (per-broker topic, QoS 0, retain where allowed). Test.
- **T8. Scheduler + cycle** — `publishNeighborsIfDue`; refresh→query→publish wired into the observer loop, non-blocking. Test: due-timing; cycle ordering.
- **T9. `set/get mqtt.neighbors[.interval]` config + persistence + `get mqtt.status` line.** Test: round-trip + bounds (12–336). Epic TEST = the golden-JSON contract suite.

### Testing — Integration / hardware  *(after all merges; owner-verified; separate flash token)*
- **T10.** Flash a companion-observer bench unit (**your explicit flash go + a minted flash token**); `discover.scopes`; confirm `/neighbors` publishes and our CoreScope ingests (observer `nodes.configured_scope` populated for self + a responded neighbour). End-to-end on a test broker, then the live map. Record results on #1399. This runs **after** the three Epics are merged, per your sequence (review → agree → merge all → test).

## Decisions (locked by owner, 2026-10-07)
1. **Companion `self.scopes` — (A):** report `self:{scopes:default_scope_name, default_scope}`. Harmless if empty.
2. **Table size:** cap **20** + set `truncated` on non-PSRAM boards (fleet default); **50** where PSRAM exists (Xiao-S3/RC32-class). Confirmed per-board in T1.
3. **Clean-room — (B):** fresh client against **our own repeater's** `handleAnonRegionsReq` format. No Gessaman code copied.

## Execution order (how "setting me off" works)
1. ✅ Build Epics created (cold-start): #1386 (existing), #1398, #1399.
2. I commit this file to `docs/initiatives/regional-operation/observer-neighbors/PLAN.md` on its **own branch, nothing else** (#707), open its PR.
3. **You merge the plan PR** to `firmware-base` = approval. **You mint** the plan grant (`dw-approve plan`, human-only) from the merged plan commit.
4. I run the chain **non-stop**: for each Epic #1386 → #1398 → #1399 → claim task + stamp board Agent field + link sub-issue → worktree (stacked branch) → T-tasks test-first → **Gemini review + resolve** → commit → push + PR (**merge held**). No stop between Epics, no per-PR approval radio.
5. At the end of the chain I hand you all three PRs for review. **You agree → you mint a merge token per Epic → I merge all three in order → then T10 testing** (your flash go + flash token).

## Notes
- Exact per-step C++ is locked at execution (it changes the code); the structure above is what the grant authorizes.
- Edit task #1395 was created prematurely (before this plan) — it will be folded into #1386's task(s), not worked standalone.
