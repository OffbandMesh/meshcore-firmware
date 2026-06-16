# Display Always-On Toggle — Design

**Status:** Approved design — 2026-06-15. Tracking: #141 (P2).
**Reviewed:** Gemini `gemini-2.5-pro` adversarial design review, 2026-06-15 — verdict *proceed with
changes*; all three changes adopted (see Review section). Log:
`docs/llm-consultations/2026-06-15-design-display-always-on-gemini-gemini-2.5-pro.log`.

## Targets

Three ESP32-S3 `*_companion_observer_wifi` builds:

| Device | Env | Display |
|---|---|---|
| Heltec V3 (OLED) | `Heltec_v3_companion_observer_wifi` | SSD1306 (no PSRAM, ~8 KB free heap) |
| Heltec V4 OLED | `heltec_v4_companion_observer_wifi` | SSD1306 |
| Heltec V4 TFT | `heltec_v4_tft_companion_observer_wifi` | ST7789 |

## Problem

USB/mains-powered bench nodes blank the screen after `AUTO_OFF_MILLIS` (15 s). Users want the screen
to stay lit. **Auto-detecting USB power is infeasible** on ESP32-S3 (no firmware power-source
detection — no nRF52-style `VBUSDETECT`), so this is a **manual opt-in toggle**. **Default OFF** — no
behavior change unless the user opts in.

## CLI

Natural-verb command (consistent with the existing `gps on` / `gps off`, `clock sync` idiom),
handled in **fork-owned `ObserverCli`** (`src/helpers/wifi_observer/ObserverCli.cpp`) reached via
`CommonCLI`'s fall-through to `dispatchObserverCli()`. Zero upstream-merge risk (no stock file edited).

**`_sys` allowlist (found during first-flash testing).** The `_sys` channel additionally gates commands
through `CliPassthrough::cliPassthroughIsAllowed`, which only admits verbs `get`/`set`/`mqtt`/`wifi`.
The natural-verb `display ...` form is rejected ("denied: not in allowlist") until `display ` is added
to that allowlist — so the fix adds it there alongside the others (this also covers the web-console
path, which shares the gate). Note: the originally-proposed `set display.always_on` form would have
passed via `set `; the allowlist entry is the price of the nicer verb wording.

| Command | Reply |
|---|---|
| `display always on` | `display: always on (screen stays lit)` |
| `display normal` | `display: normal (blanks after 15 s)` |

`display always off` is accepted as a silent **alias** for `display normal` (it reads literally, but there
is no force-dark mode — so a literal-minded user still lands in the right place). There is no usage error
for it; it returns the `display: normal …` reply. A real force-dark state is reserved for a future
`display always off` if ever requested (it would be more invasive — it must suppress the wake-on-message
and wake-on-button paths).

## Behavior — `UITask`

The whole blank logic is one branch at `examples/companion_radio/ui-new/UITask.cpp:931`.

- Add `bool _always_on` + `void setAlwaysOn(bool)` to `UITask`.
- Guard the auto-off: `if (!_always_on && millis() > _auto_off) { _display->turnOff(); }`.
- `setAlwaysOn(true)`  → set flag, `_display->turnOn()` (light immediately).
- `setAlwaysOn(false)` → clear flag **and** reset the timer: `_auto_off = millis() + AUTO_OFF_MILLIS`,
  so turning it off resumes the normal 15 s countdown from *now* instead of blanking abruptly.
- Display-agnostic: operates **above** the `DisplayDriver` abstraction (`turnOn/turnOff/isOn` are
  virtual), so SSD1306 and ST7789 inherit it identically — no per-driver code.
- e-ink (`AUTO_OFF_MILLIS==0`) is not a target; its `#if AUTO_OFF_MILLIS==0` paths are untouched.

## Persistence — NVS

- **New fork-branded namespace `offband_ui`** (NOT the generic `observer`). Rationale: upstream
  MeshCore stores all config in the **filesystem** (SPIFFS/LittleFS via `DataStore`), never NVS — all
  NVS in the tree is fork-owned (`ota_safety` log + observer `wifi`/`mqtt`/`observer`). A fork-branded
  namespace makes a collision impossible even if a future upstream merge adopts NVS, and keeps a UI
  setting out of the networking module's config.
- Key `always_on` (bool, ≤15 char NVS limit). Default `false`.
- Add `getDisplayAlwaysOn()` / `setDisplayAlwaysOn(bool)` + `kNvsOffbandUi = "offband_ui"` to
  `ConfigSchema` (mirrors the existing `mqtt.iata` get/set pattern).

## Live-apply (no reboot)

`ObserverCli` (lib module) cannot reach `ui_task` (a file-scope global in
`examples/companion_radio/main.cpp`). The existing observer↔app boundary pushes app→observer
(`wifiObserverSet*`); this is the reverse direction, so:

- Register a **raw C function pointer** applier from the app into the observer module at boot —
  `void (*)(bool)`, **not** `std::function` (zero heap; matters on the ~8 KB-free V3). e.g.
  `offband::setDisplayAlwaysOnApplier(&applyDisplayAlwaysOn)`, where the app-side fn calls
  `ui_task.setAlwaysOn(b)`.
- On `display always on/off`: `ObserverCli` persists to NVS, then invokes the applier → instant change.
- At boot: the app reads `ConfigSchema::getDisplayAlwaysOn()` and calls `ui_task.setAlwaysOn(v)` once.

## Files touched

- `examples/companion_radio/ui-new/UITask.h` / `.cpp` — flag, setter, guard.
- `src/helpers/wifi_observer/ConfigSchema.h` / `.cpp` — `offband_ui` namespace + get/set.
- `src/helpers/wifi_observer/ObserverCli.cpp` — `display always on/off` command + applier invocation.
- `src/helpers/wifi_observer/CliPassthrough.cpp` — add `display ` to the `_sys` allowlist (+ gate test `scripts/test_observer_cli_allowlist.py`).
- `src/helpers/wifi_observer/` applier-registration hook (declare + store the function pointer).
- `examples/companion_radio/main.cpp` — register the applier + boot-apply the stored value.

## Build / verify

Build all three envs; flash each; confirm the screen stays lit past 15 s and survives a reboot with
`display always on`; confirm `display always off` restores the 15 s blank. Watch heap on V3.

## Review (Gemini, 2026-06-15) — changes adopted

1. **NVS namespace** `observer` → **`offband_ui`** (collision-proof + domain-clean). *(BLOCKER/MAJOR)*
2. **Toggle-off** resets `_auto_off` so the screen doesn't blank abruptly. *(MINOR)*
3. **Applier** is a raw function pointer, not `std::function`, for V3 heap safety. *(QUESTION)*

Confirmed sound by the review: live-apply via callback (> poll > reboot); command placement in
fork-owned `ObserverCli`; the `display always on/off` wording.
