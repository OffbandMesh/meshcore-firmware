# Feature PLAN: Single-key DM introduction

| | |
|---|---|
| **Feature** | [#2327](https://github.com/OffbandMesh/meshcore-firmware/issues/2327) · Citadel `Crosswire-bkb` |
| **Serves** | Initiative [#1650](https://github.com/OffbandMesh/meshcore-firmware/issues/1650), Operating without fixed infrastructure: a group with no shared contacts can still reach each other privately. |
| **Status** | The owner approved the content on 2026-11-28 ("Proceed with the plan"). Formally approved when this PR merges. |
| **Owner** | @Strycher |
| **Last updated** | 2026-11-28 |

## At a glance

- **What:** a companion can DM a node that doesn't hold its public key yet. The recipient sees who it is, accepts or ignores it, and only then reads the message.
- **Why now:** today that DM is dropped without a trace. The recipient can't decrypt it, the sender gets no ACK, and nobody is told why (diagnosis D6). Strangers at an event, a contact added one way from a QR code, or someone who deleted you all hit this, and the only fix is a flood advert.
- **How:** carry the first DM inside MeshCore's existing anonymous-request packet, which already holds the sender's full key. The recipient holds it until the user accepts, and every retry goes out as a normal DM so stock firmware keeps working. Four Epics: diagnostics and tests, receive, send, then integration testing.
- **Done when:** on two radios, a DM from a sender the recipient never heard is delivered after accept and dropped after ignore. A stock-firmware recipient and a stock client still work, also on hardware.

## 1. Diagnosis

Recorded in full, with `file:line` evidence, on [#2327](https://github.com/OffbandMesh/meshcore-firmware/issues/2327#issuecomment-7235913026). Everything below is from reading `firmware-base` at `25486222`, not from running it.

| # | Finding | Evidence | Effect |
|---|---|---|---|
| D1–D3 | A DM is `TXT_MSG`: 1-byte dest hash, 1-byte src hash, encrypted with the pair's shared secret. The expected ACK is `sha256(plaintext, sender key)[0..3]`. | `BaseChatMesh.cpp:470-504` | Both sides need the other's key. |
| D5–D6 | The receiver tries only contacts whose 1-byte hash matches. With no match, the packet is dropped with a debug-only log. | `Mesh.cpp:147-191` | The failure this Feature fixes. It's silent today. |
| D7 | The ACK is encrypted with the shared secret. | `BaseChatMesh.cpp:286-300` | The recipient can't ACK until it holds the sender. |
| D8–D9 | `ANON_REQ` (0x07) carries the sender's long-term 32-byte key, and its MAC is keyed by `ECDH(sender, recipient)`. | `Mesh.cpp:197-223, 512-538` | An introduction needs no new packet type, and the MAC already authenticates the sender at the same strength as a normal DM. |
| D10 | `createAnonDatagram` takes at most 136 bytes of plaintext (`136 + 1 + 32 + 15 = 184`). After the 4-byte timestamp, the sub-type and the flags byte, **130 characters** fit. A normal DM holds 160. | `Mesh.cpp:514`, `MeshCore.h:21`, `BaseChatMesh.h:8` | Longer texts can't ride in the introduction. |
| D11–D12 | A stock companion ignores `ANON_REQ` (base no-op). | `Mesh.h:130` | A plain-DM fallback is mandatory, or a stock recipient that does hold us loses the message. |
| D13 | Every stock non-chat role reads an `ANON_REQ` as a login. A repeater or sensor treats a 5th byte of 0 or ≥ `' '` as a password. A room server reads a password at byte 8, and with read-only access allowed it **adds the sender to its ACL as a guest**. | `simple_repeater/MyMesh.cpp:905-917`, `simple_sensor/SensorMesh.cpp:454-467`, `simple_room_server/MyMesh.cpp:325-52` | Introduce only to chat-type contacts. Sending one to a room would sign the sender in. |
| D15–D16 | `PUSH_CODE_NEW_ADVERT` (0x8A) already offers a not-yet-added contact to the client. | `companion_radio/MyMesh.cpp:316, 1015-1027` | A stock client can accept an introduced sender with its existing add-contact flow. |

## 2. Scope

**In:**
- The **introduced DM**: the first attempt of a DM to a chat contact we have no proof holds our key, sent as an `ANON_REQ` with a new sub-type, when the text fits.
- On the receiving companion: verify, hold the message in RAM, offer the sender to the client as a new contact, and deliver the held message and its ACK when the user adds the contact.
- Retries go out as normal `TXT_MSG`, so a stock recipient still gets the DM.
- A setting, `dm.intro`: `off`, `ask` (the default) or `auto`.
- The `recv matches no peers` drop becomes visible on the rig (a MeshLog line and a counter), so the "before" picture can be measured.

**Out:**
- Repeaters, room servers and sensors. They neither send nor accept introductions (D13).
- Any client change. The plan works with stock clients; an Offband-client UI for held introductions is a separate client issue.
- On-device accept UI for GUI and badge builds. It belongs with #1770 (see and add discovered contacts on the GUI); this plan only exposes the held introduction to it.
- A DM longer than an introduction holds. It's sent normally, and the user is told why it may not arrive (section 9, choice 3).
- Fixing #1492 or #999. The plan depends on them where noted and doesn't absorb them.

## 3. Epics

Each Epic gets one PR and ends with a verification task. Points are per task.

### Epic A: Make today's failure visible, and pin the format in tests

| Task | Pts |
|---|---|
| **A1 · Visible drop:** a MeshLog line and a `dm_unknown_sender` counter at `Mesh.cpp:189-191`, so a capture shows a DM from an unknown sender being dropped. Checked by a native test and one rig capture. | 2 |
| **A2 · Pure codec `src/helpers/IntroDm.h`:** encode and decode the introduced-DM plaintext (`timestamp · sub-type · flags · text`), the 130-character limit, and the ACK hash. The hash is computed over `timestamp · flags · text`, the same bytes a plain DM of that attempt would hash, so the recipient's normal ACK code produces it. The sub-type is a `static_assert`ed constant in `0x04–0x1F`: below `' '`, not 0, and clear of the repeater's anonymous types 1–3 (D13). Unit tests: positive (round-trip, the ACK matches the plain-DM formula for the same attempt) and negative (oversize, wrong sub-type, truncated, a sub-type a repeater would read as a login). | 3 |
| **A3 · Failing tests that reproduce D6 and D11:** a native mesh test where a DM from an unknown sender is dropped, and where an `ANON_REQ` to a stock companion is ignored. They're marked as the before-state and flip when B lands. | 3 |
| **A4 · Epic verification** | 1 |

### Epic B: Receive and hold

| Task | Pts |
|---|---|
| **B1 · `onAnonDataRecv` in the companion:** accept only the intro sub-type and only on a chat node. Drop it if the sender is already a contact (send their normal ACK instead), blocked, or replayed (a timestamp no newer than the last one held for that key). | 3 |
| **B2 · Held queue:** `OFFBAND_INTRO_HELD` entries in RAM, 4 by default. Each holds the key (32), timestamp (4), flags (1), a path for the ACK (up to 64) and the text (131), about 232 bytes, so 4 entries come to about 0.9 KB. A build check reports the size per env, and a tight nRF52 env can set it lower. When it's full, the newest introduction is refused and counted, never evicting an older one. Unit tests. | 3 |
| **B3 · Offer and deliver:** push `PUSH_CODE_NEW_ADVERT` with a contact frame that is **not** added. When the client adds that key, the held messages go into the normal message queue and the ACK goes back by path return. A held message whose sender, timestamp and text already reached the store, from a plain retry, is dropped rather than shown twice. With `auto`, verified senders are added without asking. | 3 |
| **B4 · Accept-path safety:** adding a held sender never uses overwrite-oldest while an ACK is in flight. If the table is full, refuse with `PUSH_CODE_CONTACTS_FULL` and keep holding the message (#1492, #2242). | 2 |
| **B5 · Epic verification:** two Offband radios. The recipient has never heard the sender; the client accepts; the message appears; the sender sees it confirmed. | 2 |

### Epic C: Send, with stock fallback

| Task | Pts |
|---|---|
| **C1 · "Proven" set:** a RAM set of contacts we have received anything decryptable from (a DM, an ACK, a response, a path return). Any of those means they hold our key. It's lost on reboot, which only costs one extra introduction. | 2 |
| **C2 · Attempt mapping in `cmdSendDirect`:** attempt 0 to an unproven chat contact, with a text that fits and `dm.intro` not `off`, goes as an introduced DM. Every other attempt goes as a plain `TXT_MSG`. Each attempt keeps its own expected ACK in its own slot of the 8-slot table, as today (`MyMesh.cpp:5607-5610`), so attempt 0's ACK is still matched when it arrives after accept. The risk is that it arrives late enough for 8 later sends to wrap the table first. That's #999's existing eviction, made more likely here, and D2 measures it. | 3 |
| **C3 · Advert capability hint:** if the contact's Offband info advert says it doesn't accept introductions, skip straight to plain. | 2 |
| **C4 · Epic verification:** Offband to Offband without a key, Offband to stock firmware with a key (arrives on the first retry), and a stock client sending. | 2 |

### Epic D: Integration Testing (the Feature's final Epic)

| Task | Pts |
|---|---|
| **D1 · Hardware matrix:** two Offband companions (ESP32 and nRF52), one stock companion, one stock repeater in path, with stock Android and Offband clients. Every acceptance criterion on #2327, with captures. | 3 |
| **D2 · Airtime and edge cases:** the size and airtime of an introduced DM against a plain one, the held queue full, a reboot with messages held, and a refusal. | 2 |
| **D3 · User docs** for `dm.intro` and what the recipient sees. | 2 |
| **D4 · OUTCOMES.md** beside this plan. | 1 |

## 4. Order and dependencies

A, then B, then C, then D. B needs A's codec and tests. C needs B, because there's no point introducing before anything can receive one. Every Epic touches `examples/companion_radio/MyMesh.cpp`, so they run in sequence, never side by side. B4 lands after #1491 if that Epic has a fix by then; if not, B4's refusal path alone keeps the exposure closed.

## 5. Grants and authorization

| Term | Proposed | Why |
|---|---|---|
| **What merging this plan authorizes** | Building Epics A–D on `feat/2327-*` branches, opening their PRs stacked in order, and the flashes listed below. | The whole Feature, end to end. |
| **Merges** | 4 | One per Epic, A to D. |
| **Epic verification** | Owner | Each Epic's proof is on hardware; you approve each PR. |
| **Owner steps during the run** | None until D1, where you confirm the stock Android app's behavior. | The firmware and rig steps are mine. |
| **Stops** | Any failed verification, any stock-interop surprise, any change to a wire format already shipped. | These are the Feature's two hard constraints. |
| **Flash** | 8 | Two bench companions × (B5, C4, D1, D2). The stock companion is flashed once with stock firmware outside this budget, on its own approval. |

The Epics are #2329 (A), #2330 (B), #2331 (C) and #2332 (D). Epic A's tasks are #2333–#2336. Epics B–D get their tasks filed when each starts.

```grant-terms
{
  "epic_chain": [2329, 2330, 2331, 2332],
  "chain": true,
  "verification": { "mode": "owner" },
  "merge": { "budget": 4, "basis": "one merge per Epic, A through D (section 3)" },
  "flash": { "budget": 8, "basis": "two bench companions flashed for each of B5, C4, D1 and D2 (section 3)" },
  "reset": null,
  "expires_after_hours": 168
}
```

## 6. Can't-tell branches

| Where | Can't tell what | Does |
|---|---|---|
| B1: an `ANON_REQ` arrives | The MAC fails | **stop**: drop it, which is what happens today |
| B1 | The sub-type is unknown, or the length is wrong | **stop**: drop and count |
| B1 | The timestamp is no newer than one already held for that key (possible replay) | **stop**: drop and count |
| B1 | The node isn't a chat node (a repeater, room or sensor build) | **stop**: never handled, per D13 |
| B2 | The held queue is full | **stop**: refuse the newest, count it, log it; never evict |
| B3 | The client adds a key that isn't in the queue | Normal add; nothing to deliver |
| B4 | The contact table is full at accept | **stop**: refuse, push `CONTACTS_FULL`, keep holding |
| C2 | We can't tell whether the recipient holds our key | **Introduce on attempt 0, plain on every retry.** This isn't fail-open: both paths are real sends that each fail closed at the recipient. |
| C2 | The contact type is unknown or not chat | **stop**: plain DM only |
| C3 | No Offband info advert from the contact | Treat as "may accept"; attempt 0 is introduced and the retries cover stock |

There are no fail-open branches.

## 7. Verification

- **Named command:** `pio test -e native` (the native suite, including the new `test_intro_dm`) and `pio run` for every env in the CI matrix. Every Epic PR shows both outputs.
- **Acceptance bar:** A3's before-state tests flip to pass. The D1 captures show each acceptance criterion on #2327. No existing native test changes behavior.

## 8. Done when

- A DM from a sender the recipient never heard is delivered after accept and dropped after ignore, on two radios.
- Nothing from an unaccepted sender reaches the message store, the client or a screen.
- A stock-firmware recipient that holds the sender still gets the DM, on the first retry.
- A stock client sends, and accepts an introduced sender, with no change.
- A DM dropped because the sender is unknown is visible in a capture.

## 9. Your choices

You said "Proceed with the plan" without picking, so each row below takes the recommended option. Any of them can be reversed before the Epic it affects starts.

| # | Question | Option A | Option B | Rec. | Your choice |
|---|---|---|---|---|---|
| 1 | Sign the introduction (64 extra bytes, about 74 characters of text left) or rely on the ECDH MAC (D9)? | MAC only: same strength as every DM today, and room for text | Add an Ed25519 signature | ⭐ A | A (default) |
| 2 | When the recipient ignores or refuses an introduction, tell the sender? | Silent: it leaks nothing, and the sender's retries just go unconfirmed | Send a refusal the sender's client can show | ⭐ A | A (default) |
| 3 | A first DM too long for an introduction (more than 130 characters)? | Send it plain, and mark it "may not arrive: recipient may not have you" | Send an introduction without the text, then the full DM | ⭐ A | A (default) |
| 4 | Default for `dm.intro` | `ask` | `auto` | ⭐ A | A (default) |
| 5 | Initiative | #1650, as filed | Standalone, like #2246 | ⭐ A | A (default) |
| 6 | #470 | Close it as superseded by #2327 | Keep it open as a child note | ⭐ A | A, but **closing is yours**: #470 gets a pointer to #2327 and stays open until you close it |

## 10. Carried-in work

| Item | Goes to |
|---|---|
| #470: the same capability, body lost | Absorbed; closing is choice 6 |
| #1492 / #1493: stale ACK pointer | Epic B4 stays clear of it; the fix stays in #1491 |
| #993 / #971 / #999: delivery confirmation and ACK-table wrap | Unchanged by C2. D2 measures a late ACK after accept |
| #1770: GUI discovered contacts | Gets the held-introduction hook from B3 |
| The `@-` bodies on #470, #993, #996 and #971 | A separate defect; not fixed here |

## Review

Gemini 2.5 Pro reviewed this plan and the cited source on 2026-11-28: $0.0587, with 36,978 tokens in and 2,499 out. The audit log is local, at `docs/llm-consultations/2026-11-28-2327-single-key-dm-plan-gemini-gemini-2.5-pro.log` in the primary clone, and isn't committed. The findings and their dispositions are on #2327.

| # | Finding | Disposition |
|---|---|---|
| 1 | Retries change the ACK hash, so attempt 0's ACK is never matched | **Partly right.** The hash does differ per attempt, and the earlier "same ACK" wording was wrong. But each attempt takes its own table slot rather than overwriting, so the real risk is table wrap (#999). Fixed in C2. Its proposed fix was rejected; see alternatives. |
| 2 | The sub-type could collide with a stock login | **Accepted.** Pinned to `0x04–0x1F` with a `static_assert` (A2). |
| 3 | Held-queue RAM not stated | **Accepted.** About 0.9 KB at 4 entries, a build-time size, and a per-env report (B2). |
| 4 | The text limit is 128, not 138 | **Accepted that 138 was wrong.** Recomputed from `Mesh.cpp:514` as 130 (D10). |
| 5 | Accepting after a plain retry already landed shows the message twice | **Accepted.** Dedupe on sender, timestamp and text (B3). |

It confirmed that MAC-only authentication matches the existing DM's strength, that stock companions ignore the packet, and that `PUSH_CODE_NEW_ADVERT` works as a no-client-change accept path.

## Alternatives considered but not chosen

- **A new packet type for introductions.** Stock repeaters would still forward it, but nothing else in MeshCore understands it. `ANON_REQ` already carries the key and is ignored cleanly by stock companions (D11).
- **A separate introduction packet, then the normal DM.** That's two packets on every first DM, and the recipient has to hold ciphertext it can't yet attribute. One introduced DM is no extra packets when the text fits.
- **Introducing on every attempt** (Gemini's suggested fix for the ACK concern). It would keep one packet format per message, but it loses the DM on every stock recipient, which ignores `ANON_REQ` (D12). The late-ACK risk it was meant to fix is handled in C2 and measured in D2.
- **Persisting the proven set and the held queue to flash.** It's a storage-format change for a cost of one extra introduction after a reboot.
