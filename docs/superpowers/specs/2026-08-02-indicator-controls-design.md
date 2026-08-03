# User-controllable indicators — status/traffic LED + OLED

**Feature:** [#542](https://github.com/OffbandMesh/meshcore-firmware/issues/542) · sibling of #507
**Citadel:** `Crosswire-vke` · **Agent:** BrownHawk (session `8a9de97d`) · **Date:** 2026-08-02
**Status:** design — awaiting owner review before implementation

---

## 1. Purpose

Let a user turn off a device's **status/traffic LED** and its **OLED** so a deployed node
stops being a blinking, lit beacon — for battery and for stealth ("people wanted the light show
off"). Owner-requested.

The original ask was exactly this: *"ability to turn off the LEDs (and I assume OLED) on the
Heltec V4.x on repeater via CLI command to cut down on battery+light show."* Everything below is
that, generalised to all roles and wired for a future client setting.

## 2. Who it serves, and how they reach the device

The light show is pure annoyance + drain on a deploy-and-forget box. Value ranking:

| Role | Value | How the user reaches it |
|---|---|---|
| Repeater | high | serial / BLE terminal → CLI |
| Observer | high | serial / BLE terminal → CLI |
| Sensor | high | serial / BLE terminal → CLI |
| Room Server | high | serial / BLE terminal → CLI |
| Companion | *maybe* (carry device — screen often wanted) | **phone app only** |

All four non-companion example roles compile `CommonCLI` `[verified: grep]`, so one CLI change
serves them together. The **companion is the exception**: its `cliPassthrough`/`_sys` surface is
`OFFBAND_OBSERVER`-gated `[verified: MyMesh.cpp:21,27]`, so a plain companion has no way to reach
the CLI — the user lives in the app. That user needs a rendered app setting, which is why the
**0xC5 client contract** is part of this feature.

## 3. What "off" means (grounded in the hardware)

`[verified: HeltecV4Board.cpp:26,31]` On a Heltec V4 the "LEDs" are effectively **one TX LED**
(GPIO35), driven HIGH on transmit and LOW after. Its battery cost is near-zero (lit only during
TX); the value of killing it is **visual/stealth**. Other boards have a status/heartbeat LED
(e.g. `PIN_STATUS_LED=24` on t1000-e, rak3401 green heartbeat #9) — same treatment.

`[verified: SSD1306Display.cpp:36]` The OLED has a clean controller-level `turnOff()`
(`SSD1306_DISPLAYOFF`) with an `_isOn` flag and power-cut re-init handling. It is the real
continuous draw (~5–10 mA while lit). **Open item (§9):** confirm whether the plain-repeater
UITask already auto-blanks on a timer — that sets the honest battery delta.

## 4. CLI surface (all roles, `CommonCLI`)

Modeled on the existing `fem on/off` command (persisted pref + immediate apply,
`CommonCLI.cpp:570`).

### `led`
| Command | Effect |
|---|---|
| `led off` | LED never lights (TX/status LED suppressed) |
| `led on` | normal behavior — **default** |
| `led` | report state |

### `display` (tristate)
| Command | Effect |
|---|---|
| `display always on` | lit, no timeout |
| `display always off` | **dark** — screen off |
| `display auto` | on with timeout — **default**, today's behavior |
| `display` | report state |

Boards that cannot control a given indicator **report it unsupported** — never a silent no-op
(same discipline as the observer rotation command, ObserverCli.cpp:704).

## 5. The behavior change this introduces (owner-authorized)

On observer/telemetry builds `display always off` **already exists** (#141) and today means
*"normal, blanks after 15 s"* — not dark. This feature **redefines** it:

- `display always off`: ~~"normal, blanks after 15 s"~~ → **"dark, screen off"**
- new `display auto`: "on, blanks after 15 s" (the old always-off behavior)

Owner-authorized 2026-07-19. It is a user-facing change to a shipped command, called out here
explicitly so it is not a silent drift. **Confirm exact reply wording with the owner before
shipping** (per the confirm-user-facing-copy discipline).

## 6. Board capability API

New virtuals on the board base, mirroring the FEM LNA pattern
(`canControlLoRaFemLna()` / `setLoRaFemLnaEnabled()`):

```
enum class DisplayMode : uint8_t { AUTO = 0, ALWAYS_ON = 1, ALWAYS_OFF = 2 };

bool        canControlLed() const;             // false → CLI + 0xC5 report unsupported
void        setLedEnabled(bool on);
bool        isLedEnabled() const;

bool        canControlDisplay() const;         // false on NullDisplayDriver boards
void        setDisplayMode(DisplayMode m);
DisplayMode getDisplayMode() const;
```

- Default implementations in the base return `canControl* = false` so any board not implementing
  them is safely "unsupported", not a link error.
- `HeltecV4Board` implements both. The LED gate wraps the `digitalWrite(P_LORA_TX_LED, …)` calls
  in `onBeforeTransmit`/`onAfterTransmit` behind `isLedEnabled()`. The display maps
  `ALWAYS_OFF → display.turnOff()`, `ALWAYS_ON → turnOn()` + suppress the UITask blank timer,
  `AUTO → turnOn()` + normal timer.

## 7. Persistence

Role-agnostic config path (`offband::config::dispatchSet/Get`, #512) — **not** the observer-only
ConfigSchema. Keys in a fork-branded namespace:

- `ui.led` (bool, default true)
- `ui.display` (enum auto|always-on|always-off, default auto)

Applied live via a boot-registered applier (the #141 pattern). Defaults reproduce today's
behavior exactly — a fresh flash is unchanged.

## 8. Client contract — 0xC5 sub-codes (firmware owns it)

Owner-directed 2026-08-01: **firmware owns the 0xC5 contract**; the client codes to
`OffbandConfigProtocol.h`. This feature adds sub-codes to the existing `CMD_OFFBAND_DEVICE_UI`
(0xC5) — no new command code (the 0xC range is contended; one code, many sub-codes).

**PROPOSED allocation** (merge-ordered shared enums — announced before claiming, #514):

| Sub-code | Meaning |
|---|---|
| `0x05` | `OFFBAND_UI_DISPLAY_GET` → `[0xC5][0x05][mode]` |
| `0x06` | `OFFBAND_UI_DISPLAY_SET` `[0xC5][0x06][mode]` → echo |
| `0x07` | `OFFBAND_UI_LED_GET` → `[0xC5][0x07][on]` |
| `0x08` | `OFFBAND_UI_LED_SET` `[0xC5][0x08][on]` → echo |

- `mode`: 0 auto, 1 always-on, 2 always-off. `on`: 0/1.
- Errors reuse the existing `0x7F` reason-byte channel; add a reason for
  "unsupported indicator on this board" so the client shows *why*.
- Current values appended to the device-info reply (like FEM LNA v16) so the client renders the
  toggles on connect with no extra round-trip; GET is the fallback.

**Caps bit — PROPOSED `OFFBAND_CAP2_INDICATORS = 0x02`** (caps byte 2, bit 1; bit 0 = NOTIFY_SCOPE
is the only one taken `[verified: all-branch grep]`). Advertised only where
`canControlLed() || canControlDisplay()`. A client without the bit never emits these sub-codes.

Bump `FIRMWARE_VER_CODE` (currently 16).

## 9. The observer reconcile

Observer builds already expose display control two ways: the `0xC0` config keys
`display.always_on`/`display.rotation`, and the `display always on/off` + `display flip` CLI
(ObserverCli, #141/#148). This feature makes `display` a single tristate surface. Plan:

- The observer's display-always-on logic **migrates to the board capability** (`setDisplayMode`),
  so there is one implementation. `display flip` (rotation) is orthogonal and stays.
- The `0xC0 display.always_on` key becomes a thin bridge to `ui.display` (or is deprecated in
  favour of the 0xC5 sub-code) — decided in Epic B, coordinated with #511 (unify observer set/get
  onto `config::dispatchCliLine`).

## 10. Decomposition

| Epic | Depends on | Delivers |
|---|---|---|
| **A** — CLI + board capability + persistence (all roles) | nothing | the control now, every CLI-reachable role |
| **B** — 0xC5 sub-codes + caps-byte-2 bit + device-info + observer reconcile | A | the wire contract; client can be added later untouched |
| Client UI (separate repo, owner, later) | B's contract | rendered app setting for companion |

## 11. Testing

- Native unit test for the tristate + led pref (parse, persist round-trip, default).
- Per-role build of the full matrix (repeater/observer/sensor/room server/companion, OLED + TFT).
- Hardware: on a Heltec V4 repeater — `led off` stops the TX blink; `display always off` darkens
  the screen; `display auto` restores timeout behavior; all three persist across reboot.
- `canControl*` = false path: a board without the indicator reports unsupported, no wedge.
- Defaults verified to reproduce today's behavior (LED on, display auto) on a fresh flash.
- Gemini 2.5-pro review before any PR.

## 12. Open items (resolve in planning, not blockers)

1. **Battery honesty** — confirm whether the plain-repeater UITask auto-blanks today; sets the real
   OLED-off delta. Do not promise a number until measured.
2. **Exact CLI reply wording** for each state — confirm with owner before shipping (§5).
3. **0xC0 display key fate** — bridge vs deprecate (§9), decided in Epic B with #511.
4. **LED semantics per board** — TX-blink (Heltec V4) vs heartbeat (rak3401/t1000-e) vs bridge/kiss
   variants; the capability API abstracts it but each board's `setLedEnabled` must gate the right
   pin(s).

## Sources / evidence

- `variants/heltec_v4/HeltecV4Board.cpp:26,31` — TX LED on transmit
- `src/helpers/ui/SSD1306Display.cpp:36` — `turnOff()` / `SSD1306_DISPLAYOFF`
- `examples/companion_radio/MyMesh.cpp:21,27` — `cliPassthrough`/`_sys` gated on `OFFBAND_OBSERVER`
- `examples/companion_radio/OffbandConfigProtocol.h` — 0xC5 contract, caps byte 2, FEM LNA precedent
- `examples/companion_radio/MyMesh.h:8` — `FIRMWARE_VER_CODE 16`
- #507/#509/#510 (device-UI umbrella, sibling), #512 (role-agnostic config), #514 (enum registry),
  #141/#148 (observer display), #298/#329 (FEM LNA — the pattern this copies)
