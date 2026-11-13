# Feature PLAN: T-Deck Pro battery — reading, charger state and low-voltage protection

| | |
|---|---|
| **Feature** | [#2140](https://github.com/OffbandMesh/meshcore-firmware/issues/2140) |
| **Serves** | Initiative [#2004](https://github.com/OffbandMesh/meshcore-firmware/issues/2004), LilyGo T-Deck Pro e-paper (H693) device support. It builds on bring-up (Feature [#2005](https://github.com/OffbandMesh/meshcore-firmware/issues/2005)) and input (Feature [#2093](https://github.com/OffbandMesh/meshcore-firmware/issues/2093)). |
| **Status** | For the owner's approval by merge. The §9 choices are open. |
| **Owner** | @Strycher |
| **Last updated** | 2026-11-13 |

## At a glance

- **What:** the T-Deck Pro reads its battery from its fuel gauge, reports when it is on external power, and gets the low-voltage protection the other battery boards have.
- **Why now:** on 2026-11-12 the bench unit lost USB power to a bad cable. It ran on battery with no warning until the power was gone. Today the image reads the battery as 0 mV, has no runtime cutoff, and has no boot gate.
- **How:**
  - First, read the gauge and the charger on the bench unit without writing to either.
  - Then write our own read-only gauge reader and wire it into the board's existing battery calls, so everything that already reads the battery gets a real value.
  - Then settle the charger's configuration.
  - Last, make the runtime cutoff and the boot gate one policy on the gauge's reading.
- **Done when:** the bench unit shows its real battery level, reports USB power, and is cut off at owner-approved thresholds at run time and at boot. All of it is in CI, and the owner has seen it on the unit.

## 1. Diagnosis

What's true today. Each Epic traces back to a row here. Tags: [verified] means observed on the unit or confirmed by a search; [read] means read in source but not run.

| # | Finding | Evidence | Effect |
|---|---|---|---|
| 1 | The image reads the battery as 0 mV. `ESP32Board::getBattMilliVolts()` reads an ADC pin only when `PIN_VBAT_READ` is defined and returns 0 otherwise. `TDeckProBoard` overrides no battery call, and the variant defines no `PIN_VBAT_READ`. | Boot line on `tdeck-pro-1`: `mv=0 shutdown_mv=-1 ext=0` (capture `touch-b7.log`, 2026-11-12) [verified]. `src/helpers/ESP32Board.h:268`; `variants/lilygo_tdeck_pro/TDeckProBoard.h` [read]. | Epic A overrides the battery calls in `TDeckProBoard`. |
| 2 | Everything downstream reads `board.getBattMilliVolts()`. That covers the GUI's average, percent and full-voltage learner, the status bar, the Power page, the phone app's battery reply and telemetry. With 0 mV, `batteryPercent()` gives 0, so the status bar shows 0% and an empty battery. | `ui-gui/UITask.cpp:563` (`battPercent`), `StatusBar.cpp:275`, `MyMesh.cpp:4401` and `:1757` [read]. What the panel shows was not checked on the unit. | One override fixes every reader. No GUI change is needed for the value itself. |
| 3 | The board has a BQ27220 fuel gauge at I2C 0x55 and a BQ25896 charger at 0x6B. Both share the board bus (SDA 13, SCL 14) with the touch chip (0x1A), the keyboard (0x34) and the haptic driver (0x5A). Nothing in our tree reads either chip. | I2C scan on `tdeck-pro-1`, 2026-11-04 (`HARDWARE.local.md`) [verified]. No match for `BQ27220` or `BQ25896` in `src/`, `variants/` or `examples/` [verified by search]. | The gauge reads go through the board `Wire` in the UI loop, as touch does. A second controller on those pins would break touch (the R8 lesson, input plan row 4). |
| 4 | LilyGo's gauge library talks to the BQ27220 through its standard commands (voltage `0x08`, current `0x0C`, state of charge `0x2C`, design capacity `0x3C`, and others). Its `init()` unseals the chip, checks the data memory, and resets and rewrites it if the check fails. LilyGo's factory firmware sets a 1400 mAh design capacity before `init()`. The library carries no license or copyright line. | `lib/BQ27220/bq27220_def.h`, `bq27220.cpp` (`init`, `setDefaultCapacity`); `examples/factory2/factory.ino:35`, `:365-372`, branch `HD-V2-250915` [read]. | Our reader is our own, using only the standard read commands. Writing the gauge's data memory is §9 choice 2. |
| 5 | LilyGo's firmware writes a charger profile at every boot: watchdog off, 1000 mA input limit, 3300 mV minimum system voltage, 4208 mV charge target, 512 mA fast charge, 128 mA pre-charge and termination. Every 5 s, if USB is in but the charger isn't charging, it writes the whole profile again ("Restore charge path"). Our firmware has never written to the charger, so it runs at whatever it holds. | `factory.ino:36-43`, `:332-348`, `:374-405` [read]. | Epic A's probe reads the charger's registers, so §9 choice 3 is made on what the unit actually does, not on assumptions. |
| 6 | No runtime cutoff is built for this board. The GUI's cutoff code is behind `AUTO_SHUTDOWN_MILLIVOLTS`, and the T-Deck Pro envs don't set it. The cutoff already treats a 0 mV reading as "no reading", not as low, and it skips the check on external power. | `ui-gui/UITask.cpp:1964`; `src/helpers/ui/LowBattCutoff.h:71`; `shutdown_mv=-1` in the boot line [verified]. | Epic C sets the flag once the reading is real. A failed gauge read must still come out as 0. |
| 7 | SafeBoot's boot gate is compiled out. It builds only when `SAFEBOOT_PIN_VBAT_READ` is defined, and that defaults to `PIN_VBAT_READ`, which this board lacks. SafeBoot also runs before `board.begin()`, so the board bus isn't up yet when it would read. | `src/SafeBoot.cpp:77-83`; `checkAndMaybeSleep()` is called at `examples/companion_radio/main.cpp:455` [read]. | Epic C gives SafeBoot a gauge reading this early. That is the largest task here. |
| 8 | After the 2026-11-12 USB loss, the next boot was a power-on reset with its RTC memory cleared, and the NVS boot counter had gone from 15 to 79. | Capture `touch-b7b.log`, 2026-11-13 05:30Z: `nvs_count=79 rtc_count=1 ... reset_reason=1 (POWERON)` [verified]. The unit wasn't captured while it was off USB. | Reading: the board started over and over as the cell ran down. With no boot gate, nothing stopped it. Row 7's gate is what would. |
| 9 | Power-off on ESP32 is `ESP32Board::powerOff()`, which calls `enterDeepSleep(0)` with no wake source. That is terminal until a reset or a power cycle. LilyGo's power-off sets the charger's BATFET off instead (XPowersLib `shutdown()` sets REG09 bit 5), which disconnects the battery. | `src/helpers/ESP32Board.cpp:707`; LilyGo `ui_deckpro_port.cpp:457-462`; XPowersLib `PowersBQ25896.hpp:912-921` [read]. What brings the board back from BATFET-off (USB in, the side switch, or the charger's QON pin) is not checked here or on this board. | Which power-off the cutoff uses is §9 choice 4. Epic A's probe and C3 check how it comes back before either is shipped. |
| 10 | The side power switch cuts the board. With it off, USB doesn't enumerate. The board runs on battery through a USB unplug. | `HARDWARE.local.md`, `tdeck-pro-1` (2026-11-04 and 2026-11-09) [verified]. | Any bench test that needs USB off is done with the cable, not the switch, so the board stays up. |
| 11 | Bench: one unit, `tdeck-pro-1`, V1.1, registered, factory flash backed up. `tdeck-pro-2` stays boxed. | `HARDWARE.local.md` [verified]. | No new registration or backup. |

## 2. Scope

**In:**
- Reading the BQ27220 (voltage, current, state of charge and status) through our own read-only reader.
- The board's battery and external-power calls on the T-Deck Pro.
- The charger's configuration, per §9 choice 3.
- One low-voltage policy on this board: the runtime cutoff, SafeBoot's boot gate, and the power-off they use.
- CI with positive and negative tests, integration testing on the bench unit, and `OUTCOMES.md`.

**Out:**
- A charging indicator in the GUI. The GUI has none on any board today. It is a separate GUI follow-up if wanted.
- Gauge-based time-to-empty, cycle count or health on screen.
- Power use itself (sleep modes, CPU clock, the e-paper's draw). That is its own Feature.
- The other unused chips on this board: haptic motor, IMU, light sensor, SD, 4G and audio.
- Any other board's low-voltage thresholds (Epic #1007 owns the fleet audit).
- A release build. These stay `_diag` envs.

## 3. Epics

Tasks are 4 points or smaller. Each code-bearing Epic ends with a verification task and gets one PR. Gemini 2.5 Pro reviews each task before it closes.

### Epic P: PLAN OF RECORD ([#2141](https://github.com/OffbandMesh/meshcore-firmware/issues/2141))

| Task | Points |
|---|---|
| **P1 · Write this plan** ([#2142](https://github.com/OffbandMesh/meshcore-firmware/issues/2142)): Gemini-reviewed, committed. | 3 |
| **P2 · Push, PR, merge** ([#2143](https://github.com/OffbandMesh/meshcore-firmware/issues/2143)): merging is the owner's approval. | 1 |

### Epic A: BATTERY READING ([#2144](https://github.com/OffbandMesh/meshcore-firmware/issues/2144))

| Task | Points |
|---|---|
| **A1 · Probe, read-only:** in the GUI diag env, read both chips at boot and every 60 s and log the results through MeshLog.<br>• **Gauge:** device number, operation status (including INITCOMP and the seal state), voltage, current, average current, state of charge, remaining and full-charge capacity, design capacity, temperature and battery status.<br>• **Charger:** the raw registers 0x00–0x14.<br>• **Writes:** none to either chip. If the charger's ADC isn't running, that is recorded, not fixed.<br>• **Bench:** one flash. During the capture the owner pulls the USB cable, waits, and plugs it back in (row 10).<br>• **Checked by:** the capture shows the gauge's identity, the current changing sign with USB, and the charger's settings: charge current, charge target, input limit, minimum system voltage and watchdog. It also shows whether the charger is in host mode or its own default mode, and its charge and VBUS status. All of it is recorded in `HARDWARE.local.md` and on #2144. | 3 |
| **A2 · Gauge reader:** our own read-only BQ27220 reader covering the standard commands from A1.<br>• **Pure decode, host-tested:** a good read, a NACK, a short read, a wrong device number, and readings outside the plausible range.<br>• **On failure:** any failed or implausible read comes out as "no reading", so `getBattMilliVolts()` returns 0 (row 6). "Implausible" uses the same floor as SafeBoot's `offband::safeBootReadingIsPlausible()`, so a reading the boot gate ignores is never one the runtime cutoff acts on.<br>• **No data-memory writes** unless §9 choice 2 rules otherwise. | 3 |
| **A3 · Board wiring:**<br>• `TDeckProBoard::getBattMilliVolts()` returns the gauge's voltage.<br>• `TDeckProBoard::isExternalPowered()` returns the charger's power-good or VBUS bit, per A1's reading.<br>• Both read through the board `Wire` from the UI loop, never from an interrupt or another task (row 3).<br>• The GUI's average, percent and full-voltage learner take the value unchanged, per §9 choice 1. | 3 |
| **A4 · CI:** config-lint checks that the T-Deck Pro envs build the gauge reader and share the board bus. The decode tests run in the native suite. Each check has a negative case. | 2 |
| **A5 · Docs:** the draft guide's battery line and the `docs/changes/` record. | 1 |
| **A6 · Bench flash and Epic verification:** on the owner's go naming the unit, flash through `scripts/pio-flash`, check the image identity, and hold the capture.<br>• The status bar and the Power page show a level that matches the gauge's log line.<br>• The boot line shows real `mv` and `ext=1` on USB.<br>• Pulling the cable turns `ext` to 0, and the board keeps running. | 2 |
| **A7 · Push, PR, merge.** | 1 |

### Epic B: CHARGER ([#2145](https://github.com/OffbandMesh/meshcore-firmware/issues/2145))

Runs only if §9 choice 3 picks a profile. If the ruling is to leave the charger as it is, B is closed with that ruling recorded and no code.

| Task | Points |
|---|---|
| **B1 · Profile:** write the ruled settings at boot, read them back, and log any register that didn't take. If A1 shows the charger's watchdog running, rewrite after it expires, the way LilyGo's "Restore charge path" does (row 5). The register encoding is pure and host-tested, including a value out of range and a failed read-back. | 3 |
| **B2 · CI:** the encoding tests in the native suite; config-lint for the env flag. Negative cases for each. | 1 |
| **B3 · Bench flash and Epic verification:** on the owner's go, flash and capture. A1's register dump, run again, shows the ruled settings. On USB with a part-charged cell, the gauge's current shows charging at the ruled rate. | 2 |
| **B4 · Push, PR, merge.** | 1 |

### Epic C: LOW-VOLTAGE PROTECTION ([#2146](https://github.com/OffbandMesh/meshcore-firmware/issues/2146))

One policy, per the standing directive: the runtime cutoff decides when to stop, the boot gate decides when to start, and their thresholds are set as one set (§9 choice 5). Both read the same gauge value through the same plausibility rule. A failed or implausible reading is an unknown state, for both: the boot continues and the runtime cutoff doesn't act on it.

| Task | Points |
|---|---|
| **C1 · Runtime cutoff:** set `AUTO_SHUTDOWN_MILLIVOLTS` for the T-Deck Pro envs at the ruled threshold, using the existing `lowBattShouldShutdown()`: raw and smoothed must both be below the line and stay there, and the check is skipped on external power (row 6). Host test: a 0 mV "no reading" never powers the board off. | 2 |
| **C2a · Boot gate:** give SafeBoot a battery source on this board that reads the gauge's voltage before `board.begin()` (row 7). Wake and sleep thresholds per choice 5.<br>• **Early bus:** bring up the board bus on SDA 13 / SCL 14 at the same clock the board and touch code use. The ESP32 core keeps the first `Wire.begin()`'s pins and clock, logs "Bus already started", and ignores a later call's arguments (framework `Wire.cpp`, `TwoWire::begin`, [read]). So a mismatched early start would silently set the bus for the whole run. Alternatively, end the bus after the read. Pick one and say why in the task.<br>• **Host-tested:** the decision with a gauge source.<br>• **Negative case:** a gauge that doesn't answer is taken as no reading and the boot continues, as an ADC reading under 1000 mV is today. | 3 |
| **C2b · Early-bus bench check:** on the owner's go, flash C2a and capture. Touch and the keyboard work from boot and stay working for a run of a length the owner sets. The touch driver's `bus_err` count stays at 0 throughout. | 2 |
| **C3 · Power-off:** the runtime cutoff uses the power-off ruled in §9 choice 4. SafeBoot keeps its own timed sleep, which wakes to check the battery again. Before the ruled power-off ships, a bench check shows how the board comes back from it (row 9). | 2 |
| **C4 · CI:** config-lint checks that the T-Deck Pro envs set the cutoff and the SafeBoot thresholds, at the values and in the relation the choice 5 ruling sets. In the recommendation that relation is a cutoff equal to the sleep threshold, both below the wake threshold. Negative cases for each, including an env where the two drift apart. | 2 |
| **C5 · Docs:** the draft guide's low-battery behavior; the `docs/changes/` record. | 1 |
| **C6 · Bench flash and Epic verification:** on the owner's go, flash and capture. The cutoff and the boot gate are each checked by the test the owner chooses (§9 choice 6). | 2 |
| **C7 · Push, PR, merge.** | 1 |

### Epic D: INTEGRATION TESTING ([#2147](https://github.com/OffbandMesh/meshcore-firmware/issues/2147))

| Task | Points |
|---|---|
| **D1 · Integrated bench run:** cold-boot the merged build on USB, then run on battery for an owner-chosen time with the capture held. The capture shows a clean boot, the battery level falling on battery and steady or rising on USB, and touch and keys still working. | 2 |
| **D2 · `OUTCOMES.md`** beside this plan: plain summary first, then what was delivered, the evidence, and what's left. | 2 |
| **D3 · Push, PR, merge.** | 1 |

## 4. Order and dependencies

P → A → B → C → D.

- **A1 before everything:** the reader is written against what the gauge actually returns, and choices 2 and 3 are made on A1's readings.
- **A before B and C:** the charger profile is checked through the gauge's current, and the cutoff and boot gate need a real reading.
- **B before C:** C's bench checks run on the charger configuration that will ship, so a later profile can't change what they measured.
- **C2a is the riskiest task.** It starts the board bus earlier than touch and the keyboard expect, so C2b benches it on its own before the rest of C.
- **Files:** A and C both touch `TDeckProBoard.*` and `variants/lilygo_tdeck_pro/platformio.ini`, so they run in that order. C2a also touches `src/SafeBoot.cpp`, which every board builds, so C's PR runs the full matrix.

## 5. Grants and authorization

| Term | Proposed | Why |
|---|---|---|
| **What merging this plan authorizes** | Epics A, B, C and D as written, end to end, holding merges to each Epic's PR. B runs only on a profile ruling. | The plan is the master layout. |
| **Merges** | 4 | One PR each for Epics A, B, C and D. This plan's own merge is the approval, not a grant use. |
| **Epic verification** | The owner approves each Epic's PR. | Each one ends on a bench result only the owner can see. |
| **Owner steps during the run** | Pull and replug the USB cable in A1 and A6; the cutoff test in C6; the battery run in D1. | Power is the thing under test. |
| **Stops** | Listed in §6. | Each one means the next step would act on a wrong premise. |
| **Flash** | None in the grant. Each device operation gets the owner's go naming the unit. | Owner directive 2026-11-04: one flash, one explicit go. |
| **Chip writes** | None in the grant beyond what §9 choices 2 and 3 rule. | A charger or gauge write changes the hardware's behavior beyond the firmware image. |

```grant-terms
{
  "epic_chain": [2144, 2145, 2146, 2147],
  "chain": true,
  "verification": { "mode": "owner" },
  "merge": { "budget": 4, "basis": "One PR each for Epic A (#2144, battery reading), Epic B (#2145, charger, only on a profile ruling), Epic C (#2146, low-voltage protection) and Epic D (#2147, OUTCOMES.md). This plan's own PR merges as the approval and uses no budget." },
  "flash": null,
  "reset": null,
  "expires_after_hours": 336
}
```

## 6. Can't-tell branches

| Where | Can't tell what | Does |
|---|---|---|
| A1 probe | The gauge doesn't answer at 0x55, or its device number isn't 0x0220 | **stop**: report the raw reads on #2144. No reader is written. |
| A1 probe | The gauge reports INITCOMP clear, or a design capacity that isn't the cell's | **stop**: report it. Whether to write the gauge's data memory is choice 2, decided on that reading. |
| A1 probe | The current doesn't change sign when USB is pulled and replugged | **stop**: report it. Either the gauge or the charger isn't doing what the plan assumes. |
| A1 probe | On USB with a part-charged cell, the charger reports not charging | **stop**: report it before choice 3. That is the case LilyGo's "Restore charge path" covers. |
| C2b bench | Touch or the keyboard stops working after the early bus start, or the touch `bus_err` count rises | **stop**: report it on #2146. No fallback is built without the owner. |
| C3 bench | The board can't be brought back from the ruled power-off without opening it | **stop**: report it. It doesn't ship as the cutoff's power-off. |
| A6 / B3 / C6 / D1 flash | The connected unit doesn't match the registered identity | **stop**: `pio-flash` refuses. Never pass `--approve-class-match` without the owner's word. |
| Any boot | SafeBoot's boot count reaches 3 | **stop** all port-opens until the unit has run 60 s. |

No branch proceeds without its condition, so no fail-open grant is needed. `"flash": null` means the grant covers no device operation, so each flash asks the owner.

## 7. Verification

- **Named command:** `pio run -e LilyGo_TDeck_Pro_companion_radio_ble_gui_diag`, plus the native suite and the full config-lint job from `.github/workflows/ci.yml` (including `scripts/test_tdeck_pro_env.py`). C's PR also runs the full build matrix, because `src/SafeBoot.cpp` is shared.
- **Acceptance bar:**
  - On `tdeck-pro-1`, the status bar and the Power page match the gauge's logged voltage, and `ext` follows the USB cable.
  - The cutoff and the boot gate act at the ruled thresholds, by the owner's chosen test.
  - The capture shows a clean boot with no SafeBoot fallback, and touch and keys still work.
  - A1's register dump and each Epic's bench results are recorded.

## 8. Done when

- The gauge and charger readings are recorded in the hardware log.
- The battery reading, any charger profile and the low-voltage policy are merged, in CI, and their PRs show the named command's output.
- The bench unit runs the merged build, and the owner has seen its battery level on USB and on battery.
- `OUTCOMES.md` is approved.

## 9. Your choices

Open. Each needs a ruling before the task it names starts. Choices 2 and 3 are best made once A1 has read the chips; the rest can be ruled now.

| # | Question | Options | Recommendation |
|---|---|---|---|
| 1 | Where the battery percent comes from (A3). | (a) The gauge's voltage into the GUI's existing curve and full-voltage learner, as every other Offband board does. (b) The gauge's own state of charge. | (a) first. It needs no gauge configuration and keeps one battery method across boards. (b) can follow if A1 shows the gauge is configured for this cell. |
| 2 | Whether to write the gauge's data memory (A2), e.g. the 1400 mAh design capacity LilyGo sets. | (a) Read-only. (b) Write it at boot when it differs, as LilyGo's library does. | (a), unless A1 shows the gauge isn't set up for this cell. Writing means unsealing the chip and rewriting its configuration. Voltage, which choice 1(a) uses, doesn't depend on it. |
| 3 | The charger's configuration (Epic B). | (a) Leave it as it is. (b) LilyGo's profile from row 5. (c) Values you set. | Rule after A1, on its register dump. The question is whether the charger's present charge current and target suit this 1400 mAh cell. (a) runs the chip on its own settings. (b) and (c) hold only while the charger's watchdog is off or fed, so B1 handles that too. |
| 4 | The power-off the cutoff uses (C3). | (a) Deep sleep with no wake, as every ESP32 board does today. (b) The charger's BATFET off, as LilyGo's power-off does, which disconnects the battery. | No recommendation until C3's bench check shows how the board comes back from (b) (row 9). (a) is the known path. |
| 5 | The thresholds, as one set (C1, C2a). | The LCD T-Deck uses a SafeBoot sleep below 3400 mV and wake at 3700, and sets no runtime cutoff. The Heltec V4 companion envs, its TFT GUI env among them, cut off at 3400. | The same set: cutoff 3400, boot gate sleeps below 3400 and wakes at 3700. With the cutoff equal to the sleep threshold, there is no voltage at which one of them treats the cell as too low and the other doesn't. |
| 6 | How the cutoff and the boot gate are bench-tested (C6). | (a) A real run-down of the cell, with the capture held. (b) A diag build with the thresholds set high, so a charged cell trips them. (c) Another test you design. | Yours to design. Both options here change what is being measured, and the test's design is the owner's. |

## 10. Carried-in work

| Item | Goes to |
|---|---|
| The status bar shows 0% and an empty battery on a board that can't read one (row 2), on every GUI board without a battery reading | After A3 it no longer happens on the T-Deck Pro. Other boards are a separate GUI follow-up if any ship without a reading. |
| The bench unit's 2026-11-12 run-down (row 8) | Recorded in `HARDWARE.local.md`. D1 runs the same situation again with the protection in place. |

## 11. Review

Gemini 2.5 Pro reviewed this plan on 2026-11-13 ($0.05). Fixed in the plan:
- **The early bus start (C2a).** It now names the core's behavior: a later `Wire.begin()` keeps the first call's settings. So the early start must match the board's pins and clock, or end the bus after the read. C2a was split, and C2b is the bench check on its own, with the `bus_err` count.
- **One plausibility rule.** The gauge reader and SafeBoot share it, so a reading the boot gate ignores is never one the cutoff acts on (A2, Epic C intro).
- **Equal thresholds.** C4 checks the ruled relation, equal cutoff and sleep threshold in the recommendation, instead of "at or below".
- **The charger's watchdog and minimum system voltage** are recorded by A1, and choice 3 says a written profile holds only while the watchdog is off or fed.

Answered, not taken:
- **"Set the cutoff at or below the charger's 3300 mV minimum system voltage, because the charger can hold the rail up from a depleted battery."** The charger regulates its minimum system voltage from USB. On battery alone, the system rail follows the cell through the BATFET, and nothing boosts it. That is the BQ25896's power-path design as we understand it, not checked against the datasheet here; A1's readings on battery will show it. The plan's earlier line tying the cutoff to that voltage was dropped as unsupported, rather than turned around.
- **"With Epic B skipped, the watchdog can stop charging."** As we understand the chip (again not checked against the datasheet here), the watchdog matters after a host has written the charger. With no writes, the chip runs on its own settings, which is choice 3(a). A1 records the mode either way.
