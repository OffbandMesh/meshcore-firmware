# Queen City Con 0x4 badge — design of record

**Feature:** [#1172](https://github.com/OffbandMesh/meshcore-firmware/issues/1172) · **Epic:** [#1173](https://github.com/OffbandMesh/meshcore-firmware/issues/1173) · **Task:** [#1174](https://github.com/OffbandMesh/meshcore-firmware/issues/1174) · **Client companion:** [OffbandMesh/meshcore-client#657](https://github.com/OffbandMesh/meshcore-client/issues/657)
**Date:** 2026-09-12 · **Status:** approved by the owner 2026-09-12; updated 2026-09-12 with epic 1 as built ([#1184](https://github.com/OffbandMesh/meshcore-firmware/issues/1184))
**Agent:** MaroonDesert (session 81df237c)

## 1. Summary

The Queen City Conference "0x4" badge (Cincinnati, Nov 13–15, 2026) is a ProMicro nRF52840 with an Ai-Thinker Ra-01SH (SX1262), a 128×64 OLED, a CardKB-compatible keyboard, a buzzer, an LED line and an optional GPS port. It ships with Meshtastic. This Feature makes it an Offband BLE companion that also works standalone: people read, write and send messages from its own keyboard, and the phone app is optional.

## 2. Goals and non-goals

**Goals**

1. QCC logo at startup.
2. Offband (MeshCore) BLE companion on the badge.
3. Standalone messaging from the keyboard, with the phone app still working.
4. Buzzer: RTTTL alert tones, including custom tones typed as Nokia composer codes.
5. LEDs: heartbeat, message waiting, network activity.
6. GPS power control: Off / On / Power save.
7. Every screen appealing and clean.
8. Messages sent on the badge reach the Offband phone client, without breaking stock clients.
9. Nice to have: a CTF beacon. Parked; designed last.

**Non-goals**

- Repeater or room-server roles on badges.
- Any behavior change on other boards.
- Code from third-party forks.

## 3. Hardware

Sources: the badge schematic (EasyEDA "QCC 0x4 Badges" V1.0, 2026-03-21), the stock firmware image, and the owner. The schematic and the stock image stay out of git (third-party design; GPL binary).

| Function | Part / net | nRF52840 pin | Notes |
|---|---|---|---|
| LoRa SPI | SCK / MOSI / MISO | P1.11 / P1.15 / P0.02 | identical to `variants/promicro` |
| LoRa control | NSS / DIO1 / RESET / BUSY | P1.13 / P0.10 / P0.09 / P0.29 | identical |
| RF switch | RXEN; module TXEN tied to DIO2 | P0.17; DIO2 | DIO2 as RF switch |
| I2C | SDA / SCL | P1.04 / P0.11 | OLED, keyboard, SAO |
| Display | 128×64 OLED, ~63 mm diagonal | I2C | controller unconfirmed; 2.42"-class modules usually carry an SSD1309 |
| Keyboard | CardKB-compatible, HY2.0-4P | I2C | owner-stated |
| Buzzer | BZ1 "4 kHz" via MOSFET Q3, MUTE switch in series | P0.06 | the mute switch is mechanical and not readable by firmware |
| LED | MSG_LED (XL-502UGD) via MOSFET Q2, also on connector Q2D | P0.08 | single color; on the owner's keyboard-fitted badge it is the red LED beside the LCD (bench, 2026-09-12) |
| ProMicro LED | onboard blue | P0.15 | not visible on the owner's badge (bench, 2026-09-12) |
| GPS | 4-pin header, ground switched by MOSFET Q1 | P0.24 (HIGH = on); UART P0.20 (MCU TX) / P0.22 (MCU RX) | hard power cut; the module has no EN pin. Its own blue fix LED sits under the display and goes dark when the cut is applied (bench, 2026-09-12) |
| Button | user button (10 k pull-up, 100 nF) | P1.00 | |
| Battery sense | 680 k / 1 M divider on the switched battery | P0.31 | ratio 1.68; always connected (~2.5 µA). The design also offers a 220 k / 330 k pair (ratio 1.667, 0.8% lower); the 680 k / 1 M pair is the one used (confirmed 2026-09-13) |
| Spare pads | GPIO33 / GPIO34 / GPIO38 / GPIO39, plus GND and 3v3 | P1.01 / P1.02 / P1.06 / P1.07 | physical solder pads (owner-verified, 2026-09-13), each wired only to its ProMicro pin. The diag log mirror transmits on GPIO38: the pad ID beacon (#1210) showed only GPIO38 reaching the sniffer, while GPIO33, GPIO34 and GPIO39 carried nothing. P1.01, P1.02 and P1.07 reach the badge through the ProMicro's three inner holes. None is analog-capable. P1.07 is missing from the pin map inherited from `variants/promicro`, which has P1.05 at index 20; fix that entry before driving GPIO39 |
| Switch pads | Q1D(24) / Q2D(8) / Q3D(6) | drains of Q1 / Q2 / Q3 | physical solder pads (owner-verified, 2026-09-13): the GPS ground, the LED's low side and the buzzer's low side. The number is the GPIO driving the gate. They only sink current |
| Bootloader | Adafruit UF2 0.10.0 with S140 6.1.1 | — | the stock UF2 starts at 0x26000. The bootloader enumerates with the app's USB identity (same VID:PID and serial); only the UF2 drive tells the modes apart (bench, 2026-09-12) |

**Hazard.** P0.06 and P0.08 are the ProMicro variant's default `Serial1` TX/RX; opening `Serial1` on those defaults would hold the buzzer on. The badge variant moves `Serial1` to the GPS pins. It also points the default SPI and Wire at the badge's own buses, because ProMicro's defaults land on RXEN, the GPS pins and the button. It defines no SPI1 or Wire1 (#1177).

**Quirk.** D1/D2 (two 1N914) feed VCC from the switched battery. The schematic labels this a workaround for ProMicro boards that do not boot on battery power.

The stock firmware is Meshtastic 2.7.15 built from `nrf52_promicro_diy_tcxo`, the same faketec pinout as `variants/promicro`.

## 4. Owner decisions (2026-09-11/12)

- Offband-native UI, with no dependency on other forks.
- One Feature. BLE companion only.
- **Splash A:** QCC eye centered with "QueenCityCon" in ArialMT 16 below it; "Offband" top-left and the version top-right on two lines, in ArialMT 10. The MeshCore credit lives on the About screens (client and badge).
- **Fonts:** ArialMT 10 for body text; the 5×7 font only for times and counters. JetBrains Mono does not hold up at 10 px on a 1-bit panel.
- Every screen appealing and clean.
- Message history survives the power switch.
- A badge-to-client bridge for badge-sent messages, as its own epic; the client half lives in meshcore-client#657.
- **Lights:** traffic flickers with traffic; message waiting replaces the heartbeat with its own pattern; nothing is ever solid-on.
- An RGB LED option behind a build flag, not expected to be enabled soon.
- **GPS:** Off / On / Power save.
- **Radio:** the con gets its own config (frequency pending). A departure build uses normal defaults and stays QCC-branded.
- **CTF:** parked to the end, with no dependency on MQTT.

Agreed with the implementation plan (2026-09-12):

- **D1, departure radio defaults:** the US community preset, 910.525 MHz / 62.5 kHz / SF7 / CR 5.
- **D2, one low-voltage policy:** SafeBoot, before `board.begin()`, sleeps below 3500 mV and wakes at 3700 mV. At runtime, `AUTO_SHUTDOWN_MILLIVOLTS` is 3400 mV. Sleeping above the runtime cutoff stops a pack that just tripped shutdown from reboot-looping.
- **D3:** the badge's BLE companion env is in the CI build matrix.
- **D4:** keep the ProMicro sensor drivers.

## 5. Architecture

### 5.1 Variant and envs

- `variants/qcc_badge/` holds:
  - the board class (a `PromicroBoard` subclass);
  - `variant.h` / `variant.cpp`: the ProMicro pin map, with `Serial1` on the GPS pins and the default SPI and Wire on the badge's own buses (§3);
  - `target.*`;
  - `platformio.ini`.
- Envs, all BLE companion on `ui-new`:
  - `QCC_Badge_companion_radio_ble`: departure flavor, normal radio defaults (D1).
  - `QCC_Badge_con_companion_radio_ble`: con flavor, con radio defaults (epic 6, once the con config is known).
  - A `_diag` twin of each until beta: boot beacon, forced caplog, and the UART log mirror on the spare GPIO38 pad (P1.06), never on P0.06 or P0.08. Logs otherwise go over the ProMicro's USB. The departure twin is `QCC_Badge_companion_radio_ble_diag`.
- The badge envs keep the environmental-sensor drivers the ProMicro base pulls in (AHTX0, BME280, BMP280, INA3221, INA219) (D4).
  - Flash leaves room: the ProMicro companion measured 58.8% (418,832 / 712,704 B), and the badge BLE env measures 60.7% with its splash (432,376 B).
  - SAO add-ons may carry sensors.
- Release-matrix entries are added only when a release is cut (owner-gated).

### 5.2 Gating

Badge behavior is switched on by flags set only in the badge envs. Shared-code changes — the font renderer and splash hook, the keyboard input source, the history store, the bridge command — are additive and off by default, so other boards build unchanged.

### 5.3 Display and fonts

- `SSD1306Display` (Adafruit) drives the panel. The controller is confirmed on the bench; `SH1106Display` exists if the part turns out to be an SH1106.
- **ThingPulse fonts.** A driver-agnostic renderer, `offband::tpfont` (`src/helpers/ui/TpFont.*`), draws ThingPulse-format fonts (`ArialMT_Plain_10` / `_16`, already in `OLEDDisplayFonts.cpp`) through `DisplayDriver::fillRect`, one call per vertical run of ink. No driver changes; drivers keep their built-in fonts for everything else.
- **The QCC art** is a 50×50 XBM drawn with `drawXbm`, through a badge-level hook in the shared `offband::drawSplash`.
  - With `OFFBAND_EVENT_SPLASH` set (badge envs only), `drawSplash()` calls the board's `drawEventSplash()` on full-size mono panels. A board that sets the flag without supplying the function fails to link, deliberately.
  - The hook is board-level, not driver-level as #822's color art is, because event art belongs to this board and not to the SSD1306 driver that many boards share.
  - `scripts/gen-qcc-splash.py` converts the badge team's LSB-first XBM to the MSB-first order `drawXbm` reads.

### 5.4 Keyboard

- A polled I2C driver for the CardKB protocol: address 0x5F, one byte per read, 0 when no key is waiting. The full key-code map is taken from M5's CardKB documentation during implementation.
- Detected at boot. Without it, the badge runs as a button-driven companion.
- Keys feed the existing `UIScreen::handleInput(char)`. The UI's arrow codes (`KEY_LEFT`..`KEY_RIGHT`, 0xB4–0xB7) appear to match CardKB's; to be confirmed against M5's documentation.
- The badge's single button keeps today's gestures.

### 5.5 Messaging

- **History store:** the last ~100 messages across channels and DMs, incoming and outgoing, each tagged with its source (mesh, phone, badge). Persisted to the internal filesystem as a bounded, versioned ring, so it survives the power switch.
- Sending from the badge uses the same mesh calls as the phone path (`sendGroupMessage`, `sendMessage`). DMs retry and track ACKs the way the app does. Channel messages have no delivery receipts in MeshCore.
- Messages sent from the phone are recorded too, so the badge's threads are complete.
- The remaining character budget is shown while typing.

### 5.6 Badge-to-client bridge

- A new Offband companion command, client-pulled: "messages sent on the badge since cursor N", returning records with sequence number, timestamp, target, text and delivery status. The command code and its capability bit are allocated at merge time under the `OffbandConfigProtocol.h` registry rules. Frames are sized to `maxFramePayload()` (BLE MTU − 3).
- Nothing is pushed unsolicited. Stock clients never see the bit and never send the command; stock firmware never sets the bit, so the Offband client never asks.
- Client half: meshcore-client#657.

### 5.7 Sound

- `PIN_BUZZER` = P0.06, on the existing `genericBuzzer` (RTTTL; `helpers/ui/buzzer.cpp` with the NonBlockingRTTTL library).
- Events: startup, channel message, DM, message sent, shutdown. The message-sent tone is a short confirmation, like the one stock Meshtastic plays on send (owner, 2026-09-12). A CTF tone may be added later.
- **Tones screen:** a preset or a custom tone per event. Custom tones accept RTTTL or Nokia composer codes (converted to RTTTL), with preview, saved in prefs.
- Software mute is the existing notify scope. The MUTE switch is mechanical and invisible to firmware.

### 5.8 Lights

- One LED line (P0.08). The ProMicro's P0.15 LED is not visible on the badge (bench, 2026-09-12), so it takes no part.
- **Scheduler:** traffic flicker on RX/TX overrides everything; message waiting replaces the heartbeat with a distinct pattern; otherwise the heartbeat runs. Never solid-on.
- Honors the existing indicator `led off` setting, from the client or the badge.
- Optional breathing heartbeat via PWM, subject to a check that it does not contend with the buzzer for a PWM unit.
- **RGB option** (`OFFBAND_RGB_LED_PIN`, off by default): an addressable LED on a spare pad, with a color per event when fitted.

### 5.9 GPS

- Modes: **Off** (default), **On** (continuous), **Power save**.
- Power save: every N minutes (default 60, configurable, floor 60 unless the owner lowers it) power the module through P0.24, wait for a fix up to a timeout (default 180 s, configurable), record the position and sync the clock, then cut power and release the UART pins. No fix before the timeout: cut power and log it.
- The GPS screen shows the mode, the age of the last fix and the time to the next poll.
- On the bench: time to first fix after a power cut, and current in each mode.
- **Bench, 2026-09-12 (owner):** the P0.24 cut works. With GPS off in the app the module's LED goes dark; turned back on, it blinks at its fix rate. Current in each mode is still to be measured.

### 5.10 Power and battery

- **Battery reading** (`variants/qcc_badge/QccBattery.h`):
  - The 1.68 divider on P0.31 is read against the internal 0.6 V reference at gain 1/6 (3.6 V full scale), 12-bit, with a 40 µs acquisition for the ~405 kΩ source. That gives 1.4765625 mV per count.
  - The multiplier is user-settable. A NaN, infinite or non-positive result reads 0 ("no reading"), and an oversized one saturates, rather than wrapping to a plausible voltage.
  - Calibration against a meter (Task 1.10, #1186) is deferred by the owner (2026-09-12); battery readings get their own thread.
- **One low-voltage policy (D2):** SafeBoot sleeps at 3500 mV and wakes at 3700 mV, then the runtime `AUTO_SHUTDOWN_MILLIVOLTS` cuts at 3400 mV. `PWRMGT_VOLTAGE_BOOTLOCK` is not compiled for this board.
- **SafeBoot runs on this board only because the env sets `SAFEBOOT_PIN_VBAT_READ`.**
  - `SafeBoot.cpp` cannot see `PromicroBoard.h`, where `PIN_VBAT_READ` lives. Without the flag, SafeBoot compiles out silently; the stock ProMicro envs have exactly that gap (#1176).
  - The badge build fails without the SafeBoot flags. It also static-asserts that SafeBoot and the board share one pin, one divider ratio and one acquisition time.
  - SafeBoot's `SAFEBOOT_ADC_SAMPLE_US` hook sets the acquisition time for its read, then restores the core default.
  - SafeBoot also continues the boot without checking when its read comes back 0 ("no battery rail"). From outside that looks like a pass, so the diag self-test screen shows which happened (§5.14).
- Current in each mode (lights, GPS, BLE, idle) measured with the INA228 inline on the battery lead.

### 5.11 Radio defaults

- Con build: the con config, once the owner has it.
- Departure build: normal defaults. A DFU keeps saved prefs, and saved radio settings always win over build defaults, so on first boot the departure image checks whether the saved radio config is the con config and, if so, moves it to the departure defaults — once, and logged.

### 5.12 Bluetooth

A random PIN shown on the OLED at pairing, not a fixed PIN. Verified in code (`MyMesh.cpp:1415-1429`):
- With a display, no PIN saved from the app, and the default `BLE_PIN_CODE=123456`, the companion picks a random six-digit PIN each session and shows it.
- A PIN saved from the app is used as-is.

### 5.13 About screen

Offband version, "on MeshCore" with its version, build date, handle, public-key prefix.

### 5.14 Self-test

- **Epic 1, as built:** on diag builds, a legend screen follows the splash for 6 s. It names the outputs already active at boot:
  - P0.08: the heartbeat;
  - P0.06: the boot tune.
  It also shows the battery reading SafeBoot let the boot through on (`SafeBoot: <n> mV`), or `SafeBoot: no reading` when SafeBoot's read came back 0 and it skipped the check (#1185).
  The legend drives nothing itself. It first also listed P0.15 ("BT advert"). The bench showed that LED isn't visible on the badge, so the line was dropped (owner-agreed, 2026-09-12, #1185).
- **Not yet scheduled:** a Settings → Self-test that pulses each output on demand. It belongs with the lights and sound work and is to be planned with epic 4.

## 6. UI

**Principles.** One visual system: an inverted title bar carrying Bluetooth and battery; ArialMT 10 body text; the 5×7 font only for times and counters; generous spacing; no clutter; nothing on screen blinks except the text cursor.

**Screens** (the target UI; each is built in its epic, §7). Splash A; Home (today's pages, and typing jumps straight into a reply); Inbox; Thread with a compose line; New message (type to filter channels and people); join a channel by typing `#name`; Settings (handle, tones, lights, GPS, Bluetooth, self-test); About; first-boot handle; new-message card.

**Keys.** Arrows move; Enter opens or sends; Esc backs out; Backspace deletes; typing on a list filters it; typing on Home starts a reply.

Pixel-accurate mockups of splash A, the inbox, the thread with compose line, the first-boot handle screen and the new-message card were reviewed with the owner on 2026-09-11/12. They are not committed.

## 7. Epics (under #1172)

1. Bring-up: variant, splash, heartbeat, self-test — #1173.
2. Keyboard and standalone messaging.
3. Badge-to-client bridge.
4. Sound, lights, GPS power.
5. CTF beacon (nice to have; parked).
6. Integration testing on the badge.

## 8. Testing

- **Unit (native):** CardKB decoding; text wrap and layout metrics; history-store ring and record format; Nokia-to-RTTTL conversion; the LED scheduler; the GPS power-save state machine; battery multiplier math; bridge record framing.
- **Bench, per epic, on the owner's badge:** flash through the pio-flash UF2/DFU path; splash; keyboard end to end; buzzer; self-test; GPS power; battery against a meter; flash and RAM budget.
- **Integration epic:** two badges (or a badge and another Offband companion) messaging both ways with the phone app alongside; the departure-flash migration; current in each mode.

## 9. Risks

- **Flash and RAM.** The ProMicro app limit is 712,704 B (S140 6.1.1, extra-filesystem linker). Offband's companion is heavier than upstream, and another fork needed `-Os` to fit a full UI on this board. Measured first, before any UI work.
- **Prefs format.** New settings follow the append-only prefs rule; any migration lengths are computed from the read/write code, not from comments.
- **Display controller.** Probably SSD1309; driver compatibility confirmed on the bench.
- **Density.** Hundreds of badges on one channel meets MeshCore's known busy-mesh contention (#672). The con-only radio config is partly for this.
- **Departure flash** keeping the con radio config (5.11).
- **GPS back-powering** through the UART when its ground is cut (5.9).
- **PWM contention** between the buzzer, a breathing LED and an RGB LED.
- **Timeline.** The con is Nov 13–15, 2026.

## 10. Open questions

- The con radio config (owner, from the con).
- Whether badges run Offband during the con, or only after the departure flash.
- ~~Which preset "normal defaults" means for the departure build.~~ Answered by D1: the US community preset.
- ~~Where the LED sits on the keyboard-fitted badge.~~ Answered on the bench: the red LED beside the LCD (P0.08). The blue LED under the display is the GPS module's.
- The GPS module and whether it has a backup cell (bench).
- Battery capacity.
- How many badges are available for integration testing.
- The repo is public, so the badge's pinout becomes public when the variant lands; confirm the badge team is fine with that.
- The CTF design (parked).
