# Indicator Display Tristate (Epic A2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `display auto|always-on|always-off` CLI command (CLI-reachable roles) that controls the OLED — normal timeout / stay lit / stay dark — persisted across reboot, defaulting to today's behavior (auto = on with 20 s timeout).

**Architecture:** A `uint8_t ui_display_mode` in `CommonCLIPrefs` (the same `struct NodePrefs` in `CommonCLI.h` that A1 extended). The CLI writes it; the three CLI-role UITasks (repeater, room_server, sensor — byte-identical) read it **directly** from `_node_prefs` each `loop()`, so a change applies live with no applier. Mode constants shared in `CommonCLI.h`.

**Tech Stack:** C++11/Arduino-ESP32, PlatformIO, LittleFS binary prefs blobs.

**Feature:** #542 · sibling of A1. **Citadel:** create an A2 task before coding.

## Dependencies & scope

- **BLOCKED-BY A1** (`feat/542-indicator-controls`, PR #546). A2 appends `ui_display_mode` at prefs
  **offset 295**, immediately after A1's `ui_led_enabled` (294). This branch is stacked on A1; when
  A1 merges, rebase onto `firmware-base`. If A1's offset changed in review, re-confirm 295 is next.
- **Scope = CLI-reachable roles only: repeater, room_server, sensor** — exactly A1's scope. The
  **companion has no CLI** (never instantiates `CommonCLI`), so companion display + the observer
  `display always on/off` reconcile (#141/#148) are **Epic B**, not here. See "Deferred" below.
- **Behavior redefinition is NOT triggered here.** A2 introduces `display always off = dark` on the
  three CLI roles, which have no prior `display` command — so nothing changes meaning for them. The
  observer's existing `display always off = normal-timeout` is a *different code path* (ObserverCli,
  companion-based) and is untouched until Epic B reconciles them. Flag this in the PR.

## Ground truth (verified)

- All three UITasks are **byte-identical** in `begin()` and the `loop()` blank block
  `[verified: diff]`. Same edits apply to all three.
- `AUTO_OFF_MILLIS = 20000` (20 s). `begin()` does `_auto_off = millis()+AUTO_OFF_MILLIS; _display->turnOn();`
  `loop()` renders while `_display->isOn()` and calls `_display->turnOff()` once `millis() > _auto_off`.
  `[verified: simple_repeater/UITask.cpp:9,31,33,95-126]`
- UITask holds `NodePrefs* _node_prefs` and includes `helpers/CommonCLI.h`, so `_node_prefs->ui_display_mode`
  resolves to the A1 struct field. `[verified: UITask.h:10, UITask.cpp:3]`
- `SSD1306Display` has `turnOn()` / `turnOff()` / `isOn()`. `[verified: SSD1306Display.cpp:29,36]`

## File Structure

| File | Change |
|---|---|
| `src/helpers/CommonCLI.h` | Append `uint8_t ui_display_mode` to `struct NodePrefs`; add `DISPLAY_MODE_*` constants |
| `src/helpers/CommonCLI.cpp` | Read/write `ui_display_mode` @ offset 295; clamp 0..2; add `display` CLI handler (guarded by `#ifdef DISPLAY_CLASS`) |
| `examples/simple_repeater/UITask.cpp` | `begin()` initial state + `loop()` mode logic |
| `examples/simple_room_server/UITask.cpp` | **identical** edits |
| `examples/simple_sensor/UITask.cpp` | **identical** edits |
| `examples/simple_repeater/MyMesh.cpp` | default `_prefs.ui_display_mode = 0` before `loadPrefs` |
| `examples/simple_room_server/MyMesh.cpp` | same |
| `examples/simple_sensor/SensorMesh.cpp` | same |

**No board-capability virtual for display** (unlike A1's LED). A display's presence is a *build flag*
(`DISPLAY_CLASS`), not board hardware — `HeltecV4Board` can't know it. So "unsupported" is handled by
a `#ifdef DISPLAY_CLASS` guard in the CLI handler, not a board method. This is a deliberate deviation
from the spec's `canControlDisplay()` proposal, for a cleaner fit.

---

### Task 1: Prefs field + mode constants

**Files:**
- Modify: `src/helpers/CommonCLI.h` (struct tail — after `ui_led_enabled`; and a constants block)
- Modify: `src/helpers/CommonCLI.cpp` (read ~line 183, clamp ~line 219, write ~line 282 — the A1 `ui_led_enabled` sites; append after them)

- [ ] **Step 1: Add the struct field + constants**

In `src/helpers/CommonCLI.h`, after `uint8_t ui_led_enabled;` (A1's field) inside `struct NodePrefs`:

```cpp
  // #542 A2: OLED mode. 0 = auto (on, blanks after timeout — default/today), 1 = always on
  // (no blank), 2 = always off (dark). Only meaningful on builds with a DISPLAY_CLASS.
  uint8_t ui_display_mode;
```

And after the struct's closing `};`, add the shared constants (mirroring the companion's
`NOTIFY_SCOPE_*` style so the CLI and all three UITasks agree):

```cpp
// #542 A2: values for NodePrefs::ui_display_mode.
#define DISPLAY_MODE_AUTO        0
#define DISPLAY_MODE_ALWAYS_ON   1
#define DISPLAY_MODE_ALWAYS_OFF  2
```

- [ ] **Step 2: Persist it (read + write @ offset 295, clamp)**

In `src/helpers/CommonCLI.cpp`, after the `ui_led_enabled` **read** line (`// 294`):

```cpp
    file.read((uint8_t *)&_prefs->ui_display_mode, sizeof(_prefs->ui_display_mode));              // 295
    // next: 296
```
(Replace the existing `// next: 295` marker.)

After the `ui_led_enabled` **write** line (`// 294`):

```cpp
    file.write((uint8_t *)&_prefs->ui_display_mode, sizeof(_prefs->ui_display_mode));             // 295
    // next: 296
```
(Replace the existing `// next: 295` marker.)

After the `ui_led_enabled` **clamp**:

```cpp
    _prefs->ui_display_mode = constrain(_prefs->ui_display_mode, 0, 2); // #542 A2 tristate
```

- [ ] **Step 3: Build**

Run: `pio run -e heltec_v4_repeater 2>&1 | tail -6`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/helpers/CommonCLI.h src/helpers/CommonCLI.cpp
git commit -m "feat(#542): persist ui_display_mode tristate in CommonCLIPrefs @offset 295"
```

---

### Task 2: The `display` CLI command

**Files:**
- Modify: `src/helpers/CommonCLI.cpp` — immediately after the A1 `led` status branch (the
  `sprintf(reply, "led: %s, controllable: %s", ...)` block)

- [ ] **Step 1: Add the handler**

Insert after the `led` status branch:

```cpp
    } else if (memcmp(command, "display always on", 17) == 0) {
#ifdef DISPLAY_CLASS
      _prefs->ui_display_mode = DISPLAY_MODE_ALWAYS_ON;
      savePrefs();
      strcpy(reply, "display: always on (screen stays lit)");
#else
      strcpy(reply, "display: unsupported (no display on this build)");
#endif
    } else if (memcmp(command, "display always off", 18) == 0) {
#ifdef DISPLAY_CLASS
      _prefs->ui_display_mode = DISPLAY_MODE_ALWAYS_OFF;
      savePrefs();
      strcpy(reply, "display: always off (screen dark)");
#else
      strcpy(reply, "display: unsupported (no display on this build)");
#endif
    } else if (memcmp(command, "display auto", 12) == 0) {
#ifdef DISPLAY_CLASS
      _prefs->ui_display_mode = DISPLAY_MODE_AUTO;
      savePrefs();
      strcpy(reply, "display: auto (on, blanks after timeout)");
#else
      strcpy(reply, "display: unsupported (no display on this build)");
#endif
    } else if (memcmp(command, "display", 7) == 0) {
#ifdef DISPLAY_CLASS
      const char* m = _prefs->ui_display_mode == DISPLAY_MODE_ALWAYS_ON  ? "always on"
                    : _prefs->ui_display_mode == DISPLAY_MODE_ALWAYS_OFF ? "always off"
                    : "auto";
      sprintf(reply, "display: %s", m);
#else
      strcpy(reply, "display: unsupported (no display on this build)");
#endif
```

Longest match first (`display always on/off` before `display auto` before bare `display`), mirroring
the `led off`/`led` and the codebase's `fem`/`telemetry` idiom.

- [ ] **Step 2: Build**

Run: `pio run -e heltec_v4_repeater 2>&1 | tail -6`
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add src/helpers/CommonCLI.cpp
git commit -m "feat(#542): display auto|always-on|always-off CLI (DISPLAY_CLASS-guarded)"
```

---

### Task 3: UITask mode logic (apply to all three roles, identically)

The three UITasks are byte-identical here. Make the SAME edits in
`examples/simple_repeater/UITask.cpp`, `examples/simple_room_server/UITask.cpp`,
`examples/simple_sensor/UITask.cpp`.

**Files (all three):** `.../UITask.cpp` — `begin()` (~lines 29-45) and `loop()` (~lines 95-126)

- [ ] **Step 1: `begin()` — set the initial display state from the mode**

Replace the `_auto_off = millis() + AUTO_OFF_MILLIS; _display->turnOn();` pair at the top of `begin()` with:

```cpp
  _auto_off = millis() + AUTO_OFF_MILLIS;
  if (_node_prefs->ui_display_mode == DISPLAY_MODE_ALWAYS_OFF) {
    _display->turnOff();          // #542 A2: boot dark
  } else {
    _display->turnOn();           // auto + always-on both start lit
  }
```

Note: `_node_prefs` is assigned a few lines below in the original; move the `_node_prefs = node_prefs;`
assignment to BEFORE this block so the mode is readable here. (In the current code `_node_prefs =
node_prefs;` sits after `_display->turnOn();` — hoist it above.)

- [ ] **Step 2: `loop()` — honor the mode each iteration (live apply)**

At the very top of `loop()`, before the `#ifdef PIN_USER_BTN` block, add the always-off early-out and
capture the mode:

```cpp
  uint8_t disp_mode = _node_prefs->ui_display_mode;   // #542 A2
  if (disp_mode == DISPLAY_MODE_ALWAYS_OFF) {
    if (_display->isOn()) _display->turnOff();
    return;                        // no button-wake, no render while dark
  }
```

Then change the blank check inside `if (_display->isOn()) { ... }` so it only auto-blanks in AUTO mode,
and add an else-branch that relights the screen if the mode is (or becomes) always-on:

```cpp
  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();
      _next_refresh = millis() + 1000;
    }
    if (disp_mode != DISPLAY_MODE_ALWAYS_ON && millis() > _auto_off) {
      _display->turnOff();
    }
  } else if (disp_mode == DISPLAY_MODE_ALWAYS_ON) {
    _display->turnOn();            // mode switched to always-on while blanked
    _auto_off = millis() + AUTO_OFF_MILLIS;
  }
```

- [ ] **Step 3: Build all three roles**

Run: `pio run -e heltec_v4_repeater -e heltec_v4_room_server -e heltec_v4_sensor 2>&1 | tail -7`
Expected: all SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add examples/simple_repeater/UITask.cpp examples/simple_room_server/UITask.cpp examples/simple_sensor/UITask.cpp
git commit -m "feat(#542): UITask honors ui_display_mode tristate (repeater, room_server, sensor)"
```

---

### Task 4: Default the mode before loadPrefs (all three roles)

Old prefs files predate this field and short-read to EOF, so the pre-`loadPrefs` value is the default.
Set it next to A1's `_prefs.ui_led_enabled = 1;` in each role.

**Files:** `simple_repeater/MyMesh.cpp`, `simple_room_server/MyMesh.cpp`, `simple_sensor/SensorMesh.cpp`

- [ ] **Step 1: Add the default in each role**

Next to each role's `_prefs.ui_led_enabled = 1;` line (added in A1), add:

```cpp
  _prefs.ui_display_mode = DISPLAY_MODE_AUTO;   // #542 A2 default: today's on-with-timeout
```

- [ ] **Step 2: Build all three**

Run: `pio run -e heltec_v4_repeater -e heltec_v4_room_server -e heltec_v4_sensor 2>&1 | tail -7`
Expected: all SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add examples/simple_repeater/MyMesh.cpp examples/simple_room_server/MyMesh.cpp examples/simple_sensor/SensorMesh.cpp
git commit -m "feat(#542): default ui_display_mode = auto before loadPrefs (3 roles)"
```

---

### Task 5: Full-matrix build + Gemini review + hardware verify

- [ ] **Step 1: Full matrix**

Run:
```bash
pio run -e heltec_v4_repeater -e heltec_v4_room_server -e heltec_v4_sensor \
        -e heltec_v4_companion_radio_ble -e heltec_v4_tft_repeater -e RAK_4631_companion_radio_ble 2>&1 | tail -20
```
Expected: all SUCCESS. Companion + nRF52 included to prove the shared `CommonCLI.h` struct + constants
change is safe on roles that don't use the `display` command.

- [ ] **Step 2: Gemini review (required before PR — Gemini 2.5-pro ONLY)**

```bash
python scripts/llm-consult.py --backend gemini --model gemini-2.5-pro \
  --files "<A2 diff>" --prompt-file <adversarial prompt> --topic 542-a2-display
```
Scrutinize: offset 295 append correctness (read==write order); the `loop()` mode transitions
(always-off↔auto↔always-on live switches: no getting stuck-on or stuck-off); the `_node_prefs`
hoist in `begin()` doesn't read a null pointer; DISPLAY_CLASS guard covers every arm. Fix or justify
every finding.

- [x] **Step 3: Hardware verify on hv4-bench-1 — DONE, PASSED 2026-08-03**

Verified on hv4-bench-1 (Heltec V4.3 repeater, A2 build sha `ccb2e018`, owner-authorized flash):
- boot → `display` = `auto`, OLED lit then blanks (default preserved)
- `display always on` → OLED stays lit past 20 s (owner-confirmed "still lit")
- `display always off` → OLED goes dark; **button press does NOT wake it** (owner-confirmed the deliberate design)
- **reboot → `display` = `always off`, OLED stays dark through boot** (persistence)
- `display auto` → OLED **relights** then blanks on timer (owner-confirmed — the always-off→auto transition Gemini caught the stuck-dark bug on; fix verified on hardware)

Two Gemini-found MAJOR transition bugs (always_on→auto stale-timer, always_off→auto stuck-dark) were
fixed with `_last_disp_mode` transition detection and confirmed both by Gemini re-review and on-device.

- [ ] ~~**Step 3 (original): Hardware verify on hv4-bench-1 (Tier-2 flash — explicit per-flash owner GO)**~~

Round-trip (stage `preview`, get owner GO naming the device, `confirm`):
- `display` → `auto`; screen lit, blanks after ~20 s (observe)
- `display always on` → screen stays lit past 20 s
- `display always off` → screen goes dark and stays dark; reboot → still dark (persist)
- `display auto` → back to on-with-timeout
- reboot after `always off` confirms persistence

- [ ] **Step 4: PR (human merge approval)**

Body: what/why; offset-295-append note; the observer-reconcile-deferred-to-B note; Gemini findings;
hardware evidence.

## Deferred to Epic B (record, don't build here)

- Companion display control (no CLI → `0xC5` app setting).
- Reconcile the observer's `display always on/off` (#141) + `display flip` (#148) onto this tristate
  so there is ONE display surface. Coordinate with #511 (unify observer set/get onto
  `config::dispatchCliLine`). The observer's current `always off = normal-timeout` semantic differs
  from A2's `always off = dark`; unifying is a deliberate B task.

## Testing note

Same as A1: no native unit test (the `[env:native]` harness compiles only pure-logic files, not
`CommonCLI`/UITask). Owner-approved test path = the on-device round-trip (Step 3).

## Self-Review

**Spec coverage:** §4 display tristate → Tasks 1-4. §4 default=auto/today → Task 4. §3 unsupported
reported → DISPLAY_CLASS guard (Task 2). §11 build+Gemini+hardware → Task 5. §5 redefinition → scoped
out (no prior `display` on these roles; observer reconcile = B). §9 observer reconcile → Deferred. ✓

**Placeholder scan:** all code steps show code; the Gemini/hardware steps name concrete checks. ✓

**Type consistency:** `ui_display_mode` (uint8_t), `DISPLAY_MODE_AUTO|ALWAYS_ON|ALWAYS_OFF` used
identically in CommonCLI.h, the CLI handler, all three UITasks, and the three defaults. ✓

**Risk flagged:** Task 3 Step 1 hoists `_node_prefs = node_prefs;` above first use — the reviewer/impl
MUST verify no code between the old and new position depends on the original ordering (it doesn't in
the current source, but confirm per-role since all three are edited).
