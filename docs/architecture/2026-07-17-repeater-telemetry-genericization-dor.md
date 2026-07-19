# Design-of-Record — Genericize repeater WiFi telemetry for third-party deployment

**Feature #295 · Scoping Epic #296 · status: APPROVED (owner sign-off 2026-07-17)**
**Date:** 2026-07-17 · **Author:** CloudyCliff (agent session)

> This is the design-of-record for taking the owner's ESP32 burst-WiFi telemetry + command-queue + OTA stack and making it a configurable, GUI-driven, multi-broker option any operator can deploy. Grounding findings are on #296 (reconciliation + MQTT-alignment comments). This doc resolves the open architecture decisions and yields the child-epic decomposition.

## 1. Goal

De-hardcode + document + GUI-enable the existing native-ESP32 repeater telemetry stack so a third party can deploy it against **their own** WiFi / broker(s) / command server, configured from the **Offband client GUI** (not CLI). Native ESP32-WiFi repeaters only; nRF/RAK excluded (#135/#136).

## 2. Current state (verified — see #296 for detail)

- Repeater telemetry (`src/helpers/wifi_telemetry/`): burst publish (`WifiTelemetry`), command queue (`RemoteCommand`), single baked-`-D` MQTT via **`PubSubClient`** (TCP/user-pass).
- Observer (`src/helpers/wifi_observer/`): **`esp_mqtt_client`** multi-broker pool + `ConfigSchema` (runtime NVS, TLS/JWT, heap-gated) + the shipped **config wire contract** (#159–166) the Offband client already speaks.
- The two MQTT paths are **not** aligned; the observer's is the **superset**.

## 3. Target architecture

```
        ┌─────────────────────────── Repeater (ESP32) ───────────────────────────┐
        │                                                                          │
        │   burst scheduler (wake ~5min → work → drop)   [KEEP: WifiTelemetry]     │
        │        │                                                                 │
        │        ├─► PUBLISH  ──►  MqttBrokerPool + ConfigSchema   [CONVERGE onto   │
        │        │                 (multi-broker, TLS/JWT,          observer stack] │
        │        │                  heap-bounded, runtime config)                   │
        │        │                                                                  │
        │        └─► COMMAND QUEUE + OTA  ──►  RemoteCommand        [KEEP SEPARATE, │
        │                 (MQTT sub and/or HTTP cmd-relay)          already t-agnostic]│
        │                                                                           │
        │   config surface ──► #143 wire contract (configSet/Get)  ──► Offband GUI  │
        │   first-boot ──────► #260 provisioning (no baked secrets)                 │
        └───────────────────────────────────────────────────────────────────────────┘
```

Reuse ledger: publish + multi-broker + heap-gating + runtime-config + GUI-contract = reuse observer. Command-queue + OTA = keep (repeater-specific). Provisioning = reuse #260.

## 4. Decision 1 — pool lifecycle: burst vs persistent  ✅ **RESOLVED: Option A (dual lifecycle)**

**Owner decision (2026-07-17): TLS/wss/JWT IS required** — driven by the **combined repeater+observer role** on powered/PoE builds (non-solar). Real near-term example: a Christ Hospital PoE node going up this weekend as repeater+observer.

→ **Converge onto the observer `esp_mqtt` `MqttBrokerPool` + `ConfigSchema`** (Option A), extended to a **dual lifecycle**:
- **Persistent mode** — powered / observer / combined-role nodes: the pool runs as it does today (always-on worker, backoff SM, heap-gated TLS).
- **Burst mode** — solar repeaters: a begin→publish→drain→end lifecycle (bounded connect window, publish, tear down; accept the per-wake TLS handshake cost as the price of TLS-on-solar, or fall back to a TCP broker on the tightest solar budgets).

The always-on observer path must not regress — burst mode is additive.

### 4a. Combined repeater+observer role (new — owner-raised)

Converging the MQTT stack **naturally enables a single node to be both** repeater and observer with one broker pool (telemetry-publish + observer-publish share the pool + config + GUI). Powered/PoE builds are the sweet spot (no burst constraint).

**Owner decision (2026-07-17):** #295's job is only that the converged architecture **must not preclude** the combined role. The explicit combined build profile (its own env + role-compose wiring + testing) is **deferred to a separate backlog epic — #297** — to build on top of #295's converged stack later.

## 5. Decision 2 — config backend sharing  ✅ **RESOLVED: (a) shared backend, coordinated with the app**

**Owner decision (2026-07-17): yes — refactor the config-command backend (`configSet/configGet` dispatch + `ConfigSchema` accessors) out of `ObserverCli` into a shared component both roles link — BUT coordinated with the app.**

The config-command backend *is* the firmware↔client contract surface. The extraction + any repeater-key additions move in **two-repo lockstep with meshcore-client**: additive + capability-gated so stock/old clients are unaffected, contract versioned, and the client's settings screens land alongside. Coordinate via the client counterpart (Agent Mail `request_contact` to the meshcore-client agent + the durable contract on the GH issue). Observer keeps working unchanged.

## 6. Decision 3 — operator command intake  ✅ **RESOLVED: both (MQTT + HTTP cmd-relay)**

**Owner decision (2026-07-17): expose BOTH.** MQTT (`<prefix>/<node>/cmd`, shared-secret) reuses the operator's telemetry broker; HTTP cmd-relay (`GET /cmds` poll, bearer auth, `POST /responses`) is the advanced path. `RemoteCommand` already handles both — genericization = make endpoints/secrets runtime-configurable (not baked) + GUI-settable.

## 6a. OTA-pull-from-GitHub-release (new capability — owner-raised, "mega cool")

New command-queue action: instead of only OTA-**push** (operator pushes firmware to a device HTTP endpoint), add OTA-**pull**: a command tells the node to **fetch its matching firmware straight from the Offband GitHub release and self-flash**. Ideal over the HTTP cmd-relay path — a `params` field names a release (e.g. `latest` or `offband-vX.Y.Z`); node resolves + downloads + flashes.

**Design points (for the OTA child epic — not solved here):**
- **Variant matching** — the node must fetch the artifact for its **own board env** from the release matrix (`.github/release-envs.txt`, #207). Node must know its variant.
- **Artifact verification** — HTTPS fetch + verify (hash/signature) before flashing; never flash an unverified blob. Guard the flash (SAFELANE isolate-dangerous-action) — a bad pull must not brick.
- **Release naming** — Offband releases are `offband-v*` tags via `release.yml`; `latest` = newest non-prerelease.
- **Rollback / safety** — pairs with SafeFlash boot-rollback concepts; a failed pull-flash must recover.
- **Auth/rate-limit** — reuses `RemoteCommand`'s shared-secret + rate-limit + safety-log.

Phaseable: v1 could ship push-OTA genericized first, pull-from-GitHub as a fast-follow. Captured as a distinct task under the command-queue/OTA child epic.

## 7. Multi-broker count (heap) — data-gathering task

Reuse the observer's heap-gated concurrency (`OFFBAND_MAX_LIVE_TLS`, `HeldNoHeap`, #171/#177). Actual ceiling on the repeater-telemetry build = **measured, not guessed**: build `heltec_v4_repeater_telemetry`, read free heap, derive safe slot/TLS ceiling. Filed as a task under the multi-broker child epic.

## 8. GUI config (companion API)

Extend the #143 wire contract with the repeater's keys (WiFi, broker-pool slots, OTA secret, command-queue endpoint/secret), capability-gated + additive so stock clients are unaffected. Offband client (meshcore-client) adds the repeater settings screens — two-repo coordination plan + version/capability negotiation. Secrets write-only in GET responses (mirror observer redaction).

## 9. Child-epic decomposition (proposed — file after sign-off)

1. **Config backend shared surface** (Decision 2) — extract/reuse config-command dispatch; repeater consumes `ConfigSchema`. *(foundation; blocks the rest)*
2. **Repeater publish convergence** (Decision 1 = A) — repeater telemetry publishes via the observer `esp_mqtt` pool; add **burst lifecycle** (persistent path untouched); un-bake WiFi/MQTT to runtime.
3. **Multi-broker on repeater** — broker-pool config for the repeater; heap-measured ceiling.
4. **Command-queue + OTA genericization** (Decision 3 = both) — runtime cmd-queue endpoint/secret; MQTT + HTTP cmd-relay intake; OTA secret runtime. **Task: OTA-pull-from-GitHub-release** (§6a — variant-match + verify + safe-flash), phaseable as a fast-follow.
5. **Distributable provisioning** — reuse #260 first-boot/flash-time config; no baked secrets in shipped binary.
6. **GUI config (two-repo)** — contract extension + Offband client screens (coordinate #142/#143).
7. **Operator setup spec** — durable operator doc (server-side MQTT/cmd-queue contract + auth).
8. **Epic integration test** — end-to-end on a real repeater against a clean operator setup (the human-sign-off gate).

Dependencies: 1 → 2 → {3,4} ; 5 parallel after 2 ; 6 depends on 1+4 ; 7 depends on 4 ; 8 last.

## 10. Decisions log
- **D1** ✅ Option A — converge onto observer `esp_mqtt` pool, dual lifecycle (persistent + burst). Driver: TLS/wss/JWT + combined repeater+observer role (owner, 2026-07-17).
- **D3** ✅ Both intakes (MQTT + HTTP cmd-relay); + OTA-pull-from-GitHub-release as a new capability (owner, 2026-07-17).
- **D2** ✅ (a) shared config backend, **coordinated with the app** (two-repo lockstep, additive/capability-gated) (owner, 2026-07-17).
- **Combined role** ✅ #295 must-not-preclude only; explicit build profile deferred to backlog epic **#297** (owner, 2026-07-17).
