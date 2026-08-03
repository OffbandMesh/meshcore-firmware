# Epic B groundwork — companion LED persistence (removed from A1)

`542-companion-led-persist.patch` is the companion-radio side of the LED feature (#542),
**deliberately removed from Epic A1** and preserved here for Epic B.

**Why removed from A1:** A1 is the CLI epic. The companion has **no CLI** (`examples/companion_radio`
does not instantiate `CommonCLI` — see the `#395` note in `MyMesh.cpp`), so nothing in A1 can set
`ui_led_enabled` on a companion. The field + persistence + boot-apply are inert until Epic B adds the
`0xC5` device-UI setter/getter. They belong with that setter, as one testable unit.

**What the patch contains:**
- `NodePrefs.h` — `uint8_t ui_led_enabled` appended after `button_actions` (companion blob offset 145)
- `DataStore.cpp` — read + write at offset 145 (append-only, mirror order)
- `MyMesh.cpp` — default `= 1` before `loadPrefs`, and a `canControlLed()`-guarded boot-apply

**Source of truth:** this exact code was committed and built green (7 envs) at commit `2277c42f` on
branch `feat/542-indicator-controls` before being reverted from A1. Recover with either:
- `git apply docs/superpowers/epic-b-groundwork/542-companion-led-persist.patch`, or
- `git show 2277c42f -- examples/companion_radio/`

**Verified facts to carry into Epic B (from the A1 Gemini review + verification):**
- The companion `struct NodePrefs` (companion_radio/NodePrefs.h) is a DIFFERENT type from
  `CommonCLI.h`'s `struct NodePrefs` — same name, different layout. They never cross because the
  companion doesn't use `CommonCLI`. Do NOT route companion LED through `CommonCLI`.
- Companion offset 145 is correct (after `button_actions` 141..144). Confirm no new field landed
  between then and Epic B (offsets are merge-ordered).
