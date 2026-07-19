# Block / Ignore Users — Firmware ↔ App Architectural Contract (Offband)

**Status:** Draft contract — shared for firmware + app alignment. Not yet implemented.
**Date:** 2026-06-28
**Parties:** Offband firmware (`OffbandMesh/meshcore-firmware`) ↔ Offband client app (`meshcore-open` / DarkBasin).
**Tracking:** Feature [#241](https://github.com/OffbandMesh/meshcore-firmware/issues/241) · Epic [#242](https://github.com/OffbandMesh/meshcore-firmware/issues/242) · this doc [#244](https://github.com/OffbandMesh/meshcore-firmware/issues/244).
**Scope of this doc:** the firmware ↔ app interface and the division of responsibility. Wire-level codes marked **PROPOSED** finalize at firmware implementation; everything else is the agreed design.

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

- A list of blocked **public keys** (full 32-byte identity / `pub_key`). Persisted in NVS; survives reboot **and client switches**.
- **Independent of the contacts list** — a pubkey can be blocked without being a contact (important when contacts is full, or you don't want to store the spammer).
- Capacity: **PROPOSED** 32–64 entries; confirm against the NVS budget at design.

---

## 4. Sync commands — **AS-BUILT** (`FIRMWARE_VER_CODE` 15, cap bit `0x02`; PR #247)

New fork command **`0xC2 = CMD_OFFBAND_BLOCK`** (0xC0 = config, 0xC1 = GPS already used), sub-typed, following the existing 0xC0 broker-pool dump convention. **Companion-API only** — these frames never traverse LoRa (§11). Request byte `[0]` = `0xC2`, byte `[1]` = sub-code; replies echo `[0xC2][sub]…`.

| Sub | Name | Request | Reply |
|---|---|---|---|
| `0x01` | BLOCK_ADD | `[0xC2][0x01][pubkey:32]` | `[0xC2][0x01][ok]` — `ok` = 1 (present after call: added or already there) / 0 (store full). `ERR_ILLEGAL_ARG` if frame < 34 B. |
| `0x02` | BLOCK_REMOVE | `[0xC2][0x02][pubkey:32]` | `[0xC2][0x02][ok]` — `ok` = 1 (removed) / 0 (not present). `ERR_ILLEGAL_ARG` if frame < 34 B. |
| `0x03` | BLOCK_LIST | `[0xC2][0x03]` | **streamed dump** — see below. |
| `0x04` | BLOCK_CLEAR | `[0xC2][0x04]` | `[0xC2][0x04][0x01]` |
| other | — | — | `ERR_ILLEGAL_ARG` |

**`BLOCK_LIST` is a *streamed* dump — one frame per idle main-loop pass**, not a single burst (mirrors the config/broker dump; the companion send queue drops when full, so the whole list is never bursted — #169). The client MUST read frames until it sees END:

- **START:** `[0xC2][0x03][0xFF][count]` (4 B) — `count` = number of key frames to expect.
- **Per key** (×`count`): `[0xC2][0x03][index][pubkey:32]` (35 B) — `index` is `0…count-1`.
- **END:** `[0xC2][0x03][0xFE]` (3 B).

Byte `[2]` disambiguates the frame: `0xFF` = START, `0xFE` = END, otherwise a key `index`. There is no collision because `MAX_BLOCKED_KEYS = 32`, so `index` is always `≤ 31` (never `0xFE`/`0xFF`). If the client (re)sends `CMD_APP_START` mid-dump, the firmware emits an early END terminator and drops the stream (#178 pattern), so a reconnecting client is never left hanging.

**Capacity:** `MAX_BLOCKED_KEYS = 32` (32 × 32 B = 1 KB). ADD returns `ok = 0` (**not** an error) when the store is full.

The app **pulls `BLOCK_LIST` on connect** to load the node's portable list, and pushes `ADD`/`REMOVE` as the user blocks/unblocks.

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

- ~~Firmware block-store capacity vs NVS budget.~~ **RESOLVED** — `MAX_BLOCKED_KEYS = 32`, persisted to a flat `/blocks` file (not NVS); see §4.
- Auto-add policy for a blocked **non-contact** pubkey (recommend: don't clutter contacts; the app's own name↔key index covers resolution).
- ~~Final `0xC2` sub-type codes + dump framing.~~ **RESOLVED** — implemented as-built in §4 (PR #247).
- Confirm advert-notification suppression stays purely app-side (recommended).

---

## 11. Interoperability guarantee

This feature is **fully compatible with stock MeshCore and any non-Offband node** (repeaters and companions). It introduces **no mesh / RF protocol change.**

- Every wire addition — the `0xC2` block commands and the `0x02` capability bit — is a **companion-API frame** (BLE/USB, in the `0xC0+` extension namespace), a **different namespace** from mesh payload types (`PAYLOAD_TYPE_*`). These frames **never traverse LoRa**; no other node ever sees them. Stock firmware simply doesn't set the cap bit, and the app falls back to app-only.
- All enforcement is **receive-side / app-facing** — it decides only what *your* device surfaces to *its* user.

**Binding invariant:** block **MUST NOT** alter forwarding, relaying, ACKs, advert re-flood, or any RF behaviour. A blocked sender's packets still route and flood **through** your node normally — the drop happens at the **app-push layer, after routing decisions**, never in the forward path. Blocking someone must never make your node a worse relay (protects both interop **and** mesh health).

**Explicitly out of scope (these would break interop):** changing forwarding to drop a blocked sender's *transit* packets; adding a mesh packet type; touching path-hash / routing. None of that is in this design — the whole feature is a per-user view filter plus a portable list synced over the local companion link.
