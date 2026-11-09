# Offband capability advert — `0xC5 0x1C`

**Status:** revision 2, replacing the version merged in PR #1996 unread.
**Epic:** #1994 · **Repos:** `meshcore-firmware`, `meshcore-client` · **Rule:** merged is claimed.

Revision 1 had four defects the owner caught. They are listed in §8 with what each one cost, because
the reasoning matters more than the diff.

---

## 1. The problem, in four lines

Offband appends its fields to **upstream's** `RESP_CODE_DEVICE_INFO` and announces them by
incrementing **upstream's** `FIRMWARE_VER_CODE` (byte 1 of that frame, upstream's own discriminator).
Six consecutive Offband features did this — ver codes 18 through 23. When upstream ships one field
and bumps their counter, every Offband offset shifts under every deployed client at once, and one
version number means two different things.

Three features then independently computed "the next free byte" as **87** and each was right in
isolation: `offband_caps3` (#606), `EXPECTED_ACK_TABLE_SIZE` (#1001), and display rotation (#1990).
A collision there is **silent misparsing**, not a refused command.

`[verified 2026-11-04: upstream/dev still ends at byte 81, as it did when we forked. Nothing has
broken in the field yet.]`

---

## 2. Two rules, permanent

**No session adds an Offband field to upstream's device-info frame. Ever.** Not at 87, not at 88, not
anywhere. Bytes 82–86 keep working and migrate off on the owner's schedule, so that upstream
extending their own frame costs us nothing.

**A claim is made by merge, and the merge comes FIRST.**

This is an ordering obligation, not a definition of when a number becomes valid. The sequence is
fixed and every step precedes the next:

```
1.  identify the number you need          (sub-code, tag, caps bit, error reason, record field)
2.  add it to OffbandConfigProtocol.h     — the claim, as a constant, nothing else
3.  push, PR, MERGE that claim            — to firmware-base
   ────────────────────────────────────── only now is the claim yours
4.  the feature plan may be completed, approved and merged
5.  implementation work may begin
```

**No work begins before step 3 completes.** Not design work against the number, not a branch that
uses it, not a client build, not a "provisional" constant on an unmerged branch. **A feature plan
that depends on an unmerged number is not a complete plan and must not be approved.**

The reason is the whole of this document: an unmerged claim is invisible. `git ls-remote` cannot see
a branch that was never pushed, so two sessions reading the merged tree both correctly conclude the
same number is free. Three features did exactly that on offset 87. Announcing a claim on Agent Mail
is not a substitute — mail reaches whoever happens to read it, and the colliding party may not be in
the mailbox at all.

**This rule already existed and was not followed.** Sessions treated "allocations settle at merge" as
a statement about the future rather than a gate on starting, and claimed numbers on branches while
building against them. That is the failure, stated plainly so it is not repeated: *claiming at merge
is worthless if you build before the merge.*

The claim lives in a **merged header the compiler reads** — `OffbandConfigProtocol.h` — not in prose
a session can skip. This document records intent and rationale; **the header is the registry.** §5
restates the tag table for readability, and the header wins if they ever disagree.

**This epic is bound by its own rule.** The tags and sub-code in §5 are not claimed until a
header-only PR carrying them is merged, so that PR lands before this epic's plan is approved and
before any firmware or client work starts.

---

## 3. The advert

### Request — client asks, once, on connect

```
[0xC5][0x1C]            whole advert
[0xC5][0x1C][cursor]    resume from this tag
```

**The client requests it; the device does not push it.** This is deliberate and measured:
`_att_mtu` is 23 on connect and is only raised when `onMTUChange` fires
(`esp32/SerialBLEInterface.h:104`). A frame pushed at connect time therefore gets a **20-byte**
payload, while a frame the client requests — after it has negotiated — gets the full MTU, typically
244+. Pushing would make truncation the normal case.

One request returning the whole list is **advertising, not probing.** What is forbidden is
discovering support by trying each feature's sub-code and watching for errors.

### Reply

```
[0xC5][0x1C][ tag | len | value… ][ tag | len | value… ] …
```

`tag` 1 byte, non-zero. `len` 1 byte, may be 0. Records in ascending tag order.

### A tag IS a capability

**Presence of a tag means the device has that feature. There is no capability bitmap in this frame.**

A bitmap inside a tagged frame would be a second source of truth for "is this supported" — the same
two-stores defect rejected on #606, one layer up. So:

- `len = 0` — "I have this feature, there is nothing to report." The normal case.
- `len > 0` — only where the value is **one or two bytes, fixed for the build, and has no sub-code of
  its own.** Anything a client can read with a GET is read with that GET, not carried here.
- **absent** — the device does not have the feature. Not an error, not a zero value.

### Support for the advert itself

An unimplemented sub-code already replies `[0xC5][0x7F][0x05]` immediately — the `0xC5` handler has a
terminal typed error and never falls silent. So a **zero-payload** request answered that way means
"device predates the advert", and the client falls back to today's absolute offsets. No capability
bit is spent to discover the capability mechanism.

The request must be zero-payload for this to be unambiguous: reason `5` is also what a known sub-code
with a malformed payload returns.

---

## 4. Rules for both sides

- Clients **MUST** skip unknown tags using `len`. A client built today must stay correct against a
  device emitting tags it has never heard of.
- Clients **MUST NOT** rely on record position. Ascending order exists for the cursor, not for
  counting.
- Firmware **MUST** omit a tag for a feature the board does not have, rather than emit a placeholder.
- Firmware **MUST NOT** split a record across frames.
- **During the transition, each capability is advertised in exactly one place** — old features keep
  their `offband_caps`/`caps2` bits, new features use tags. No feature appears in both. When 82–86
  migrate, their bits become tags and the bits are retired in the same change.

### Size

The reply is sized from `maxFramePayload(header)`, never a constant. Measured: **20 bytes**
pre-negotiation floor, ~244 once negotiated. Because the client requests this post-negotiation, the
real budget is the large one; the floor is why the device must not push.

If the advert does not fit, firmware emits whole records and makes the last one `0xFF` `TAG_CONTINUE`,
`len = 1`, value = the tag to resume from. A reply without it is complete. With values kept out of
this frame, today's advert is a handful of bytes and no shipping client will see a continuation — it
is specified so the first overflow is a defined case instead of a truncation bug.

### Errors

| condition | reply |
|---|---|
| sub-code not implemented | `[0xC5][0x7F][0x05]` |
| request payload longer than 1 byte | `[0xC5][0x7F][0x05]` |
| cursor above every available tag | `[0xC5][0x1C]`, no records — not an error |

---

## 5. Tag registry

Authoritative copy is `OFFBAND_INFO_TAG_*` in `OffbandConfigProtocol.h`; this table is a convenience
copy and the header wins if they disagree.

**Every row below is subject to §2: the claim merges before the plan is approved and before any work
begins.** Adding a row here is not a claim. Merging the constant into the header is the claim. A row
present here with no merged constant means the number is still unclaimed and nobody may build on it.

| tag | name | len | meaning | value read via | owner |
|---|---|---|---|---|---|
| `0x01` | `TAG_CHAN_NOTIFY` | 0 | per-channel notification mode supported | `0xC5 0x18`/`0x19` | #606 |
| `0x02` | `TAG_DISPLAY_ROTATION` | 0 | display can rotate | `0xC5 0x1A`/`0x1B` | #1990 |
| `0x03` | `TAG_ACK_CAPACITY` | 1 | outstanding-ACK capacity. Carries its value by the §3 exception: one byte, fixed for the build, no sub-code of its own | — | #994/#1001 |
| `0x04` | `TAG_DISPLAY_CONTROL` | 0 | the display can be switched on/off/auto | `0xC5 0x05`/`0x06` | #2067 |
| `0x05` | `TAG_LED_CONTROL` | 0 | the LED can be switched on/off | `0xC5 0x07`/`0x08` | #2067 |
| `0x06`–`0xFE` | free | | | | |
| `0xFF` | `TAG_CONTINUE` | 1 | resume cursor | — | #1994 |

`0x00` is not a valid tag. `0xC5` sub-codes `0x01`–`0x1B` are allocated; `0x1C` is this advert;
`0x1D` is next free; `0x7F` is ERR with reasons 1–10 taken.

### `0x04` / `0x05` — why two tags and not one bit (#2065, #2067)

`OFFBAND_CAP2_INDICATORS` (caps byte 2 bit 2, device-info frame offset 84) is documented as
"led/display indicator control" and was gated on `canControlLed() || DISPLAY_CLASS`. One bit, two
pieces of unrelated hardware. A board with a working display and an uncontrollable LED set the bit,
the client offered both controls, and the LED write came back `[0xC5][0x7F][0x06]`
`UNSUPPORTED_INDICATOR`. Owner-verified on a Heltec T096, a SenseCAP T1000-E and a QCC badge.

There is no bit available to fix it with: caps byte 2 is full and byte 3 does not exist (§7, #1994).
So the per-indicator answer is a tag.

**Emission rules** (implemented by #2068, not by the claim itself):

- `0x04` only when the **live display driver** reports a usable display. Not `#ifdef DISPLAY_CLASS`:
  `NullDisplayDriver::begin()` returns `false` and is marked "not present", yet satisfies that macro
  on 15 variants. Ask the driver, as `displaySupportsRotation()` asks `_display->supportsRotation()`.
- `0x05` only when `board.canControlLed()` is true **and** the write is honored. A board that reports
  the capability and ignores the setting is the #1992 defect.

**Precedence.** `OFFBAND_CAP2_INDICATORS` is **kept, not retired**, which is a deliberate departure
from §4's "the bits are retired in the same change". Radios on 1.7.0 and earlier answer this advert
*without* the new tags, so a tags-only client would hide display control on a Heltec V4 that works
fine. The bit is the compatibility answer for firmware predating #2068. The rule is the same shape as
the merged CHAN_NOTIFY precedence:

| radio answers | client behavior |
|---|---|
| advert carries `0x04` and/or `0x05` | tags are authoritative, caps2 bit `0x04` ignored, each control gated on its own tag |
| advert answered, neither tag present | pre-#2068 firmware — caps2 bit `0x04` answers, both controls shown as today |
| `[0xC5][0x7F][0x05]` to a zero-payload `0x1C` | predates the advert — the bit answers |

#2068 regates the bit itself: set when at least one of the two capabilities is present, decided at
runtime rather than from a macro.

A client must never discover either capability by sending `0x07` and reading the refusal. §3 forbids
it, and such a probe was already written and deleted once.

---

## 6. What each repo delivers

**Firmware:** the `0x1C` arm; a tag writer; the three tags; `TAG_CONTINUE`; omission of absent
features; **no write to offset 87 anywhere**; `FIRMWARE_VER_CODE` neither bumped nor read. Host tests
for ordering, zero-length records, omission, the exact-fit boundary, one byte over, the continuation
round trip, the unknown-sub-code error and the over-long request. A `config-lint` guard that fails the
build on a tag used in source but absent from the header, a tag claimed twice, or any new byte
appended to upstream's device-info frame — scanning **local branches and worktrees**, since every
claim in all three collisions lived where `git ls-remote` cannot see.

**Client:** request `0x1C` once on connect, after MTU negotiation; treat a tag's presence as the
capability; skip unknown tags by length; read values through each feature's own sub-code; treat
`[0xC5][0x7F][0x05]` to a zero-payload request as "device predates this" and fall back to current
offsets; honour `TAG_CONTINUE`. No `FIRMWARE_VER_CODE` gating.

---

## 7. What is NOT in this contract

Bytes 82–86 and the existing `offband_caps`/`caps2` bits are untouched. Sub-codes `0x01`–`0x1B`,
the `/channels2` record and every existing caps bit keep their meanings. Nothing is removed from
upstream's frame, so stock MeshCore clients — which stop reading at byte 81 and always did — and
every shipped Offband client are unaffected. Migrating the legacy bytes is the owner's later effort;
this buys the time for it.

---

## 8. What revision 1 got wrong

Recorded because the owner merged revision 1 without reading it, on trust, and these were my
decisions rather than agreed ones.

1. **A capability bitmap (`TAG_CAPS_EXT`).** Removed. The owner asked for the caps-bit limitation
   eliminated and I made the bitmap unbounded instead of removing it, leaving two sources of truth
   for "supported" inside a frame whose tags already answer that question.
2. **Discovery by probing each feature.** Replaced by one advert. Trial-and-error per sub-code was
   what the owner objected to, and he was right.
3. **A 255-byte value cap I never measured.** The binding limit is the transport's: 20 pre-negotiation,
   ~244 after. The cap was decorative, and measuring it is what established that the device must not
   push the advert.
4. **A newly-written allocation process.** The rule already existed — merged is claimed — and was
   being broken, not missing. The registry now lives in the header rather than in this file.

Revision 1 also assigned a tag number on another session's behalf before they had agreed to it.

---

## 9. Record

| rev | date | note |
|---|---|---|
| 1 | 2026-11-04 | Merged unread in PR #1996. Four defects above. |
| 2 | 2026-11-04 | This revision. Bitmap removed, advert replaces probing, size model measured, prohibition made permanent, registry moved into the header. |
