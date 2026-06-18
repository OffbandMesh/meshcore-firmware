# Gemini Adversarial DESIGN Review — Offband (MeshCore fork): `display always on` toggle

You are an adversarial **design** reviewer for **Offband**, an MIT fork of **MeshCore**
(NOT Meshtastic — do not apply Meshtastic conventions). C++ firmware, PlatformIO / arduino-esp32.
There is NO code yet — review the DESIGN below. Prove every concern with reasoning; if you can't
determine something from what's given, say so rather than speculating. Do NOT rubber-stamp, flatter,
or pad. Be blunt about real hazards.

## Targets

Three ESP32-S3 "observer" builds, all `*_companion_observer_wifi`:
- **Heltec_v3** (ESP32-S3FN8, **no PSRAM**, ~8 KB free heap at boot) — OLED, SSD1306 driver.
- **heltec_v4** (ESP32-S3R2, has PSRAM) — OLED, SSD1306 driver.
- **heltec_v4_tft** (has PSRAM) — TFT, ST7789 driver.

`OFFBAND_OBSERVER` (formerly `CROSSWIRE_OBSERVER`) gates the observer layer in
`src/helpers/wifi_observer/`, built on top of the companion firmware in `examples/companion_radio/`.
Offband periodically merges upstream MeshCore, so edits to **stock** MeshCore files cause merge
conflicts and must be gated behind `OFFBAND_*`.

## The feature

A user-toggled setting so a USB/mains-powered bench node keeps its screen **lit** instead of
blanking. We are NOT auto-detecting USB power: all three targets are ESP32-S3 with **no firmware
power-source detection** (no nRF52 `VBUSDETECT`), so a manual toggle is the only viable path.

**Default OFF** — today's behavior (15 s auto-blank) is unchanged unless the user opts in.

## Design under review

### 1. Behavior (display-agnostic, one guard)
The screen-blank logic is a single branch in `examples/companion_radio/ui-new/UITask.cpp:931`:
```cpp
if (millis() > _auto_off) { _display->turnOff(); }   // _auto_off pushed forward on activity
```
Add `bool _always_on` to `UITask` + `setAlwaysOn(bool)`. Guard becomes:
```cpp
if (!_always_on && millis() > _auto_off) { _display->turnOff(); }
```
`setAlwaysOn(true)` also calls `_display->turnOn()` so it lights immediately. This sits **above** the
`DisplayDriver` abstraction (`turnOn/turnOff/isOn` are virtual), so SSD1306 and ST7789 inherit it
identically — zero per-driver code.
Note: e-ink builds use `AUTO_OFF_MILLIS==0` (no auto-off, compiled out via `#if AUTO_OFF_MILLIS==0`),
but our 3 targets all have `AUTO_OFF_MILLIS>0`.

### 2. Persistence — store a bool in the fork-owned observer NVS namespace
`ConfigSchema` (bundled below) defines fork NVS namespaces `wifi` / `mqtt` / `observer` via the
Arduino `Preferences` API. Plan: add a key (e.g. `disp_aon`, ≤15 chars) to the **`observer`**
namespace with get/set helpers, mirroring the existing `mqtt.iata` pattern.

**Collision/forward-compat claim to stress-test:** upstream MeshCore stores ALL its application config
in the **filesystem** (SPIFFS/LittleFS via `DataStore`, e.g. `fs->open(filename,...)`) — **not NVS**.
A repo-wide sweep shows the ONLY NVS users are fork code:
- `src/helpers/ESP32Board.cpp` — raw ESP-IDF `nvs_*` API in the **`ota_safety`** namespace (boot
  counter / OTA safety log).
- `src/helpers/wifi_observer/ConfigSchema.cpp` — `Preferences` in `wifi`/`mqtt`/`observer`.

Therefore a new key in our `observer` namespace cannot collide with upstream now, nor across future
upstream version bumps, because upstream never writes application config to NVS. NVS is
namespace-partitioned; the only collision risk is intra-namespace key reuse, which we fully control.

**Is this reasoning sound?** Call out any forward-compat hazard I'm missing — e.g. a future upstream
merge that introduces NVS use under a clashing namespace name; the 15-char NVS key/namespace limits;
NVS type-mismatch on re-read; per-namespace blob/entry limits; or any reason `observer` is the wrong
namespace for a *display* setting.

### 3. Live-apply (no reboot) — decoupling question
On `set`, we want the running display to change immediately (reboot-to-apply is poor UX for a screen
toggle, even though `wifi.*` settings use it). Problem: `ObserverCli` (lib module in
`src/helpers/wifi_observer/`) cannot directly reach `ui_task` — a **file-scope global in
`examples/companion_radio/main.cpp`**.

The existing observer↔app boundary is **push-from-app**: `main.cpp` pushes state INTO the observer via
setters like `wifiObserverSetGpsTimeState(...)`, `wifiObserverSetStatusSnapshot(...)`. This new flow is
the **reverse** — a value set in the observer CLI must reach the app/UI.

Proposed: an applier callback the app registers into the observer module at boot —
`offband::setDisplayAlwaysOnApplier([](bool b){ ui_task.setAlwaysOn(b); })` — which `ObserverCli` calls
after persisting; boot reads the NVS value and applies it once.

**Is the registered-callback the right inversion of the boundary, or is app-side POLLING of an observer
getter each loop (consistent with the existing push/pull split) cleaner? Or is reboot-to-apply
genuinely the right call here?**

### 4. Command wording + placement
The CLI supports natural multi-word verbs (`gps on`, `gps off`, `clock sync`, `clear stats` in
`src/helpers/CommonCLI.cpp`) AND dotted-key `set x.y v` (observer config in `ObserverCli`).

Plan: command `display always on` / `display always off` (reads naturally; modeled on `gps on/off`),
handled in **fork-owned `ObserverCli`** (zero upstream-merge risk) rather than stock `CommonCLI` (which
would need an `OFFBAND_OBSERVER` guard). This relies on `CommonCLI`'s if-ladder falling through to
`dispatchObserverCli()` for an unrecognized `display ...` line.

**Any dispatch-order pitfall (does an unrecognized `display ...` reliably reach `dispatchObserverCli`?),
and is fork-owned placement the right merge-safety call?**

## Output format
```
## Summary
<1-3 sentences: is the design sound?>

## Issues
- **[BLOCKER] <area>** — <problem + reasoning>. <concrete fix>.
- **[MAJOR] ...**
- **[MINOR] ...**
- **[QUESTION] ...**
(Omit any severity with no findings.)

## Direct answers
1. NVS collision/forward-compat reasoning — sound? hazards?
2. Live-apply decoupling — callback vs poll vs reboot?
3. Command placement/wording — merge-safe + dispatch-correct?

## Verdict
<proceed as-is / proceed with changes / reconsider — one paragraph, state what MUST change.>
```

## Reference file
__FILES__
