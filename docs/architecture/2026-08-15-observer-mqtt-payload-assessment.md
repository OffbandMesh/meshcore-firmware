# Observer MQTT payload assessment — raw + metadata vs. parsed fields

**Date:** 2026-08-15
**Status:** assessment. One decision is BLOCKED on a third party; the unblocked half already shipped.
**Issues:** #726 (the bug that started it), #727 (this assessment / proposed change)
**Prompted by:** a field report on `offband-v1.5.0-beta1`

---

## 1. Why this exists

A tester on `Xiao_S3_WIO_companion_observer_wifi` reported:

> the observer seen the first message on corescope but didn't with the second or third … the companion side of it seen all three messages

His broker slot 0 is `mqtt://mqtt1.okimesh.org:1883` — **tcp, always-on, exempt from TLS
rotation**. That single fact killed two plausible explanations and led to the real one.

Chasing it exposed a silent publish ceiling (#726), and then a broader question the owner
asked directly: *if CoreScope decodes the raw packet itself, why are we decoding on-device
at all?*

---

## 2. What an observer actually publishes

Two topics, and they are **not** equivalent. This surprised both the firmware and
CoreScope sides.

| Path | Topic | hex under key | Buffered? |
|---|---|---|---|
| `publishRawFromBytes()` | `<prefix>/<iata>/<id>/raw` | **`data`** | no — direct to `Up` brokers only |
| `publishParsedPacket()` → `publishPacket()` | `<prefix>/<iata>/<id>/packets` | **`raw`** | **yes — through `MqttRingLog`** |

`/packets` body (`buildPacketJson`, `MqttPayload.cpp`):

```json
{"origin":"..","origin_id":"..","timestamp":"..","type":"PACKET","direction":"rx",
 "time":"..","date":"..","len":"..","packet_type":"..","route":"F","payload_len":"..",
 "raw":"<whole packet in hex>","SNR":"..","RSSI":"..","score":"..","duration":"..",
 "hash":"<8-byte sha256 prefix>","path":"path_NxN_Nb"}
```

---

## 3. The bug that started it (#726 — FIXED, shipped separately)

`publishParsedPacket()` builds into `char json[1024]`; `MqttRingLog::append()` rejected
anything over `MQTT_RING_MSG_MAX` (512). Refused appends returned 0 with **no log and no
counter** — `droppedCount()` tracks cursor overrun at `commit()`, not a refused append.

Because the JSON embeds `"raw":"<whole packet hex>"`, with ~316 B of other fields:

```
 81 B packet -> JSON 478 B  OK
100 B packet -> JSON 516 B  DROPPED
255 B packet -> JSON 826 B  DROPPED   (max MeshCore packet)
```

**Every packet over ~98 bytes was silently discarded**, transport-independent — the drop
is upstream of broker fan-out, which is exactly why a plaintext always-on slot 0 gave no
protection.

Introduced by #175 (`62ad4439` added the ring at 512 while the builder already used 1024).
**Not** a regression from the #708/#710/#723 rotation work.

Fixed in #726: `MQTT_RING_MSG_MAX` 512 → 1024, `MQTT_RING_SLOTS` 32 → 20 to pay for it,
and refused appends are now counted (`rejectedCount()`) and surfaced on the `[ring]` line
as `rej=`, separate from `drops=`.

**Cost:** the ring went from 16,384 B to 20,480 B. Net savings from the #701 heap trim
fell from 14,660 B to roughly 2,250 B. That is what motivates §5.

---

## 4. Findings from the CoreScope side (verified, with references)

Answers below are from the CoreScope maintainer session, reading `OKI-Mesh/CoreScope`
directly. Reproduced faithfully, including the one caveat they raised themselves.

### 4.1 CoreScope decodes `raw` itself and ignores our parsed fields — CONFIRMED

`cmd/ingestor/main.go:650` → `DecodePacket`; `main.go:653` / `decoder.go:932`.
A whole-repo grep for `packet_type`, `payload_len`, `route`, `path`, `hash`, `len`,
`time`, `date` **as message keys returns zero hits**. `RouteType` / `PayloadType` /
`PathJSON` are re-derived from raw.

### 4.2 `/raw` is dead weight — CONFIRMED

CoreScope subscribes `meshcore/#` (`main.go:150`) so it *receives* `/raw`, but the entire
packet branch is gated on `rawHex, _ := msg["raw"]; if rawHex != ""` (`main.go:650-651`).
Our `/raw` puts hex under `data`; `msg["data"]` is read nowhere. **`/raw` messages fall
through unused.** We publish every packet twice and CoreScope consumes one.

### 4.3 Our on-device hash is unused — CONFIRMED

`msg["hash"]` is read nowhere. Dedup/identity is CoreScope's own
`ComputeContentHash(msg.Raw)` (`db.go:1712`, `decoder.go:1064`). The SHA-256 prefix we
compute per packet is discarded.

### 4.4 Fields CoreScope genuinely needs

From a `/packets`-shaped message it reads:

| Key | Notes |
|---|---|
| `raw` | **required** — the gate. Must be named `raw`, not `data` |
| `timestamp` | feeds rxTime. `time` / `date` are ignored |
| `SNR` / `snr` | |
| `RSSI` / `rssi` | |
| `score` / `Score` | |
| `direction` / `Direction` | optional |
| `origin` | |
| `region` | optional |

**Observer identity comes from the topic segment `parts[2]` (`main.go:684`), not
`origin_id`.** `duration` and `origin_id` are not read at all.

`model` / `firmware_version` / `client_version` / `radio` / `stats` arrive on the
`/status` topic via `extractObserverMeta` (`main.go:602-629`); on `/packets` they are
ignored.

### 4.5 Nothing downstream depends on the parsed fields — CONFIRMED, with a caveat

`BuildPacketData` is the sole write path from an observer message into the DB. Analytics,
neighbor-graph, sub-paths, replay/VCR and audio-lab all read DB rows populated from
CoreScope's own decode plus our metadata. `path_json` is CoreScope-computed from raw, not
our `path`.

> **Caveat, stated by the CoreScope session and NOT independently verified here:** they
> read the ingest path exhaustively but *inferred* that VCR / audio-lab / analytics operate
> on stored DB rows from the architecture, rather than reading each module line-by-line.
> They offered to trace those directly if we need it airtight.

Since "nothing downstream depends on these fields" is the entire basis for removing them,
**that trace should be requested before any field is dropped.**

---

## 5. Our claim: none of the metadata requires a decode

Traced in firmware (`src/Dispatcher.cpp:207-208`):

```cpp
score    = _radio->packetScore(_radio->getLastSNR(), len);   // SNR + LENGTH only
air_time = _radio->getEstAirtimeFor(len);                    // LENGTH only
```

`packetScore(float snr, int packet_len)` takes a **length**, not a packet.

| Field | Source | Needs decode? |
|---|---|---|
| RSSI, SNR | radio driver | no |
| score | `f(snr, len)` | no |
| duration | `f(len)` | no |
| origin, timestamp | observer identity / clock | no |
| region (iata) | observer config | no |

**The on-device parse produces only fields CoreScope ignores.** For that consumer it is
pure overhead — and it is the overhead that created the #726 ceiling.

---

## 6. What slimming would buy

Removable for CoreScope: `packet_type`, `payload_len`, `route`, `path`, `hash`, `len`,
`time`, `date`, `duration`, `origin_id` — roughly **197 B per message**.

```
current non-raw overhead : ~316 B  -> ceiling ~98 B of packet at MSG_MAX 512
slimmed overhead         : ~119 B  -> max 255 B packet builds a ~629 B body

MSG_MAX 768 x 24 slots = 18,432 B   (vs today's 20 x 1024 = 20,480 B)
   -> smaller ring, MORE depth, no ceiling
```

Slimming would return the memory #726 spent **and** increase ring depth past where it
started. Dropping the unread `/raw` publish would also halve observer MQTT publish volume.

---

## 7. Consumer set — this is the blocker

`/packets` is a **public** payload. Other consumers may parse what CoreScope ignores.

| Consumer | Status |
|---|---|
| **CoreScope (OKI)** | verified, §4. Loses nothing |
| **MeshMapper** | **not a consumer in practice.** OKI publishes to MeshMapper directly from mqtt2; observer-side publishing is discouraged and the seeded slot is disabled by default |
| **Eastmesh.au** | **same operator as CoreComms** — `map.eastme.sh` 301-redirects to `us-east.corecomms.net`. One party, not two (consistent with #677's rebrand) |
| **CoreComms.net** | **UNKNOWN — the blocker** |

### What was attempted on CoreComms, and why it did not settle it

CoreScope's `/api/health` returns a distinctive JSON fingerprint
(`{"engine":"go","version":"edge","commit":"..."}`), so a stock instance is identifiable
from outside, and the commit hash would reveal a fork.

- `map.corecomms.net`, `us-east.corecomms.net` → HTTP 200, but `/api/health` and
  `/api/stats` both return the **SPA HTML shell, not JSON**.
- `eastmesh.au`, `map.eastmesh.au` → **HTTP 403** (bot protection). Probed once with plain
  `curl` and default user-agent; **no attempt was made to work around the block**, by
  design.

The HTML response is consistent with three different worlds — API not mounted at that
path, API on another host, or not CoreScope at all — and they cannot be distinguished from
outside. The owner's expectation is that CoreComms runs CoreScope with modifications.

**Owner ruling (2026-08-15): we do not drop anything CoreComms might parse without
absolute confirmation from their documentation or a direct answer.** Fingerprinting is not
confirmation.

---

## 8. Decision

**BLOCKED.** No payload field is removed until CoreComms confirms.

The question for them is narrow:

> Do you parse `packet_type` / `payload_len` / `route` / `path` / `hash` / `len` / `time` /
> `date` / `duration` / `origin_id` from observer `/packets` messages, or do you decode
> `raw` yourself?

### Sequenced

1. **Done, shipped** — #726 raises the ring limit so nothing is silently dropped today.
2. **Ask CoreComms.** One answer closes the contract question.
3. **Ask CoreScope to trace VCR / audio-lab / analytics directly** (§4.5 caveat), so
   "nothing downstream depends on these" is verified rather than inferred.
4. **Only then**, if both clear: slim `/packets`, re-tune `MQTT_RING_MSG_MAX` / `SLOTS`,
   and decide whether `/raw` is dropped or repaired to use the `raw` key.

### Constraints that survive regardless

- Hex stays under the key **`raw`**. A raw-only topic naming it `data` ingests nothing.
- `timestamp` stays (not `time`/`date`).
- `origin`, `SNR`/`snr`, `RSSI`/`rssi`, `score` keep those exact names.
- Observer id is the **topic segment**; do not rely on `origin_id` reaching a consumer.
- `MQTT_RING_MSG_MAX` must always be **>= the buffer `buildPacketJson()` writes into**, or
  #726 reopens.

---

## 9. What this episode says about the code

Three separate silent-failure paths in one subsystem, all found by a human watching a map
rather than by tests or review:

- Ring overrun clamped the cursor, counted nothing, and `lapped()` erased its own evidence
  on drain (#710).
- Oversize payloads were refused with no counter at all — invisible even to the drop
  counter added to expose silent loss (#726).
- `held(no-heap)` reported normal rotation as a memory error, so the one visible signal
  pointed at the wrong subsystem (#715).

The pattern is a publish path that discards data on a `return 0` and reports success
everywhere else. Any future `return 0` on this path should be assumed to need a counter
until proven otherwise.
