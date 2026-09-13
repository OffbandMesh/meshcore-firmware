# QCC Epic 2, first slice: keyboard and SW1 — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the badge's CardKB-compatible keyboard drives the badge UI, and SW1 (the user button) keeps working beside it, with every key and gesture checkable on a diag build.

**Architecture:**
- A dependency-free key-decode header (`CardKbKeys.h`) maps the keyboard's bytes to the UI's existing `KEY_*` codes.
- A small I2C poller (`CardKbInput`) finds the keyboard at boot and reads one byte per poll.
- `UITask` feeds those keys into the screens' existing `handleInput(char)`, next to the SW1 events.
- A diag-only key-test screen shows every key and gesture for the bench check.

**Tech stack:** Adafruit nRF52 core (Wire), googletest native suite, the existing `ui-new` companion UI.

**Epic:** #1188 (Feature #1172). **Branch:** `epic/1188-qcc-keyboard`, stacked on `epic/1173-qcc-badge-bringup` because Epic 1 is not merged yet.

---

## Authority and scope (owner, 2026-09-13)

- The priority is the keyboard and SW1.
- The owner authorized overnight work to build this, **except flashing, push/PR, merge and closing epics**. It was too late to approve a separate plan, so the design authority is:
  - the approved spec §5.4 (Keyboard) and §6 (Keys);
  - Epic 2 items 1–2 in `2026-09-12-qcc-0x4-badge-plan.md`;
  - the owner's key list, quoted below;
  - M5's CardKB firmware as the source of record for the codes.
- The owner's key list: *"sticky SHIFT, FN, SYM keys, it has TAB, ESC, DEL (where BKSPC would normally be, look at cardkey for reference), 2 space keys, ENTER, and arrows (4 directions)."*
- The badge team: *"The keyboard on the badge is 100% compatible with the M5Stack CardKB."*
- Out of this slice: the history store, UI kit, inbox, compose and the other Epic 2 outline items. They are planned when this slice lands.

## Source of record: M5 CardKB firmware

From `m5stack/M5-ProductExampleCodes`, `Unit/CARDKB/firmware_328p/CardKeyBoard/CardKeyBoard.ino`:

| Key | Code sent | UI key |
|---|---|---|
| Esc | 27 | `KEY_CANCEL` (27) |
| Del (the backspace position) | 8; 127 with Shift | backspace (8) |
| Tab | 9 | tab (9) |
| Enter | 13 | `KEY_ENTER` (13) |
| Space (both keys) | 32 | 32 |
| Left / Up / Down / Right | 180 / 181 / 182 / 183 | `KEY_LEFT..KEY_RIGHT` (0xB4–0xB7), already the UI's values |
| Letters, digits, Sym-layer symbols | printable ASCII | passed through |
| Any Fn-layer key | 128–175 | dropped for now |

- **Modifiers.** Shift ("Aa"), Sym and Fn are sticky inside the keyboard: one press modifies the next key, a double press locks. The firmware never sends a code for a modifier itself, so the badge sees only the resulting character and carries no modifier logic.
- **Idle reads.** The keyboard writes a byte only when a key is waiting. An idle read returns 0, the AVR TWI default, so 0 means "no key".
- **0xFF.** Also treated as "no key", because that is what a missing device reads as.

---

### Task 2.1: CardKB key decode and I2C driver

**Files:**
- Create: `src/helpers/ui/CardKbKeys.h` — header-only, native-safe:
  - `cardkb::kAddress` = 0x5F;
  - `cardkb::toUiKey(uint8_t raw)`, the mapping in the table above;
  - `cardkb::keyName(uint8_t raw, char* out, size_t n)`, the short names for the key-test screen.
- Create: `src/helpers/ui/CardKbInput.h/.cpp` — `begin(TwoWire&)` probes 0x5F once; `poll(now)` reads one byte at most every 20 ms and returns the raw code, or 0.
- Test: `test/test_cardkb/test_cardkb.cpp` — table-driven over M5's full 48×7 key map:
  - every printable passes through;
  - Del and Shift+Del map to backspace;
  - Esc, Enter and Tab map to their UI keys, and the arrows are unchanged;
  - every Fn-layer code drops;
  - 0 and 0xFF are "no key";
  - `keyName` returns the expected names.

**Done when:** the native suite passes, the badge and ProMicro envs build, the Gemini 2.5 review is acted on, and it is committed.

### Task 2.2: Keyboard keys drive the badge UI

**Files:**
- Modify: `variants/qcc_badge/platformio.ini` — `-D UI_HAS_CARDKB=1`, and add `CardKbInput.cpp` to the badge's source filter.
- Modify: `examples/companion_radio/ui-new/UITask.h/.cpp`:
  - under `UI_HAS_CARDKB`: a `CardKbInput` member, probed in `begin()` (Wire is already up from `board.begin()`);
  - polled in `loop()` after the button;
  - keys pass through `checkDisplayOn()`, so the first key wakes a dark display just as SW1 does;
  - a keyboard key never goes through `handleLongPress()`, so it can never enter CLI rescue.
- Create: `src/helpers/ui/KeyNav.h` — `keynav::pageStep(key)`:
  - Left/Up/Prev return Prev, and Right/Down/Next return Next.
  - `HomeScreen` uses it, so the arrows move pages the same way SW1's click and double-click do.
- Modify: `UITask::loop()` dispatch — **Esc backs out**. If a screen does not take `KEY_CANCEL`, and it is not Home, go Home.
  - No input path produces `KEY_CANCEL`, `KEY_UP` or `KEY_DOWN` today, so other boards see no change.
- Modify: `src/helpers/ui/UIScreen.h` — add `KEY_BACKSPACE` (8) and `KEY_TAB` (9).
- Test: `test/test_keynav/test_keynav.cpp`.

**Behavior after this task:**

| Input | Home | Message preview | Self-test legend |
|---|---|---|---|
| Left or Up | previous page | — | — |
| Right or Down | next page | next message | — |
| Enter | page action (advert, BT toggle, GPS, hibernate) | clear and go Home | — |
| Esc | — | back to Home | back to Home |
| SW1 (unchanged) | click next, double-click previous, long press action, triple-click toggles the buzzer | same | same |

**Done when:** the unit tests pass; the badge, ProMicro and one other `ui-new` env build; the review is acted on; it is committed.

### Task 2.3: SW1 gestures pinned in unit tests

SW1 is the user button: P1.00, active low, with a 10 k pull-up and 100 nF on the badge. The code already matches the hardware: `MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true)` in `variants/qcc_badge/target.cpp`. This task pins that behavior so a later change cannot silently break it.

**Files:**
- Modify: `test/mocks/Arduino.h` — controllable pin levels: `digitalRead`, `pinMode`, `analogRead`, `HIGH`/`LOW`, the `INPUT*` modes.
- Modify: `platformio.ini` `[env:native]` — add `MomentaryButton.cpp`.
- Test: `test/test_sw1_button/test_sw1_button.cpp`, with the badge's exact configuration:
  - idle high is silent;
  - a short press is a CLICK only once the multi-click window closes;
  - two presses are a DOUBLE_CLICK, and three a TRIPLE_CLICK;
  - holding 1 s is a LONG_PRESS while held, with no CLICK on release;
  - `pinMode` gets `INPUT_PULLUP`.
- Modify: `scripts/test_qcc_variant_pins.py` — SW1 must be `PIN_USER_BTN` = index 6 = P1.00, built active-low with a pull-up.

**Done when:** the tests pass, the review is acted on, and it is committed.

### Task 2.4: Diag key-test screen

The bench instrument for this slice.

**Behavior:**
- On `_diag` builds with a keyboard, TAB on Home opens it.
- The self-test legend says so: `KB: found  TAB=keys`, or `KB: none`.
- **What it shows:**
  - the last keyboard key, as name and raw hex (`LEFT 0xB4`, `'a' 0x61`, `FN+128 0x80`);
  - the last SW1 gesture;
  - a typed line with a cursor: printables append, Del deletes, Tab inserts a space, Enter clears.
- **Leaving it:** Esc exits to Home. A long press on SW1 also exits, so a badge whose keyboard fails mid-test is never stuck.

**Files:**
- Create: `src/helpers/ui/LineEdit.h` — a fixed-buffer one-line editor (append, backspace, clear). The compose line reuses it later.
- Modify: `UITask.h/.cpp` — `KeyTestScreen` under `QCC_BADGE_SELFTEST && UI_HAS_CARDKB`; UITask records the last raw keyboard code and the last SW1 gesture, from before a handler consumes it.
- Modify: `variants/qcc_badge/QccSelfTest.h` — the keyboard line, and its test.
- Tests: `test/test_line_edit/`; `test/test_qcc_selftest/` extended.

**Done when:** the tests pass, the builds succeed, the review is acted on, and it is committed.

---

## Bench checklist (Epic 2 verification task, after the owner flashes)

1. The legend shows `KB: found  TAB=keys`.
2. On the key-test screen, each key shows its name and code:
   - Esc (exits), 1–0, Del (deletes);
   - Tab, q–p, a–l, Enter (clears), z–m, `,` `.`;
   - **both** space keys, and the four arrows.
3. Sticky modifiers:
   - Shift+a gives `'A' 0x41`, and Sym+q gives `'{' 0x7B`;
   - Fn+1 shows `FN+129 0x81` (shown, not acted on);
   - a double-tap lock keeps applying until tapped again.
4. SW1 on the key-test screen shows click, double-click and triple-click, and a long press exits.
5. Home:
   - Left/Up and Right/Down move pages, and Enter runs the page action;
   - with the keyboard unplugged at boot, SW1 alone still navigates, and the legend shows `KB: none`.
6. The display asleep: the first key wakes it and does nothing else.
