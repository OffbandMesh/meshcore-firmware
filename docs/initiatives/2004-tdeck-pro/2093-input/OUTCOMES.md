# OUTCOMES: T-Deck Pro input — touch, then keyboard

| | |
|---|---|
| **Feature** | [#2093](https://github.com/OffbandMesh/meshcore-firmware/issues/2093) · Citadel `Crosswire-njb` |
| **Plan of record** | [`PLAN.md`](PLAN.md), merged in [#2104](https://github.com/OffbandMesh/meshcore-firmware/pull/2104) |
| **Written by** | Epic D, task D2 ([#2255](https://github.com/OffbandMesh/meshcore-firmware/issues/2255)) |
| **Status** | For the owner's approval by merge |
| **Last updated** | 2026-11-24 |

## In short

The T-Deck Pro's GUI now takes touch and the keyboard, on `firmware-base`, in the `LilyGo_TDeck_Pro_companion_radio_ble_gui_diag` image.
- **On the bench unit,** `tdeck-pro-1`, the owner tapped through every tab and typed and sent channel messages.
- **On the merged build,** touch and keys used together lost nothing over a 64-minute run.

Four things are not proven on the unit:
- **A cold power-on boot.** The board has no power switch, and its case stays closed.
- **A channel message arriving while typing.** None arrived during the run.
- **Typing into a name field.**
- **That a tap during a refresh never becomes a long press.** It is host-tested only.

The CST328 touch chip, which some V1.1 units ship with instead of the bench unit's CST3530, is not supported yet.

## 1. What was delivered

### Epic B: TOUCH ([#2097](https://github.com/OffbandMesh/meshcore-firmware/issues/2097)), PR [#2122](https://github.com/OffbandMesh/meshcore-firmware/pull/2122), merged 2026-11-12

- **The chip, identified on the unit (B1, #2108).** It is the Hynitron cst66xx family; by elimination, the CST3530. Its info block was valid at 240x320, and the cst3xx read returned junk.
- **Our own driver (B2, #2109).** `Cst66xxTouch`, with `TouchFrame` parsing that is pure and host-tested. No vendor code: LilyGo's touch code carries no license.
- **E-paper tuning (B3, #2110).**
  - A tap made during a refresh is read in the panel's BUSY wait, queued, and handed to LVGL after the refresh.
  - A hold caught inside a refresh is replayed as a hold.
  - A drag stops when the finger lifts, with no momentum and no stretch.
  - Restyles redraw only on a real change.
- **One refresh per swipe, and no timed full refresh (B6, #2113).**
  - Wrapped chat messages.
  - Scroll-back through a whole conversation.
  - A **Newest** button, shown only while scrolled up. These three are shared fixes for every GUI image.
- **A tap during a refresh is never a long press (B7, #2114).** Found at the PR's review gate.
- **CI and docs (B4, B5; #2111, #2112).** config-lint checks the touch wiring with negative cases, and the draft guide covers touch.

### Epic C: KEYBOARD ([#2098](https://github.com/OffbandMesh/meshcore-firmware/issues/2098)), PR [#2238](https://github.com/OffbandMesh/meshcore-firmware/pull/2238), merged 2026-11-23

- **Our own TCA8418 driver (C1, #2149)**, from TI's datasheet.
  - The FIFO is read on INT (GPIO15) or a 100 ms safety poll.
  - It is drained in the panel's BUSY wait during a refresh.
  - The key state resets after an overflow, a full queue, or five bus errors in a row.
- **The key map (C2, #2150).** Shift, Alt and Sym work held or tapped. Backspace repeats while held.
- **GUI wiring (C3, #2151).** Keys go into the GUI's key path. With no field open, Enter opens and Backspace goes back. The key backlight follows the screen.
- **CI and docs (C4, C5; #2152, #2153).**
- **Found on the bench and fixed in the same Epic:**
  - **A key reaches the glass sooner (#2164):** a 200 ms gap before the refresh that follows a key, down from 1 s.
  - **Backspace never leaves a message (#2166).**
  - **Sentence capitals (#2167).**
  - **An open conversation follows new messages at the newest (#2231).**
  - **Mentions typed from compose with a picker (#2234).** No key prints `[` or `]`.
  - **The keyboard is read through the pre-sleep lock paint (#2155).**

### Epic D: INTEGRATION TESTING ([#2099](https://github.com/OffbandMesh/meshcore-firmware/issues/2099))

- **D1, the integrated bench run (#2254):** the merged build `8be9a02` on `tdeck-pro-1`.
- **D2:** this document.

## 2. Evidence

| What | Result | Where |
|---|---|---|
| Touch chip | cst66xx answered with a valid block at 240x320; cst3xx returned junk. The keyboard at 0x34 still answered after both probes. | [#2108](https://github.com/OffbandMesh/meshcore-firmware/issues/2108) |
| Tap during a refresh | Not lost. The BUSY hook ran on every refresh, with 24 reads in a 0.7 s partial. Taps caught in a refresh were handed over afterward. | [#2110](https://github.com/OffbandMesh/meshcore-firmware/issues/2110) |
| Press to finished screen | Taps in B3: 0.7–1.35 s, made up of a 0.70–0.77 s partial refresh, LVGL's redraw, and the pacer's gap. All presses in B6: 0.78–2.77 s, median 1.44 s (19 presses). The top of that range is a held drag, which redraws on lift by design. | [#2110](https://github.com/OffbandMesh/meshcore-firmware/issues/2110), [#2113](https://github.com/OffbandMesh/meshcore-firmware/issues/2113) |
| Drag and hold | 11 swipes, each one or two refreshes, then still. A 1.8 s hold opened the LoRa popover. | #2110, #2113 |
| Every key | All 35 matrix positions logged once each, distinct. Special keys decoded as mapped. 76 events with no overflow, drop or bus error, and `int_missed=0`. | [#2150](https://github.com/OffbandMesh/meshcore-firmware/issues/2150) |
| Typing, follow, lock screen, mentions | Owner-verified on the unit: node search, conversation follow, the lock-screen hint and Enter to unlock, and a mention posted to a channel. | [#2154](https://github.com/OffbandMesh/meshcore-firmware/issues/2154), [#2231](https://github.com/OffbandMesh/meshcore-firmware/issues/2231), [#2234](https://github.com/OffbandMesh/meshcore-firmware/issues/2234) |
| Merged build, touch and keys together | Taps reached all five tabs. 31 key presses and 62 events, with no overflow, drop, bus error or missed INT. A channel message was typed and sent. Taps and keys interleaved with nothing lost. 8 taps were caught in a refresh and kept, none dropped. Taps caught in a refresh opened their tab. | [#2254](https://github.com/OffbandMesh/meshcore-firmware/issues/2254) |
| Held Backspace | Repeats. Owner-verified on the merged build. | #2254 |
| Key to glass | 724–912 ms for a typed key on the merged build; 1.3 s for a send. | #2254 |
| Stability | One boot in 64 minutes, with no reset, SafeBoot, watchdog or panic. Lowest internal free memory 52,556 B. | #2254 |
| Host tests and CI | At merge, touch: native 2091/2091, native_lvgl 46/46, config-lint 65/65, 8 GUI builds. Keyboard, re-landed branch: native 2200/2200, native_lvgl 46/46, shell flows 31/31 on both builds, 5 GUI builds. `ci-green` passed on both PRs. | #2122, #2238 |

## 3. The plan's acceptance bar (§7)

| Item | Met? | Evidence |
|---|---|---|
| The owner reaches every tab and the screens behind it by touch on `tdeck-pro-1` | **Yes** | #2113; all five tabs again on the merged build, #2254 |
| The owner types text into a message and a name field on its keyboard | **Partly** | Message: yes, sent on the merged build (#2254) and with a mention (#2234). Name field: not shown. The node search took typed text (#2154), but no run reported typing a node name. |
| The capture shows the touch chip's identity, the taps, and a clean boot | **Yes, with a caveat** | Identity #2108, taps #2110/#2113/#2254. The boot is clean, but it is a chip reset, not a power-on (§5). |
| The touch-during-refresh measurement is recorded | **Yes** | #2110 |

## 4. Done when (§8)

| Item | Met? |
|---|---|
| The touch chip is identified and recorded in the hardware log | **Yes**: cst66xx (CST3530), #2108 |
| Touch and keyboard are merged, in CI, and their PRs show the named command's output | **Yes**: #2122 and #2238, both behind `ci-green` |
| The bench unit runs the merged build, and the owner has tapped and typed on it | **Yes**: #2254 |
| `OUTCOMES.md` is approved | **Pending**: merging this is the approval |

## 5. What's left

| Item | Issue |
|---|---|
| **A cold power-on boot.** The V1.1 board has no power switch, and the only buttons, RST and BOOT, reset the ESP32 alone. So the touch chip and keyboard have never started from unpowered on these drivers. Opening the case is ruled out. The way in is a firmware BATFET shutdown on battery, then USB in. | [#2140](https://github.com/OffbandMesh/meshcore-firmware/issues/2140) (battery Feature, power-off choice) |
| **A channel message arriving while typing** is still received. None arrived in D1. Adverts were received between keystrokes. | [#2254](https://github.com/OffbandMesh/meshcore-firmware/issues/2254) |
| **Typing into a name field** on the unit (§3). | [#2254](https://github.com/OffbandMesh/meshcore-firmware/issues/2254) |
| **The long-press fix on the unit.** A tap made during a refresh must not become a long press, for example resending a failed DM. It is host-tested against the real LVGL. On the bench, the taps caught in a refresh opened their tabs, but that alone doesn't rule out a long press. | [#2114](https://github.com/OffbandMesh/meshcore-firmware/issues/2114) |
| **The CST328 touch chip**, for units that ship with it (§9 ruling 1). Blocked until such a unit is on hand. | [#2257](https://github.com/OffbandMesh/meshcore-firmware/issues/2257) |
| **The Theme control does nothing on e-paper** (#2092 review, item 2). | [#2258](https://github.com/OffbandMesh/meshcore-firmware/issues/2258) |
| **Disabled rows, scrims and "sending" bubbles look normal on e-paper** (#2092 review, item 1). | [#2259](https://github.com/OffbandMesh/meshcore-firmware/issues/2259) |
| **An idle Nodes tab refreshes about five times a minute** (B3). | [#2260](https://github.com/OffbandMesh/meshcore-firmware/issues/2260) |
| **The key-to-glass diag line counts keys the GUI didn't take** (D1). | [#2261](https://github.com/OffbandMesh/meshcore-firmware/issues/2261) |
| **A screen change takes about 570 ms of rebuild before its refresh.** | [#2168](https://github.com/OffbandMesh/meshcore-firmware/issues/2168) |
| **What Sym should do.** Today it reaches the printed symbols, as Alt does. Owner's choice; a small change in the pure key layer. | [#2238](https://github.com/OffbandMesh/meshcore-firmware/pull/2238) |
| **"2m up" spacing is tight on the home screen** (plan §10). A later font tweak. | [#2037](https://github.com/OffbandMesh/meshcore-firmware/issues/2037) |

**Trackers waiting on the owner's OK to close.** All of their work is merged:
- Epic P: #2094, with #2095 and #2096.
- Epic B: #2097, with #2108–#2114.
- Epic C: the open tasks #2149, #2151, #2152, #2153, #2164, #2166 and #2167.
- The Feature itself, #2093, once this merges.

## 6. Corrections to the plan

- **Row 6, "no LVGL default theme."** Wrong: LVGL initializes its default theme on display registration (`lv_hal_disp.c:215`). Buttons do get a pressed style. Found in B3 (#2110).
- **§9 ruling 2 and C6, "the navigation keys."** The T-Deck Pro's keyboard has no arrow keys, so moving through a list is touch only. Enter and Backspace work as ruled. Corrected on #2154.
  - Two items B6 left for Epic C (#2113) fall away with this: keyboard focus not reaching **Newest**, and a key-driven scroll animating on e-paper. With no key that moves focus or scrolls, neither can happen on this board. Newest is reached by touch.- **The bench unit's "side power switch."** It doesn't exist. The V1.1 schematic has none, and the hardware log and the battery plan (row 10) are corrected. Found in D1; the correction is on #2140.
