# Indicator Companion Contract (Epic B1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the **companion** user led + display control (the light-show-off feature) through the app, by extending the `0xC5` device-UI contract — because the companion has no CLI. Firmware side only; the client UI is a separate repo (owner).

**Architecture:** Companion led/display persisted in its own `NodePrefs`/`DataStore` blob; display applied through `ui-new`'s existing `setAlwaysOn` mechanism (generalized to a tristate, which also serves the observer's #141 `display always on`); exposed over the wire as new `0xC5` sub-codes and a new caps-byte-2 bit, with the `0xC5` gate generalized from "whole-command buzzer gate" to per-sub-code capability gating. Mirrors the `0xC3` FEM-LNA precedent.

**Tech Stack:** C++11/Arduino-ESP32, PlatformIO, companion-API binary frames, LittleFS prefs blob.

**Feature:** #542 · **Citadel:** create a B1 task before coding.

## Ownership & coordination (owner-directed 2026-08-03)

- **BrownHawk owns the `0xC5` gate structure** for this. **FuchsiaCreek (#509/#510) adapts** their
  notify-scope + button-matrix work to the generalized gate; they are currently on button-press
  detection. Coordination sent (Agent Mail msg 374). **Do not restructure the `0xC5` dispatch until
  FuchsiaCreek confirms no in-flight conflict on that block.**
- The matrix sub-codes (0x03/0x04) and their internal logic stay FuchsiaCreek's — this plan only
  lifts the outer buzzer gate into the scope arm and adds the led/display arms.

## ⚠ CORRECTED by FuchsiaCreek coordination (msgs 375/376, 2026-08-03)

Their #509/#510 work is **committed but unpushed**, so a branch scan couldn't see it. Two of this
plan's original assumptions were wrong:

1. **The `0xC5` gate is ALREADY per-sub-code** on `feat/509-button-dispatch @ 5becb6aa` — the blanket
   `#ifndef PIN_BUZZER` reject is gone (scope→`PIN_BUZZER`, matrix→`PIN_USER_BTN`). **Task 5 is no
   longer a restructure** — it is purely additive: two predicates (`is_display_sub`/`is_led_sub`),
   two gate arms (led runtime `board.canControlLed()` modeled on `CMD_OFFBAND_FEM_LNA` @ MyMesh.cpp:1977,
   display compile-time `DISPLAY_CLASS`), add both bools to the `(void)` line (:1851, or `-Werror`
   breaks on the full matrix), handlers before the unknown-sub tail (:1969).
2. **caps2 `0x02` is TAKEN** (`OFFBAND_CAP2_BUTTON_MATRIX`, #509). **Use `0x04`.**

## Execution — split by dependency

- **Companion-internal half (Tasks 2, 3, 4) — DO NOW, conflict-free.** `NodePrefs`/`DataStore` prefs
  (append after the already-**merged** `button_actions`) + `ui-new` tristate + boot-apply. Zero
  overlap with FuchsiaCreek (they're in `ui-orig/Button*` for #527).
- **Contract/dispatch half (Tasks 1, 5, 6) — GATED on #509/#510 landing.** Stack on their branch (not
  `firmware-base`) so the dispatch is already per-sub-code and the registry (0x01/0x02) is settled.
  **Owner decides** whether to land #509/#510 ahead of the #527 button work (FuchsiaCreek deferred it
  to Ben). Also still stacked behind A1 (#546) + A2 (#548).

## Merge-ordered shared enums (CORRECTED per msgs 375/376)

| Allocation | Value |
|---|---|
| `OFFBAND_UI_DISPLAY_GET` / `SET` | `0xC5` sub `0x05` / `0x06` (free — confirmed) |
| `OFFBAND_UI_LED_GET` / `SET` | `0xC5` sub `0x07` / `0x08` (free — confirmed) |
| `OFFBAND_CAP2_INDICATORS` | caps byte 2 bit **2 (`0x04`)** — 0x02 taken by BUTTON_MATRIX |
| error reason byte | **6** (`UNSUPPORTED_INDICATOR`) — 1-5 taken |
| `FIRMWARE_VER_CODE` | **read the merged value at rebase** (20 on FuchsiaCreek's branch; do NOT hardcode 21) |

**Conventions to keep in the SET arms (FuchsiaCreek):** compare-before-`savePrefs()` (a client
`onChange` control emits a SET burst; unconditional save = flash burn), and echo the STORED value back
(client renders what the device holds).

## Ground truth (verified)

- Companion uses **`ui-new`** UITask on all envs `[verified: platformio.ini +<ui-new/*.cpp>]`.
- `ui-new` already has `setAlwaysOn(bool)` + `_always_on` (#141 observer applier) —
  `[verified: ui-new/UITask.cpp:721, UITask.h:41]`. Generalizing it to a tristate serves BOTH the
  companion display setting and the observer's existing `display always on` (reconcile folds in here).
- `0xC5` handler at `MyMesh.cpp:1819`; hard `#ifndef PIN_BUZZER` early-return rejects the whole
  command; Heltec V4 has no `PIN_BUZZER`. `[verified]`
- caps byte 2 assembled at `MyMesh.cpp:2026-2033`; device-info appends at the tail, VER-gated.
- `0xC3` FEM-LNA (`MyMesh.cpp:1942`) is the SET/GET + device-info-value template.
- Companion `NodePrefs`/`DataStore`: led was offset 145 (A1's reverted patch); display → **146**.

## File Structure

| File | Change |
|---|---|
| `examples/companion_radio/OffbandConfigProtocol.h` | Declare `OFFBAND_UI_DISPLAY_GET/SET`, `OFFBAND_UI_LED_GET/SET`, `OFFBAND_CAP2_INDICATORS`; document the per-sub-code gate; bump VER_CODE note |
| `examples/companion_radio/NodePrefs.h` | Add `ui_led_enabled` (re-apply) + `ui_display_mode` |
| `examples/companion_radio/DataStore.cpp` | Persist both at offsets 145 / 146 (append-only, matched read/write) |
| `examples/companion_radio/MyMesh.cpp` | Defaults before loadPrefs; boot-apply led (board) + display (ui_task); generalize `0xC5` gate + add led/display sub-codes; caps2 `INDICATORS`; device-info values + VER_CODE 20 |
| `examples/companion_radio/ui-new/UITask.h` | Add `setDisplayMode(uint8_t)`; keep `setAlwaysOn` as a thin wrapper |
| `examples/companion_radio/ui-new/UITask.cpp` | Generalize the always-on mechanism to a tristate (auto/always-on/always-off) |
| `examples/companion_radio/MyMesh.h` | `FIRMWARE_VER_CODE` 19 → 20 |

Observer reconcile: the `#141` `display always on` applier (registered in main.cpp) calls the same
`setDisplayMode` — no separate B2 needed; note it in the PR.

---

### Task 1: Contract declarations (OffbandConfigProtocol.h)

**Files:** Modify `examples/companion_radio/OffbandConfigProtocol.h`

- [ ] **Step 1: Verify no new sub-code / caps bit landed since msg 365**

Run: `grep -nE "OFFBAND_UI_[A-Z_]+ *=|OFFBAND_CAP2_[A-Z_]+ *=" examples/companion_radio/OffbandConfigProtocol.h`
Expected: sub-codes 0x01-0x04 + 0x7F, caps2 0x01 only. If 0x05-0x08 or caps2 0x02 are taken, STOP and re-coordinate.

- [ ] **Step 2: Add the declarations**

In the `0xC5` sub-code block, after `OFFBAND_UI_MATRIX_SET = 0x04`:

```cpp
// #542 B1: indicator sub-codes (led + OLED). Gated on OFFBAND_CAP2_INDICATORS, NOT on
// the buzzer -- a display/LED setting needs no buzzer. mode: 0 auto, 1 always-on, 2 always-off.
constexpr uint8_t OFFBAND_UI_DISPLAY_GET = 0x05;  // -> [0xC5][0x05][mode]
constexpr uint8_t OFFBAND_UI_DISPLAY_SET = 0x06;  // [0xC5][0x06][mode] -> echo
constexpr uint8_t OFFBAND_UI_LED_GET     = 0x07;  // -> [0xC5][0x07][on]
constexpr uint8_t OFFBAND_UI_LED_SET     = 0x08;  // [0xC5][0x08][on] -> echo
```

Near the caps byte 2 block, after `OFFBAND_CAP2_NOTIFY_SCOPE`:

```cpp
// #542 B1: led/display runtime control present (canControlLed() || DISPLAY_CLASS).
// Independent of NOTIFY_SCOPE: a buzzer-less display board advertises THIS but not scope.
constexpr uint8_t OFFBAND_CAP2_INDICATORS = 0x02;
```

Update the `0xC5` header comment: the command is NO LONGER whole-gated on the buzzer; each sub-code
is gated on its own capability (scope→PIN_BUZZER, matrix→its capability, led→canControlLed,
display→DISPLAY_CLASS).

- [ ] **Step 3: Commit**

```bash
git add examples/companion_radio/OffbandConfigProtocol.h
git commit -m "feat(#542): 0xC5 led/display sub-codes + INDICATORS caps bit; per-sub-code gate contract"
```

---

### Task 2: Companion prefs — led (re-apply) + display

**Files:** `examples/companion_radio/NodePrefs.h`, `examples/companion_radio/DataStore.cpp`

- [ ] **Step 1: Re-apply A1's reverted companion led field, then add display**

In `NodePrefs.h`, after `button_actions[4]`:

```cpp
  // #542: indicator prefs. LED enable (1=normal,0=off); display mode (0 auto,1 always-on,2 off).
  // Serialized LAST in DataStore (offsets 145/146), append-only.
  uint8_t ui_led_enabled;
  uint8_t ui_display_mode;
```

(Base source: `docs/superpowers/epic-b-groundwork/542-companion-led-persist.patch` for the led half.)

- [ ] **Step 2: Persist both (DataStore read + write, matched order)**

After the `button_actions` read (offset 141):

```cpp
    file.read((uint8_t *)&_prefs.ui_led_enabled, sizeof(_prefs.ui_led_enabled));          // 145
    file.read((uint8_t *)&_prefs.ui_display_mode, sizeof(_prefs.ui_display_mode));         // 146
```

After the `button_actions` write:

```cpp
    file.write((uint8_t *)&_prefs.ui_led_enabled, sizeof(_prefs.ui_led_enabled));          // 145
    file.write((uint8_t *)&_prefs.ui_display_mode, sizeof(_prefs.ui_display_mode));         // 146
```

- [ ] **Step 3: Build + commit**

Run: `pio run -e heltec_v4_companion_radio_ble 2>&1 | tail -6` → SUCCESS

```bash
git add examples/companion_radio/NodePrefs.h examples/companion_radio/DataStore.cpp
git commit -m "feat(#542): persist companion ui_led_enabled + ui_display_mode (DataStore 145/146)"
```

---

### Task 3: Generalize ui-new to a display tristate

**Files:** `examples/companion_radio/ui-new/UITask.h`, `examples/companion_radio/ui-new/UITask.cpp`

- [ ] **Step 1: Read the full ui-new blank/always-on logic first**

Run: `sed -n '270,300p;680,740p' examples/companion_radio/ui-new/UITask.cpp`
Identify where `_always_on` gates the auto-off blank in `loop()`, so the tristate extends the SAME
gate rather than a parallel one.

- [ ] **Step 2: Add `setDisplayMode` + an always-off member**

In `UITask.h`, near `_always_on`:

```cpp
  uint8_t _disp_mode = 0;   // #542: 0 auto, 1 always-on, 2 always-off
```

Declare `void setDisplayMode(uint8_t mode);` and keep `void setAlwaysOn(bool on);`.

- [ ] **Step 3: Implement the tristate**

Replace/extend `setAlwaysOn` so it delegates, and add `setDisplayMode`:

```cpp
void UITask::setDisplayMode(uint8_t mode) {
  _disp_mode = mode;
  _always_on = (mode == 1);            // reuse existing blank-gate for always-on
  if (_display == NULL) return;
  if (mode == 2) {                     // always-off: dark now
    if (_display->isOn()) _display->turnOff();
  } else {                             // auto / always-on: light + fresh timeout
    if (!_display->isOn()) _display->turnOn();
    _auto_off = millis() + AUTO_OFF_MILLIS;
  }
}

void UITask::setAlwaysOn(bool on) { setDisplayMode(on ? 1 : 0); }  // #141 observer applier reconciles here
```

In `loop()`, where the auto-off blank runs, add an always-off guard so mode 2 stays dark and does not
relight on activity/newMsg (find each `_display->turnOn()` / `_auto_off = ...` activity path and gate
it on `_disp_mode != 2`). **Enumerate every relight path in ui-new (newMsg, button, connection) — the
grep in Step 1 lists them — and gate each.**

- [ ] **Step 4: Build + commit**

Run: `pio run -e heltec_v4_companion_radio_ble 2>&1 | tail -6` → SUCCESS

```bash
git add examples/companion_radio/ui-new/UITask.h examples/companion_radio/ui-new/UITask.cpp
git commit -m "feat(#542): ui-new display tristate (generalizes #141 always-on; reconcile)"
```

---

### Task 4: Defaults + boot-apply (companion)

**Files:** `examples/companion_radio/MyMesh.cpp`

- [ ] **Step 1: Defaults before loadPrefs**

Next to the existing `_prefs.radio_fem_rxgain = 1;` init:

```cpp
  _prefs.ui_led_enabled = 1;                   // #542 default on
  _prefs.ui_display_mode = 0;                   // #542 default auto
```

- [ ] **Step 2: Boot-apply**

Where FEM LNA is applied at boot (`if (board.canControlLoRaFemLna()) ...`):

```cpp
  if (board.canControlLed()) board.setLedEnabled(_prefs.ui_led_enabled != 0);
#ifdef DISPLAY_CLASS
  ui_task.setDisplayMode(_prefs.ui_display_mode);
#endif
```

- [ ] **Step 3: Build + commit** (`heltec_v4_companion_radio_ble` → SUCCESS)

```bash
git commit -am "feat(#542): companion boot-apply led + display mode from prefs"
```

---

### Task 5: Generalize the 0xC5 gate + add led/display sub-codes  ⚠ COORDINATION-GATED (FuchsiaCreek)

**Files:** `examples/companion_radio/MyMesh.cpp` (~1819)

**Do not start until FuchsiaCreek (msg 374) confirms no in-flight edit to this block.**

- [ ] **Step 1: Lift the buzzer gate into the scope arm**

Remove the top-level `#ifndef PIN_BUZZER { return NO_BUZZER; }` early-return. Move that check INSIDE
the scope sub-codes (`OFFBAND_UI_SCOPE_GET`/`SET`): if `!PIN_BUZZER`, those two arms return the same
`OFFBAND_UI_ERR_NO_BUZZER`. Matrix arms (0x03/0x04) unchanged. **Preserve exact scope/matrix behavior.**

- [ ] **Step 2: Add the led/display arms** (pattern from `0xC3`, echo the sub-code):

```cpp
    if (sub == offband::OFFBAND_UI_DISPLAY_GET) {
      out_frame[0]=RESP_CODE_OFFBAND_DEVICE_UI; out_frame[1]=sub; out_frame[2]=_prefs.ui_display_mode;
      _serial->writeFrame(out_frame,3); return;
    }
    if (sub == offband::OFFBAND_UI_DISPLAY_SET && len>=3) {
#ifdef DISPLAY_CLASS
      uint8_t want=cmd_frame[2];
      if (want>2){ /* OFFBAND_UI_ERR + OFFBAND_UI_ERR_MALFORMED */ ... return; }
      _prefs.ui_display_mode=want; savePrefs(); ui_task.setDisplayMode(want);
      out_frame[0]=RESP_CODE_OFFBAND_DEVICE_UI; out_frame[1]=sub; out_frame[2]=want;
      _serial->writeFrame(out_frame,3);
#else
      /* OFFBAND_UI_ERR + a "no display" reason */
#endif
      return;
    }
    // OFFBAND_UI_LED_GET / _SET: same shape, value 0/1, gated on board.canControlLed().
```

Add a reason byte for "unsupported indicator" to the error enum if none fits.

- [ ] **Step 3: Build + commit** (`heltec_v4_companion_radio_ble`, `heltec_v4_tft_companion_radio_ble` → SUCCESS)

```bash
git commit -am "feat(#542): 0xC5 per-sub-code gate + led/display SET/GET (companion)"
```

---

### Task 6: caps byte 2 INDICATORS + device-info values + VER_CODE

**Files:** `examples/companion_radio/MyMesh.cpp` (caps ~2026, device-info tail), `MyMesh.h`

- [ ] **Step 1: Advertise INDICATORS**

At the caps2 assembly (after the `#ifdef PIN_BUZZER ... NOTIFY_SCOPE` block):

```cpp
#ifdef DISPLAY_CLASS
    offband_caps2 |= offband::OFFBAND_CAP2_INDICATORS;
#else
    if (board.canControlLed()) offband_caps2 |= offband::OFFBAND_CAP2_INDICATORS;
#endif
```

- [ ] **Step 2: Append current values to device-info** (so the client renders on connect, no GET
round-trip — the FEM-LNA precedent). Append AFTER `offband_caps2`, unconditionally, VER-gated:

```cpp
    out_frame[i++] = _prefs.ui_led_enabled;    // v20+
    out_frame[i++] = _prefs.ui_display_mode;   // v20+
```

- [ ] **Step 3: Bump VER_CODE** in `MyMesh.h`: `19` → `20`, with a comment noting +led/display values.

- [ ] **Step 4: Build + commit**

```bash
git commit -am "feat(#542): advertise INDICATORS cap + led/display in device-info; VER_CODE 20"
```

---

### Task 7: Full matrix + Gemini + hardware

- [ ] **Step 1: Build matrix** — `heltec_v4_companion_radio_ble`, `_usb`, `heltec_v4_tft_companion_radio_ble`,
  a buzzer board (`t1000-e`-class if present) to prove scope still gates on buzzer, and `RAK_4631_companion_radio_ble`. All SUCCESS.
- [ ] **Step 2: Gemini 2.5-pro review** — scrutinize: the gate generalization preserves scope/matrix
  behavior exactly; offsets 145/146 read==write; the ui-new tristate gates EVERY relight path for
  always-off; device-info append + VER bump; INDICATORS advertised correctly on buzzer-less display boards.
- [ ] **Step 3: Hardware (Tier-2 flash, explicit per-flash owner GO)** — on a Heltec V4 companion via the
  **client app** (or a raw `0xC5` frame): set display always-off → OLED dark, persists; led off; verify
  the buzzer-less V4 now ACCEPTS these (no NO_BUZZER). Owner-driven since it needs the app.
- [ ] **Step 4: PR** (human merge approval). Body: the gate generalization + FuchsiaCreek coordination,
  the observer-reconcile fold-in, Gemini findings, hardware evidence. Note client UI is a separate repo.

## Deferred / out of scope

- **Client UI** (`OffbandMesh/meshcore-client`) — owner's repo, consumes this contract.
- No native test (harness can't compile MyMesh/companion API; owner-approved on-device test — as A1/A2).

## Self-Review

**Spec coverage (design §8 contract, §9 observer reconcile):** 0xC5 sub-codes → T1/T5; caps bit → T1/T6;
device-info → T6; companion pref+apply → T2/T3/T4; per-sub-code gate → T5; observer reconcile → folded
into T3 (ui-new setAlwaysOn delegates to setDisplayMode). ✓
**Placeholders:** T3 Step 3 and T5 Step 2 require enumerating ui-new relight paths / matching the exact
error-frame shape from the existing scope arm — both are "read-the-existing-pattern-then-mirror" steps
with concrete greps, not vague handwaves. All new code shown.
**Type consistency:** `ui_led_enabled`/`ui_display_mode` (uint8_t), `setDisplayMode(uint8_t)`,
`OFFBAND_UI_{DISPLAY,LED}_{GET,SET}` 0x05-0x08, `OFFBAND_CAP2_INDICATORS` 0x02, VER 20 — consistent
across contract, prefs, handler, caps, device-info.
**Load-bearing risk:** T5 must preserve scope/matrix behavior byte-for-byte (FuchsiaCreek's contract) —
the gate move is the one place a regression could hit their feature; Gemini T7-2 checks it explicitly.
