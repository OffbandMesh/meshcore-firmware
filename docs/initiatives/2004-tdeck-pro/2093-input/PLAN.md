# Feature PLAN: T-Deck Pro input — touch, then keyboard

| | |
|---|---|
| **Feature** | [#2093](https://github.com/OffbandMesh/meshcore-firmware/issues/2093) · Citadel `Crosswire-njb` |
| **Serves** | Initiative [#2004](https://github.com/OffbandMesh/meshcore-firmware/issues/2004), LilyGo T-Deck Pro e-paper (H693) device support. It builds on bring-up, Feature [#2005](https://github.com/OffbandMesh/meshcore-firmware/issues/2005). |
| **Status** | Draft for the owner's review. Owner's call, 2026-11-10: one Feature, touch first. |
| **Owner** | @Strycher |
| **Last updated** | 2026-11-10 |

## At a glance

- **What:** the T-Deck Pro's Offband GUI takes touch, then the keyboard.
- **Why now:** after bring-up the GUI draws on the e-paper panel, but BOOT is the only control. Only the home screen can be reached, so nothing past it has been seen on the glass.
- **How:** identify the bench unit's touch chip, then write our own driver and wire it into the GUI's existing touch path, tuned for e-paper. Then our own keyboard driver, feeding the GUI's existing key input.
- **Done when:** on the bench unit a tap reaches every tab and the screens behind it, typing enters text, both are in CI, and the owner has seen them on the panel.

## 1. Diagnosis

What's true today. Each Epic traces back to a row here.

| # | Finding | Evidence | Effect |
|---|---|---|---|
| 1 | The GUI already has one touch path. A `TouchSource` driver is picked by a build flag (GT911 or CHSC6x today). The path has axis swap and mirror flags, rotation, a late probe, a raw-tap log in diag builds, and a wake-on-touch pref. | `src/helpers/gui/hw/LvglPort.cpp` (`touchReadCb`, `lvglAttachTouch`); `examples/companion_radio/ui-gui/UITask.cpp` (`s_touch`, `attachTouch`, `pollTouchLate`); `Chsc6xTouch.h` [read]. | Epic B adds a driver and its flags, not a new path. |
| 2 | V1.1 shipped with a CST328 or a CST3530, both at I2C 0x1A, and **they speak different protocols**. LilyGo's front end probes Hynitron's `cst66xx` family ("36xx 35xx 66xx 68xx 148E") and its `cst3xx` family ("328 128 140 148 340 348") in turn. Which chip `tdeck-pro-1` has is not known. | `examples/factory2/hyn_touch.cpp:131-163`, `hyn_cst3xx.c` (16-bit registers from `0xD000`), branch `HD-V2-250915` [read]. Bring-up recorded only the address (`HARDWARE.local.md`, `tdeck-pro-1`). | B1 identifies the chip on the unit before any protocol code is written. |
| 3 | LilyGo's touch code is Hynitron's and carries no license. Its keyboard code uses a third-party TCA8418 library. | `hyn_cst3xx.c`, `hyn_touch.cpp`: no license or copyright line; `peri_keypad.cpp` (`keypad.begin`, `keypad.matrix`) [read]. | Both drivers are our own, from the register protocol and the datasheet. Precedents: `Chsc6xTouch` (#1442) and `TDeckInput.h` ("nothing is taken from another library"). |
| 4 | Pins: touch INT GPIO12 and RST GPIO38; keyboard INT GPIO15 and key backlight GPIO42. Both chips share the board I2C bus (SDA 13, SCL 14) with the BQ27220 (0x55), the DRV2605 (0x5A) and the BQ25896 (0x6B). | LilyGo `examples/factory2/utilities.h`, branch `HD-V2-250915` [read]. `PIN_KEYBOARD_LED=42` and `PIN_BOARD_SDA/SCL` are already in the variant. | One `Wire` controller for both. The R8's lesson applies: a second controller on the same pins breaks both (`UITask.cpp`, the `s_touch` comment). |
| 5 | LVGL reads touch every 30 ms, but an e-paper refresh blocks the loop: about 0.7 s for a partial, 1.2 s for a full, 2.3 s for init. A tap that starts and ends inside a refresh is never read, and the chip reports nothing once the finger lifts, so reading it afterward can't recover it [hypothesis for the loss, not measured]. The keyboard's 10-event FIFO (5 keys, press and release) can also overflow during one refresh. Most of a refresh is GxEPD2 waiting on the panel's BUSY line, and GxEPD2 calls a callback on every pass of that wait. | `LvglPort.cpp` (`kReleaseAfterFailedReads` comment); refresh times measured on `tdeck-pro-1` (#2037); `GxEPD2_EPD.h:93` `setBusyCallback`, called at `GxEPD2_EPD.cpp:146`, GxEPD2 1.6.2 [verified]. | B3 and C1 read touch and drain the keyboard from the BUSY callback into small queues that the GUI takes after the refresh. All I2C stays in the UI loop's task, so touch, keyboard and fuel gauge never contend for the bus. |
| 6 | Every visible change costs a refresh, and refreshes are at least 1 s apart. The GUI sets no pressed-state styles and no LVGL default theme; the only change a tap draws before the screen it opens is the focus outline. | `EinkPacer` (`src/helpers/gui/EinkFrame.h`); no `LV_STATE_PRESSED` or `lv_theme_default_init` in `ui-gui` or `src/helpers/gui`; the focus outline at `Screens.cpp:285` [verified by search]. | No press-feedback change is planned. B3 measures tap-to-screen time, and the outline is revisited only if it costs a refresh of its own. |
| 7 | The keyboard path exists: key bytes go through `classifyKey`, an open text field takes them, and otherwise they become key events. On the LCD T-Deck, moving between items is the trackball's job, and the Pro has none. | `UITask::pollKeys` (`UITask.cpp`); `TDeckInput.h` [read]. | Epic C feeds the same byte stream. How keys navigate is §9 choice 2. |
| 8 | The key matrix is 4 rows by 10 columns. LilyGo's keymap uses placeholder codes for alt, sym, shift, enter, backspace and the microphone and speaker keys, and reverses the column order when it reads. | `peri_keypad.cpp:13-17`, `:82-84` [read]. | C2 records each key's matrix position against the key it prints, from a bench press of every key, not from the placeholders. |
| 9 | Bench: one unit, `tdeck-pro-1`, V1.1, registered, with its factory flash backed up. | `HARDWARE.local.md`; bring-up Epics A and B (#2007, #2008). | No new registration or backup. |
| 10 | The variant and the e-paper GUI port are in PR #2092, not yet merged. | OffbandMesh/meshcore-firmware#2092. | Epics B and C branch from `firmware-base` after #2092 merges. |

## 2. Scope

**In:**
- Touch on `tdeck-pro-1`: the chip identified, our own driver, wired into the GUI with e-paper tuning.
- The keyboard on `tdeck-pro-1`: our own TCA8418 driver, the key map, the key backlight, and key navigation as §9 rules.
- CI for both, with positive and negative tests.
- Integration testing on the bench unit, and `OUTCOMES.md`.

**Out:**
- The touch chip the bench unit doesn't have, until a unit with it is on hand (§9 choice 1).
- Haptic feedback (the DRV2605 motor), GPS, charging and the fuel gauge, the IMU, the light sensor, SD, 4G and audio. Each is its own Feature.
- An e-paper look for disabled rows and other opacity cues (#2092's review, item 1).
- A release build. These stay `_diag` envs.

## 3. Epics

Tasks are 3–4 points or smaller. Each code-bearing Epic ends with a verification task and gets one PR. Gemini 2.5 Pro reviews each task before it closes.

### Epic P: PLAN OF RECORD ([#2094](https://github.com/OffbandMesh/meshcore-firmware/issues/2094))

| Task | Points |
|---|---|
| **P1 · Write this plan** ([#2095](https://github.com/OffbandMesh/meshcore-firmware/issues/2095)): Gemini-reviewed, committed. | 3 |
| **P2 · Push, PR, merge** ([#2096](https://github.com/OffbandMesh/meshcore-firmware/issues/2096)): merging is the owner's approval. | 1 |

### Epic B: TOUCH ([#2097](https://github.com/OffbandMesh/meshcore-firmware/issues/2097))

| Task | Points |
|---|---|
| **B1 · Identify the chip:** a probe in the GUI diag env that, for each family in LilyGo's order (`cst66xx`, then `cst3xx`, row 2), resets the chip on GPIO38 (the board's touch reset, `BOARD_TOUCH_RST`), reads that family's chip-ID registers, and logs what answered through MeshLog. A reset before each family keeps one family's read from leaving the other chip in a mode the next probe doesn't expect. LilyGo's shipping firmware probes these units the same way. One bench flash. Checked by: the capture names the chip, recorded in `HARDWARE.local.md` and on the Epic. | 2 |
| **B2 · Driver:** our own `TouchSource` for the identified family. Reset, read one frame, and decode point and press state. The frame decode is pure and host-tested, including a glitch frame and a bus error. The stuck-press rule (`kReleaseAfterFailedReads` in `LvglPort.cpp`) moves into a pure helper with host tests, so a bus that stays down is shown to release the press. | 4 |
| **B3 · GUI wiring and e-paper tuning:** the build flag and pins in the GUI env, axes from the raw-tap log of a bench tap in each corner, and scrolling per §9 choice 3. Touch is read in the panel's BUSY callback during a refresh and queued (row 5). Measured on the bench, before and after: whether a tap inside a refresh is lost, and the time from a tap to the screen it opens. | 4 |
| **B4 · CI:** config-lint checks that the GUI env selects the driver with its INT and RST pins and shares the board `Wire`. Each check has a negative case. | 2 |
| **B5 · Docs:** the draft guide says what touch does on e-paper; the `docs/changes/` record. | 1 |
| **B6 · Bench flash and Epic verification:** on the owner's go naming the unit, flash through `scripts/pio-flash`, check the image identity, hold the capture. The owner taps every tab and opens the screens behind them; the capture shows the taps. Recorded: the tap-to-screen time, how many refreshes a drag through the Nodes list takes, labels on the newly reachable screens against the wider e-paper font (#2037), and the screens where a disabled or "sending" state can't be told apart (carried-in work). | 2 |
| **B7 · Push, PR, merge.** | 1 |

### Epic C: KEYBOARD ([#2098](https://github.com/OffbandMesh/meshcore-firmware/issues/2098))

| Task | Points |
|---|---|
| **C1 · Driver:** our own TCA8418 driver: the 4x10 matrix, the key-event FIFO and its interrupt on GPIO15. Read in the UI loop and drained in the panel's BUSY callback during a refresh (row 5). An overflow flag from the chip is logged through MeshLog. The event decode is pure and host-tested. | 3 |
| **C2 · Key map:** each matrix position against the key it prints, from a bench press of every key logged in a diag build (row 8). Shift, alt and sym are a small state machine in the key-map layer, as LilyGo's keycaps print them, producing the bytes `classifyKey` already takes. Key repeat per §9 choice 4. Host-tested over the whole table, with each modifier. | 3 |
| **C3 · GUI wiring:** keys into `pollKeys`, key navigation per §9 choice 2, and the key backlight on GPIO42: on while the screen is on, off when it dims or turns off, as the T-Deck's `keyboardBacklightFor` does. | 3 |
| **C4 · CI:** config-lint for the keyboard flags and pins; the key-map tests in the native suite. Negative cases for each. | 2 |
| **C5 · Docs:** the draft guide's keyboard section; the `docs/changes/` record. | 1 |
| **C6 · Bench flash and Epic verification:** on the owner's go, flash and capture. The owner types a channel message and a node name, and uses the navigation keys. | 2 |
| **C7 · Push, PR, merge.** | 1 |

### Epic D: INTEGRATION TESTING ([#2099](https://github.com/OffbandMesh/meshcore-firmware/issues/2099))

| Task | Points |
|---|---|
| **D1 · Integrated bench run:** cold-boot the merged build. Touch and keys both work, together and in turn. The capture shows a clean boot with no SafeBoot fallback and no reset. | 2 |
| **D2 · `OUTCOMES.md`** beside this plan: plain summary first, then what was delivered, the evidence, and what's left. | 2 |
| **D3 · Push, PR, merge.** | 1 |

## 4. Order and dependencies

P → B → C → D.

- **#2092 before B and C:** both branch from `firmware-base` with the variant and the e-paper GUI in it.
- **B1 before B2:** the driver is written for the chip the unit has.
- **B before C's wiring:** touch makes every screen reachable, so C's text fields can be reached and tested. C1 and C2 (driver and key map) depend only on #2092 and can be written while B is in progress.
- **C before D:** D proves the merged build.
- **Files:** B and C both touch `variants/lilygo_tdeck_pro/platformio.ini` and `UITask.cpp`, so they run in that order.

## 5. Grants and authorization

| Term | Proposed | Why |
|---|---|---|
| **What merging this plan authorizes** | Epics B, C and D as written, end to end, holding merges to each Epic's PR. That is the `epic_chain` below. | The plan is the master layout. |
| **Merges** | 3 | One PR each for Epics B, C and D. This plan's own merge is the approval, not a grant use. |
| **Epic verification** | The owner approves each Epic's PR. | Each one ends on a bench result only the owner can see and touch. |
| **Owner steps during the run** | Tap and type on the unit in B6, C2, C6 and D1. | The input is the thing under test. |
| **Stops** | A touch chip that answers neither family's probe; an identity that doesn't match the registered unit; any SafeBoot boot count of 3. | Each one means the next step would act on a wrong premise. |
| **Flash** | None in the grant. Each device operation gets the owner's go naming the unit. | Owner directive 2026-11-04: one flash, one explicit go. |

```grant-terms
{
  "epic_chain": [2097, 2098, 2099],
  "chain": true,
  "verification": { "mode": "owner" },
  "merge": { "budget": 3, "basis": "One PR each for Epic B (#2097, touch), Epic C (#2098, keyboard) and Epic D (#2099, OUTCOMES.md). This plan's own PR merges as the approval and uses no budget." },
  "flash": null,
  "reset": null,
  "expires_after_hours": 336
}
```

## 6. Can't-tell branches

| Where | Can't tell what | Does |
|---|---|---|
| B1 probe | Neither family's ID registers answer | **stop**: report the raw reads on #2097. No protocol code is written until the chip is known. |
| B1 probe | Both families answer | **stop**: report it; the owner decides which to write. |
| B3 measure | A tap inside a refresh is still lost with the BUSY-callback read | **stop**: report the capture on #2097. The next option, refreshing from a separate task, puts the shared SPI bus under two tasks (the radio is on it), so it is the owner's call. |
| B3 measure | The BUSY callback isn't called during a refresh on this panel | **stop**: report it; no fallback is built without the owner. |
| B6 / C6 / D1 flash | The connected unit doesn't match the registered identity | **stop**: `pio-flash` refuses; never pass `--approve-class-match` without the owner's word. |
| B6 / C6 / D1 boot | SafeBoot boot count reaches 3 | **stop** all port-opens until the unit has run 60 s. |
| C2 key map | A key logs nothing or the same position as another | **stop**: report it on #2098 with the capture before mapping the rest. |

No branch proceeds without its condition, so no fail-open grant is needed. `"flash": null` means the grant covers no device operation, so each flash asks the owner.

## 7. Verification

- **Named command:** `pio run -e LilyGo_TDeck_Pro_companion_radio_ble_gui_diag`, plus the native suite and the full config-lint job from `.github/workflows/ci.yml` (including `scripts/test_tdeck_pro_env.py`).
- **Acceptance bar:**
  - The owner reaches every tab and the screens behind it by touch on `tdeck-pro-1`.
  - The owner types text into a message and a name field on its keyboard.
  - The capture shows the touch chip's identity, the taps, and a clean boot.
  - The touch-during-refresh measurement (row 5) is recorded.

## 8. Done when

- The touch chip is identified and recorded in the hardware log.
- Touch and keyboard are merged, in CI, and their PRs show the named command's output.
- The bench unit runs the merged build, and the owner has tapped and typed on it.
- `OUTCOMES.md` is approved.

## 9. Your choices

| # | Question | Options | Recommendation |
|---|---|---|---|
| 1 | Which touch chip to support. Units ship with either, and the bench unit has one (row 2). | (a) The bench unit's chip now; the other when a unit with it is on hand. (b) Both now, one of them unverified. | **(a).** A driver nobody has run on a panel is a guess. |
| 2 | Key navigation, with no trackball (row 7). | (a) When no text field is open, Enter opens the focused item and Backspace goes back; touch does the moving. (b) As (a), plus four keys that move focus (for example W, A, S, D). | **(a)** first, then revisit after bench use. Fewer keys with hidden meanings. |
| 3 | Scrolling on e-paper. LVGL's momentum and elastic scrolling (on by default; the GUI doesn't turn them off) draw many frames, each a refresh. | (a) Turn momentum and elastic off on e-paper, so a drag lands in one refresh. (b) Leave them. | **(a).** |
| 4 | Key repeat. The TCA8418 reports press and release only, with no repeat. | (a) Backspace repeats while held (after 0.5 s, about 10 a second); no other key does. (b) No repeat. (c) Every key repeats. | **(a).** Deleting a word is the case that needs it, and a screen that refreshes once a second gains nothing from repeated letters. |

## 10. Carried-in work

| Item | Goes to |
|---|---|
| "2m up" spacing is tight on the home screen (owner, #2037) | Stays a later font tweak; not this Feature. |
| Disabled rows, scrims and "sending" bubbles look the same as normal ones on e-paper (#2092 review, item 1) | Its own follow-up; the touch Epic may make it visible on more screens, and B6 records where. |
| The theme picker has no effect on e-paper (#2092 review, item 2) | Hiding or relabeling it on e-paper fits B3 if it is a few lines; otherwise a follow-up. |
