# Display Rotation (0/180) Toggle — Design

**Status:** Approved design — 2026-06-15. Tracking: #148 / Crosswire-p4b (P2).
**Reviewed:** Gemini `gemini-2.5-pro` adversarial design review, 2026-06-15 — verdict *proceed with
changes*; all adopted (see Review section). Log:
`docs/llm-consultations/2026-06-15-design-display-rotation-gemini-gemini-2.5-pro.log`.
**Sibling to** #141 (display always-on): reuses the `display` `_sys` verb, the `offband_ui` NVS namespace,
and the boot-applier pattern established there.

## Targets

| Device | Env | Display |
|---|---|---|
| Heltec V3 (OLED) | `Heltec_v3_companion_observer_wifi` | SSD1306 (no PSRAM, ~8 KB free heap) |
| Heltec V4 OLED | `heltec_v4_companion_observer_wifi` | SSD1306 |
| Heltec V4 TFT | `heltec_v4_tft_companion_observer_wifi` | ST7789 (via custom `ST7789Spi`) |

## Problem & scope

Let a user flip the display **180°** for upside-down mounting. **Scope = 0° / 180° only.** 90/270 portrait
is deliberately **out of scope**: the UI screens are laid out for a fixed 128×64 **landscape** canvas
(`width()/height()` set once at construction), so portrait needs a UI reflow plus — on the TFT — new
driver code. That's a rewrite, not this feature. Default 0°.

## CLI

The `_sys` `CliPassthrough` allowlist already admits the `display ` verb (added in #141), so these need no
allowlist change. Handled in fork-owned `ObserverCli`.

| Command | Behavior | Reply |
|---|---|---|
| `display rotate 0` | set 0° (absolute) | `display: rotation 0 (default)` |
| `display rotate 180` | set 180° (absolute) | `display: rotation 180 (flipped)` |
| `display flip` | toggle (read persisted current → set the other) | reports the **resulting** state |
| `display rotate 90` / `270` / other | rejected | `ERROR: display rotate supports 0 or 180` |

ASCII replies (no `°`) to avoid any `_sys`-channel encoding issue.

## Per-driver mechanism

| Device | 0° | 180° |
|---|---|---|
| OLED `SSD1306Display` (Adafruit-GFX) | `display.setRotation(0)` | `display.setRotation(2)` |
| TFT `ST7789Display` → `ST7789Spi` | `landscapeScreen()` (existing `begin()` default) | **`flipScreenVertically()`** = `RGB\|MV\|MY` |

TFT 180° = `flipScreenVertically()` per Gemini's MADCTL reasoning: a true 180° flips both axes, so from the
`landscapeScreen()` default (`RGB|MV|MX`) the result is `RGB|MV|MY` (a rotation, not `mirrorScreen`'s
mirror). **Best-guess — unverified until a real TFT confirms it** (see Verification).

## Behavior — `UITask` (apply at the frame boundary)

Per Gemini, the live change is applied **inside the render loop**, not mutated from the CLI context, so a
frame is never half-rotated:

- Add `int _rotation = 0;` (currently-applied) and `int _pending_rotation = -1;` (none).
- `void requestRotation(uint8_t deg)` — just sets `_pending_rotation = deg` (a single int write; safe under
  the single-threaded loop, and atomic even if that changes).
- In `UITask::loop`, at the **top of the render cycle**: if `_pending_rotation >= 0 &&
  _pending_rotation != _rotation` → `_display->setRotation(_pending_rotation)`, clear, force a full repaint
  (`_next_refresh = 0`), `_rotation = _pending_rotation`, `_pending_rotation = -1`.
- `_rotation` is the RAM cache; seeded at boot via `requestRotation(getDisplayRotation())`.

## Persistence — NVS

- `offband_ui` namespace (same as `always_on`), key **`rotation`** (uint8 = 0 or 180, default 0).
- `getDisplayRotation()` / `setDisplayRotation(uint8_t)` in `ConfigSchema` (mirrors `always_on`).
- NVS is the persistence layer. `display flip` toggles from an **in-session RAM cache** (seeded from NVS
  on first use, updated on every rotate/flip) — **not** a write-then-read NVS round-trip. A fresh
  read-only NVS handle does not reliably observe a just-committed write in the same session, which makes
  `flip` always compute the same direction. (Always-on never hit this: it's only read once at boot.)

## Live-apply wiring

- **DisplayDriver**: add `virtual void setRotation(uint8_t deg) {}` — default **no-op**, degrees semantic.
  Override in `SSD1306Display` + `ST7789Display`; other drivers (e-ink, etc.) inherit the no-op.
- **ObserverCli**: `display rotate <0|180>` + `display flip` branches; a second raw-fn-pointer applier
  `void (*)(uint8_t)` (`setDisplayRotationApplier`) parallel to the always-on `bool` one. (Two typed
  appliers is acknowledged debt — a generic mechanism is YAGNI/heap-costly for two settings.)
- **main.cpp**: register the rotation applier (`[](uint8_t d){ ui_task.requestRotation(d); }`) + boot-apply
  `ui_task.requestRotation(offband::getDisplayRotation())` after `ui_task.begin`.

## Files touched

- `src/helpers/ui/DisplayDriver.h` — `virtual void setRotation(uint8_t) {}`.
- `src/helpers/ui/SSD1306Display.{h,cpp}` — override → `setRotation(0|2)`.
- `src/helpers/ui/ST7789Display.{h,cpp}` — override → `landscapeScreen()` / `flipScreenVertically()`.
- `examples/companion_radio/ui-new/UITask.{h,cpp}` — `_rotation`/`_pending_rotation`, `requestRotation`,
  frame-boundary apply.
- `src/helpers/wifi_observer/ConfigSchema.{h,cpp}` — `rotation` key + get/set.
- `src/helpers/wifi_observer/ObserverCli.{h,cpp}` — `display rotate`/`display flip` + rotation applier.
- `examples/companion_radio/main.cpp` — register applier + boot-apply.
- `scripts/test_observer_cli_allowlist.py` — gate cases for `display rotate 0` / `display flip` (allowlist
  unchanged; cases confirm the `display ` prefix still admits them).

## Verification plan

- **OLED (V3 + V4)**: build + flash + hardware-verify 0/180 + persistence on **ST-P** (connected) — us.
- **TFT (V4 TFT)**: build; **hardware-verify 0/180 by Jim** on a real TFT (no TFT on our bench). The TFT
  180° MADCTL choice (`flipScreenVertically`) is the verification target. **The combined PR/release is
  gated on Jim's TFT confirmation** — the TFT path is "implemented, unverified" until then.
- Also confirm during impl: `ST7789Spi`'s addr-window/geometry isn't orientation-cached from `begin()`
  such that a runtime MADCTL change would desync (very likely fine for same-dimension 0/180).

## Review (Gemini, 2026-06-15) — changes adopted

1. **Apply at the frame boundary** via `_pending_rotation`, not a direct CLI-context mutation (avoids a
   half-rotated frame; robust even if threading changes). *(MAJOR)*
2. **TFT hardware verification is a mandatory gate**; TFT 180° = `flipScreenVertically()` by MADCTL
   reasoning, confirmed by Jim before release. *(BLOCKER)*
3. **Applier debt acknowledged**, not generalized (YAGNI / V3 heap). *(MINOR)*
4. **Verify `ST7789Spi` runtime-rotation safety** during impl. *(QUESTION)*

Confirmed sound: the no-op `setRotation` interface extension; RAM-cached flip state; default-0 on fresh
NVS; negligible heap impact on V3.
