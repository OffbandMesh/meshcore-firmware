<!-- Generated 2026-06-01 by the observer-architecture-review workflow (wf_96f00d58-6ef): 6 cross-cutting lenses -> synthesis -> narrowing critic -> final. Read-only analysis of meshcore-firmware @ fix-325-serial-cli worktree (crosswire 9b2dfc28). Refs Strycher/LoRa #325 #326 #327. -->

# CROSSWIRE COMPANION-OBSERVER -- FINAL TARGET ARCHITECTURE & REMEDIATION PLAN

**Owner-review artifact. Supersedes the synthesis. Resolves all six NARROWING gaps.**

The synthesis found the right missing abstraction; the critique was correct that it then *costed each box in isolation and never reassembled the whole-system invariants*. This plan keeps the one-system through-line and adds the six things that were missing: a total-DRAM ledger that proves boot, defined broker-down behavior, a boot/init + NVS-migration plan, a step ordering where corruption-safety lands before concurrency, a per-variant provisioning matrix, and a command-bus buffer storage decision. Two of the synthesis's moves are corrected outright: the slot-40 security fix is **separated** from the stock-app UI deletion (they were wrongly fused), and Step 0's "let esp_mqtt auto-reconnect" option is **rejected** (it silently breaks the JWT refresh design).

---

## 1. ROOT STRUCTURAL FAULTS

There is no clean layering. What exists is **one monolith -- the full BLE companion-radio messenger -- with an observer, an MQTT uplink, a fake "control channel," and a USB CLI welded onto its side**, all sharing the messenger's data structures, its single transport pointer, and its scarce internal DRAM. Every symptom across all six diagnostic lenses traces to three faults, and the three faults are themselves one through-line: **the firmware has no first-class, transport-neutral command/control concept**, so the control plane is forced to impersonate first a *transport*, then a *data structure*, and the product is forced to *inherit* an entire messenger rather than *compose* itself.

**ROOT FAULT A -- The operator link is a single, compile-time-chosen, frame-protocol-shaped `_serial` object, not a transport-agnostic command bus.**
`MyMesh::_serial` is one `BaseSerialInterface*`, set once, with ~60 hardcoded `_serial->writeFrame` call sites. Transports are mutually exclusive by an `#if` ladder (`main.cpp:48-97`); a transport is born at boot and dies at reset -- there is no runtime arrive/leave. Because every *door* is transport-bound, each new operator surface needs a new hand-rolled reader: there are four ingress paths (BLE slot-40 intercept, USB raw-Serial, the absent Web, LoRa admin packet), three converging on `cliPassthroughExecute` by copy-paste convention and one (LoRa->CommonCLI) bypassing the gate entirely. **#325 is the smoking gun:** a USB operator on a BLE build has no reach into config, so #325 added a *fourth* hand-rolled reader plus `obsCliIsPwdSetPrefix`, which **re-implements the dispatcher's normalization purely to know when to stop echoing the PSK** -- a security invariant now encoded byte-identically in two files that must be hand-synced. The per-peer state (`_iter*`, `pending_*`, `app_target_ver` at `MyMesh.cpp:1007`, the shared `cmd_frame`/`out_frame`) are device-global singletons: this is not a pointer you can duplicate, it is "the device" conflated with "the one app talking to it."

**ROOT FAULT B -- The control plane is welded to the mesh data plane via shared `ChannelDetails` storage (slot 40).**
The `_sys` channel smuggles a request/response device CLI into a LoRa group-chat slot so it renders inside the *unmodified* MeshCore Companion app. This one "borrow the chat UI" decision cascades: `MAX_GROUP_CHANNELS` is bumped 40->41 by a fragile macro **redefinition** guarded by `-Wno-builtin-macro-redefined`; `kSystemChannelSlot=40` is compared literally in four `MyMesh.cpp` sites; a do-nothing `systemChannelAllowSet()` exists only to say "no"; `systemChannelInit` ships a printf to detect its own misconfiguration and calls `saveChannels()` at boot (`MyMesh.cpp:1695`). The security collision is the severe part: the slot-40 PSK is `SHA256(pubkey || "crosswire-sysch-v1")` -- **derivable by anyone who has heard the device's advert** -- AND it is a live OTA LoRa decrypt key (slot 40 is walked by `searchChannelsByHash`, verified), AND `CMD_GET_CHANNEL 40` returns it in cleartext over BLE because the intercept only covers SET. The "locked channel" is write-locked and fully read-exposed.

**ROOT FAULT C -- "Observer" is defined by inheritance of the entire BLE messenger, not by composition, and the platform's memory model means that inheritance is paid in the one currency that is scarce: internal DRAM.**
`heltec_v4_companion_observer_wifi extends ..._companion_radio_ble`, adding 3 `-D` flags + one src-filter line, inheriting `MAX_CONTACTS=350`, `MAX_GROUP_CHANNELS=41`, `OFFLINE_QUEUE_SIZE=256`, the full NimBLE GATT stack, bonding, the companion frame surface, and the messenger UITask -- none of which an RX->publish box needs. The real internal-DRAM ceiling is ~320KB; the identical code on no-PSRAM V3/XIAO reports **57-64% static RAM (~200KB)** -- that is the truth for V4 too. V4's "9.6% of 2MB" is a vanity number: static `.bss` **cannot** live in PSRAM on this framework. Three independent gates bar every large consumer from PSRAM -- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` (sub-4KB -> DRAM), `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=1` (all TLS buffers pinned to DRAM), and `EXT_RAM_ATTR` is **inert** (gated on an absent symbol). The firmware uses zero `ps_malloc`/`heap_caps_malloc(SPIRAM)`/`psramFound()`. The memory model is "everything static or small-heap" -- the one model that derives zero benefit from PSRAM -- which is the entire heap-exhaustion history (#281, #312).

**Where the three roots co-locate (highest-leverage targets):**

| Consumer | Root A | Root B | Root C | Dead? |
|---|---|---|---|---|
| `offline_queue[256]` ~ 44KB DRAM | sized by per-transport build flag | drains to synthesized-frame BLE path | largest static consumer; latency-tolerant  ideal PSRAM tenant, locked in `.bss` | live but oversized |
| `ObserverPipeline::ring_[50]` ~ 13KB DRAM | written on LoRa RX hot path (memcpy <=256B/pkt) | -- | static `.bss`, "sized for V3 no-PSRAM" per its own comment | **zero consumers -- verified** |
| `/status` vertical (builder+scheduler+NVS+CLI) | -- | -- | ~1KB stack builder + NVS schema | **dead -- `wifiObserverSetStatusSnapshot` never called from loop -- verified** |
| `BrokerConfig.jwt_token[512]` x 6 | -- | -- | always-resident; re-minted live at connect | half-dead |

---

## 2. TARGET ARCHITECTURE

### 2a. Unified transport-agnostic command bus

One pipeline, one owner, transports as thin byte-adapters. **The bus does not own the reply buffer -- the caller does** (resolves GAP 6):

```c
enum class Transport  { Usb, Ble, Lora, Web };
enum class AuthLevel  { Local, Bonded, Remote };

struct CommandRequest {
    const char* line;          // caller-normalized-or-raw input
    Transport   src;
    AuthLevel   auth;
    char*       reply;         // CALLER-PROVIDED buffer
    size_t      reply_cap;     // CALLER's true capacity
};
struct CommandReply {
    size_t len;
    bool   redact_input;       // bus tells the adapter to hide what was typed
    bool   requires_reboot;    // uniform "this setting needs a restart" hint (#326)
    bool   truncated;          // reply_cap was too small; adapter decides how to surface
};

CommandReply commandBusExecute(CommandRequest& req);
// 1. normalize()  -- the #313 lowercase-fold + trimLeading, ONE copy
// 2. authorize(src, auth) -- allow/deny by (transport, auth), ONE copy
// 3. dispatch()   -- dispatchObserverCli (+ future CommonCLI fall-through), ONE copy
// 4. returns redact_input + requires_reboot WITH the reply
```

The bus runs **synchronously on the caller's task and writes only into the caller's buffer.** It holds no static scratch of its own. This kills the 140-vs-256-vs-claimed-1024-actual-161 size lie *and* its dangerous inverse: a unified bus must never impose the BLE-sized worst case on the repeater task's ~161-byte stack (GAP 6). Each adapter declares the truth about its own buffer; the bus respects `reply_cap` and sets `truncated` rather than overrunning. The repeater path passes `reply_cap=161` and gets a clean truncation flag instead of a stack smash.

**Adapters after:**
- **USB:** line reader -> bus -> `Serial.println(reply)`. (#325 ships as this adapter, not a fourth reader.)
- **BLE:** dedicated control opcode/characteristic (2b) -> bus -> reply on that characteristic. **Not** a chat channel.
- **LoRa (repeater):** admin packet -> bus with `auth=Remote` -> datagram. **Inherits the allowlist for free** -- the path-4 OTA-credential hole closes structurally.
- **Web (when it lands):** POST body -> bus -> HTTP response.

`obsCliIsPwdSetPrefix` and its "MUST stay byte-identical to the dispatcher" contract are **deleted**; redaction comes from `reply.redact_input`, computed once where normalization lives.

### 2b. Mesh-channel vs command-bus separation -- and the security/UI split the synthesis fused

The synthesis fused two separable things. They are split here:

- **The security bug** (derivable PSK in the OTA decrypt loop + cleartext `CMD_GET_CHANNEL` leak) is fixed in **Step 1**, cheaply, by (a) intercepting `CMD_GET_CHANNEL` for slot 40 so the PSK is never returned, and (b) **excluding slot 40 from `searchChannelsByHash`** so the control key is never a live LoRa decrypt key. This is ~2 intercepts and one loop guard. It does not touch the operator's setup experience.
- **The architectural cleanup** (control plane stops being a `ChannelDetails`) is the **Step 6** first-class BLE control opcode. Mesh channels then revert to vanilla upstream: `MAX_GROUP_CHANNELS=40` everywhere, delete the macro hack + `-Wno-builtin-macro-redefined`, no reserved slot, no lock checks, no GET/SET slot-40 special cases, delete `systemChannelInit`'s boot `saveChannels()` and the NVS double-storage.

**The cost the synthesis under-stated (GAP 5):** the slot-40 chat rendering *was the entire no-custom-client setup path* -- configure WiFi from the stock MeshCore app anyone already has. Step 6 replaces it with a dedicated opcode that needs a custom driver. Therefore Step 6 is **gated on the provisioning matrix (4) having a named, existing no-custom-software path for every shipped variant.** Until that exists, the stock-app `_sys` *rendering* is preserved as a **read-only-secret-safe compatibility shim** (PSK never rendered, GET intercepted) -- security fixed, onboarding intact. We do not delete the onboarding path before its replacement exists.

The control plane never touches the radio in either state; operator auth lives in the transport (BLE bonding / web session / repeater admin auth), never in a derivable mesh secret.

### 2c. Pluggable / concurrent transport model -- with the corruption-safety ordering fixed

A `TransportRouter` owns a *list* of `BaseSerialInterface*`, polls each, tags every inbound frame with `(transport, conn_handle)`, and routes replies to the originating source. `handleCmdFrame` becomes `handle(req, Session&)` writing via `session.reply(frame)`, not `_serial->writeFrame`. **Per-peer state (`_iter*`, `pending_*`, `app_target_ver`, `cmd_frame`/`out_frame`) moves into a `Session` struct keyed by `conn_handle`.**

GAP 4 correction -- **`Session` isolation lands BEFORE multi-ingress.** The synthesis scheduled `Session` last (Step 6) while adding a second concurrent ingress earlier (Steps 4-5); between them, USB-CLI + BLE-companion would race the shared `out_frame`. In this plan the ordering is inverted: the `Session` extraction is **Step 4** and the multi-ingress bus is **Step 5**, so concurrency is never introduced before the state that makes it safe. The single-peer invariant is never violated mid-sequence.

Transports become runtime-registered by composition, not a winner-take-all `#elif` ladder, which makes a **USB-only, BLE-absent observer buildable** (recovers the NimBLE DRAM floor -- the headless hand-off box).

### 2d. Memory / PSRAM model -- with the total-DRAM ledger that proves boot (GAP 1)

Treat **internal DRAM (~320KB) as the real currency**; the V4 percentage is discarded. The reclaims are *not uniformly additive across boards*, and the honest per-board ledger is the artifact that proves the device still boots:

| | V4 (2MB PSRAM) | V3 / XIAO (no PSRAM) |
|---|---|---|
| Static floor today | ~200KB `.bss` (vanity 9.6%) | ~200KB = **57-64% reported** |
| Step 1: delete ring_ (13KB) + dead verticals | 13KB floor | 13KB floor (**only floor win these boards get**) |
| Step 2: drop resident `jwt_token[512]`xN + lazy broker slots | 6-9KB | 6-9KB |
| Step 3: offline_queue->PSRAM (44KB) | **44KB DRAM floor** | **0 -- stays in DRAM (no PSRAM)** |
| **Worst-case simultaneous-handshake peak** | N wss brokers re-handshaking x ~40KB mbedTLS *internal-pinned* | same per-broker cost, far less headroom |

**The number that matters and that no prior lens owned:** the crashes are *peak-driven*, not floor-driven. `MqttBrokerPool::loop` re-initiates every `Backoff` broker; if WiFi drops and **3 wss brokers re-handshake at once, that is ~120KB of internal-pinned mbedTLS transient** that *no reclaim in this plan touches*, because TLS-in-PSRAM is frozen until #87. Therefore the plan makes two hard, owner-facing statements:

1. **V4 with TLS:** sustainable **only if concurrent handshakes are capped.** This plan adds a **handshake admission gate** (3 Step 7): at most *K* brokers in `Connecting` simultaneously (K=1 conservative, tunable), the rest deferred in `Backoff`. This bounds peak DRAM to ~Kx40KB regardless of broker count -- and it is the *cheap* alternative to #87 for keeping V4 wss alive.
2. **V3/XIAO with TLS:** the honest answer is **"observer-with-wss on no-PSRAM boards requires #87 first."** The floor relief (13KB + jwt) does not create room for even one ~40KB mbedTLS transient on a board already at 57-64%. This is recorded as a **DEFER decision in 2e**, not buried. V3/XIAO can ship as **plaintext-MQTT or single-broker-TLS observers** today; multi-broker wss waits for the toolchain swap.

PSRAM actions, by cost tier:
- **Free today, no toolchain change:** `offline_queue` -> `psramFound() ? heap_caps_malloc(...,SPIRAM) : static_fallback` (latency-tolerant, drains at BLE-notify speed); JSON/MQTT scratch >4KB lands in PSRAM once heap-allocated. `ring_` is *deleted*, not relocated.
- **Right-sizing, no toolchain change:** lazy `new` broker slots only when enabled; drop resident `jwt_token[512]` (re-minted live).
- **Requires toolchain swap (#87, DEFERRED):** static-BSS-in-PSRAM + TLS-buffers-in-PSRAM are frozen in prebuilt `espressif32@6.11.0`; the only path is migrating V4 esp32s3 to **pioarduino** (unifying with the existing `[esp32c6_base]` line) or a self-built lib-builder. This is the gate on whether V4 wss is *unconditionally* sustainable; the handshake gate is the bridge until then.

`EXT_RAM_ATTR` is never used after this (inert + misleading). A per-build DRAM-budget ledger replaces the vanity percentage and is checked in CI.

### 2e. Broker-down / publish-failure semantics (GAP 2 -- the product's core loop under its commonest condition)

The product is "RX -> publish." Public brokers are flaky, so "publish is down" is the *normal* condition, and it was undefined across all six lenses. Defined now:

- **Different queues, not conflated:** `offline_queue` is the **BLE companion** message store and has nothing to do with MQTT. There is **no MQTT-side store-and-forward** today, and adding an unbounded one is the wrong default for a no-operator box (it grows until OOM).
- **Policy: best-effort with bounded drop.** Observations publish to each `Up` broker; for `Down`/`Backoff` brokers the observation is **dropped, counted, and the drop counter is exposed** via the (now-wired, see Step 1) status publish on whichever brokers *are* up. No silent loss; no unbounded growth. A small optional bounded ring (e.g. last-N, PSRAM-backed on V4) MAY buffer across short outages, but it is *capped and lossy by design*, never a guarantee.
- **Per-broker independence:** one broker down never blocks publish to the others (already true via per-broker state; made explicit and tested).
- **The 6-hour-handed-to-someone case:** the box keeps RXing and keeps trying brokers on the backoff schedule; observations during a full outage are dropped-and-counted, not stored. When any broker recovers, fresh observations flow and the status publish reports the outage duration + drop count. This is the deliberate trade for a zero-operator, bounded-memory device.

### 2f. Keep / Cut / Defer

| Decision | Items |
|---|---|
| **KEEP (the real product)** | `WifiBootstrap` (STA connect); `MqttBroker`/`Pool`/`Auth`/`JwtHelper`/`ConfigSchema` (publish engine, 6 brokers frozen per user); `ObserverPipeline` **`/raw` path only** (`logRxRaw -> publishRawFromBytes`); `cliPassthroughExecute`/`dispatchObserverCli` (the one clean layer); the config CLI surface (unified per 2a). |
| **CUT NOW (dead / scaffolding)** | `ObserverPipeline::ring_[50]` + `recent()`/`recentCount()` (verified zero consumers, 13KB, hot-path memcpy); `publishPacket`/`buildPacketJson` (no producer hook); resident `jwt_token[512]`; cross-branch tombstones (`set web.allow_initial`, `web`/`ota.` deny-refs, `WifiBootstrapState::ApMode`->`AwaitingSetup`, `CROSSWIRE_AP_SSID_PREFIX`/`deriveApSsid`, unreachable `kCombinedPem`). **WIRE, don't cut:** the `/status` vertical -- one `wifiObserverSetStatusSnapshot()` call from `MyMesh::loop` -- it is the carrier for the GAP-2 drop-counter/outage telemetry, so it earns its keep. |
| **DEFER (explicit owner decisions)** | OTA push/ElegantOTA (end-state is OTA-*pull*, #227 -- always intermediate); parsed `/packets` topic (needs post-Packet-construction hook); Web Console (lands with its stack; then wire certs correctly: LetsMesh->WE1); **toolchain swap #87**; **multi-broker wss on V3/XIAO** (requires #87 -- recorded here, not buried); demote `CrashLog` (538 lines, largest file) to a `-D CROSSWIRE_DIAG` gate keeping only reset-reason + a small panic ring in production. |

---

## 3. REMEDIATION SEQUENCE

Each step is a self-contained, revertible PR. Ordering is corrected from the synthesis so that **(a) the cheap security fix lands first and reaches fielded devices via migration, (b) `Session` isolation precedes multi-ingress, and (c) heap headroom is bought before the structural refactors.** 325/326/327 fold in as noted.

**Step 0 -- Land #327 as a structural start<->stop pairing (NOT the "let esp_mqtt auto-reconnect" option).**
*Change:* in `MqttBroker::onDisconnected`, call `esp_mqtt_client_stop(client_)` before transitioning to `Backoff`, pairing it with the `tryConnect` `esp_mqtt_client_start` at line 192. **Reject the synthesis's "cleaner" option (b)** -- handing socket lifecycle to esp_mqtt's internal auto-reconnect would bypass the JWT re-mint at lines 214-235, which only runs while `state==Up` and pushes the new token "for the next reconnect cycle." Auto-reconnect would silently reconnect with a stale credential after a long disconnect. Keep the manual loop; just stop the client on the disconnect edge so the start is paired. *Risk/revert:* one file, behavior-observable (heap stops bleeding on the 5/15/30s cadence); revert is a one-line removal. *Unblocks:* a stable heap baseline to measure every later step against. **#327 folds in here in full.**

**Step 1 -- Cheap security fix + dead-weight deletion + wire `/status` (pure subtraction + 2 intercepts).**
*Change:* (a) **Security:** intercept `CMD_GET_CHANNEL` for slot 40 (never return the PSK) and exclude slot 40 from `searchChannelsByHash` (control key leaves the OTA decrypt loop) -- fixes the severe security holes *without* touching onboarding. (b) **Delete:** `ObserverPipeline::ring_` + accessors (13KB), `publishPacket`/`buildPacketJson`, resident `jwt_token[512]`, the cross-branch tombstones. (c) **Wire:** add the single `wifiObserverSetStatusSnapshot()` call from `MyMesh::loop` so `/status` becomes the carrier for 2e telemetry. *Risk/revert:* deletions are verified-unused (`recentCount`/`setStatusSnapshot` have no callers -- confirmed); each independently revertible; the two intercepts are localized and testable against a known advert. *Unblocks:* honest surface + immediate hot-path heap relief + the security holes closed on *new* builds (fielded devices handled in Step 6's migration).

**Step 2 -- Fix the TLS cert lookup (add the WE1 PEM; do not just remap).**
*Change:* the current `lookupCaCertPem` maps `letsencrypt`/`eastmesh`/`isrg-x1` ALL to `kEastmeshIsrgRootX1Pem` (ISRG Root X1). Per the broker-config record, LetsMesh-US and EastMesh present **Google Trust Services WE1**, not ISRG. So **add a `kGtsWe1Pem` to `MqttCaCerts.h`** and map the LetsMesh/EastMesh broker names to it; keep ISRG only for brokers that actually use Let's Encrypt. *Risk/revert:* one PEM + lookup-table edit; makes broken wss defaults verify. *Unblocks:* real wss publishing to the seeded brokers. *(Note: this is more than the synthesis's "remap" -- the WE1 PEM does not yet exist in the tree.)*

**Step 3 -- Relocate `offline_queue` to PSRAM (V4 44KB DRAM; no toolchain change).**
*Change:* `psramFound() ? heap_caps_malloc(...,SPIRAM) : static fallback`. *Risk/revert:* latency-tolerant queue; fallback preserves no-PSRAM boards exactly as today; revert restores the static array. *Unblocks:* the single largest DRAM reclaim available without a platform swap -- buys V4 headroom for the always-linked TLS+BLE+WiFi stack before the bus refactor. **(Ledger note: this is a V4-only win; V3/XIAO get nothing here -- see 2d.)**

**Step 4 -- Extract per-peer state into `Session` (corruption-safety BEFORE concurrency).**
*Change:* introduce a `Session` struct keyed by `conn_handle`; move `_iter*`, `pending_*`, `app_target_ver`, and the per-exchange `cmd_frame`/`out_frame` into it; `handleCmdFrame -> handle(req, Session&)`. With only one transport live today this is behavior-preserving (single session), but it establishes the isolation that makes Step 5's second ingress safe. *Risk/revert:* mechanical field-relocation; the init-order contract (Session + `app_target_ver` initialized before first frame -- `app_target_ver` is read at `MyMesh.cpp:442/547`, set at `1007`, reset at `863`) is part of this step's acceptance test. *Unblocks:* safe multi-ingress (Step 5) and concurrent transports (Step 7).

**Step 5 -- Introduce `commandBusExecute` with caller-provided buffers (collapses 3 ingress clones; supersedes #325's shape).**
*Change:* one bus owning normalize/authorize/dispatch, **writing only into the caller's `reply`/`reply_cap`** (GAP 6), returning `redact_input` + `requires_reboot` + `truncated`. Re-point the USB reader (#325), the BLE intercept, and CommonCLI at it. Delete `obsCliIsPwdSetPrefix` and its byte-identical-mirror contract. The repeater path passes its real ~161-byte cap and gets `truncated`, never a stack smash. *Risk/revert:* the dispatcher is already transport-agnostic -- this consolidates callers, not logic; the reply-size lie and the path-4 ungated hole both close (LoRa adapter passes `auth=Remote`, denied `wifi.pwd`). Safe because Step 4 already isolated per-peer state. **#325 folds in here** (thin adapter). **#326 folds in here** (`requires_reboot` is a uniform bus hint, not per-surface rediscovery). *Unblocks:* the first-class BLE control opcode (Step 6).

**Step 6 -- First-class BLE control opcode + mesh-channel revert + NVS migration (gated on the provisioning matrix).**
*Change:* the bus's BLE adapter uses a dedicated control opcode instead of slot 40. Delete `SystemChannelCli` (337 lines), `postSystemChannelText`'s forged `RESP_CODE_CHANNEL_MSG_RECV_V3` frame, the four magic-40 comparisons, `systemChannelAllowSet`, `MAX_GROUP_CHANNELS`->40, the macro hack + `-Wno-builtin-macro-redefined` in all three envs. **NVS migration (GAP 3):** because fielded devices have slot 40 persisted in `/channels2`, deleting the init path alone would leave a ghost channel that `searchChannelsByHash` still walks. Ship a one-shot boot migration that detects a persisted slot-40 entry and rewrites `/channels2` to clear it -- so the security fix actually reaches provisioned hardware, not just fresh flashes. **Gate:** this step does not land until the provisioning matrix (4) confirms a no-custom-software setup path exists for every shipped variant; until then the read-only-secret-safe `_sys` *rendering* shim from Step 1 stays. *Risk/revert:* the new opcode and old `_sys` path coexist behind a flag during transition; migration is idempotent and one-shot. *Unblocks:* clean mesh-channel concept + removal of NVS double-storage.

**Step 7 -- `TransportRouter` + composition-based observer env + handshake admission gate.**
*Change:* `MyMesh` holds a router with a transport *list*; ~60 `_serial->writeFrame` sites route through `session.reply`. Replace `extends _ble` with an `[observer_mixin]` flag/src-filter fragment applied onto a transport base of choice (`_ble`, `_wifi`, or headless `_none`); set `MAX_CONTACTS`/`MAX_GROUP_CHANNELS`/`OFFLINE_QUEUE_SIZE` to observer minima. **Add the handshake admission gate (GAP 1):** `MqttBrokerPool::loop` admits at most K brokers to `Connecting` simultaneously (K=1 default), bounding peak mbedTLS-internal DRAM to ~Kx40KB regardless of broker count. **Headless `_none` smoke-test gate (GAP 3):** prove `setup()` completes with no BLE/`_serial` transport present before shipping the variant. *Risk/revert:* largest refactor, so it lands last on a de-risked, heap-relieved base; can land transport-by-transport; the admission gate is a small loop guard, revertible. *Unblocks:* the actual hand-off box -- a headless observer that doesn't pay the NimBLE DRAM floor and can't OOM on a thundering-herd reconnect. This is the move that reclaims the most heap; everything before it is rounding error next to "stop inheriting the messenger."

**Deferred -- Step 8: migrate V4 esp32s3 to pioarduino (#87)** for unconditional TLS-in-PSRAM + BSS-in-PSRAM. Gate on whether V4 wss is the *committed* target beyond what the Step 7 admission gate already makes survivable. This is also the prerequisite for **multi-broker wss on V3/XIAO** (2d/2f).

---

## 4. OPEN QUESTIONS / OWNER DECISIONS

1. **Provisioning matrix -- needs an owner ruling before Step 6 can delete the stock-app path.** State the accepted setup path per shipped variant. Step 6 cannot proceed until at least one no-custom-software path exists per variant:

   | Build variant | WiFi-creds provisioning after Step 6 | No custom software? |
   |---|---|---|
   | `_ble` + observer | dedicated BLE control opcode -> **needs a custom driver/tool** (the stock-app path is gone) | **NO -- needs the companion tool built, or keep the read-only-safe `_sys` shim** |
   | headless `_none` + observer | USB serial CLI (#325 adapter) | NO -- requires USB + serial CLI |
   | future Web | browser form over AP/STA | YES |
   *Decision needed:* (a) build a minimal BLE control-opcode tool, OR (b) keep the secret-safe `_sys` rendering shim indefinitely as the stock-app onboarding path, OR (c) accept USB-only provisioning for headless and ship the BLE tool later. This is the single decision gating Step 6.

2. **V4 wss commitment (#87).** Is multi-broker wss-on-V4 a *committed* target, or is the Step-7 handshake admission gate (K=1, bounded peak) sufficient? If committed -> schedule #87. If "best-effort wss is fine" -> the admission gate alone may suffice and #87 stays deferred.

3. **V3/XIAO TLS scope.** Confirm the 2d ruling: V3/XIAO ship as **plaintext or single-broker-TLS** observers now; **multi-broker wss waits for #87.** Owner sign-off that this is acceptable for those boards.

4. **MQTT outage policy (2e).** Confirm "best-effort, bounded-drop, counted, no unbounded store-and-forward" is the desired behavior for the zero-operator box, vs. wanting a capped PSRAM-backed short-outage buffer on V4.

5. **`CrashLog` demotion.** OK to gate the 538-line `CrashLog` behind `-D CROSSWIRE_DIAG`, keeping only reset-reason + a small panic ring in production builds?

6. **Handshake admission K.** K=1 (most conservative, slowest broker convergence) vs K=2-3 (faster, higher peak). Recommend K=1 until measured.

---

## 5. HOW THIS DESIGN AVOIDS RE-NARROWING

- **No per-transport duplication.** The entire failure family (four cloned CLI doors, the byte-identical redaction mirror, the path-4 ungated door, the forged channel-recv frame) is replaced by *one* `commandBusExecute` with *thin* adapters. The bus is introduced (Step 5) only *after* per-peer `Session` isolation (Step 4), so consolidation never opens a corruption window -- the precise re-narrowing the critique caught in the synthesis's ordering. New operator surfaces are adapters, never new readers.
- **No concept overload.** Three first-class concepts replace impersonation: a **command bus** (was `_serial`), a **control opcode** (was a chat channel), and a **composed observer** (was inheritance). The control plane stops pretending to be a transport *and* stops pretending to be a data structure. `MAX_GROUP_CHANNELS` returns to 40 and the macro hack, the lock-indirection, the magic-40 comparisons, and the self-diagnosing printf all disappear -- fewer concepts, not more.
- **No symptom-patching.** #327 is fixed as a *structural* start<->stop pairing (and the tempting "auto-reconnect" shortcut is *rejected* because it would break the JWT design -- a symptom-patch masquerading as architecture). #325/#326 fold into the bus rather than shipping as standalone readers/flags. The security fix is *separated* from the UI deletion (the synthesis wrongly fused them): the leak is closed in Step 1 by two intercepts + a decrypt-loop exclusion + an NVS migration, independent of whether the stock-app UI is ever removed.
- **Whole-system invariants are owned, not boxed.** A per-board DRAM ledger proves boot and names the peak-driven handshake transient (with the admission gate that bounds it); broker-down behavior is defined as the product's core loop; boot/init order and NVS migration carry the security fix to fielded devices; the provisioning matrix owns the recipient's experience, not just the engineer's mental model.
- **The plan favors deletion over reshuffling.** The largest single move (Step 7, compose-not-inherit) *removes* `MAX_CONTACTS=350`, the bonding/contact/ACL machinery, the messenger UITask, and -- on the headless variant -- the entire NimBLE DRAM floor. Net direction is subtraction: 13KB ring deleted, 44KB queue relocated out of DRAM, jwt/pool right-sized, 337-line `SystemChannelCli` + forged frame deleted, the redaction mirror collapsed to one site, the inherited messenger weight shed. The through-line stays one sentence: **the firmware had no first-class transport-neutral command/control concept, so the control plane impersonated a transport then a data structure and the product inherited an entire messenger -- and on this frozen memory model that inherited weight was paid in the one currency (internal DRAM) the device cannot spare.**

**Verified facts of record (canonical `meshcore-firmware`, not worktrees):** #327 leak -- `MqttBroker.cpp:336-343` (`onDisconnected`->Backoff, no `esp_mqtt_client_stop`) vs `:192` (`esp_mqtt_client_start`), comment `:337`. JWT re-mint seam -- `:214-235` (only while `Up`; "next reconnect cycle" `:213`). Cert mismatch -- `lookupCaCertPem` `:35-46` maps all names to ISRG `kEastmeshIsrgRootX1Pem`; broker record requires GTS WE1 (PEM absent -> Step 2 must add it). Dead ring -- `ObserverPipeline.{h:49-54,cpp:26-47}`, zero external callers. Dead status -- `WifiObserver.cpp:129` `wifiObserverSetStatusSnapshot`, zero callers. Per-peer global -- `app_target_ver` `MyMesh.cpp:442/547/863/1007`. Slot-40 boot persist -- `saveChannels()` `MyMesh.cpp:1695/2004`.
