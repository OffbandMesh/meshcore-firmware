# Indicator LED Control (Epic A1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `led on|off` CLI command (all roles) that suppresses the Heltec V4 TX LED, persisted across reboot, defaulting to today's behavior (on).

**Architecture:** Mirror the existing `fem on/off` feature exactly — a board capability API on `MainBoard` (default unsupported), implemented by `HeltecV4Board`; a `uint8_t` field appended to the shared `CommonCLIPrefs` blob (and the companion's `DataStore` blob) at the next free offset; a CLI handler in `CommonCLI.cpp`; applied to the board at boot in each role's `MyMesh` setup.

**Tech Stack:** C++11/Arduino-ESP32, PlatformIO, LittleFS binary prefs blobs, googletest (native env).

**Scope:** LED only. Display tristate is Epic A2 (separate plan — it touches 5 UITask implementations + the observer reconcile). The 0xC5 client contract is Epic B.

**Feature:** #542 · **Citadel:** `Crosswire-vke` (design) — create an A1 task before coding.

**Non-negotiables carried from the spec:**
- Default reproduces today's behavior exactly: `led` = on. A fresh flash is unchanged.
- Boards without a controllable LED report unsupported — never a silent no-op.
- Prefs offsets are append-only; never shift an existing field (existing blobs must still load).

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `src/MeshCore.h` | `MainBoard` base — capability virtuals | Add `setLedEnabled`/`isLedEnabled`/`canControlLed` (default no-op / false) |
| `variants/heltec_v4/HeltecV4Board.h` | Heltec V4 board decl | Declare the three overrides + `_led_enabled` member |
| `variants/heltec_v4/HeltecV4Board.cpp` | Heltec V4 board impl | Gate TX-LED writes behind `_led_enabled`; implement the three methods |
| `src/helpers/CommonCLI.h` | `CommonCLIPrefs` struct | Append `uint8_t ui_led_enabled` |
| `src/helpers/CommonCLI.cpp` | prefs load/save + CLI dispatch | Read/write `ui_led_enabled` @ offset 294; add `led` command handler |
| `examples/companion_radio/DataStore.cpp` | companion prefs blob | Read/write `ui_led_enabled` at the companion blob's next offset |
| `examples/simple_repeater/MyMesh.cpp` | repeater boot | Init default + apply `board.setLedEnabled(...)` at boot |
| `examples/companion_radio/MyMesh.cpp` | companion boot | Init default + apply at boot |
| `test/test_config_overlap/test_config_overlap.cpp` | prefs-offset regression | Assert the new field's offset does not overlap |

**Deferred to a follow-up task within this epic (Task 7):** `simple_room_server` and `simple_sensor` boot-apply — FEM is *not* wired in those roles today (`grep` shows apply only in repeater + companion), so their prefs-load path must be checked before wiring, not assumed.

---

### Task 1: Board capability API on `MainBoard`

**Files:**
- Modify: `src/MeshCore.h:124-126` (immediately after the FEM LNA virtuals)

- [ ] **Step 1: Add the virtuals**

In `src/MeshCore.h`, directly below the existing FEM LNA block (line 126, `isLoRaFemLnaEnabled`), add:

```cpp
  // Status/traffic indicator LED control (boards with a controllable LED override these).
  // Default: not supported. #542.
  virtual bool setLedEnabled(bool /*on*/) { return false; }
  virtual bool canControlLed() const { return false; }
  virtual bool isLedEnabled() const { return true; }  // default: LED behaves normally
```

- [ ] **Step 2: Build a role that only touches the base (compile check)**

Run: `pio run -e heltec_v4_repeater 2>&1 | tail -20`
Expected: build SUCCESS (no behavior change yet — virtuals are unused).

- [ ] **Step 3: Commit**

```bash
git add src/MeshCore.h
git commit -m "feat(#542): MainBoard LED-control capability virtuals (default unsupported)"
```

---

### Task 2: Implement LED control on `HeltecV4Board`

**Files:**
- Modify: `variants/heltec_v4/HeltecV4Board.h:32-34` (after the FEM LNA overrides)
- Modify: `variants/heltec_v4/HeltecV4Board.cpp:25-33` (the transmit hooks) and end of file

- [ ] **Step 1: Declare overrides + member in the header**

In `variants/heltec_v4/HeltecV4Board.h`, after the FEM LNA override declarations (the `isLoRaFemLnaEnabled() override;` line), add:

```cpp
  // Status/traffic (TX) LED runtime control (`led on/off` CLI). #542.
  bool setLedEnabled(bool on) override;
  bool canControlLed() const override;
  bool isLedEnabled() const override;
```

And in the `protected:` section (after `float adc_mult = ADC_MULTIPLIER;`):

```cpp
  bool _led_enabled = true;   // #542: default on = today's behavior
```

- [ ] **Step 2: Gate the TX-LED writes**

In `variants/heltec_v4/HeltecV4Board.cpp`, change `onBeforeTransmit` and `onAfterTransmit` (currently lines 25-33) to guard the LED writes:

```cpp
  void HeltecV4Board::onBeforeTransmit(void) {
    if (_led_enabled) digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
    loRaFEMControl.setTxModeEnable();
  }

  void HeltecV4Board::onAfterTransmit(void) {
    if (_led_enabled) digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
    loRaFEMControl.setRxModeEnable();
  }
```

- [ ] **Step 3: Implement the three methods (append at end of file, before the final nothing)**

Add at the end of `variants/heltec_v4/HeltecV4Board.cpp`:

```cpp
  // #542: status/traffic LED control. When disabled, the TX LED is forced LOW and
  // the transmit hooks stop driving it. The LED is a plain GPIO, always controllable
  // on this board, so canControlLed() is unconditionally true.
  bool HeltecV4Board::setLedEnabled(bool on) {
    _led_enabled = on;
    if (!on) digitalWrite(P_LORA_TX_LED, LOW);  // ensure it's dark immediately
    return true;
  }

  bool HeltecV4Board::canControlLed() const { return true; }

  bool HeltecV4Board::isLedEnabled() const { return _led_enabled; }
```

- [ ] **Step 4: Build**

Run: `pio run -e heltec_v4_repeater 2>&1 | tail -20`
Expected: build SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add variants/heltec_v4/HeltecV4Board.h variants/heltec_v4/HeltecV4Board.cpp
git commit -m "feat(#542): HeltecV4Board TX-LED gate + LED-control overrides"
```

---

### Task 3: Add the persisted pref field to `CommonCLIPrefs`

**Files:**
- Modify: `src/helpers/CommonCLI.h:68` (end of struct)
- Modify: `src/helpers/CommonCLI.cpp` (load path ~line 181, save path ~line 280, clamp path ~line 218)

The blob is append-only with fixed offsets. Current tail: `radio_fem_rxgain`@291, `flood_max_unscoped`@292, `flood_max_advert`@293. **Next free offset = 294.**

- [ ] **Step 1: Add the struct field**

In `src/helpers/CommonCLI.h`, after `uint8_t radio_fem_rxgain;` (line 68, last field before the closing brace), add:

```cpp
  // #542: status/traffic LED enable (1 = normal, 0 = suppressed). Default 1 (ON).
  // Only meaningful where MainBoard::canControlLed() is true.
  uint8_t ui_led_enabled;
```

- [ ] **Step 2: Write it in savePrefs (after the `flood_max_advert` write, ~line 282)**

Add immediately after the `flood_max_advert` write line:

```cpp
    file.write((uint8_t *)&_prefs->ui_led_enabled, sizeof(_prefs->ui_led_enabled));               // 294
```

- [ ] **Step 3: Read it in loadPrefs (matching position in the read block)**

Add the matching read at the same relative position in the load path (after the `flood_max_advert` read):

```cpp
    file.read((uint8_t *)&_prefs->ui_led_enabled, sizeof(_prefs->ui_led_enabled));                // 294
```

- [ ] **Step 4: Default + clamp**

In the load path where defaults/clamps are applied (near the `radio_fem_rxgain` constrain, ~line 218), the field must default to 1 for prefs blobs written before this field existed. The read of a short (old) blob leaves the field at its constructor value, so set the default in the prefs struct's initializer OR immediately after load. Add after the load, guarded so a legitimately-stored 0 is preserved only when the blob is long enough:

Use the same convention the codebase uses for appended fields — initialize to 1 in the owning `MyMesh` prefs setup (Task 5) so a missing field reads as default-on. Add a clamp next to the fem clamp:

```cpp
    _prefs->ui_led_enabled = constrain(_prefs->ui_led_enabled, 0, 1); // boolean
```

- [ ] **Step 5: Build**

Run: `pio run -e heltec_v4_repeater 2>&1 | tail -20`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/helpers/CommonCLI.h src/helpers/CommonCLI.cpp
git commit -m "feat(#542): persist ui_led_enabled in CommonCLIPrefs @offset 294"
```

---

### Task 4: The `led` CLI command

**Files:**
- Modify: `src/helpers/CommonCLI.cpp:634` (immediately after the `fem` status branch, before the `#ifdef ENABLE_WIFI_TELEMETRY` at line 639)

- [ ] **Step 1: Add the handler**

Insert after the `fem` status `sprintf(...)` block (line 638) and before line 639's `#ifdef ENABLE_WIFI_TELEMETRY`:

```cpp
    } else if (memcmp(command, "led on", 6) == 0) {
      _prefs->ui_led_enabled = 1;
      _board->setLedEnabled(true);
      savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command, "led off", 7) == 0) {
      _prefs->ui_led_enabled = 0;
      _board->setLedEnabled(false);
      savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command, "led", 3) == 0) {
      sprintf(reply, "led: %s, controllable: %s",
              _prefs->ui_led_enabled ? "on" : "off",
              _board->canControlLed() ? "yes" : "no");
```

Note: `led off` is matched before the bare `led` prefix, exactly as `fem off` precedes `fem` — order matters.

- [ ] **Step 2: Build**

Run: `pio run -e heltec_v4_repeater 2>&1 | tail -20`
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add src/helpers/CommonCLI.cpp
git commit -m "feat(#542): led on|off|status CLI command"
```

---

### Task 5: Apply at boot — repeater + companion

**Files:**
- Modify: `examples/simple_repeater/MyMesh.cpp:922` (init) and `:974` (apply)
- Modify: `examples/companion_radio/MyMesh.cpp:1260` (init) and `:1406` (apply)

- [ ] **Step 1: Repeater — default + apply**

In `examples/simple_repeater/MyMesh.cpp`, next to `_prefs.radio_fem_rxgain = 1;` (line 922) add:

```cpp
  _prefs.ui_led_enabled = 1;   // #542 default: LED on
```

Next to `board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain != 0);` (line 974) add:

```cpp
    board.setLedEnabled(_prefs.ui_led_enabled != 0);
```

- [ ] **Step 2: Companion — default + apply**

In `examples/companion_radio/MyMesh.cpp`, next to `_prefs.radio_fem_rxgain = 1;` (line 1260) add:

```cpp
  _prefs.ui_led_enabled = 1;   // #542 default: LED on
```

Next to `board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain != 0);` (line 1406) add:

```cpp
    board.setLedEnabled(_prefs.ui_led_enabled != 0);
```

- [ ] **Step 3: Build both roles**

Run: `pio run -e heltec_v4_repeater -e heltec_v4_companion_radio_ble 2>&1 | tail -25`
Expected: both SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add examples/simple_repeater/MyMesh.cpp examples/companion_radio/MyMesh.cpp
git commit -m "feat(#542): apply ui_led_enabled to board at boot (repeater + companion)"
```

---

### Task 6: Persist in the companion `DataStore` blob

The companion uses its own prefs blob (`DataStore`), separate offsets from `CommonCLI`. `radio_fem_rxgain` lives there at offset 137. Without this task, `led off` would not survive reboot on the companion.

**Files:**
- Modify: `examples/companion_radio/DataStore.cpp` (the prefs read at ~line 248 and the matching write)

- [ ] **Step 1: Find the companion blob's fem field + its tail**

Run: `grep -n "radio_fem_rxgain\|file.write\|file.read" examples/companion_radio/DataStore.cpp | sed -n '1,40p'`
Identify the read AND write of `radio_fem_rxgain` and the last field written after it. The new field appends after the current tail (do not shift existing offsets).

- [ ] **Step 2: Add the read and write**

Mirror the `radio_fem_rxgain` read/write for `ui_led_enabled`, appended at the blob tail (matched read/write positions), with the offset comment continuing the file's existing convention.

```cpp
    // read side (matched position, after the last existing field):
    file.read((uint8_t *)&_prefs.ui_led_enabled, sizeof(_prefs.ui_led_enabled));
    // write side (matched position):
    file.write((uint8_t *)&_prefs.ui_led_enabled, sizeof(_prefs.ui_led_enabled));
```

- [ ] **Step 3: Build companion**

Run: `pio run -e heltec_v4_companion_radio_ble 2>&1 | tail -20`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add examples/companion_radio/DataStore.cpp
git commit -m "feat(#542): persist ui_led_enabled in companion DataStore blob"
```

---

### Task 7: Room server + sensor — verify prefs path, then wire apply

FEM is not applied in these roles today, so do not assume the hook exists. Check first.

**Files:**
- Inspect then Modify: `examples/simple_room_server/MyMesh.cpp`, `examples/simple_sensor/MyMesh.cpp`

- [ ] **Step 1: Determine whether these roles load `CommonCLIPrefs` and have a board apply point**

Run: `grep -n "radio_fem_rxgain\|setLoRaFemLnaEnabled\|_prefs\.\|board\." examples/simple_room_server/MyMesh.cpp examples/simple_sensor/MyMesh.cpp | head -30`

- [ ] **Step 2: If a prefs-load + board reference exists, add the default + apply**

Where the role initializes prefs, add `_prefs.ui_led_enabled = 1;`. Where it has a board handle at boot, add `board.setLedEnabled(_prefs.ui_led_enabled != 0);`. If a role has NO board-apply point (as FEM lacks one), the CLI still works live; document in the commit that persistence-at-boot is not wired there and why, rather than inventing a hook.

- [ ] **Step 3: Build both**

Run: `pio run -e heltec_v4_room_server -e heltec_v4_sensor 2>&1 | tail -25`
Expected: both SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add examples/simple_room_server/MyMesh.cpp examples/simple_sensor/MyMesh.cpp
git commit -m "feat(#542): apply ui_led_enabled at boot where the role supports it (room_server, sensor)"
```

---

### Task 8: Prefs-offset regression test (native)

The one piece with native-testable logic: the appended field must not collide with an existing offset. `test/test_config_overlap/` already guards blob layout.

**Files:**
- Modify: `test/test_config_overlap/test_config_overlap.cpp`

- [ ] **Step 1: Read the existing test to match its style**

Run: `sed -n '1,80p' test/test_config_overlap/test_config_overlap.cpp`
Identify how it asserts field offsets / non-overlap and add a case for `ui_led_enabled` at 294 in the same form. If the test computes offsets via `offsetof`, add an `offsetof(CommonCLIPrefs, ui_led_enabled)`-based assertion consistent with the existing ones.

- [ ] **Step 2: Add the assertion**

Add a check that `ui_led_enabled` sits after `flood_max_advert` and does not overlap any prior field (mirror the existing assertions' exact macro/style — do not invent a new assertion shape).

- [ ] **Step 3: Run native tests**

Run:
```bash
export PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH"
pio test -e native -f test_config_overlap -v 2>&1 | tail -30
```
Expected: `[PASSED]` for the suite (note the cosmetic "0 test cases" summary quirk — trust the `[  PASSED  ]` line).

- [ ] **Step 4: Commit**

```bash
git add test/test_config_overlap/test_config_overlap.cpp
git commit -m "test(#542): assert ui_led_enabled prefs offset does not overlap"
```

---

### Task 9: Full-matrix build + Gemini review + hardware verify

- [ ] **Step 1: Build the full role matrix touched by this change**

Run:
```bash
pio run -e heltec_v4_repeater -e heltec_v4_companion_radio_ble -e heltec_v4_companion_radio_usb \
        -e heltec_v4_room_server -e heltec_v4_sensor -e heltec_v4_tft_repeater 2>&1 | tail -30
```
Expected: all SUCCESS. (TFT included: same board class, confirms the LED gate compiles on the TFT variant.)

- [ ] **Step 2: Gemini review (required before PR — Gemini 2.5-pro ONLY)**

Run:
```bash
python scripts/llm-consult.py --backend gemini --model gemini-2.5-pro \
  --files "src/MeshCore.h,variants/heltec_v4/HeltecV4Board.cpp,src/helpers/CommonCLI.cpp,examples/simple_repeater/MyMesh.cpp" \
  --prompt-file <review prompt> --topic 542-led
```
Fix every finding or justify it in the PR body.

- [ ] **Step 3: Hardware verify on a Heltec V4 repeater (Tier-2 flash — needs explicit per-flash owner GO)**

Stage via `pio-flash preview`, get the owner's explicit go naming the device, then `confirm`. On device:
- `led` → reports `on, controllable: yes`
- send traffic → TX LED blinks
- `led off` → `ok`; send traffic → LED stays dark
- reboot → `led` still reports `off`, LED stays dark (persistence)
- `led on` → `ok`; LED blinks again

- [ ] **Step 4: Open PR**

Human-approval gate for merge. PR body: what/why, the offset-append note, Gemini findings, hardware evidence, and that Display (A2) + contract (B) are separate follow-ups.

---

## Self-Review

**Spec coverage (§4 LED, §6 board API, §7 persistence, §11 testing):**
- §4 `led on/off/status` → Task 4. ✓
- §6 `canControlLed`/`setLedEnabled`/`isLedEnabled` → Tasks 1-2. ✓
- §7 persistence + default-on + reproduce-today → Tasks 3, 5, 6, 7. ✓
- §11 native test + full-matrix build + Gemini + hardware → Tasks 8-9. ✓
- §3 "unsupported → reported not silent" → `canControlLed()` false path, surfaced in `led` status (Task 4) and by the default base (Task 1). ✓
- Display tristate (§4 display, §5 redefine, §9 observer reconcile) → **out of scope, Epic A2.** Explicitly noted.

**Placeholder scan:** Task 3 Step 4 and Tasks 6-7 intentionally require reading the existing convention before writing (offset default handling; companion blob tail; whether room_server/sensor have an apply hook) — these are *investigate-then-implement* steps with concrete grep commands and a concrete decision rule, not "handle appropriately" placeholders. All code-bearing steps show the code.

**Type consistency:** `ui_led_enabled` (uint8_t), `setLedEnabled(bool)`, `canControlLed() const`, `isLedEnabled() const` — identical names used in MeshCore.h, HeltecV4Board, CommonCLI handler, and all apply sites. ✓

**Open item honestly flagged:** Task 3 Step 4 — the default-on-for-old-blobs mechanism must follow the exact convention the codebase uses for previously-appended fields (`radio_fem_rxgain` did the same); Step 1 of the task reads that convention rather than guessing it.
