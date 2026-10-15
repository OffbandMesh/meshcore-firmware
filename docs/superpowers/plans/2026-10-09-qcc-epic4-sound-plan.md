# QCC badge sound, GPS power and RGB — **Feature** plan of record

**Feature:** #1190, re-leveled from `type:epic` · **Plan task:** #1408 · **Branch:** `epic/1190-badge-sound`
**Derives from:** `docs/superpowers/plans/2026-09-12-qcc-0x4-badge-plan.md`, Epic 4 (the eight-item outline)
**Written:** 2026-10-09 · **Revised:** 2026-10-14 (the maker's RTTTL answer) · **Re-leveled:** 2026-10-14

> **Why this is a Feature and not an Epic.** The 2026-09-12 outline filed this as Epic 4, and the
> first two drafts of this plan kept that label while describing five independent deliverables
> plus GPS modes plus an RGB flag. The owner called it on 2026-10-14: *it reads as a feature with
> epics underneath it.* He is right, and the hierarchy's own test says so — an "Epic" that keeps
> growing new independent deliverables was a Feature all along, and each of the units below needs
> several PRs and its own verification. Left as one Epic it would have been a single branch
> carrying a dozen tasks to one PR, which is exactly the shape that backfires.
>
> #1190 is therefore re-leveled to `type:feature`, with the Epics below as its children and an
> Integration Testing Epic last.
>
> **The filename still says `epic4`, deliberately.** A Feature plan of record belongs at
> `docs/initiatives/<initiative>/<feature>/PLAN.md`, and choosing that path means naming the
> initiative — which depends on whether #1172 becomes one. That is the owner's call (see the end
> of this document), so the file stays put until he rules, rather than being moved twice.

---

## What you get when this is done

A badge whose sounds you choose rather than inherit. Settings → Tones picks the tune for each
event from a built-in list, with a preview so you hear it before you keep it. Then **any RTTTL
code at all**: type one in on the keyboard and it becomes a tune the badge can use anywhere a
built-in can — which is what the badge maker asked for, and the same thing Meshtastic's custom
ringtone gives you. A later step lets a conversation carry its own tune, so `#queencitycon` and a
DM sound different and you know what buzzed without looking at the badge. The GPS stops being
on-or-off and gains a power-save mode with a duty cycle. And there is one tune the picker never
shows, reachable only by a trigger, so the CTF work under #1191 can make a hall of badges play
something together.

---

## Grounding: the sound stack is already built

Epic 4's outline was written on 2026-09-12, before the badge was running. It reads as though
sound has to be built. It does not. `[verified: origin/firmware-base @ 324dd6c5, 2026-10-09]`

| Already in the tree | Where |
|---|---|
| Non-blocking RTTTL driver, pumped from the UI loop | `src/helpers/ui/buzzer.{h,cpp}`, `NonBlockingRtttl`; `UITask::loop()` calls `buzzer.loop()` while playing |
| Startup and shutdown tunes | `buzzer.h` private `startup_song` / `shutdown_song`; `UITask::begin()` and the shutdown path |
| A quiet pref that survives reboot | `NodePrefs::buzzer_quiet`, serialized as `comp.buzz_q`; applied at `begin()`, toggleable from the UI, and settable from the client through notify-scope |
| Hardware mute | MUTE slide switch in series with BZ1, so silence never depends on firmware |
| Three events that play | `UITask::notify()` — `contactMessage`, `channelMessage`, `ack` |

**What is actually missing is two things, and neither is the engine.**

1. **No human choice.** Every tone is an RTTTL string literal at its call site:
   `[verified: examples/companion_radio/ui-new/UITask.cpp:928-945, mirrored in ui-orig/UITask.cpp:92-103]`

   ```cpp
   case UIEventType::contactMessage: buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7"); break;
   case UIEventType::channelMessage: buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");       break;
   case UIEventType::ack:            buzzer.play("ack:d=32,o=8,b=120:c");                   break;
   ```

2. **The event set is short, and one event does not exist.** `roomMessage` and
   `newContactMessage` fall through the `switch` silent, and **there is no sent event at all** —
   the outline's "message sent" was never built. `kerplop` is the *channel* tone, not the sent
   tone. Any earlier note calling it the sent tone is wrong.

---

## Decisions

Owner, 2026-10-08/09, answering the scoping questions on the epic.

| # | Decision | Consequence for this plan |
|---|---|---|
| D1 | A **built-in tune picker** is the want. Ship recognizable melodies; let a human pick. | Slice A. No client work. |
| D2 | **Per-source tunes** are wanted "if we can get there". | Slice D, last, because D6 displaced it. |
| D3 | **RTTTL, not Nokia Composer notation.** **Settled 2026-10-14** by the badge maker (owner-relayed): he pointed at how Meshtastic lets you set a custom ringtone from an RTTTL code, and that is the mechanism he wants. | **The notation converter is dropped.** See Alternatives. |
| D4 | A **hidden tune** — a Rick Roll, not on the default playlist — triggered remotely as part of the #1191 secret-trigger work. | Slice C: a play-by-id seam plus an un-listed table range, so #1191 needs no change to sound code when it is unparked. |
| D5 | **Sound stays default on** for attendees. | No change: `buzzer_quiet` already defaults to 0 and the startup tune already plays. Recorded so nobody "fixes" it. The MUTE switch and Settings both recover it. |
| D6 | **Arbitrary RTTTL is a requirement, not a nice-to-have.** **New 2026-10-14.** The maker's ask is not "pick from a list" — it is to set the incoming-message sound to *any* RTTTL code, the way Meshtastic does. | Slice B, promoted ahead of per-source. Satisfied by custom slots, which the id indirection already affords. |

### What D3 and D6 changed

The 2026-10-09 draft treated notation as the uncertain, expensive thing and arbitrary entry as the
cost driver to avoid. Both readings were wrong once the maker answered:

- **The converter was solving a problem nobody has.** He asked for RTTTL by name. A notation
  converter would have been built, tested and never used.
- **Arbitrary entry is cheap, because the widget already exists.**
  `[verified: src/helpers/ui/MsgCompose.h]` `compose::Composer<N>` is header-only, free of the
  mesh, already unit-tested natively, already templated on its capacity, and already has both an
  inline and a full-screen editor mode (`ThreadScreen::kInlineChars = 14`, `drawEditor`). An
  RTTTL field is `Composer<232>` plus a pref. The thing the draft flinched at is a type parameter.
- **So the ordering inverts.** Arbitrary RTTTL becomes Slice B and per-source drops to Slice D.
  Per-source was always the "if we can get there" item (D2); this one is the maker's actual ask.
  Flagged as a change from the agreed shape rather than made silently — veto it and I will put
  per-source back ahead of it.

| D7 | **The hidden tune may ship in the repo.** The owner assessed the risk and accepted it, 2026-10-14, on the grounds of non-commercial use, a few monophonic tones not being a reproduction of a work, and parody. | **O1 is closed.** The tune table may carry it: no USB-provisioning requirement, no separate decision gate, and not re-opened here. |
| D8 | **RTTTL is configured in Settings.** The owner's plan, stated 2026-10-14. | All tune configuration lives under **Settings → Tones**: the per-event rows, the preview, and the custom-slot editor. No separate top-level screen, and no RTTTL entry anywhere else. The one thing that stays on the conversation is the *per-source assignment* (E3), because that is where `pinned` and `muted` already live — flagged in case he wants that under Settings too. |

### Open, and deliberately not guessed

Nothing in this plan is now blocked on a decision. One **non-legal** consideration remains for
Slice C, recorded once and not a gate: a baked-in hidden tune is readable out of a public
source tree, so the surprise is only a surprise to people who have not looked. If that matters
for the CTF, the custom-slot path from Slice B carries it without the repo seeing it. If it does
not matter, bake it in — it is the simpler build.

---

## Shape

### The Epics

Each unit below is an **Epic**: its own branch, its own worktree, its own tasks, its own PR, its
own Epic verification. Each ends at a clean-built image the owner benches before the next starts.
The design narrative further down still calls them Slices A–D; this table is the mapping, and
**Slice = Epic** throughout.

| # | Epic | Slice | Why it is its own Epic | Depends on |
|---|---|---|---|---|
| E1 | Sound foundation and Settings → Tones | A (A0–A6) | Playback priority, the tune table, the event set, the prefs **and the picker screen**. The screen is in this Epic deliberately: it is how the engine gets verified, and you cannot verify what you cannot see. ~7 tasks. | — |
| E2 | Custom RTTTL slots | B (B1–B5) | The maker's ask (D6). Entry widget, two persisted slots, validation, its own bench round. | E1 |
| E3 | Per-source tunes | D (D1–D3) | Override table, conversation UI, eviction-safe cache. | E1, E2 |
| E4 | GPS power modes | G1, G2 | Off / On / Power-save state machine, UART release, the GPS screen's mode. Nothing to do with sound; shares only the Feature. | — |
| E5 | **Integration testing** | the verification section | The Feature's final Epic. Proves the whole thing together on the badge, on both cells, with current measured per GPS mode. | E1–E4 |

Two items are **Tasks, not Epics**, because each is a single PR and inventing an Epic for them
would be towering:

| Task | Slice | Parent |
|---|---|---|
| Hidden tune + `playTuneById` seam | C (C1–C3) | E1 — it is a seam on the table E1 builds, and it unblocks #1191 |
| RGB LED behind `OFFBAND_RGB_LED_PIN`, off by default | R1 | E4, or standalone if E4 slips |

**Ordering:** E1 → E2 → E3, with E4 parallel-able (it touches no sound code), then E5 last.
E1 is the base-capabilities-first Epic; nothing else is testable without it.

### The design narrative

```
A  Tune table + picker + the full event set        <- the nostalgia hit, standalone
B  Custom RTTTL slots + entry screen               <- the maker's actual ask (D6)
C  Hidden tune + play-by-id seam                   <- unblocks #1191, tiny
D  Per-source tunes (conversation overrides A/B)   <- the "if we can get there" item
```

**One architecture carries all four, and it is the id indirection in Slice A.** A tune is a
`uint8_t` everywhere — in the per-event prefs, in the per-source table, in the trigger call. Ids
below `kBuiltinCount` index the compiled table; ids at or above it index a custom slot. So
Slice B does not add a parallel mechanism for "custom" tunes: it adds *entries*, and per-event
(A), the hidden trigger (C) and per-source (D) all point at a typed-in RTTTL string with no
extra plumbing. Getting A's indirection right is what makes B, C and D small.

---

## Slice A — tune table, picker, full event set

### A0. Playback priority — a prerequisite, not a polish item

`[verified: src/helpers/ui/buzzer.cpp:18-28]` `genericBuzzer::play()` opens with
`if (isPlaying()) rtttl::stop();` — it **preempts unconditionally and has no notion of
priority**. Today that is harmless, because the only callers are real events. The moment the
picker previews a tune (A4), two bad things become reachable:

- a real notification arriving mid-preview is cut off by nothing, or cuts the preview off
  mid-note, depending on ordering; and worse,
- **a preview silences a real notification that is already playing** — on a con floor, the one
  chime that mattered.

So A0 comes before A4: `play()` takes a priority, real events are high, previews are low. A high
call preempts a low one; a low call is **dropped** while a high one is playing (and the screen
says so, rather than appearing to do nothing). `playTuneById` (Slice C) takes a priority too, and
a trigger is high.

This is a change to shared `buzzer.{h,cpp}`, which 60 other variants compile. It is additive —
the existing `play(const char*)` keeps working as high priority — so no other board's behavior
moves. Host tests cover the three orderings.

### A1. The tune table

New `src/helpers/ui/Tunes.h`, header-only and pure (no Arduino, no mesh), so it unit-tests on
the host like `BadgeLayout.h` and `BadgeStore.h` do.

```cpp
namespace offband {
struct Tune {
  const char* name;    // what the picker shows; nullptr for a hidden entry
  const char* rtttl;   // the string buzzer.play() takes
};
}
```

- A `constexpr` array, indexed by `uint8_t`. **Index 0 is reserved for "silent"** so an event can
  be turned off individually without muting the badge.
- **Ids are append-only and never renumbered.** A stored pref holds an id; renumbering would
  silently re-point every badge in the field at a different tune. New tunes append.
- `name == nullptr` marks an entry the picker does not enumerate. That is the whole hidden-tune
  mechanism (Slice C), and it costs one `if` in the enumerator.
- Compiled behind `OFFBAND_TUNE_TABLE`, set only by the badge env, so the other sixty variants
  neither compile the table nor change size.

Contents of the shipped list are the owner's to approve as user-facing content before it lands —
that is a copy review, not an implementation question. The plan proposes: silent, the three tones
already in use (so the current behavior is a selectable option and nothing is lost), *Gran Vals*,
and a handful of short recognizable public-domain melodies.

**Flash budget, stated rather than assumed.** The table's strings live in `.rodata` and are not
free. A recognizable melody is roughly 60–120 bytes of RTTTL, so a 16-entry list is on the order
of 1–2 KB. The badge env carries `board_upload.maximum_size = 712704`
`[verified: variants/qcc_badge/platformio.ini]`, so A1 ends by recording the measured image-size
delta against that ceiling, not an estimate. If the list ever has to grow past what fits, the
answer is fewer shipped tunes, not a bigger ceiling.

### A2. The event set

Extend `UIEventType` handling so every event has a slot, and **add the sent event** the outline
asked for:

| Event | Today | After |
|---|---|---|
| `contactMessage` | MsgRcv3 | pref `t_dm` |
| `channelMessage` | kerplop | pref `t_ch` |
| `roomMessage` | **silent** | pref `t_room` |
| `newContactMessage` | **silent** | pref `t_new` |
| `ack` | ack | pref `t_ack` |
| **sent** | **does not exist** | pref `t_sent` |
| startup / shutdown | hard-coded in `buzzer.h` | prefs `t_up` / `t_down` |

`UITask::notify()` becomes a table lookup: event → pref → tune id → `buzzer.play(table[id].rtttl)`,
with id 0 playing nothing. The literals leave the call sites.

**The sent event needs a caller, not just a case.** The send path is where Slice A touches
badge-adjacent code rather than only sound code, so A2 is the task that carries the file-overlap
risk and must be sequenced before A3.

### A3. Prefs

New keys in the existing `UIPrefs` group in `NodePrefs.h`, one `uint8_t` each.

This is safe and it is **not** the record-length trap. `[verified: NodePrefs.h:175-191 and its
own #1362 comment]` `/prefs.json` is keyed, so a badge with existing prefs simply has no `t_dm`
key yet and takes the compiled default; nothing shifts. The trap applies to `ContactInfo` and
`ChannelDetails`, which are separate length-serialized binary records — Slice B must not touch
those, and does not.

Defaults are chosen so **a badge that updates sounds exactly as it does today.**

### A4. Settings → Tones screen

Follows the existing badge settings pattern in `examples/companion_radio/ui-new/BadgeScreens.cpp`
(the same shape as Text size, Screen off and Battery): a row per event, pressing it opens the
tune list, moving the selection **previews the tune**, Enter keeps it.

- Preview goes through the same `buzzer.play()` the events use, so what you audition is what you
  get. It respects quiet: in quiet mode the screen says so rather than playing silently and
  looking broken.
- **No typing.** Everything on this screen is a selection from a list. That is deliberate — the
  reason D3 is a separate slice is that entering `d=4,o=5,b=180:…` on a CardKB is miserable, and
  Slice A never asks anyone to.

### A5. Tests (host, `test/test_tunes/`)

- Every table entry parses as RTTTL: name, `d=`, `o=`, `b=` and at least one note.
- Id 0 is silent; ids are dense and the array is `static_assert`-ed against its count.
- The enumerator skips `name == nullptr` entries and the visible count is stable.
- Event → pref → id resolution, including an out-of-range stored id falling back to the
  compiled default rather than reading past the array.

### A6. Slice A bench image

Clean build of `QCC_Badge_companion_radio_ble_diag`, bench steps written on the testing issue:
pick a tune per event, preview, reboot, confirm it stuck; mute and confirm the preview says so.

---

## Slice B — custom RTTTL slots and the entry screen

This is D6, the maker's actual ask: set the incoming-message sound to **any** RTTTL code, the way
Meshtastic does.

### How the string gets onto the badge — the constraint that decides the design

**There is no serial CLI on this badge.** `[verified: variants/qcc_badge/platformio.ini:130]`
`QCC_Badge_companion_radio_ble` builds `helpers/nrf52/SerialBLEInterface.cpp` and nothing else;
the companion protocol rides BLE and USB-CDC is the diag/log channel. So Meshtastic's own route —
set it from the app or a CLI — is not available here without client work the owner has excluded
before the con.

**It also cannot arrive as a mesh message.** The maker's own example is **218 characters**
`[measured]`, and `MAX_TEXT_LEN` is **160** (`10 * CIPHER_BLOCK_SIZE`)
`[verified: src/helpers/BaseChatMesh.h:8]`. A real RTTTL code does not fit in a text payload, so
"message it to the badge" is not a mechanism — it is a truncation bug waiting to happen.

That leaves **on-badge entry**, which is the route the owner already sanctioned for this audience
("patient when doing something asinine"). It is far cheaper than the 10-09 draft assumed.

### B1. The entry widget is already built

`[verified: src/helpers/ui/MsgCompose.h]` `compose::Composer<N>` is header-only, mesh-free,
natively unit-tested, **templated on its capacity**, and `ThreadScreen` already drives it in two
modes — inline under `kInlineChars = 14`, and a full-screen editor past that via `drawEditor()`.

So the RTTTL field is `compose::Composer<kRtttlCap>` with `kRtttlCap = 231`, reusing the editor
mode the thread screen already has. No new text-entry code, no new key handling.

**It lives under Settings → Tones** (D8), as a *Custom tunes* row that opens the slot list, where
a slot opens the editor. That keeps every tune decision — per-event choice, preview, and the
strings themselves — on one screen, and it means the full-screen editor is reached the same way
on the badge as composing a message is.

- **231, not 160.** Sized above the maker's 218-char example with headroom, and deliberately not
  tied to `MAX_TEXT_LEN` — these are unrelated budgets and coupling them is how the 160-char
  truncation gets rediscovered later.
- `start(budget)` already clamps to the template bound and backspace already always works, so the
  overrun case is handled by code that is already tested.

### B2. The slots

`kCustomSlots = 2` entries, each `char[kRtttlCap + 1]`, persisted as char-array prefs in the `ui`
group — the same mechanism `node_name` and `defs_nm` already use
`[verified: NodePrefs.h:127, 216]`. 464 bytes of prefs.

- They appear in the per-event picker as `Custom 1` / `Custom 2`, named by their RTTTL title field
  when it has one, so the list reads as the tune's own name rather than a slot number.
- **Ids at or above `kBuiltinCount` are custom slots.** That is the whole integration: a custom
  tune is selectable per event (A), assignable per source (D) and playable by the trigger (C)
  without any of them knowing it is custom.
- **Two, not eight.** The ask is the incoming-message sound, not a full soundboard. Two covers
  "DMs sound like this, channels like that" and costs under half a kilobyte; eight is 1.9 KB of
  prefs for a want nobody stated.

### B3. Validation at entry, not at playback

An RTTTL string typed on a tiny keyboard will contain typos. The slot is validated **when it is
saved**, with the reason shown on screen, and a slot that fails to validate is not stored.

Validation is a pure function in `Tunes.h` — title, `d=`, `o=`, `b=` and at least one parseable
note — so it is the same check A5 already runs over the built-in table, and it unit-tests on the
host. Playback never receives an unvalidated string.

### B4. Tests

Round-trip through the pref, the 218-char example stored and recovered byte-for-byte, an
over-length paste refused at `kRtttlCap`, malformed strings refused with a reason, an empty slot
rendering as empty rather than as a tune, and id resolution across the builtin/custom boundary.

### B5. Slice B bench image

Type the maker's own 218-char example into a slot, assign it to incoming DMs, reboot, confirm it
survived and plays. That is the acceptance: his example, on his badge, from the keyboard.

---

## Slice C — hidden tune and the trigger seam

Small, and its value is that #1191 never has to touch sound code.

- **C1.** `playTuneById(uint8_t id, Priority)` on the UI task — plays any id including un-listed
  ones, takes the high priority A0 introduced, honors quiet and the MUTE switch, and is a no-op
  for an out-of-range id. This is the entire public surface #1191 needs.
- **C2.** One un-listed table entry reserved for the trigger. Per **D7** the owner has assessed
  and accepted the risk of shipping it, so it may simply live in the table. The one remaining
  consideration is not legal but practical: a baked-in tune is readable out of a public source
  tree, so if the CTF wants the surprise intact, Slice B's custom slots carry it without the repo
  seeing it. Either build is the same seam.
- **C3.** Tests: an un-listed id plays through `playTuneById` and never appears in the picker's
  enumeration; a custom-slot id plays through it identically.

**Not in this slice:** the trigger itself — the beacon, the provisioning, the jitter. That is
#1191, parked by the owner, and nothing here unparks it.

---

## Slice D — per-source tunes

The D2 "if we can get there" item, last because D6 displaced it. Additive: without it, the
per-event choice from A (and any custom slot from B) simply applies to every conversation.

### The seam already exists, and it is RAM-only

`[verified: src/helpers/BadgeStore.h]` `BadgeStore::Convo` already identifies a conversation the
right way for this — `kind` (Channel or Contact) plus `key[KEY_LEN]`, a contact's public key or a
channel's secret, deliberately key-based so "a conversation can't turn into someone else" when
the phone renames or re-sorts. It already carries a `muted` flag per conversation.

But `BadgeStore` is RAM (~8 KB, rebuilt from traffic) and its conversations are **evicted** under
pressure. So a tune stored only on `Convo` would not survive a reboot, and would be lost when a
quiet DM aged out. The same gap already applies to `muted`, which is worth noting as a separate
finding rather than fixing here.

### D1. A bounded persisted override table

A small badge-owned table, `kMaxTuneOverrides = 16`, each entry a **6-byte key prefix plus a
1-byte tune id**: 112 bytes. Persisted as a byte-array pref in the `ui` group, exactly the way
`btn` already carries the four button actions `[verified: NodePrefs.h:197]` — so no new file, no
new storage mechanism, and nothing shared is touched.

- 6 bytes of a 32-byte public key is the same prefix length the mesh itself uses for addressing
  at this scale; a collision picks the wrong tune and nothing worse. Stated as the accepted
  trade-off rather than left implicit.
- `Convo` gains a `uint8_t tune`, so the hot path in `notify()` is a field read, not a table scan.
- **`BadgeStore` does not read the table itself, and must not.** It is deliberately pure — no
  Arduino, no mesh, no filesystem — so it has no way to reach `NodePrefs`, and a first draft of
  this plan described a hydration step that could not be built. The flow is: the caller gets the
  conversation index from `store.convo()`, looks the key prefix up in the pref table, and pushes
  the result in through a new `store.setTuneForConvo(int convo, uint8_t tune)`. The lookup lives
  in the layer that can see both halves; `BadgeStore` stays a pure container.
- The table is the truth; `Convo.tune` is a cache. Eviction loses the cache, not the setting.
- **16, not 350.** A per-contact tune for every contact is not the want and would not fit; the
  table is for the handful that matter, and anything not in it falls back to the per-event tune
  from A. That fallback is what makes D additive rather than a rewrite of A.

### D2. Where it is set

On the conversation, not in a separate list: the thread screen's existing per-conversation
actions (where `pinned` and `muted` already live) gain a Tune row using the same picker widget
Slice A built — which by then also lists B's custom slots, so a conversation can be given a
typed-in tune with no extra work.

### D3. Tests

Insert, replace, clear, full-table behavior, prefix collision, and fallback to the per-event tune
when absent. Round-trip through the packed pref blob. All host-side.

---

## GPS power and the RGB option

Carried from the 2026-09-12 outline. **GPS modes are E4, their own Epic** — they share nothing
with the sound code but the Feature, which is exactly why bundling them under one Epic was wrong.
The RGB flag is a single Task.

- **G1.** GPS Off / On / Power save, interval default 60 min, fix timeout default 180 s, UART
  pins released when off. The badge already has `DEFAULT_GPS_ENABLED=1` and `PIN_GPS_EN=5`
  (P0.24 → Q1, HIGH = powered) `[verified: variants/qcc_badge/platformio.ini]`, and the owner has
  already confirmed the power cut works — the GPS module's own fix LED goes dark when the app
  turns GPS off. So this is a state machine over a proven pin, with host tests for the machine.
- **G2.** GPS screen. One exists already; this adds the mode.
- **R1.** RGB LED behind `OFFBAND_RGB_LED_PIN`, off by default, no badge env setting it until
  there is hardware to point it at.

---

## Can't-tell branches

Every branch where the code cannot confirm its condition, and what it does. None of these
proceeds on a guess.

| Branch | Cannot confirm | What it does |
|---|---|---|
| **Any** stored tune id ≥ the compiled table size | Whether the id came from a newer firmware's table | **Falls back** to the compiled default for that event, and logs it. Never indexes the array. **One bounds check serves every source of an id** — the per-event prefs *and* the per-source override blob — because the downgrade case is real: a badge given tune 20 on a later build, then rolled back to a 15-entry table, still has 20 in its prefs. |
| A typed custom slot does not validate | Whether the human meant something parseable | **Refused at save, with the reason on screen.** The old slot stays. An unvalidated string never reaches playback. |
| A custom slot's id is selected but the slot is empty | Nothing — emptiness is checkable | **Falls back** to the compiled default for that event, and the picker shows the slot as empty rather than as a tune. |
| Entry reaches `kRtttlCap` | Nothing — the bound is known | **Further printables are dropped and the remaining count shows 0.** `Composer` already does exactly this; backspace still works. No truncation of a stored string, because an over-length string is never stored. |
| Override table full on insert | Which entry the human cares least about | **Refuses and tells the human the table is full**, with what to clear. It does not evict silently. |
| Key prefix matches two conversations | Which one was meant | **Plays the matched entry's tune.** Named as an accepted trade-off, not a silent branch; the cost of being wrong is the wrong chime. |
| `playTuneById` out of range | Whether a caller meant a real tune | **No sound, returns false.** A trigger that names a tune the badge does not have is not a reason to play a random one. |
| Low-priority preview arrives while a high-priority tune plays | Nothing — the driver knows | **Dropped, and the screen says so.** The alternative, preempting, is the failure A0 exists to prevent. |
| Preview requested while quiet | Nothing — this is known | **Shows "Buzzer: OFF"** rather than previewing silently, so the screen is never mistaken for broken. |

No fail-open path is proposed, so no fail-open grant is needed.

## Reviewed, and one finding refuted

Gemini 2.5 Pro reviewed the 2026-10-09 draft against `BadgeStore.h`, `buzzer.h` and
`NodePrefs.h` (`docs/llm-consultations/2026-10-09-qcc-epic4-sound-plan-gemini-gemini-2.5-pro.log`).
Five findings were folded in: playback priority (A0), the `BadgeStore` hydration flow (now D1),
the flash budget (A1), the string-failure contract, and generalizing the id bounds check to every
source of an id (can't-tell table).

**One of those findings outlived the slice it was written against.** It hardened
`nokiaToRtttl`'s failure contract — return false *and* null-terminate, never leave a partial
string for a caller that ignored the return value. The converter is gone (D3), but the hazard is
not: Slice B now takes a typed string of up to 231 characters and stores it. So the principle
moved rather than lapsed — **B3 validates at save and refuses, so no unvalidated or truncated
string is ever stored or played.** The 2026-10-14 revision is not Gemini-reviewed yet; that
happens before Slice A's task closes, per the per-task review rule.

**One finding does not hold, and is recorded rather than acted on.** It read the plan as silent
on waking the display for a notification. The badge already does that:
`[verified: examples/companion_radio/ui-new/UITask.cpp:980-990]` `UITask::newMsg()` turns the
display on for an incoming message — but **only `if (!hasConnection())`**, so a badge with a
phone paired plays the tune and leaves the screen dark. That is deliberate (#1230: the phone
catching up is no reason to take the badge's screen away) and **this Feature does not change it.** Wake
policy was settled in #1230 and #1239; sound is a separate concern and re-opening it here would
be scope creep against a decision already made.

Two observations fall out of that check, for the owner to dispose of, not for this epic:

- A phone-connected badge gets an audible notification and no visual one. Intended today; worth
  knowing before the con.
- The new `sent` and `ack` tones fire from the send path, which never touched display wake. They
  will be audible only. Also intended, by the same reasoning.

---

## Order, dependencies and file overlap

```
A0 priority ── A1 table ──┬─ A2 events ── A3 prefs ── A4 screen ── A5 tests ── A6 BENCH ─┐
  (shared buzzer)         └─ (A1's id indirection is what B, C and D all stand on)        │
                                                                                         ▼
                                       B1 widget ── B2 slots ── B3 validate ── B4/B5 BENCH
                                                                                         │
                                                         C1 seam ── C2 entry ── C3 tests ─┤
                                                                                         │
                                       D1 table ── D2 UI ── D3 tests ─────────── BENCH ───┤
                                                                                         ▼
                                       G1/G2 GPS (E4) ── R1 RGB ──── E5 integration testing
```

Read the lanes as Epics: `A0-A6` = **E1**, `B1-B5` = **E2**, `D1-D3` = **E3**, `G1/G2` = **E4**,
and the C tasks + R1 ride E1 and E4 respectively. Each lane is its own branch and PR.

- A0 lands before A4, because A4 is what makes the missing priority reachable.
- A2 and A4 both touch `ui-new/UITask.cpp` and the badge screens, so they are sequential, not
  parallel.
- A3, B2 and D1 all touch `NodePrefs.h`, in that order. Each adds keys; none moves an existing one.
- B1/B2 and D2 both touch the badge screens, so D waits on B — a second reason per-source is last.
- `ui-orig/UITask.cpp` carries the same three literals. It is not the badge's UI, so Slice A
  routes the badge through the table and **leaves `ui-orig` exactly as it is** — no behavior
  change for the boards that compile it.

## E5 — Integration testing (the Feature's final Epic)

This is the Feature-level check, distinct from and in addition to each Epic's own verification.
Run after E1–E4 have each merged, on the badge, with the INA228 inline on the battery lead. It
ends with `OUTCOMES.md` beside this plan, and the owner approves the results:

1. Each event fires its chosen tune, including sent and the two that were silent.
2. **A typed custom RTTTL slot plays** — the maker's own 218-character example, entered on the
   keyboard, surviving a reboot. This is D6's acceptance and the maker should be the one to say
   it passed.
3. A per-source tune beats the per-event tune; clearing it falls back.
4. Settings survive a DFU (the filesystem is preserved) and a power cycle.
5. `playTuneById` plays the un-listed id and the picker still does not show it.
6. Quiet and the MUTE switch both silence everything, and the badge says so.
7. **Current per GPS mode**, measured, not estimated — Off, On and Power save, and the delta
   that buys. Any threshold or figure that reaches an issue is a reading from this badge, not
   from the rig's gauge with an assumed offset.
8. Battery life at the chosen defaults, on the attendee cell (2000 mAh) as well as the bench
   badge's 3000 mAh, because they are different cells with different sag.

## Grant terms

Re-leveling changes these: five Epics means five PRs, not one.

```yaml
grant-terms:
  epic-chain: [E1, E2, E3, E4, E5]   # issue numbers filled in at AGREE; merged in this order
  verified-by: each Epic's own verification, owner-signed on the badge; E5 for the Feature
  merges: 5          # basis: one per Epic. Each merges only after its own verification,
                     #        so a red matrix or a failed bench strands that Epic, not the chain
  flashes: 8         # basis: one bench image per Epic that touches the badge (E1-E4) plus
                     #        four retries across the chain; each is a config-preserving
                     #        nRF52 serial DFU. E5 re-flashes nothing new
  reset: null        # rig RST is not gated
  expires: 30d       # basis: five Epics, each waiting on a bench round with the owner.
                     #        The 14d in the pre-re-level draft assumed one merge
```

## Alternatives considered but not chosen

| Alternative | Why not |
|---|---|
| A tune field on `ContactInfo` / `ChannelDetails` | Both are shared by every variant and persisted by record length. Adding a field changes the on-disk length fleet-wide — the migration failure already paid for on #627 / #663 / #665. |
| A separate badge tune file in the filesystem | A new storage mechanism for 112 bytes, when a keyed byte-array pref already exists and already works (`btn`). |
| Store tunes as strings per event instead of ids | Costs filesystem space proportional to tune length, makes the picker's identity fuzzy, and loses the "renumber is forbidden" guarantee that keeps a field badge pointed at the tune its owner chose. |
| Client-side tune management | The owner does not want to touch the client before the con. It is the right long-term home (it is how Meshtastic does it) and worth a follow-up issue after the con, but it cannot be the mechanism for D6 now. |
| **A Nokia Composer → RTTTL converter** | **Dropped.** The badge maker asked for RTTTL by name (D3). The converter would have been built, unit-tested and never used. Its one lasting contribution is the string-failure discipline now in B3. |
| Carrying an RTTTL string in a mesh text message | It does not fit. The maker's own example is 218 characters against a 160-character `MAX_TEXT_LEN`. A mechanism whose happy path truncates the payload is not a mechanism. |
| Reusing `MAX_TEXT_LEN` as the custom-slot capacity | Couples two unrelated budgets. A message cap is a radio-payload constraint; an RTTTL slot is a prefs-storage one. Coupling them is how the 160-character truncation gets rediscovered six weeks later. |
| Eight custom slots, one per event | 1.9 KB of prefs for a want nobody stated. Two covers the stated ask and anything beyond it is a soundboard, not a notification setting. |
| Bake the hidden tune's RTTTL into the repo | A copyrighted melody in a public MIT tree, and a surprise that anyone can read out of the source. USB provisioning solves both, and matches what Epic 5 already specifies. |
| Put the hidden-tune trigger in this epic | That is #1191, parked by the owner. Slice C gives it a seam and stops there. |

---

## Filing the chain (at AGREE)

**First, the re-level.** #1190 is retyped `type:epic` → `type:feature` and its Citadel entry
(`Crosswire-30i`) follows. Its three closed LED children (#1366, #1367, #1382) stay where they
are: they were delivered under the old shape and re-parenting closed work buys nothing.

**Then one Epic issue per E1–E5**, as `type:epic` sub-issues of #1190, Citadel entries depending
on `Crosswire-30i`, in dependency order. Each Epic gets its **own branch and worktree**
(`epic/<n>-<slug>`), its own PR, and its own lifecycle tasks as `type:task` sub-issues of it:

- **DIAGNOSE** — only where something is still unknown. E1 and E2 need none; their diagnosis is
  this plan. E4 needs one (the GPS state machine's current draw per mode is unmeasured).
- **PLAN** — the Epic plan, deriving from this Feature plan and refining only what it left open.
  E1's is thin because this document already specifies A0–A6 to the task.
- **BUILD** — one per task in the slice, not one per Epic. Each closes when its commit lands.
- **TEST** — Epic verification. Still open at push time, which is what clears the pre-push gate
  with BUILD closed.
- **push / PR / merge** — closes on `MERGED`.

The two standalone Tasks (the `playTuneById` seam, the RGB flag) are filed as `type:task`
sub-issues of their parent Epic and ride that Epic's branch.

This PLAN task (#1408) closes at agreement. Commit subjects reference their own BUILD task; the
Epic and the Feature stay out of commit subjects and take "Part of" in the body. Each Epic closes
after its verification and the owner's sign-off; **#1190 closes only after E5.**

### One thing for the owner, not for me

Re-leveling #1190 to a Feature puts a Feature under Feature #1172. The spine permits mixed
parentage and native sub-issues do not care, so this is not a mechanical problem — but #1172 now
has five substantial children of its own, which by his own stated levels (a broad umbrella is an
Initiative, each board is a Feature) may mean #1172 wants to be an Initiative. **That is his call
and this plan does not touch #1172.** Raised once, here, so it is on the record.
