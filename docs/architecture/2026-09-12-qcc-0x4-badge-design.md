# Queen City Con 0x4 badge — design of record

**Feature:** [#1172](https://github.com/OffbandMesh/meshcore-firmware/issues/1172) · **Epic:** [#1173](https://github.com/OffbandMesh/meshcore-firmware/issues/1173) · **Task:** [#1174](https://github.com/OffbandMesh/meshcore-firmware/issues/1174) · **Client companion:** [OffbandMesh/meshcore-client#657](https://github.com/OffbandMesh/meshcore-client/issues/657)
**Date:** 2026-09-12 · **Status:** draft for owner review
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
| LED | MSG_LED (green) via MOSFET Q2, also on connector Q2D | P0.08 | single color; position on the keyboard-fitted badge not yet known |
| ProMicro LED | onboard blue | P0.15 | visibility on the badge not yet known |
| GPS | 4-pin header, ground switched by MOSFET Q1 | P0.24 (HIGH = on); UART P0.20 (MCU TX) / P0.22 (MCU RX) | hard power cut; the module has no EN pin |
| Button | user button (10 k pull-up, 100 nF) | P1.00 | |
| Battery sense | 680 k / 1 M divider on the switched battery | P0.31 | ratio 1.68; always connected (~2.5 µA) |
| Bootloader | Adafruit UF2 with S140 6.1.1 | — | the stock UF2 starts at 0x26000 |

**Hazard.** P0.06 and P0.08 are the ProMicro variant's default `Serial1` TX/RX. Any UART use must set its pins explicitly; opening `Serial1` on its defaults holds the buzzer on.

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

## 5. Architecture

### 5.1 Variant and envs

- `variants/qcc_badge/`: board class, `variant.h` / `variant.cpp` (the ProMicro pin map with `Serial1` remapped to the GPS pins), `target.*`, `platformio.ini`.
- Envs, all BLE companion on `ui-new`:
  - `QCC_Badge_companion_radio_ble`: departure flavor, normal radio defaults.
  - `QCC_Badge_con_companion_radio_ble`: con flavor, con radio defaults.
  - A `_diag` twin of each until beta: boot beacon, forced caplog, and the UART log mirror on the spare GPIO33 pad (P1.01), never on P0.06 or P0.08. Logs otherwise go over the ProMicro's USB.
- The badge envs drop the environmental-sensor drivers the ProMicro base pulls in (AHTX0, BME280, BMP280, INA3221, INA219) to recover flash.
- Release-matrix entries are added only when a release is cut (owner-gated).

### 5.2 Gating

Badge behavior is switched on by flags set only in the badge envs. Shared-code changes — the font hook, the keyboard input source, the history store, the bridge command — are additive and off by default, so other boards build unchanged.

### 5.3 Display and fonts

- `SSD1306Display` (Adafruit) drives the panel. The controller is confirmed on the bench; `SH1106Display` exists if the part turns out to be an SH1106.
- A new additive `DisplayDriver` text hook renders ThingPulse-format fonts (`ArialMT_Plain_10` / `_16`, already in `OLEDDisplayFonts.cpp`). A driver that does not implement it falls back to its built-in font.
- The QCC art is a 50×50 XBM drawn with `drawXbm`, through a badge-level hook in the shared `offband::drawSplash`, selected by the badge env. The hook is board-level, not driver-level as #822's color art is, because event art belongs to this board and not to the SSD1306 driver that many boards share.

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

- `PIN_BUZZER` = P0.06, on the existing `genericBuzzer` (RTTTL).
- Events: startup, channel message, DM, shutdown. A CTF tone may be added later.
- **Tones screen:** a preset or a custom tone per event. Custom tones accept RTTTL or Nokia composer codes (converted to RTTTL), with preview, saved in prefs.
- Software mute is the existing notify scope. The MUTE switch is mechanical and invisible to firmware.

### 5.8 Lights

- One LED line (P0.08), plus P0.15 if the bench shows the ProMicro LED is visible.
- **Scheduler:** traffic flicker on RX/TX overrides everything; message waiting replaces the heartbeat with a distinct pattern; otherwise the heartbeat runs. Never solid-on.
- Honors the existing indicator `led off` setting, from the client or the badge.
- Optional breathing heartbeat via PWM, subject to a check that it does not contend with the buzzer for a PWM unit.
- **RGB option** (`OFFBAND_RGB_LED_PIN`, off by default): an addressable LED on a spare pad, with a color per event when fitted.

### 5.9 GPS

- Modes: **Off** (default), **On** (continuous), **Power save**.
- Power save: every N minutes (default 60, configurable, floor 60 unless the owner lowers it) power the module through P0.24, wait for a fix up to a timeout (default 180 s, configurable), record the position and sync the clock, then cut power and release the UART pins. No fix before the timeout: cut power and log it.
- The GPS screen shows the mode, the age of the last fix and the time to the next poll.
- On the bench: time to first fix after a power cut, and current in each mode.

### 5.10 Power and battery

- ADC multiplier derived from the 1.68 divider and the ADC reference, then calibrated against a meter.
- SafeBoot (`SLEEP_MV` / `WAKE_MV`), `AUTO_SHUTDOWN_MILLIVOLTS` and `PWRMGT_VOLTAGE_BOOTLOCK` are enumerated and aligned as one low-voltage policy.
- Current in each mode (lights, GPS, BLE, idle) measured with the INA228 inline on the battery lead.

### 5.11 Radio defaults

- Con build: the con config, once the owner has it.
- Departure build: normal defaults. A DFU keeps saved prefs, and saved radio settings always win over build defaults, so on first boot the departure image checks whether the saved radio config is the con config and, if so, moves it to the departure defaults — once, and logged.

### 5.12 Bluetooth

A random PIN shown on the OLED at pairing, not a fixed PIN. Current behavior with a display and no fixed PIN is verified in epic 1.

### 5.13 About screen

Offband version, "on MeshCore" with its version, build date, handle, public-key prefix.

### 5.14 Self-test

Settings → Self-test, and automatically at first boot on diag builds: pulse P0.08, then P0.15, then chirp the buzzer, naming each on screen.

## 6. UI

**Principles.** One visual system: an inverted title bar carrying Bluetooth and battery; ArialMT 10 body text; the 5×7 font only for times and counters; generous spacing; no clutter; nothing on screen blinks except the text cursor.

**Screens.** Splash A; Home (today's pages, and typing jumps straight into a reply); Inbox; Thread with a compose line; New message (type to filter channels and people); join a channel by typing `#name`; Settings (handle, tones, lights, GPS, Bluetooth, self-test); About; first-boot handle; new-message card.

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
- Which preset "normal defaults" means for the departure build.
- Where the LED sits on the keyboard-fitted badge (the self-test will show).
- The GPS module and whether it has a backup cell (bench).
- Battery capacity.
- How many badges are available for integration testing.
- The repo is public, so the badge's pinout becomes public when the variant lands; confirm the badge team is fine with that.
- The CTF design (parked).
