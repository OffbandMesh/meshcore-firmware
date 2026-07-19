# Block / Ignore Users — Firmware ↔ App Architectural Contract (Offband)

**Status:** **AS-BUILT** — firmware half shipped (#246 / PR #247, `FIRMWARE_VER_CODE 15`) and verified end-to-end (#242). Wire codes below are final.
**Date:** 2026-06-28 · **As-built revision:** 2026-07-14 (#313)
**Parties:** Offband firmware (`OffbandMesh/meshcore-firmware`) ↔ Offband client app (`OffbandMesh/meshcore-client`).
**Tracking:** Feature [#241](https://github.com/OffbandMesh/meshcore-firmware/issues/241) · Epic [#242](https://github.com/OffbandMesh/meshcore-firmware/issues/242) · this doc [#244](https://github.com/OffbandMesh/meshcore-firmware/issues/244) · as-built reconciliation [#313](https://github.com/OffbandMesh/meshcore-firmware/issues/313).
**Scope of this doc:** the firmware ↔ app interface and the division of responsibility. Wire-level codes are **as-built** (verified on hardware); everything else is the agreed design.

---

## 0. Why two layers

Blocking has to work on **any** firmware a user might run (stock MeshCore, WadaMesh, Offband) — so the app must own a complete, self-sufficient block. But an app-local block list **does not follow the user across clients** (phone → phone, phone → laptop): switch devices and you re-block everyone. The fix is to store the block list **on the node**, where it's the same regardless of which client connects. Hence two cooperating layers.

| | App layer (universal) | Firmware layer (Offband-only) |
|---|---|---|
| Runs on | any firmware | Offband only (capability-gated) |
| Owns | UI, channel blocking, app-only fallback | portable pubkey store, DM-drop, sync |
| Block list | local to the install | **on the node** → portable across clients |

---

## 1. Identity per path (the technical floor) `[verified]`

| Path | Sender identification | Real identity block? |
|---|---|---|
| **DM** | encrypted to/from a specific **pubkey** (contact) | ✅ yes — strong |
| **Advert** | **signed** by the sender's pubkey | ✅ yes — strong |
| **Channel (Public / #)** | encrypted with the **shared channel key**; sender is only a **claimed name string** in the text — no pubkey, no signature `[BaseChatMesh.cpp:367-387]` | ❌ no — spoofable name only |

Consequence: the firmware can do a real **pubkey** block for **DM + adverts**, but **cannot** identity-block a channel sender. Channel blocking is therefore an **app-side** name↔key resolution problem.

---

## 2. Capability + version gate

- New capability bit **`OFFBAND_CAP_BLOCK = 0x02`** (bit 1) in the capability byte of `RESP_CODE_DEVICE_INFO` (reply to `CMD_DEVICE_QUERY`). Bit 0 is the existing WIFI_OBSERVER cap. `[OffbandConfigProtocol.h:102-113, MyMesh.cpp:1368-1375]`
- Gated on **`FIRMWARE_VER_CODE >= 15`** (bumped from 14) **and** the cap bit set.
- **App rule:** cap bit absent → **app-only mode** (no sync, no firmware drop). Block still works locally; it just isn't portable.

---

## 3. Firmware block store

- A list of blocked **public keys** (full 32-byte identity / `pub_key`). Persisted to a flat **`/blocks` file** — `[count:1][key:32]*count` (`MyMesh.cpp:2504-2513`), **not NVS**. Survives reboot **and client switches** (verified on hardware: 32/32 keys survive a power-cycle).
- **Independent of the contacts list** — a pubkey can be blocked without being a contact (important when contacts is full, or you don't want to store the spammer).
- Capacity: **32 entries** — `MAX_BLOCKED_KEYS` (`BlockStore.h:20`), 32 × 32 B = 1 KB.

---

## 4. Sync commands — **AS-BUILT**

`[verified: OffbandConfigProtocol.h:119-124 · MyMesh.cpp:1402-1428, 2536-2551 · on-device round-trip 14/14, #242]`

Fork command **`0xC2 = CMD_OFFBAND_BLOCK`** (0xC0 = config, 0xC1 = GPS already used), sub-typed. **Companion-API only** — these frames never traverse LoRa (§11). Request byte `[0]` = `0xC2`, byte `[1]` = sub-code; replies echo `[0xC2][sub]…` (`RESP_CODE_OFFBAND_BLOCK` = 0xC2).

| Sub | Name | Request | Reply |
|---|---|---|---|
| `0x01` | BLOCK_ADD | `[0xC2][0x01][pubkey:32]` (34 B) | `[0xC2][0x01][ok]` — `ok`=1 present after call (added **or already there**), `ok`=0 store full |
| `0x02` | BLOCK_REMOVE | `[0xC2][0x02][pubkey:32]` (34 B) | `[0xC2][0x02][ok]` — `ok`=1 removed, `ok`=0 not present |
| `0x03` | BLOCK_LIST | `[0xC2][0x03]` | **streamed dump** — see below |
| `0x04` | BLOCK_CLEAR | `[0xC2][0x04]` | `[0xC2][0x04][0x01]` |
| other / short | — | — | generic error `[0x01][0x06]` (`RESP_CODE_ERR`, `ERR_CODE_ILLEGAL_ARG`); ADD/REMOVE under 34 B also errors |

⚠ The error frame is **not** 0xC2-prefixed — a client must recognise the generic 2-byte error frame, not wait for a `0xC2` echo.

**`BLOCK_LIST` is a streamed dump, not a single reply.** Frames arrive **one per firmware idle main-loop pass** (the companion send queue drops when full, so the list is paced — #169). The client MUST read until END:

```
START      [0xC2][0x03][0xFF][count]        4 B    count = key frames to expect (advisory)
key × N    [0xC2][0x03][index][pubkey:32]  35 B    index = 0…count-1
END        [0xC2][0x03][0xFE]               3 B
```

- Byte `[2]` is the demux tag: `0xFF` = START, `0xFE` = END, anything else = a key `index`. No collision — `index ≤ 31` (capacity 32), never `0xFE`/`0xFF`.
- **END, not `count`, is authoritative.** If the client (re)sends `CMD_APP_START` mid-dump the firmware emits an **early END** and drops the stream (`MyMesh.cpp:1483-1488`), so a reconnecting client never hangs. Detect truncation by comparing key-frames-received against START's `count`, and re-request once the link settles.
- **Never derive removals from a LIST** (partial or full). Removal is explicit-unblock only, so a partial pull can never cause a false unblock — worst case is missing node keys, filled by a re-request.

**No unsolicited pushes.** Strictly app-pull + app-initiated ADD/REMOVE/CLEAR/LIST. The only firmware-initiated 0xC2 frame is the APP_START early-END terminator (reconnect-triggered, not a state change). There is also **no notification when a DM is dropped** — the drop is silent at receive, so the app never sees blocked DMs and cannot count or badge them.

The app **pulls `BLOCK_LIST` on connect** to load the node's portable list, and pushes `ADD`/`REMOVE` as the user blocks/unblocks. If the app's local list exceeds 32, overflow ADDs return `ok=0` (not an error) and those keys simply aren't node-portable — the app-local list stays authoritative.

---

## 5. Firmware enforcement

- **DM** from a blocked pubkey → **dropped at receive, not pushed to the app.** This is the core offload.
- **Advert** from a blocked pubkey → **still processed** (contact-name update kept). **Do NOT drop blocked adverts** — the channel self-heal (§7) depends on adverts continuing to refresh the contact name. The firmware keeps feeding the contact update; the **app suppresses the user-facing advert notification** using its synced copy of the list.
- **Channel** → **not enforced in firmware.** App-side only (§6).

---

## 6. App responsibilities

- Maintain **`blockedKeys`** (synced to/from firmware when the cap is present) and **`blockedNames`** (app-local fallback for unresolvable channel senders).
- Maintain a **name↔key index**, sourced from **two** places — do **not** rely on a single push type:
  - **`PUSH_CODE_NEW_ADVERT` (0x8A)** — carries the **full record (pubkey + name)** and fires for **every** advert from a sender **not** currently in the node's contact store, with **no replay guard** `[MyMesh.cpp:411-412, BaseChatMesh.cpp:120-126]`. So non-contacts — including a **blocked-but-unstored spammer** — always resolve. (This closes the "permanent name-only" worry.)
  - **the synced contact list** — for renamed **stored** contacts, whose re-advert pushes only `PUSH_CODE_ADVERT` (0x80) = **pubkey, no name** `[MyMesh.cpp:414-416]`; the new name must be read from the contact record, not the advert tickle.
- **Channel filter (at display):** hide a channel post **only if** (its claimed name resolves to ≥1 pubkey **and *every* matching pubkey is blocked**) **OR** its claimed name is in `blockedNames`. If **any** matching pubkey is **unblocked → SHOW** (an ambiguous name = two people/devices sharing a name; don't censor a possibly-legit same-name user — an L1 false positive). A channel post carries **no pubkey**, so there's **no per-post key** to single out — resolution and "Block sender" act on the **claimed name** only.
- **Long-press "Block sender":**
  - resolves to a pubkey → add to `blockedKeys` (**one action unifies DM + advert + channel**; sync to firmware).
  - **multi-match** (one person, multiple devices = multiple pubkeys, same name) → surface *all* matching pubkeys so the user can block each device's key.
  - no resolution → add to `blockedNames`, flagged "name-only, evadable."
- **Suppress advert / notification UX** for blocked pubkeys (firmware still feeds contact updates so the self-heal stays alive).
- **App-only fallback** when the cap bit is absent: do everything locally, no sync, no firmware drop.

---

## 7. Self-heal (name ↔ key)

When a blocked person renames, their next advert updates **their contact's name under the same pubkey** — verified on the firmware: `onAdvertRecv` matches the existing contact by pubkey and `strncpy`s the new name, then pushes the update `[BaseChatMesh.cpp:120,177,186]` (client mirror `meshcore_connector.dart:4639`). So a renamed channel post re-resolves to the same blocked pubkey. The spammer's own advert-flooding keeps the link fresh — their behaviour works against them.

**Push-variant detail (drives §6 index sourcing):** the self-heal is **automatic and complete for non-contacts** — every advert from an unstored sender pushes the **full `NEW_ADVERT` record (name + key)** with no replay guard, so a blocked-unstored spammer's index entry refreshes on every advert. For a **stored** contact, the rename push is `PUSH_CODE_ADVERT` (**pubkey only**); the firmware updates the stored name internally but does **not** carry it in the push, so the app must refresh a renamed stored contact's name from the **contact record**, not the advert tickle.

---

## 8. `blockedNames` age-out (recommended)

**Promote-and-prune:** a name-only block is provisional. When the index links a `blockedNames` entry to a pubkey (from an advert), **promote** it — add the pubkey to `blockedKeys` (sync), remove the name entry. Any name that never links **expires after ~30 days**. The durable block is always the pubkey; `blockedNames` stays small and self-cleaning, bounding the L1/L2 false-positive window.

---

## 9. Known gaps & limitations (carry into the spec — do not lose)

- **G1 — advert replay-guard can stall the re-link.** The contact-name re-link only takes when the advert `timestamp` strictly increases `[BaseChatMesh.cpp:123]`; a clock-skewed/replayed advert is rejected, so a rename → re-resolve can briefly stall. Edge case.
- **L1 — spoof-into-block false positive.** Channel names are attacker-controlled: anyone can post claiming a blocked name and get hidden, and a bad actor could hide a *legit* person's channel posts by impersonating a blocked name. Low severity.
- **L2 — stale name entry post-rename.** A renamed blocked person's old name no longer matches them and may catch an innocent who later reuses it. Mitigated by §8 age-out.
- **L3 — zero-identity hard limit (protocol).** If a blocked person renames **and** goes advert-silent but keeps posting to a channel, that post is genuinely unattributable — nothing in the channel packet identifies them. Not closable app- or firmware-side; residual options are name-pattern matching or muting the channel.

---

## 10. Open questions (design step)

- ~~Firmware block-store capacity vs NVS budget.~~ **RESOLVED** — `MAX_BLOCKED_KEYS = 32`, persisted to a flat `/blocks` file (not NVS); see §3.
- ~~Final `0xC2` sub-type codes + dump framing.~~ **RESOLVED** — as-built in §4 (sub-codes `0x01`–`0x04`; LIST = streamed START/key/END with `0xFF`/index/`0xFE` demux), verified on hardware.
- Auto-add policy for a blocked **non-contact** pubkey (recommend: don't clutter contacts; the app's own name↔key index covers resolution). *(still open)*
- Confirm advert-notification suppression stays purely app-side (recommended). *(still open)*

---

## 11. Interoperability guarantee

This feature is **fully compatible with stock MeshCore and any non-Offband node** (repeaters and companions). It introduces **no mesh / RF protocol change.**

- Every wire addition — the `0xC2` block commands and the `0x02` capability bit — is a **companion-API frame** (BLE/USB, in the `0xC0+` extension namespace), a **different namespace** from mesh payload types (`PAYLOAD_TYPE_*`). These frames **never traverse LoRa**; no other node ever sees them. Stock firmware simply doesn't set the cap bit, and the app falls back to app-only.
- All enforcement is **receive-side / app-facing** — it decides only what *your* device surfaces to *its* user.

**Binding invariant:** block **MUST NOT** alter forwarding, relaying, ACKs, advert re-flood, or any RF behaviour. A blocked sender's packets still route and flood **through** your node normally — the drop happens at the **app-push layer, after routing decisions**, never in the forward path. Blocking someone must never make your node a worse relay (protects both interop **and** mesh health).

**Explicitly out of scope (these would break interop):** changing forwarding to drop a blocked sender's *transit* packets; adding a mesh packet type; touching path-hash / routing. None of that is in this design — the whole feature is a per-user view filter plus a portable list synced over the local companion link.
