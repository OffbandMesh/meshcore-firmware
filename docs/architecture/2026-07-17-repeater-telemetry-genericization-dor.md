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

### 4a. TLS on solar — **default OFF** (review BLOCKER 1)

v1 said "accept the per-wake TLS handshake cost." **That was wrong for solar.** Estimated cost: 288 wakes/day × ~1.5 s handshake @ ~150 mA ≈ **~18 mAh/day for handshakes alone**, against a documented **~17 mAh/day** incremental burst budget — i.e. TLS alone could consume the entire power budget, leaving nothing for LoRa/mesh.

`[hypothesis: 1.5 s / 150 mA are estimates and the 17 mAh figure is the incremental burst cost — Epic 0 must measure both empirically before this is treated as settled.]`

**Binding position:**
- **Solar / burst nodes default to plain TCP** (user/pass).
- **TLS/wss is opt-in**, with a clear power warning in the GUI when selected on a burst-mode node.
- **Powered / `always` mode nodes use TLS freely** — no burst penalty; this is the combined repeater+observer case that motivated D1.
- The default must not be changed without the Epic-0 power measurement.

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

**Epic 0 — Feasibility spike (BLOCKS EVERYTHING; review MAJOR 4).** The design is assumption-based until this lands. Delivers: (a) **heap ceiling** on `heltec_v4_repeater_telemetry` with LoRa active → real max concurrent TLS/brokers; (b) **measured power profile** of a burst wake with and without TLS → validates or kills §4a; (c) **burst-lifecycle PoC** on `esp_mqtt_client` + mode-switch cost. Output gates go/no-go on the architecture.

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
- **D1a** ✅ TLS **default off** on solar/burst; opt-in with warning; powered/`always` uses TLS freely; gated on Epic-0 measurement (2026-07-18).
- **D1b** ✅ `wifi.mode = burst | always` (extensible mode, not boolean) selects behaviour **and** code path (owner, 2026-07-18).
- **D2** ✅ shared config backend, app-coordinated, **+ mandatory observer no-regression gate** (owner 2026-07-17; gate added 2026-07-18).
- **D3** ✅ both intakes + OTA-pull-from-GitHub, now with five mandatory security controls (owner 2026-07-17; controls added 2026-07-18).
- **Combined role** ✅ must-not-preclude only; build profile deferred to **#297**.
- **Review response** — all 7 findings accepted; F1/F3 escalated to owner and approved. Adjudication: #295.
