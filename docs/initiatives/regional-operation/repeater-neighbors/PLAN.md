# Repeater CoreScope `/neighbors` — Epic Plan of Record

> **For agentic workers:** REQUIRED SUB-SKILL: use `superpowers:executing-plans` (native) to implement task-by-task. Steps use `- [ ]` checkboxes. This plan is the argument for the grant minted from the `grant-terms` block below. Decisions owner-approved 2026-10-29; the plan PR merging is the approval of record.

| | |
|---|---|
| **Initiative** | [#1384](https://github.com/OffbandMesh/meshcore-firmware/issues/1384) — Regional operation |
| **Feature** | [#1385](https://github.com/OffbandMesh/meshcore-firmware/issues/1385) — observer neighbor + region-scope reporting |
| **Sibling plan** | [`observer-neighbors/PLAN.md`](../observer-neighbors/PLAN.md) — the companion observer (Epics #1386 → #1398 → #1399, merged) |
| **Plan task** | [#1904](https://github.com/OffbandMesh/meshcore-firmware/issues/1904) — this plan (outside the chain) |
| **Build chain** | [#1902](https://github.com/OffbandMesh/meshcore-firmware/issues/1902) self report → [#1903](https://github.com/OffbandMesh/meshcore-firmware/issues/1903) neighbor queries (stacked PRs) |
| **Spec** | this file + the companion plan's CoreScope contract |

**Goal:** The repeater with WiFi telemetry (`heltec_v4_repeater_telemetry`) publishes CoreScope's `/neighbors` report through the brokers it already feeds: its own configured scopes first, then the scopes of the repeaters it hears directly.

**Why the repeater:** it is always on and always connected, and it has a node record in CoreScope (it adverts), so its own scopes land on that record directly. The companion's `self` scopes land nowhere: HV3's key returns 404 from CoreScope's `/api/nodes/` (T10, #1399).

**Architecture:** Reuse, not a port. The companion work left most of the stack in Arduino-free, host-tested headers under `src/helpers/wifi_observer/`:
- `NeighborsReport.h` — report builder, 10 KB cap, `NeighborsFanOut` (one queued copy at a time, PR #1901), heap gate.
- `NeighborsSchedule.h` — interval, retries, manual walk.
- `ObserverNeighbors.h` — `buildRegionsRequest` / `parseRegionsResponse`, `InFlightRegionsQuery`, `ScopeQueryRun<N>` (the walk).
- `MqttBrokerPool` — `publishNeighbors` + fan-out tick, today compiled only under `OFFBAND_OBSERVER`.

The repeater already has what the companion had to build: a zero-hop neighbor table (`NeighbourInfo neighbours[MAX_NEIGHBOURS]`, 50 on the V4), `discover.neighbors`, the region map, and a broker pool on the telemetry env (`RepeaterMqttPool`, `OFFBAND_MQTT_POOL`) that already publishes `/packets` to CoreScope.

**Tech Stack:** C++ (Arduino/ESP32-S3), MeshCore 1.17.0 base, PlatformIO, native unit tests (`test/`). Heltec V4 has 2 MB PSRAM, so the report gets the 10 KB cap with its build buffer in PSRAM.

## Grant terms (owner mints from this block; one block only)

```grant-terms
{
  "epic_chain": [1902, 1903],
  "chain": true,
  "verification": { "mode": "owner" },
  "merge": { "budget": 2, "basis": "one merge per build Epic: #1902 self report, #1903 neighbor queries" },
  "expires_after_hours": 168
}
```

> **Structure (owner-approved 2026-10-29):** two build Epics, #1902 → #1903, worked non-stop as one chain; each ends with a reviewed push + PR stacked on the previous; **all merges held to the end**. Hardware flash is **not** in this grant: separate per-flash token at the bench step.

## Global Constraints
- Repeater telemetry env only (`heltec_v4_repeater_telemetry`). The companion observer's behavior does not change; its tests stay green unchanged.
- **Match the CoreScope contract exactly**, as the companion does (`CoreScope-OKI cmd/ingestor/main.go handleNeighborsReport`). CoreScope routes any `meshcore/<region>/<id>/neighbors` topic and needs no observer registration.
- **Self scopes = what the repeater answers others.** `self.scopes` comes from `region_map.exportNamesTo(..., REGION_DENY_FLOOD)`, the same call `handleAnonRegionsReq` uses, so the repeater can never report itself differently from how a neighbor sees it. `default_scope` = `region_map.getDefaultRegion()->name`, or empty when there is none.
- Reuse `ANON_REQ_TYPE_REGIONS (0x01)` — **no new enum/cap allocation.**
- **Non-blocking.** The ESP32 repeater has no task watchdog, so a stall is silent. One request in flight, timeout-driven, nothing waits in `loop()`.
- **A neighbor's reply must never reach the admin paths.** Today `searchPeersByHash` matches ACL clients only. Whatever lets a regions reply decrypt must route that packet to the regions handler and nowhere else.
- Keep MeshCore nomenclature (#197). US English. Diag builds until beta. Every Epic ends with its TEST task.
- **Adversarial review on every code task before anything reaches the owner** (Gemini 2.5; Sonnet while Gemini credits are out, per the standing order).

## Control Surface (repeater CLI: serial and remote admin)
| Command | Effect |
|---|---|
| `set mqtt.neighbors on\|off` | enable/disable the periodic report (persisted, live; default **off**) |
| `set mqtt.neighbors.interval <hours>` | 12–336, default 24 (persisted) |
| `get mqtt.neighbors` / `get mqtt.neighbors.interval` | read current settings |
| `mqtt status` | the `nbr:` schedule line, as on the companion |
| `discover.neighbors` | **unchanged** stock MeshCore command |
| `discover.scopes` | one-shot: query the table (#1903) and publish now; under #1902 alone, publishes the self report now |

## Review Focus (failure modes pinned by tests)
1. A regions reply from a non-ACL peer decrypts, reaches only the regions handler, and can never execute a CLI command or log in. → **B2**
2. A reply from the in-flight peer that is also an ACL client (an admin who is also a neighbor) still reaches the client paths. Same rule as the companion's Review #5. → **B2**
3. The walk never stalls the repeater: one request in flight, 20 s timeout, all-timeout finishes. → **B3**
4. The report matches what the repeater answers: `self.scopes` equals the `handleAnonRegionsReq` payload for the same region map. → **R1**
5. The companion envs build and behave as before once the pool's neighbors code is no longer `OFFBAND_OBSERVER`-only. → **R2**

## File Map
- `src/helpers/wifi_observer/MqttBrokerPool.{h,cpp}` — `publishNeighbors` + fan-out tick move from `#ifdef OFFBAND_OBSERVER` to a feature flag both roles set.
- `src/helpers/wifi_telemetry/RepeaterMqttPool.{h,cpp}` — pass-through for the report.
- `src/helpers/wifi_observer/ObserverCli.cpp`, `ConfigSchema` — the `mqtt.neighbors[.interval]` keys and the `nbr:` line, reachable on the repeater.
- `examples/simple_repeater/MyMesh.{h,cpp}` — self-scope export, schedule tick, `discover.scopes`; #1903 adds the in-flight peer match, `sendRegionsReq`, reply routing and the walk.
- `variants/heltec_v4/platformio.ini` — the flag on `heltec_v4_repeater_telemetry`.
- `test/test_neighbors_json/`, new `test/test_repeater_neighbors/` — the self report, the peer-match rules.

## Build chain (tasks = commits under the Epic)

### Epic #1902 — The repeater's self report  *(build 1/2)*
Tasks: R1 [#1905](https://github.com/OffbandMesh/meshcore-firmware/issues/1905), R2 [#1906](https://github.com/OffbandMesh/meshcore-firmware/issues/1906), R3 [#1907](https://github.com/OffbandMesh/meshcore-firmware/issues/1907), TEST [#1908](https://github.com/OffbandMesh/meshcore-firmware/issues/1908).
- **R1. Self scopes.** A helper that fills `self.scopes` / `default_scope` from the region map with the same call and flag `handleAnonRegionsReq` uses. Test: equals the answer payload; no default region → empty; a list longer than `kScopesLen` is cut at a whole name.
- **R2. The pool's neighbors code for both roles.** Replace the `OFFBAND_OBSERVER` gate on `publishNeighbors` / the fan-out tick with a feature flag set on the observer envs and `heltec_v4_repeater_telemetry`; `RepeaterMqttPool` passes the report through. Test: config-lint capability claims (positive and negative) + the existing fan-out suite unchanged.
- **R3. Schedule + config + CLI on the repeater.** `NeighborsSchedule` driven from the repeater loop; `mqtt.neighbors[.interval]` set/get and the `nbr:` line in the repeater's CLI; `discover.scopes` publishes the self report now. Report: `self` filled, `neighbors: []`, `total_neighbors` = table count, `queried_neighbors: 0`. Test: round-trip + bounds; an empty-neighbors report against the golden contract.
- **TEST.** Epic verification: the native suites + all matrix envs green.

### Epic #1903 — Neighbor scope queries on the repeater  *(build 2/2; stacked on #1902)*
Tasks: B1 [#1909](https://github.com/OffbandMesh/meshcore-firmware/issues/1909), B2 [#1910](https://github.com/OffbandMesh/meshcore-firmware/issues/1910), B3 [#1911](https://github.com/OffbandMesh/meshcore-firmware/issues/1911), B4 [#1912](https://github.com/OffbandMesh/meshcore-firmware/issues/1912), TEST [#1913](https://github.com/OffbandMesh/meshcore-firmware/issues/1913).
- **B1. `sendRegionsReq(neighbor)`** on the repeater: `buildRegionsRequest` + `createAnonDatagram`, zero-hop; the in-flight peer's shared secret computed once per request. Test: emitted type/sub-type/target.
- **B2. Reply matching.** `searchPeersByHash` adds the in-flight peer (Decision 2) after the ACL clients; `onPeerDataRecv` sends that index to the regions handler only, through `InFlightRegionsQuery` (key + tag). Test: Review #1 and #2.
- **B3. The walk.** `ScopeQueryRun<MAX_NEIGHBOURS>` over the repeater's own table, one step per loop pass; RSSI is `null` (the repeater's table has none); `heard_secs_ago` from `heard_timestamp`. Test: responded / timeout / send_failed, advance, no hang on all-timeout.
- **B4. Wire the walk into the cycle.** Refresh → query → publish, as on the companion; `discover.scopes` runs the walk. Test: cycle ordering.
- **TEST.** Epic verification: the walk suite + golden contract + matrix.

### Testing — Integration / hardware  *(after all merges; owner-verified; separate flash token)*
- **T-HW.** Flash a `heltec_v4_repeater_telemetry` bench unit (**your flash go + a minted flash token**). `discover.scopes`; read the report back from a broker; confirm CoreScope sets `configured_scope` on **the repeater's own node** (and, after #1903, on a responded neighbor). Measure the report's heap cost on the device, as T10 did on the V3. Record on #1902 and #1903.

## Decisions (locked by owner, 2026-10-29)
1. **Scope of the chain — (A):** self report first (#1902), then neighbor queries (#1903). Neighbors still matter for repeaters with no WiFi, which reach CoreScope only through someone else's query. *(Rejected: self report only.)*
2. **Regions-reply decryption — (A):** a dedicated in-flight peer: one extra match after the ACL clients, its own cached secret, a sentinel index only the regions handler accepts. The ACL is untouched. *(Rejected: a transient ACL entry, which would put a non-client in the table that grants admin and guest access.)*
3. **`mqtt.neighbors` default on the repeater — off**, matching the companion.

## Execution order
1. ✅ Build Epics and tasks created: #1902 (#1904–#1908), #1903 (#1909–#1913); mirrored in Citadel (`Crosswire-lpx`, `Crosswire-nph`).
2. I commit this file on its **own branch, nothing else** (#707), open its PR. **You merge the plan PR** = approval. **You mint** the plan grant.
3. I run the chain non-stop: claim + stamp the board Agent field per Epic → worktree (stacked) → tasks test-first → review + resolve → commit → push + PR (merge held).
4. At the end of the chain I hand you the PRs. You agree → merge token per Epic → I merge in order → then T-HW (your flash go + flash token).

## Notes
- Evidence grade: everything above about the repeater's code is **read, not tested**: `searchPeersByHash` (`examples/simple_repeater/MyMesh.cpp:626`), `handleAnonRegionsReq` (`:150`), `discover.neighbors` (`:1291`), `RepeaterMqttPool.h`, the V4 PSRAM flag (`boards/heltec_v4.json`), CoreScope's topic routing (`cmd/ingestor/main.go:873`).
- The repeater's existing telemetry "neighbors" topic (`WifiTelemetry::publishNeighbors`) is a different report and is not changed.
