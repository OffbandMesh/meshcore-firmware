# Feature plan — the three offset-87 features, realigned

**Epic:** #1994 · **Contract:** `docs/offband_info_frame_contract.md` rev 2 (PR #1998)
**Claim:** `claim/1994-wire-allocations` · **Written:** 2026-11-04, for approval before an unattended run

Everything that collided on device-info offset 87 is in this plan. Not the plumbing alone — the
features, working, on the new contract.

---

## 1. What you get in the morning

| feature | what works | where you touch it |
|---|---|---|
| **Channel notification All / Self / None** | per-channel mode, stored on the channel record | **on the badge**, a picker on the thread screen beside the tone — and from the client over `0x18`/`0x19` |
| **Display rotation** | rotation read/written over `0x1A`/`0x1B`, persisted, applied | client; advertised so it needs no round trip |
| **ACK capacity** | the device reports its outstanding-ACK capacity | client reads it from the advert |
| **The advert itself** | `0xC5 0x1C` — one request, whole capability list | nothing to touch; it's what makes the above discoverable |
| **The guard** | a red build when two branches claim one number or append to upstream's frame | nothing to touch; it's what stops the next collision |

**Offset 87 is written by nothing, and `FIRMWARE_VER_CODE` is neither bumped nor read.**

The badge picker is the piece you went looking for and found only a tone picker. Without it, channel
notify stays client-only, and your client half is itself overnight work by another session — so the
badge is the only path that doesn't depend on someone else finishing.

---

## 2. Order of work

**Nothing starts until `claim/1994-wire-allocations` is merged.** Contract §2: the numbers are not
mine until then, and a plan depending on unmerged numbers is not a complete plan. The claim is
header-only, compiles clean (`t1000e` SUCCESS), and covers `0x18`/`0x19`, `0x1A`/`0x1B`, `0x1C`,
caps2 `0x80`, error reason `10`, and the four advert tags.

Branch: `epic/1994-offband-info-frame`, cut fresh from `firmware-base` after the claim merges, with
`epic/606-channel-notify` merged into it so the whole thing is one PR you review once.

| # | task | what it touches |
|---|---|---|
| **T1** | `offband::InfoAdvert` — pure, Arduino-free tag writer. Ascending order, whole records only, omission, cursor resume, "did it complete" | new `src/helpers/OffbandInfoAdvert.h` |
| **T2** | Host tests for T1: ordering, len 0, omission, exact fit, one byte over, cursor round trip, ascending violation | new `test/test_offband_info_advert/` |
| **T3** | The `0x1C` arm — request validation (zero-payload or one cursor byte; longer → `ERR_MALFORMED`), assemble live tags, size from `maxFramePayload(header)` | `MyMesh.cpp` |
| **T4** | **The guard.** Tag/sub-code/caps-bit/error-reason uniqueness, source-vs-header agreement, and **no new byte appended to upstream's device-info frame** — scanning local branches and worktrees. Lifted from WhiteFalcon's `check_subcode_uniqueness.py` (#1990), credited | new `scripts/check_wire_allocations.py` + test, `ci.yml` |
| **T5** | Channel notify: delete the offset-87 write from the merged-in #606 work; emit `TAG_CHAN_NOTIFY` | `MyMesh.cpp` |
| **T6** | **Badge per-channel notify picker** — on the thread screen beside the tune picker, All / Self / None / follow-device, writing the same channel-record byte the client writes | `ui-new/BadgeScreens.cpp`, tests |
| **T7** | Rotation: apply the inherited patch, realign to the contract — `0x1A`/`0x1B` keep working, reason `10` for a non-rotating display, emit `TAG_DISPLAY_ROTATION`, **nothing in device-info** | `MyMesh.cpp`, `NodePrefs.h`, `AbstractUITask.h`, `ui-new/UITask.h`, `main.cpp` |
| **T8** | ACK capacity: emit `TAG_ACK_CAPACITY`; drop #994's byte-87 write and **revert its `FIRMWARE_VER_CODE` 23 → 22** | `MyMesh.cpp`, `MyMesh.h` |
| **T9** | Full native suite, curated matrix, user docs updated, images staged for `qcc-badge-1` and `sensecap-t1000e-1` | — |
| **T10** | Push, PR, CI monitor on | — |

T1–T4 don't depend on the contract PR merging, only on the claim. T5–T8 are the features.

---

## 3. Two things I will not guess about

**T6's wording and layout.** It's UI on a 128-pixel badge screen, and your standing rule is that
user-facing copy gets your eyes before it ships. I'll build it with labels matching the existing
`notifyScopeLabel` idiom and the tune picker's shape, and flag the exact strings in the morning
report for you to change. I am not holding the task for it.

**Rotation's inherited semantics.** The patch I extracted is WhiteFalcon's design, unreviewed by you.
I'm realigning it to the contract, not redesigning it — if its rotation value space or UI behaviour
is wrong, that surfaces in the morning rather than me inventing a different one overnight.

---

## 4. Gates — approving this plan approves these

1. Commits on `epic/1994-offband-info-frame`, under their own issues and Citadel tasks.
2. Issue and task creation for T1–T10, board Agent field stamped.
3. **Closing** each task issue and Citadel task as its commit lands and the SHA is read back. *(Normally yours per standing rule — this plan is the authorization, and every close is listed in the morning report.)*
4. Push and open the PR when the matrix is green.
5. Gemini 2.5 Pro review per task — ~10 calls, historically $0.02–$0.05 each, so **under $0.60**. Exact spend reported.
6. CI red: I fix and push again, as many times as needed. That is the work, not a catastrophe.
7. **Not authorized, and I will not:** merge any PR, flash any device, touch another session's branch, or change a merged contract.

---

## 5. Catastrophe policy

I do not stop. Work around it, keep going, present the workaround and the problem when you wake.

| problem | what I do |
|---|---|
| the claim isn't merged when I reach T1 | build nothing that uses a number; write T2's test scaffolding and T4's guard, which need no allocation |
| a matrix env fails | fix it; if the cause is outside this epic, isolate, record, carry on |
| Citadel unreachable (it hung earlier tonight) | retry; keep working and commit when it returns rather than idling |
| the inherited rotation patch conflicts after the #606 merge | resolve it; if the conflict is semantic rather than textual, implement to the contract and record the deviation |
| another session pushes a conflict | re-land on a fresh branch from the base, never merge the base in |
| genuinely no path | stop, write up where and why, leave the tree clean |

---

## 6. The risk worth stating

The contract is ~160 lines of wire specification written tonight under time pressure, revised once
after you caught four defects in it, and reviewed by nobody but me and two agent sessions. Both repos
then build to it in parallel overnight. A remaining design error gets implemented twice.

What reduces it: the claim is merged separately and first, so the numbers can't drift; §5 is
non-authoritative so the header is the single source; the guard in T4 makes a future collision a red
build; and nothing is removed from upstream's frame, so no deployed client changes behaviour.

What doesn't reduce it: me re-reading my own spec. The cheapest insurance remains you reading §3 and
§5 of rev 2 before approving — the part that says what a tag means and which tags exist.
