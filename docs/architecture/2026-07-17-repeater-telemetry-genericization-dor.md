# Design-of-Record — Genericize repeater WiFi telemetry for third-party deployment

**Feature #295 · Scoping Epic #296 · status: APPROVED (v2, revised after adversarial review)**
**Date:** 2026-07-17 (v1) · **Revised:** 2026-07-18 (v2 — Gemini adversarial review response) · **Author:** CloudyCliff (agent session)

> Design-of-record for taking the owner's ESP32 burst-WiFi telemetry + command-queue + OTA stack and making it a configurable, GUI-driven, multi-broker option any operator can deploy. Grounding findings on #296; adversarial review + adjudication on #295 (log: `docs/llm-consultations/2026-07-18-296-dor-review-gemini-gemini-2.5-pro.log`).

## 1. Goal

De-hardcode + document + GUI-enable the existing native-ESP32 repeater telemetry stack so a third party can deploy it against **their own** WiFi / broker(s) / command server, configured from the **Offband client GUI** (not CLI). Native ESP32-WiFi repeaters only; nRF/RAK excluded (#135/#136).

## 2. Current state (verified — see #296 for detail)

- Repeater telemetry (`src/helpers/wifi_telemetry/`): burst publish (`WifiTelemetry`), command queue (`RemoteCommand`), single baked-`-D` MQTT via **`PubSubClient`** (TCP/user-pass).
- Observer (`src/helpers/wifi_observer/`): **`esp_mqtt_client`** multi-broker pool + `ConfigSchema` (runtime NVS, TLS/JWT, heap-gated) + the shipped **config wire contract** (#159–166) the Offband client already speaks.
- The two MQTT paths are **not** aligned; the observer's is the **superset**.
- The telemetry stack **already distinguishes burst vs persistent at runtime** (`g_tel_persistent_until_ms`: `0` = burst, non-zero = persistent-until-time), with separate command-poll cadences, and `wifi on N` / OTA-keepalive already force a temporary persistent window. What's missing is a **permanent default mode**.

## 3. Target architecture

```
        ┌─────────────────────────── Repeater (ESP32) ───────────────────────────┐
        │                                                                          │
        │   wifi.mode ──┬── burst   → wake ~5min → publish → drop   (solar)        │
        │               └── always  → stay connected                (PoE/powered)  │
        │                                                                          │
        │   PUBLISH ──┬─► BurstMqttPublisher   [NEW: small, synchronous,           │
        │             │      (burst mode)       connect→publish→ack→disconnect]    │
        │             └─► MqttBrokerPool       [EXISTING observer pool, UNTOUCHED, │
        │                    (always mode)      persistent/event-driven]           │
        │                        └──── both read the SAME ConfigSchema ────┘       │
        │                                                                          │
        │   COMMAND QUEUE + OTA ──► RemoteCommand   [KEEP SEPARATE — already        │
        │        (MQTT sub and/or HTTP cmd-relay)    transport-agnostic]           │
        │                                                                          │
        │   config surface ──► #143 wire contract (configSet/Get) ──► Offband GUI  │
        │   first-boot ──────► #260 provisioning (no baked secrets)                │
        └──────────────────────────────────────────────────────────────────────────┘
```

**Reuse ledger:** config schema + broker definitions + heap logic + GUI contract = reuse observer. Publish *engine* = two purpose-built implementations (see D1). Command-queue + OTA = keep (repeater-specific). Provisioning = reuse #260.

## 4. Decision 1 — how burst gets supported  ✅ **REVISED (v2): separate burst publisher**

**v1 said** "converge onto `MqttBrokerPool` with a dual (persistent + burst) lifecycle."
**v2 replaces that.** Adversarial review flagged the retrofit as a design smell: the pool is complex, event-driven, and **already deployed on live observers**. Retrofitting a burst lifecycle invites state-machine conflicts (teardown during backoff/reconnect), event races (`MQTT_EVENT_DISCONNECTED` arriving mid-teardown → double-free / use-after-free), and an ill-defined "drain" step that would silently drop async publishes.

→ **Leave `MqttBrokerPool` untouched. Add a separate, small `BurstMqttPublisher`** — still on `esp_mqtt_client` (so **TLS remains available**), but a simple synchronous sequence: `connect → wait → publish → wait-for-ack → disconnect`. Both components read the **same `ConfigSchema`**, so configuration and GUI are identical either way.

This preserves the owner's D1 intent (TLS/wss/JWT possible on a repeater, driven by the combined repeater+observer role on powered/PoE builds) while removing the regression risk to working observers.

### 4a. TLS cost is a function of **reconnect frequency**, not power source

The review framed this as "TLS on solar is non-viable." **That framing is wrong and has been corrected (owner challenge, 2026-07-18).** TLS is encryption, not a power feature. It costs energy only *indirectly*:

- **Handshake round-trips** keep the WiFi radio awake waiting on the server (~100–150 mA — the dominant term on the board).
- **Crypto CPU work** for the key exchange (~hundreds of ms at full clock).

**The variable that matters is how often you reconnect — not whether you're on battery.**
- **`always` mode:** handshake once, connection persists for days → cost amortizes to ~zero. Power source irrelevant.
- **`burst` mode:** WiFi drops between wakes, so **every wake pays a full handshake**. At a 5-minute interval that's ~288/day; at a 6-hour interval it's 4/day and TLS is a non-issue.

Solar is merely *correlated* (it's why you'd choose burst), not causal. So cost scales with **`wifi.burst_interval`**, and the estimate that motivated the original BLOCKER is only meaningful at short intervals.

**Mitigation already planned:** #175 (round-robin TLS broker scheduler) includes **TLS session resumption** — a reconnect reuses a cached session and skips the full handshake, substantially reducing per-wake cost. Cross-link when it lands.

**Binding position:**
- **`burst` mode defaults to plain TCP** — a safe *default* for operators who haven't considered it. It is **not** a restriction.
- **TLS is freely selectable in either mode.** No warning gate, no blocking, no measurement precondition. The operator knows their panel, battery, latitude, interval, and season; we do not. Guardrails here would contradict the point of the feature.
- **Instead of warning, we observe** — the device reports its own radio-active time (§4d) so the operator can judge empirically.

### 4b. Mode selection — `wifi.mode` (owner-approved)

```
wifi.mode = burst    → wake, publish, sleep      (default; solar)   → BurstMqttPublisher
wifi.mode = always   → stay connected            (PoE/powered)      → MqttBrokerPool
wifi.burst_interval  → applies in burst mode only (existing tunable, 60 s floor / 24 h ceiling)
```

Chosen as an extensible **mode** (not a boolean) so it renders as a GUI dropdown and leaves room for future modes (scheduled, etc.). Dotted-key naming matches the existing schema (`wifi.ssid`, `mqtt.broker.N.*`).

**Interactions:**
- The setting is the **steady-state default**; existing temporary overrides still work — an OTA/keepalive command can hold a burst node online for a session, then it drops back.
- **Both components compile into the binary** (flash cost) and mode switching must cleanly stop one and start the other. Sizing + switch semantics are an Epic-0 item.

### 4c. Combined repeater+observer role

The shared `ConfigSchema` + pool naturally allows one node to be both roles. **#295's obligation is only that the architecture must not preclude it**; the explicit combined build profile is deferred to backlog epic **#297**.

### 4d. Radio-active-time telemetry — observability instead of guardrails (owner-proposed)

Rather than warn operators about a cost we cannot compute for their hardware, **the node reports what the burst cycle actually cost it**, in situ:

```
wifi_active_last_s   ← radio-active seconds, most recent burst session
wifi_active_avg4_s   ← rolling mean over the last 4 sessions
```

Two fields added to `TelemetryData`, published over MQTT and surfaced as **Home Assistant entities** via the existing HA discovery path. Implementation is a `millis()` delta around the burst session's existing `begin()`/`end()` boundaries plus a 4-element ring — small.

**Why this matters more than a warning:** it makes the TLS-cost question **self-answering for anyone**. Enable TLS, watch `wifi_active_avg4_s` move, decide. No bench estimate, no power analyzer, no assumptions about panel/battery/latitude. The owner can already infer this from power draw; **a third-party operator cannot** — and making this deployable by third parties is the entire point of the Feature.

Owned as a task under the publish epic (#301), which owns the session boundaries.

## 5. Decision 2 — config backend sharing  ✅ (a) shared backend, app-coordinated

Refactor the config-command backend (`configSet`/`configGet` dispatch + `ConfigSchema` accessors) out of `ObserverCli` into a shared component both roles link. It **is** the firmware↔client contract surface, so it moves in **two-repo lockstep with meshcore-client**: additive + capability-gated (stock/older clients unaffected), contract versioned, client screens land alongside.

**Mandatory regression gate (review MAJOR 5):** after the refactor and **before** any repeater work proceeds, operate an observer node on the refactored firmware using the **currently released** app and prove **zero functional regression**. The observer fleet is deployed and high-value; "coordinated with the app" alone does not protect it.

## 6. Decision 3 — operator command intake  ✅ both (MQTT + HTTP cmd-relay)

MQTT (`<prefix>/<node>/cmd`, shared secret) reuses the operator's telemetry broker; HTTP cmd-relay (`GET /cmds` poll, bearer auth, `POST /responses`) is the advanced path. `RemoteCommand` already handles both — genericization = make endpoints/secrets runtime-configurable and GUI-settable.

**Security trade-off (review MINOR 7) — must be documented, not hidden:** a shared secret guarding a channel that can trigger firmware flashes means one compromised secret exposes every device using it. Requirements: document the trade-off + rotation guidance in the operator spec; store secrets in NVS under **flash encryption**; note per-device-identity commands (e.g. per-device topic/JWT or signed commands) as a known architectural weakness for a future epic.

## 6a. OTA-pull-from-GitHub-release

A command tells the node to **fetch its matching firmware from the Offband GitHub release and self-flash** (`params` names `latest` or `offband-vX.Y.Z`). Best over the HTTP cmd-relay path.

**MANDATORY requirements (review BLOCKER 2 — non-negotiable, not "design points"):**
1. **Cryptographic signature verification** — artifacts signed with an offline key; **public key baked into firmware**; signature verified before flash. A release-side SHA is **insufficient** (a repo compromise replaces binary *and* hash).
2. **Compile-time board-variant match** — the running firmware's variant is a **compile-time constant**, not a configurable value; refuse any artifact whose embedded target doesn't match (`.github/release-envs.txt`, #207).
3. **Atomic A/B OTA with automatic rollback** — write to the inactive partition, verify, then switch; bootloader auto-reverts if the new image fails to boot (aligns with existing SafeFlash rollback direction).
4. **Full TLS chain validation** to GitHub against a baked-in CA store — no CN/verification shortcuts.
5. **Power-fail safe at every stage.**

Phaseable: genericized push-OTA first, pull-from-GitHub as a fast-follow — but it ships with **all five** controls or not at all.

## 7. Migration of already-deployed repeaters (review MAJOR 6 — gap in v1)

Existing repeaters run **build-time-baked** config. Flashing them to NVS-based config without a migration path strands them (no WiFi/broker → soft-brick → manual re-provisioning).

**Required:** on first boot after update, detect absent `ConfigSchema` keys and **migrate legacy `-D` values into NVS** (retained read-only for one release). If migration isn't possible, the node must enter a **well-defined, clearly signalled "awaiting configuration" state** (LED pattern documented for operators) rather than failing silently. Owned by the provisioning epic.

## 8. Multi-broker count (heap)

Reuse the observer's heap-gated concurrency (`OFFBAND_MAX_LIVE_TLS`, `HeldNoHeap`, #171/#177). The repeater ceiling is **measured, not guessed** — with **LoRa active** — in Epic 0.

## 9. GUI config (companion API)

Extend the #143 contract with repeater keys (`wifi.mode`, WiFi creds, broker slots, OTA secret, command-queue endpoint/secret), capability-gated + additive. Offband client adds repeater settings screens; secrets **write-only** in GET responses (mirror observer redaction).

## 10. Child-epic decomposition

**Epic 0 — Feasibility spike (BLOCKS EVERYTHING; review MAJOR 4).** Delivers: (a) **heap ceiling** on `heltec_v4_repeater_telemetry` with LoRa active → real max concurrent brokers and TLS contexts (a hard RAM limit, and the number #302 consumes); (b) **burst-lifecycle PoC** on `esp_mqtt_client`; (c) **config subset** — the strict subset of `ConfigSchema` `BurstMqttPublisher` honours, with the GUI hiding/disabling settings inapplicable to `wifi.mode = burst` (two publishers sharing one schema otherwise risks divergent behaviour); (d) **mode-switch state machine** — switching logic plus **precedence rules** against existing temporary overrides (`wifi on N` / OTA keepalive), so they cannot conflict; (e) **flash budget** — quantified cost of shipping both publisher implementations, confirmed to fit every env in the release matrix.

**Explicitly NOT in Epic 0:** a bench power profile / power-analyzer measurement. Dropped (owner, 2026-07-18) — TLS is freely selectable, the default is safe, and §4d's radio-active-time telemetry answers the cost question empirically on the operator's own hardware. No gear required.

1. **Config backend shared surface** (D2) — extract dispatch; **+ observer zero-regression gate** before anything proceeds.
2. **Repeater publish convergence** (D1 v2) — build `BurstMqttPublisher`; add `wifi.mode`; un-bake WiFi/MQTT to runtime.
3. **Multi-broker on repeater** — slots + heap-derived ceiling from Epic 0.
4. **Command-queue + OTA genericization** (D3) — runtime endpoints/secrets; both intakes; **OTA-pull-from-GitHub with all five mandatory controls**.
5. **Distributable provisioning** — #260 reuse; no baked secrets; **+ legacy-config migration (§7)**.
6. **GUI config (two-repo)** — contract extension + client screens.
7. **Operator setup spec** — server-side contract + auth + secret-rotation guidance.
8. **Epic integration test** — end-to-end vs a clean operator setup (human sign-off gate).

**Dependencies:** `0 → 1 → 2 → {3,4} ; 5 after 2 ; 6 after 1+4 ; 7 after 4 ; 8 last.`

## 11. Decisions log
- **D1** ✅ **v2:** separate `BurstMqttPublisher` (not a dual-lifecycle retrofit); pool untouched; both share `ConfigSchema` (owner, 2026-07-18, post-review).
- **D1a** ✅ **REVISED (owner challenge, 2026-07-18):** TLS cost tracks **reconnect frequency**, not power source. `burst` **defaults** to plain TCP (safe default only) but TLS is **freely selectable in either mode** — no warning gate, no blocking, no measurement precondition. Bench power measurement **dropped**; replaced by §4d radio-active-time telemetry so operators judge empirically. Session resumption (#175) is the real mitigation.
- **D1b** ✅ `wifi.mode = burst | always` (extensible mode, not boolean) selects behaviour **and** code path (owner, 2026-07-18).
- **D2** ✅ shared config backend, app-coordinated, **+ mandatory observer no-regression gate** (owner 2026-07-17; gate added 2026-07-18).
- **D3** ✅ both intakes + OTA-pull-from-GitHub, now with five mandatory security controls (owner 2026-07-17; controls added 2026-07-18).
- **Combined role** ✅ must-not-preclude only; build profile deferred to **#297**.
- **Review response** — all 7 findings accepted; F1/F3 escalated to owner and approved. Adjudication: #295.
