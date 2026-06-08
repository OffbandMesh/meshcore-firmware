<!-- DRAFT for owner review. Task Strycher/Crosswire#32, Epic #31, Feature #30. Author: CyanGate (claude-code). First pass: research + draft. Not a committed design-of-record until human sign-off (Epic #31 acceptance gate). -->

# Position-to-Map Telemetry Pipeline -- Draft Architecture

**Status:** DRAFT, first pass. Not yet design-of-record.
**Feature:** Strycher/Crosswire#30 -- **Epic:** #31 -- **Task:** #32
**Scope:** Evaluate architecture and means. Hardware selection (antennas, enclosures, exact boards) is explicitly out of scope.

---

## 1. Problem

Get a person's GPS position from a Crosswire mesh device back to a map at a "core" server: simply, reproducibly, and without ATAK-level complexity.

**Motivating scenario.** A Crosswire repeater/observer sits somewhere with internet (a vehicle parked at a trailhead with cell coverage). People roam out on trails carrying Crosswire GPS devices (T1000-E, WisMesh Tag) that can reach that node over LoRa. Someone is injured and needs to be located. Their position must ride the mesh to the internet-connected node, go out over MQTT to the core, and render on a map.

**Second path.** When a person is fully outside MeshCore RF range (e.g., at a workplace, or beyond the last repeater), they can still get their position to the core over phone-hotspot internet.

**Design values:** built on Crosswire; reproducible by others; not tied to any single operator's gear; "ATAK result without ATAK."

---

## 2. Ground truth (what already exists in Crosswire)

Sourced from `docs/architecture/2026-06-01-observer-architecture-review.md` and `README.md`. **Verify against current source before implementation; this is a research-pass reading, not a line-audit.**

| Capability | State | Implication for this pipeline |
|---|---|---|
| Observer republishes **raw LoRa frames** to MQTT (`logRxRaw -> publishRawFromBytes`, the kept `/raw` path) | EXISTS | Layer 2 (RF -> internet) substantially exists, but emits **raw bytes**, not decoded position. |
| Parsed `/packets` topic (`publishPacket`/`buildPacketJson`) | DEFERRED / dead (no producer hook) | On-firmware position decode is explicitly deferred; the DRAM-starved ESP32-S3 is the wrong place to parse. |
| MQTT broker pool (6 brokers, wss+JWT, Mosquitto, ConfigSchema, JwtHelper) | EXISTS | Transport to a broker is solved. Plaintext-MQTT and single-broker-TLS work today; multi-broker wss has heap caveats (see review 2d). |
| `/status` telemetry (drop counters, outage duration) | being WIRED (review Step 1) | A health channel exists for "is the gateway alive / how lossy." |
| Broker-down policy: best-effort, bounded-drop, counted, no store-and-forward | DEFINED | A full-outage window drops-and-counts rather than buffering. Matters for "find me during an outage." |
| Memory model: everything static/small-heap; internal DRAM (~320KB) is the binding constraint | KNOWN | Strong argument to keep the firmware dumb and push decode/state to the core. |
| Tracker variants `t1000-e`, `rak_wismesh_tag` build in Crosswire | EXIST | The trackers are in-tree targets; position-emission behavior can be added to them. |

**The single most important consequence:** the cheapest, most memory-safe pipeline keeps the observer as a dumb raw-frame republisher (which exists) and **decodes position at the core**, not on the firmware.

---

## 3. The pipeline (5 layers)

```
[Tracker GPS]                 [Parked node]            [Core server]
 T1000-E / WisTag              Crosswire observer       broker + decoder + map
      |                             |                        |
  (L1) emit position    --LoRa-->  (L2) hear + republish   (L3) MQTT   (L4) decode + plot
  into the mesh                     raw frame to MQTT  --------------->  on a map
                                                                          ^
 [Off-mesh device] --BLE--> [phone w/ hotspot] --(L5) publish position over IP--+
```

| Layer | What it does | Exists vs net-new |
|---|---|---|
| **L1 Position emission** | Tracker pushes its GPS position into the mesh (interval + SOS button + move-triggered) | **NET-NEW** firmware behavior on the tracker variants |
| **L2 RF -> internet ingest** | Parked observer hears the position traffic and republishes to MQTT | **EXISTS** (raw-frame republish); needs only that L1 traffic is something it already forwards |
| **L3 Transport** | MQTT (optionally TLS) from observer to the core's broker | **EXISTS** (broker pool) + **config** (which broker, reachability) |
| **L4 Core + decode + map** | Subscribe, decode position out of the frames, plot on a map | **NET-NEW** (core-side service + map), but off-firmware and unconstrained |
| **L5 Off-mesh bridge** | Device beyond RF range publishes its position to the same broker over phone internet | **NET-NEW** integration; MQTT-centric = "just another publisher"; Reticulum-centric = intrinsic |

---

## 4. Key design decision: where does position get decoded?

This is the decision that most shapes the build. Three places to turn a LoRa position payload into map-ready `{node, lat, lon, t}`:

| Option | Pros | Cons | Verdict |
|---|---|---|---|
| **A. On the tracker** -- emit a structured, easily-parsed position payload into the mesh | Simple for the observer/core; self-describing | Tracker firmware work; payload format becomes a wire contract | Needed regardless: the tracker MUST emit *something* (L1) |
| **B. On the observer** -- parse frames, publish decoded JSON (`/packets`) | Clean MQTT for the core | Re-opens the explicitly-deferred parse path; heap cost on the worst-constrained board | **Avoid for v1** (fights the memory model) |
| **C. At the core** -- observer stays dumb (`/raw`), core decodes | Zero added firmware heap; reuses what exists; decoder iterates freely on a real computer | Core must understand the MeshCore/Crosswire packet + the L1 payload format | **Recommended for v1** |

**Recommendation: A + C.** The tracker emits a position payload into the mesh (L1, unavoidable). The observer republishes raw (L2, exists). The **core decodes** (L4). This sidesteps the firmware heap problem entirely and lets the map/decoder evolve without reflashing anything.

Open sub-question (4a): **what carries the position in the mesh?** Candidates -- a normal MeshCore position/telemetry payload on a known channel, or a channel text message in a documented mini-format. Whichever the observer already forwards raw with least friction wins. Needs a source-level confirmation of what the observer's `/raw` path captures and what the tracker firmware can emit. (Recorded, not answered this pass.)

---

## 5. Top-level architecture fork: MQTT-centric vs Reticulum-centric

| | **MQTT-centric (recommended start)** | **Reticulum-centric (future upgrade)** |
|---|---|---|
| Core idea | One broker; everything publishes to it | Transport-agnostic encrypted routing to a core destination |
| L2 ingest | Reuses existing Crosswire observer MQTT | Observer would speak RNS, or bridge to it |
| L5 off-mesh bridge | Phone is "just another publisher" to the same broker | Intrinsic: RNS rides LoRa when meshed, IP when on hotspot, same destination |
| Encryption | Per-broker TLS + payload/channel crypto; broker sees metadata | End-to-end by design |
| Cost | Lowest; mostly reuses what exists | A second stack alongside MQTT; more moving parts |
| Maturity in Crosswire | Active observer/repeater MQTT roles | Not present today |

**Recommendation:** build **MQTT-centric** now (fastest to a working "find me on a map", maximal reuse). Keep **Reticulum** as a named upgrade path specifically for L5 (its transport-agnostic, e2e property is the cleanest answer to "ride the mesh, fall back to IP"). Designing L4's decoder to read from a normalized internal position event (not raw-MQTT-specific) keeps the door open to swapping transports later.

---

## 6. Component interfaces (first sketch)

Define these as contracts so each layer is independently buildable/testable.

- **L1 mesh position payload (wire contract):** `{node_id, lat, lon, alt?, t, fix_quality?, batt?, flags(SOS/move/interval)}`. Exact encoding TBD in 4a. This is the one cross-layer contract that must be stable.
- **L3 MQTT topic schema:** reuse the observer's existing raw topic for v1 (core decodes). A future parsed topic (e.g. `crosswire/<gw>/pos/<node>`) is a v1.1 enhancement once decode is proven at the core. Adopting the de-facto `meshcore/{IATA}/{pubkey}/...` shape where compatible keeps third-party analyzers usable.
- **L4 core decoder contract:** input = raw frames (or parsed pos topic); output = normalized position event `{node, lat, lon, t, src_gateway, rssi?, snr?}` written to whatever the map consumes.
- **L4 map consumer:** pluggable. Candidates -- Node-RED `worldmap` node (MQTT-in -> map, lowest effort), a small Leaflet page fed by the decoder, or an existing self-hosted map. Vendor-neutral; the operator picks.

---

## 7. Security (first pass)

- Position is sensitive (it is literally "where this person is"). Channel/payload crypto on the mesh and TLS on L3 are both in scope.
- Do NOT reuse the slot-40 `_sys` control-channel key material; the observer review (Root Fault B) documents that as derivable-from-advert. Position traffic must ride a properly-keyed channel.
- No PSK / broker creds in repo, logs, issues, or chat (Crosswire CLAUDE.md). Per-host gitignored config only.
- L5 over public internet: the phone-to-broker hop needs authenticated TLS; a compromised broker should not be able to spoof positions (consider signed position payloads -- candidate reason to prefer Reticulum's e2e for L5 later).

---

## 8. Open questions / owner decisions (recorded, not all answered this pass)

1. **L1 payload format (4a).** Native MeshCore position/telemetry payload vs documented channel-text mini-format. Gated on a source-level check of what the observer `/raw` path captures and what tracker firmware can emit.
2. **Decode location.** Confirm the recommended **core-side decode (Option C)** vs observer-side parse. (Recommendation: core-side.)
3. **Architecture fork.** Confirm **MQTT-centric for v1**, Reticulum as L5 upgrade path. (Recommendation: yes.)
4. **Map consumer.** Node-RED worldmap vs Leaflet vs existing map. (Recommendation: Node-RED worldmap for fastest working result; revisit.)
5. **Broker location/reachability.** Self-hosted-at-home broker reached via VPN/tunnel (no port-forward) vs a broker both sides reach. Affects L3 + L5.
6. **Outage behavior for "find me."** The observer's best-effort/bounded-drop policy means positions during a full broker outage are dropped-and-counted, not stored. Is a capped short-outage buffer wanted for this safety use case, or is dropped-and-counted acceptable?
7. **Cadence vs battery.** Position interval, move-trigger threshold, and SOS behavior on T1000-E / WisMesh Tag drive battery life. Needs a target (e.g., normal 5-10 min interval, SOS = faster).
8. **TLS scope per board.** Per the observer review, multi-broker wss on no-PSRAM boards (V3/XIAO) waits on toolchain swap #87; plaintext/single-broker-TLS work now. The parked observer's board choice interacts with this.

---

## 9. Indicative build sequence (NOT a commitment; for shape only)

1. Confirm L1 payload format + decode location (Q1, Q2) by reading observer `/raw` + tracker variant source.
2. Stand up the core: broker + a minimal decoder that turns raw frames into normalized position events + a map (Node-RED worldmap).
3. Add L1 position emission to one tracker variant (interval first; SOS + move-trigger next).
4. End-to-end bench test: tracker -> observer -> broker -> map.
5. Add L5 phone-hotspot publisher to the same broker.
6. (Later) Evaluate Reticulum for L5; evaluate parsed `/packets` topic if core-side decode proves limiting.

Each becomes its own epic/task with its own approval gate. This draft only establishes the architecture and the recommended forks.

---

## 10. Cross-references

- Observer internals: `docs/architecture/2026-06-01-observer-architecture-review.md`
- Reference gateway (Meshtastic-style, infra-advert oriented): jmead/Meshcore-Repeater-MQTT-Gateway
- MeshCore observer ecosystem + de-facto topic schema: external survey (EastMesh, MeshCoreTel, FreeKopcap analyzer)
- Toolchain/heap gate for multi-broker wss: Strycher/LoRa#87
