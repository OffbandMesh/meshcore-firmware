# Gemini Adversarial DESIGN Review — Offband (MeshCore fork): `display rotate` (0/180)

You are an adversarial **design** reviewer for **Offband**, an MIT fork of **MeshCore** (NOT Meshtastic —
do not apply Meshtastic conventions). C++ firmware, PlatformIO / arduino-esp32. There is NO code yet —
review the DESIGN. Prove concerns with reasoning; if you can't tell from what's given, say so. Do NOT
rubber-stamp, flatter, or pad. Be blunt about real hazards.

## Targets

Three ESP32-S3 `*_companion_observer_wifi` builds: Heltec_v3 (OLED, no PSRAM, ~8 KB free heap),
heltec_v4 (OLED), heltec_v4_tft (TFT). `OFFBAND_OBSERVER` gates observer code in
`src/helpers/wifi_observer/`; the companion UI is `examples/companion_radio/ui-new/UITask.cpp`. Upstream
MeshCore is periodically merged, so edits to stock files want `OFFBAND_*` guards.

This is the **sibling** of a just-shipped `display always on/off` toggle (#141): a `display …` verb on the
`_sys` channel, persisted in a fork-branded **`offband_ui`** NVS namespace, applied live to the running
`UITask` via a raw-function-pointer applier the app registers at boot. The `_sys` `CliPassthrough`
allowlist already admits the `display ` verb prefix.

## The feature (SCOPED to 0° / 180° only)

A `_sys` command to flip the display 180° (upside-down mounting) and back. **90/270 portrait was
deliberately dropped** — it would need a portrait UI reflow (screens are laid out for a fixed 128×64
landscape canvas) plus, on the TFT, new driver code; out of scope.

Commands (all admitted by the existing `display ` allowlist entry):
- `display rotate 0` → `display: rotation 0 (default)`
- `display rotate 180` → `display: rotation 180 (flipped)`
- `display flip` → toggle (read current, set the other), reply reports the **resulting** state
- unsupported angle (e.g. `display rotate 90`) → `ERROR: display rotate supports 0 or 180`

Default 0. Persisted in `offband_ui` (key `rotation`, uint8 = 0 or 180). Applied live + at boot.

## Design under review

### Per-driver mechanism (both families already expose what's needed)
- **OLED `SSD1306Display`** (Adafruit-GFX): `display.setRotation(0)` = 0°, `display.setRotation(2)` = 180°.
- **TFT `ST7789Display`** → custom `ST7789Spi : public OLEDDisplay` (NOT Adafruit-GFX). It has discrete
  MADCTL methods, all keeping landscape (MV set):
  - `resetOrientation()` = `RGB|MV`
  - `landscapeScreen()` = `RGB|MV|MX`  ← **current default**, called in `begin()`; comment says "landscape
    rotated 180° with correct text direction"
  - `flipScreenVertically()` = `RGB|MV|MY`
  - `mirrorScreen()` = `RGB|MV|MX|MY`
  So TFT 0° = `landscapeScreen()` (the existing default); TFT 180° = a sibling MADCTL combo (TBD which is a
  clean 180° on the physical panel).

### Components (mirrors the always-on feature)
- **DisplayDriver** (base, bundled): add `virtual void setRotation(uint8_t deg) {}` — default **no-op**,
  degrees semantic. Override in `SSD1306Display` + `ST7789Display`, each mapping 0/180 to its own
  mechanism and ignoring other values. Non-display / e-ink drivers inherit the no-op.
- **UITask**: `setRotation(uint8_t deg)` → `_display->setRotation(deg)` then force a full redraw so the
  next frame repaints rotated. Also caches the current rotation so `display flip` can read it.
- **ConfigSchema** (bundled): `offband_ui` key `rotation` (uint8) + `getDisplayRotation()/setDisplayRotation()`.
- **ObserverCli**: `display rotate <0|180>` + `display flip` branch; a second applier
  `void (*)(uint8_t)` (parallel to the bool always-on applier); 90/270 → the supported-values error.
- **main.cpp**: register the rotation applier + boot-apply the stored rotation after `ui_task.begin`.

### Key claims to stress-test
- **0/180 keeps the canvas dimensions** (both are landscape), so `DisplayDriver::_w/_h` (set once at
  construction, e.g. 128×64) need NOT change — the 90/270 reflow problem does not apply. Correct?

## Direct questions
1. Is the `virtual setRotation(uint8_t) {}` no-op interface extension the right pattern, or is there a
   cleaner seam? Any existing driver it would break?
2. **TFT 180°**: from the `landscapeScreen()` default (`RGB|MV|MX`), which MADCTL combo is a true 180°
   rotation (not a mirror) — `mirrorScreen` (`MV|MX|MY`) or `flipScreenVertically` (`MV|MY`)? Reason from
   the bits. (We can only hardware-verify the OLED here; no TFT on the bench — so the reasoning matters.)
3. **Live re-rotation**: is `setRotation`/MADCTL-write + a redraw enough at runtime, or does the
   `ST7789Spi`/`OLEDDisplay` framebuffer need a re-init or address-window reset when rotation changes
   mid-run? Any partial-paint / stale-window hazard?
4. **`display flip` current-state source**: cache the rotation in `UITask` (RAM) vs re-read NVS each
   toggle — which is cleaner/safer given boot-apply already seeds it?
5. Anything missed (heap on the no-PSRAM V3; the second applier vs generalizing; persistence; default-0
   on fresh NVS)?

## Output format
```
## Summary
<1-3 sentences: is the design sound?>
## Issues
- **[BLOCKER/MAJOR/MINOR/QUESTION] <area>** — <problem + reasoning>. <fix>.
## Direct answers
1. interface  2. TFT 180° MADCTL  3. live re-rotation  4. flip state source  5. anything missed
## Verdict
<proceed as-is / proceed with changes / reconsider — what MUST change.>
```

## Reference files
__FILES__
