# Offband device-info extension contract — `0xC5 0x1C`

**Status:** contract of record. Merged before implementation, by owner directive 2026-11-04.
**Epic:** #1994. **Applies to:** `OffbandMesh/meshcore-firmware` and `OffbandMesh/meshcore-client`.

Both repositories implement against *this document*. It is merged to `firmware-base` before either
side writes code, so that the allocations below are readable from the merged tree rather than from
somebody's unpushed branch.

---

## 1. Why this exists

Offband appends its own fields to **upstream MeshCore's** `RESP_CODE_DEVICE_INFO` reply, and
advertises them by incrementing **upstream's** `FIRMWARE_VER_CODE` — which is byte 1 of that frame
and upstream's own discriminator for upstream's fields (`v3+`, `v9+`, `v10+`).

Current squatted region:

| offset | field | advertised as |
|---|---|---|
| 82 | `offband_caps` | v14+ |
| 83 | FEM/LNA enabled | v16+ |
| 84 | `offband_caps2` | v18+ |
| 85 | `ui_led_enabled` | v21+ |
| 86 | `ui_display_mode` | v21+ |
| 87 | **contended by three features** | — |

That produced two defects with one root cause:

1. **Positional.** Clients read absolute offsets. If upstream inserts a field before ours, every
   Offband offset shifts and every deployed client misreads capabilities, FEM state, LED state and
   display mode *at the same time, with no error raised anywhere*.
2. **Version-space.** Announcing our fields by bumping upstream's counter means one number comes to
   mean two different things once upstream ships their next field. This is the deeper reason behind
   the standing ruling that nothing may gate on `FIRMWARE_VER_CODE`: that counter was never ours.

The visible symptom: **three independent features each computed "the next free byte" as 87 and each
was correct in isolation** — `offband_caps3` (#606), `EXPECTED_ACK_TABLE_SIZE` (#1001), and display
rotation's withdrawn contract (#1990). The frame is built by `out_frame[i++]` with no declared
layout, so "next free" is unanswerable from the merged tree and gives a different answer per branch.
A collision here is silent misparsing, not a refused command. It would have recurred at 88.

`[verified 2026-11-04: upstream/dev's builder still ends at path_hash_mode, byte 81 — the same place
it ended when Offband forked. Nothing has broken in the field yet.]`

---

## 2. Scope of THIS contract

**In scope — only the three fields that collided on offset 87.** They move into the structure below
and **offset 87 is never written.**

**Out of scope, deliberately:**

- Bytes 82–86 keep being emitted, byte-for-byte, with their current meanings. `offband_caps` and
  `offband_caps2` stay exactly where they are.
- Nothing is removed from upstream's frame. Stock MeshCore clients are unaffected — they stop
  reading at byte 81 and always did. Every shipped Offband client keeps working unchanged.
- Migrating the legacy squatted bytes out of upstream's frame is a **separate, later effort**, on the
  owner's schedule. This contract buys the time for it; it does not do it.

The only change to upstream's frame is that **no byte is appended at 87 or beyond.**

---

## 3. The frame

### Request

```
[0xC5][0x1C]            whole-description request
[0xC5][0x1C][cursor]    resume request; cursor = the first tag to include
```

`0xC5` is `CMD_OFFBAND_DEVICE_UI`, already Offband's own command code — upstream has no knowledge of
it. `0x1C` is `OFFBAND_UI_INFO_GET`, the next free sub-code, confirmed by all four in-flight sessions
on 2026-11-04 (`0x01`–`0x1B` allocated, `0x7F` is ERR).

### Reply

```
[0xC5][0x1C][ tag | len | value… ][ tag | len | value… ] …
```

- `tag` — 1 byte, non-zero. Identifies the field. See the registry in §5.
- `len` — 1 byte, the length of `value` in bytes. **May be zero.**
- `value` — `len` bytes.

Records appear in **ascending tag order**. The reply carries no record count; the client consumes
records until the frame payload is exhausted.

### Support discovery — no capability bit is spent

A device that does not implement `0x1C` answers `[0xC5][0x7F][0x05]` (`OFFBAND_UI_ERR`,
`ERR_MALFORMED`), three bytes, immediately — the `0xC5` handler has a terminal typed error and never
falls through silently. So:

- a reply with sub-code `0x1C` ⇒ supported;
- `[0xC5][0x7F][0x05]` to a **zero-payload** request ⇒ not supported.

The request must be zero-payload for this to be unambiguous, because reason `5` is also returned for
a *known* sub-code carrying a malformed payload.

No caps bit, and no `FIRMWARE_VER_CODE` check, is used to detect support. This is deliberate: a
discovery mechanism that needed a capability bit would reintroduce the scarcity this contract exists
to remove.

---

## 4. Rules both sides must obey

**Clients MUST skip unknown tags using `len`.** This is what makes the frame extensible: a client
built today must stay correct against a device that emits tags it has never heard of. A client that
fails on an unknown tag is non-conforming.

**Clients MUST NOT assume any tag is present.** A field's absence means the device does not have that
field — not an error, and not a zero value. Absence and `len = 0` are distinct: absent means "no such
field here", `len = 0` means "the field exists and has no payload".

**Clients MUST NOT rely on record position.** Ascending tag order is guaranteed for the cursor
mechanism below, not so that fields can be found by counting.

**Firmware MUST omit a tag whose field the board does not have**, rather than emitting a placeholder.

**Firmware MUST NOT split a record across frames.** Only whole records are emitted.

### Size and continuation

The reply must fit one serial/BLE frame. The payload ceiling is the transport's, obtained from
`maxFramePayload(header)` — never a fixed constant, because the BLE ceiling is MTU-dependent.

If the whole description does not fit, firmware emits as many whole records as fit, and the **last
record is `0xFF` `TAG_CONTINUE`**, `len = 1`, value = the tag the client should resume from. The
client then re-requests with that value as `cursor`. A reply without `TAG_CONTINUE` is complete.

Today's field set fits one frame on every supported transport, so no shipping client will see a
continuation. It is specified now so that the first field that overflows is a defined case rather
than a truncation bug.

### Errors

| condition | reply |
|---|---|
| sub-code not implemented | `[0xC5][0x7F][0x05]` |
| request payload longer than 1 byte | `[0xC5][0x7F][0x05]` |
| `cursor` higher than every available tag | `[0xC5][0x1C]` with no records — not an error |

---

## 5. Tag registry

**A new tag is added to this table by PR, and merged, BEFORE it is implemented.** That is the whole
point of this file and it is the rule that was missing. Same model as `number_allocations.md`:
modify the table, submit a PR, have it authorised.

| tag | name | len | value | owner |
|---|---|---|---|---|
| `0x01` | `TAG_CAPS_EXT` | 0–8 | capability bits continuing **beyond** `offband_caps2`. Bit *n* of byte *b* = capability `(b × 8) + n` of the extension space. Replaces what would have been `offband_caps3` at offset 87. | #1994 / #606 |
| `0x02` | `TAG_ACK_TABLE_SIZE` | 1 | `EXPECTED_ACK_TABLE_SIZE` — outstanding-ACK capacity. Replaces the byte `feat/994-delivery-observability` put at offset 87. | #994 / #1001 |
| `0x03` | `TAG_DISPLAY_ROTATION` | 1 | current display rotation, same value space as `0xC5 0x1A` `ROTATION_GET`. Present only on boards that can rotate. Saves a round trip on connect; `0x1A` remains the authoritative read. | #1990 / #1993 |
| `0x04`–`0xFE` | free | | | |
| `0xFF` | `TAG_CONTINUE` | 1 | resume cursor; see §4. Never carries a field. | #1994 |

`0x00` is not a valid tag.

### Capability bits in `TAG_CAPS_EXT`

No bits are defined by this contract. `offband_caps2` is full at 8 of 8, and **every capability bit
after it goes here**, not into a new byte in upstream's frame. A bit is allocated by adding a row
below, by PR, before implementation.

| bit | name | owner |
|---|---|---|
| — | none allocated yet | |

---

## 6. What each repository delivers

**Firmware (`meshcore-firmware`, this session):** the `0x1C` arm, a tag/length/value writer, the
three tags above, `TAG_CONTINUE`, omission of absent fields, and **removal of any write to offset
87**. Host tests for the writer, ascending order, omission, zero-length values, the unknown-sub-code
error, the over-long request, and the continuation boundary. A `config-lint` guard that fails the
build when a tag is used in source but absent from §5, or claimed twice.

**Client (`meshcore-client`, CalmBay):** request `0x1C` on connect; parse by tag; skip unknown tags
by length; treat `[0xC5][0x7F][0x05]` to a zero-payload request as "device predates this frame" and
fall back to today's absolute offsets; honour `TAG_CONTINUE`. No `FIRMWARE_VER_CODE` gating.

**Neither side reads or writes device-info offset 87, in any build, from this contract forward.**

---

## 7. Record

| date | change |
|---|---|
| 2026-11-04 | Created. Owner directive: contract merged before any code in either repo. Supersedes the ruling that caps byte 3 keeps offset 87 — that answered the wrong question. |
