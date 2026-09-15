# QCC Epic 2, slice 2: compose and send from the keyboard

Epic #1188 (Feature #1172). This implements the approved design-of-record, `docs/architecture/2026-09-12-qcc-0x4-badge-design.md` §5.4–5.5. Slice 1 (the keyboard and SW1, #1204–#1207) is already built.

**Goal:** type a message on the badge and send it to a channel or a contact. DMs retry and show an ACK tick.

## In scope

- **Entry.** A **Message** page on Home. Enter, or a SW1 hold, opens the target picker. The page exists only when a keyboard is present.
- **Target picker.**
  - Lists the badge's channels first, then its chat contacts.
  - Arrows or a SW1 click move through the list; Enter or a SW1 hold picks; Esc backs out.
- **Compose.**
  - One line of text, using the existing `LineEdit`, with the remaining budget shown.
  - Enter sends; Esc cancels.
  - Typing stops at the budget. The mesh would otherwise cut the text silently:
    - a DM carries up to 160 bytes (`MAX_TEXT_LEN`);
    - a channel message carries 160 − (length of the node name + 2), because the packet is `<name>: <text>`.
- **Send.** These are the same mesh calls the phone path uses.
  - **Channel:** `sendGroupMessage` with the node name. There is no delivery receipt, as in MeshCore.
  - **DM:** `sendMessage`, with up to 3 attempts; each retry waits for the previous send's estimated timeout.
    - The status goes sending → delivered ✓ when the ACK arrives, or failed after the last attempt.
    - Badge sends keep their own small ACK table, checked in `processAck`. The phone never receives a confirmation for a message it didn't send.
- **Status.** The compose screen shows the last send's status until the next key.

## Later slices

- **Slice 3:** saved history (~100 messages), inbox and threads.
- **Slice 4:** new message with type-to-filter, join `#channel`, the first-boot handle, Settings, About, type-to-talk, and the new-message card.
- **Separate epic:** the phone bridge (#1189).

## Tasks

**A. The badge send API.** `MyMesh` gains:
- `uiSendChannel(idx, text)` and `uiSendDirect(contact, text)`;
- a pure tracker, `BadgeSendTracker.h`, for attempts, timeouts and ACK matching, driven from the mesh loop;
- a hook in `processAck`;
- a status callback to the UI.

Tests: native tracker cases (the ACK arrives in time, a retry after a timeout, failed after 3 attempts, a duplicate ACK, a full table), plus the badge and ProMicro builds.

**B. The compose UI.**
- `MsgCompose.h` is pure: it holds the budget rules and the target list.
- `UITask` gains the Message page, the target picker and the compose screen, driven by the keyboard and SW1.

Tests: native cases for the budget and the target list, the QCC pin guard, and the badge, diag and ProMicro builds.

**C. Bench verification, recorded on #1208.**
- A channel message typed on the badge arrives on the phone or another node.
- A DM arrives, and the badge shows ✓.
- A DM to an unreachable contact shows failed after 3 attempts.
- Typing stops at the budget.

## Gating

All of this sits behind `UI_HAS_CARDKB`, which is set only in the badge envs, so other boards build unchanged. Each task gets its own tests, a Gemini 2.5 review and a commit on `epic/1188-qcc-keyboard`.

## Defaults the owner can change

- 3 DM attempts.
- The Message page sits right after the first Home page.
