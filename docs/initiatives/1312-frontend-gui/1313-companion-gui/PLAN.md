# Feature PLAN: on-device companion GUI

| | |
|---|---|
| **Feature** | [#1313](https://github.com/OffbandMesh/meshcore-firmware/issues/1313) · Citadel `Crosswire-k77` |
| **Serves** | Initiative [#1312](https://github.com/OffbandMesh/meshcore-firmware/issues/1312), Frontend GUI. The Initiative has no plan of record of its own yet; this Feature delivers its main outcome. |
| **Diagnosis** | Epic [#1314](https://github.com/OffbandMesh/meshcore-firmware/issues/1314), D1–D5 (done; outside the grant chain) |
| **Plan Epic** | [#1328](https://github.com/OffbandMesh/meshcore-firmware/issues/1328) (this plan; outside the grant chain) |
| **Build chain** | A [#1425](https://github.com/OffbandMesh/meshcore-firmware/issues/1425) → B [#1426](https://github.com/OffbandMesh/meshcore-firmware/issues/1426) → C [#1427](https://github.com/OffbandMesh/meshcore-firmware/issues/1427) → D [#1428](https://github.com/OffbandMesh/meshcore-firmware/issues/1428) → E [#1429](https://github.com/OffbandMesh/meshcore-firmware/issues/1429) → F [#1430](https://github.com/OffbandMesh/meshcore-firmware/issues/1430) → G [#1431](https://github.com/OffbandMesh/meshcore-firmware/issues/1431) → H [#1432](https://github.com/OffbandMesh/meshcore-firmware/issues/1432) → I [#1433](https://github.com/OffbandMesh/meshcore-firmware/issues/1433) → J [#1434](https://github.com/OffbandMesh/meshcore-firmware/issues/1434) → K [#1435](https://github.com/OffbandMesh/meshcore-firmware/issues/1435) |
| **Design of record** | `docs/frontend-gui/Offband MeshCore GUI design-handoff.zip`, spec v0.3 |
| **Status** | Draft. Approved when this PR merges. |
| **Owner** | @Strycher |
| **Last updated** | 2026-10-08 |

## At a glance

- **What:** a real on-device GUI for the companion on color panels. It covers status bar, home, chats, compose, nodes, map (radar) and settings. It runs on the Heltec V4 TFT first, then the V4 R8 TFT and the T-Deck, and stays usable from the phone app at the same time.
- **Why now:** these boards show the inherited MeshCore status screen. Everything the GUI needs exists in firmware, but it reaches only the phone: message history lives on the phone, request replies go only to the phone link, and command logic sits inside the phone-frame parser.
- **How:** eleven Epics.
  1. Foundation: display, touch, input, theme.
  2. A firmware data layer the GUI can call.
  3. An on-device message store.
  4. Four screen Epics.
  5. Landscape layouts.
  6. R8 and T-Deck ports.
  7. Integration testing.
- **Done when:** the V4 TFT and V4 R8 TFT run every class-A screen standalone and with a phone connected, from a GUI env built in CI, within the measured memory budget. The T-Deck builds and runs class B.

## 1. Diagnosis

Verified in Epic #1314. Every Epic below traces back to a row here.

| # | Finding | Evidence | Effect |
|---|---|---|---|
| 1 | The design defines 17 screens across three layout classes (A 240×320, B 320×240, C wide), five themes as token tables, a status bar, an input map, and Settings mapped from the app. It has 11 gaps. | D1 [#1323](https://github.com/OffbandMesh/meshcore-firmware/issues/1323#issuecomment-5976944131) | Screen Epics D–H; gaps in §9 |
| 2 | Message history exists only as a queue the phone drains (`offline_queue`, 256 frames on the V4 TFT). Sent text isn't kept. The only on-device store is the badge-only `BadgeStore`. | D2 [#1324](https://github.com/OffbandMesh/meshcore-firmware/issues/1324#issuecomment-5977196529) finding 1 | Epic C |
| 3 | Login, status, trace and path replies go only to the phone link and are dropped with no app connected. | D2 finding 2 | B3 |
| 4 | Send, advert, radio save, share contact, set time and reboot are inline in `handleCmdFrame`. The badge's `ui*` methods are the precedent for callable versions. | D2 finding 3 | B2 |
| 5 | Many fields are missing or phone-only: per-channel notification mode, canned messages, median SNR, the demod floor per SF, a 1-hour airtime window, per-contact SNR/RSSI and unread count, last advert time, and the packet log. | D2 tables | B4, B6 |
| 6 | GPS has no 2D/3D signal (`getAltitude()` discards the valid flag), no time since the fix was lost, and one satellite count. | D2 review; verification on [#1327](https://github.com/OffbandMesh/meshcore-firmware/issues/1327#issuecomment-6054252731) | B5 |
| 7 | A radio failure halts boot (`main.cpp:495`). Subsystem phases exist only as trace markers. | [#1327 verification](https://github.com/OffbandMesh/meshcore-firmware/issues/1327#issuecomment-6054252731) | G5 |
| 8 | There's no touch driver and no LVGL in firmware. The CHSC6x library has no license; Seeed's MIT library has a CHSC6x reader. SensorLib (MIT) has GT911. | D3 [#1325](https://github.com/OffbandMesh/meshcore-firmware/issues/1325#issuecomment-5977646495) finding 6; [correction](https://github.com/OffbandMesh/meshcore-firmware/issues/1325#issuecomment-6054903080) | A4, J1 |
| 9 | The second (USER) button pin is Offband's TX LED on both Heltec kits (GPIO35 V4, GPIO46 R8). No second button is defined. | D3 finding 1 | A2, I2 |
| 10 | The R8 TFT env disagrees with Meshtastic and wadamesh on the reset pins and isn't in CI. Its SD shares the display SPI bus (CS GPIO3). The V4 V1 kit has no SD slot. | D3 findings 2–3 | I1–I3 |
| 11 | Internal RAM is the binding budget. With a phone connected there's about 117 KB at the low-water mark and a 106–110 KB largest block; PSRAM (2 MB V4-R2, 8 MB R8) is unused. Static data can't live in PSRAM. | D4 [#1326](https://github.com/OffbandMesh/meshcore-firmware/issues/1326#issuecomment-6018595802) | A3, C2 |
| 12 | A 350-contact save blocks the main loop about 1.8 s. | D4 [save](https://github.com/OffbandMesh/meshcore-firmware/issues/1326#issuecomment-6010349447) | B7 |
| 13 | LVGL 8.3.11 on the current panel path (40 MHz SPI, no DMA, one 1/10 buffer) scrolls at 9.3 / 14.1 / 16.9 fps at 80 / 160 / 240 MHz. The flush is partly CPU-bound. | D4 [LVGL](https://github.com/OffbandMesh/meshcore-firmware/issues/1326#issuecomment-6030843573) | A3, A5 |

**Owner decisions this plan is built on** ([#1327](https://github.com/OffbandMesh/meshcore-firmware/issues/1327#issuecomment-6054993245), [#1326](https://github.com/OffbandMesh/meshcore-firmware/issues/1326#issuecomment-6030843573)):
1. Our own CHSC6x driver in the first GUI Epic.
2. Message store: PSRAM working copy, persisted to SD where present and to flash elsewhere.
3. The phone queue and protocol stay unchanged.
4. Board order: V4 TFT → R8 → T-Deck.
5. The CrowPanel is a separate Feature.
6. MAX_CONTACTS stays at 350.
7. LVGL 8.3.11, with v9 evaluated later.
8. The nine undrawn Settings screens are built to the drawn grammar, then owner review.
9. LVGL's own RAM cost is not measured separately.
10. GUI builds are BLE-only. WiFi+BLE coexistence is measured later.
11. Messages are 160 bytes.
12. The CPU runs at 240 MHz with the screen on and 80 MHz with it off.
13. The contacts save must not run on the drawing path.
14. Every board gets a GUI env and a non-GUI env.
15. MIT only: nothing is copied from wadamesh.

## 2. Scope

**In:**
- Class A (240×320 portrait) and class B (320×240 landscape) layouts for every screen in D1 except the map tiles.
- Themes Offband, Steel, Mono HC and Accent; Look Modern and Classic; Barlow Condensed and JetBrains Mono as LVGL bitmap fonts (both OFL-1.1).
- Input: USR and AUX buttons, CHSC6x touch (Heltec), and keyboard, trackball and GT911 (T-Deck). Button map (owner, A7 bench 2026-10-15, #1451; replaces D1's PRG/USER map):
  - **USR** (the board's user button, GPIO0 on the V4) works as on stock MeshCore: click = next / scroll down, double-click = previous, hold 1.2 s = select (CLI rescue in the first 8 s after boot), triple-click = buzzer on/off. Hold 3 s = **pocket lock**: screen off, and only USR wakes it.
  - **AUX** (the TFT kit's second button): click = previous / scroll up, hold = back.
  - **Touch:** tap = open, swipe = back.
  - Multi-click window: 400 ms from release to the next press.
- The firmware data layer and message store the screens need. The phone keeps working exactly as today.
- Targets: Heltec V4 TFT, Heltec V4 R8 TFT, LilyGo T-Deck.

**Out:**
- **CrowPanel Advance 7 (ESP32-P4) and class C wide layouts:** a separate Feature, filed when this plan merges. This includes the class-C-only "heard on air" packet log on the wide Home.
- **T-Deck Pro and the E-ink theme:** a later Feature (see §9).
- **Map tiles:** Radar only until the tile-licensing question is settled (see §9).
- **WiFi status on GUI builds:** they're BLE-only, per decision 10.
- **MAX_CONTACTS above 350, a phone-protocol change, and an LVGL 9 migration.**
- **The SafeBoot menu UI:** it needs its own design pass (see §9).

## 3. Epics

Tasks are 3–4 points. Each Epic opens with failing tests for the logic it adds and ends with a verification task and one PR. Each Epic's verification is a bench image on the owner's board; per the bench rule, the next Epic starts after his bench test.

### Epic A: GUI foundation on the Heltec V4 TFT

| Task | Pts |
|---|---|
| **A1 · Failing tests:** native tests for the signal-bar scale (SNR margin over the SF floor), theme token tables (every token, every theme), the input-map dispatcher (button and touch events to actions) and the CPU clock policy (screen state to MHz). | 3 |
| **A2 · GUI env:** `heltec_v4_tft_companion_radio_ble_gui` plus a `_diag` twin, added to CI. GPIO35 becomes the AUX button in GUI envs only; the TX LED stays in the others. Fix `boards/heltec_v4.json`'s RAM ceiling, which reports the PSRAM size. | 3 |
| **A3 · LVGL 8.3.11 and display driver:** ST7789 with DMA flush and two 1/10-screen buffers in internal RAM, so drawing overlaps the transfer. Checked with `tools/diag/lvgl-bench-1326` and `OFFBAND_MEM_TELEMETRY`: scroll at least the measured 16.9 fps at 240 MHz, largest internal block recorded. | 4 |
| **A4 · CHSC6x touch driver:** our own MIT driver, polled on I2C1 (SDA47/SCL48, no interrupt pin; D3 summary table, from Meshtastic's V4 TFT variant, confirmed on the bench first) at 0x2E, as an LVGL input device. Seeed's MIT reader is the reference. Tested on the bench: tap position across the panel. | 3 |
| **A5 · Input and power:** USR/AUX press and hold per the §2 button map, touch, wake and sleep, dim and screen-off timers. 240 MHz screen-on and 80 MHz screen-off, logged. | 3 |
| **A6 · Themes and fonts:** token tables for Offband, Steel, Mono HC and Accent; Look Modern and Classic; the font table (Barlow Condensed and JetBrains Mono, OFL-1.1 license kept with the font files). | 4 |
| **A7 · Epic verification:** native tests green, CI green, bench image on `heltec-v4-tft-1` showing a themed test screen driven by touch and buttons. | 1 |

### Epic B: GUI data layer (companion firmware, no screens)

| Task | Pts |
|---|---|
| **B1 · Failing tests** for B2–B7 logic: command API results, reply routing, metric windows, GPS fix type, prefs round-trip, save scheduling. | 3 |
| **B2 · Command API:** send DM and channel, advert, join channel, save radio (restart), reset paths, share contact, set time, reboot, moved out of `handleCmdFrame` into methods. The phone frames call the same methods, so their behavior is unchanged (the CI matrix and existing tests are the check). | 4 |
| **B3 · Reply routing:** login, status, trace and path replies also delivered to the GUI, and kept when no phone is connected. | 4 |
| **B4 · Metrics:** median SNR of the last 8 packets, an SF→demod-floor table (SX1262 datasheet), TX airtime over a rolling hour, last advert time and mode, heard counts by type, and per-contact last SNR/RSSI and unread DM count. | 4 |
| **B5 · GPS:** 2D/3D from MicroNMEA's altitude-valid flag (the value `getAltitude()` discards today), and time since the fix was lost. | 3 |
| **B6 · Prefs:** per-channel notification mode (All / Mentions / Off) and the new-channel default, 9 canned messages, theme, accent, look, brightness, dim, rotation. All persisted with a versioned prefs layout. | 4 |
| **B7 · Contacts save off the drawing path:** the 1.8 s rewrite may no longer block the GUI. It's measured with the `[mem] contacts_save` line and a frame-time check during a save. | 3 |

C6 also checks the reverse risk: a 350-contact save running while the store writes. Both share the flash partition, and neither may stall the other past the next frame.
| **B8 · Epic verification:** tests green, CI green; phone app behavior unchanged on the bench (send, receive, admin login, radio save). | 1 |

### Epic C: on-device message store

| Task | Pts |
|---|---|
| **C1 · Failing tests:** ring wrap, thread index, unread counts, the 160-byte limit, the versioned header, persistence round trip, and corrupt or unknown-version files. | 3 |
| **C2 · Store core in PSRAM:** threads (channels, DMs, rooms) and a ring of fixed records holding sender, time, route or hops, echo count, SNR, text up to 160 bytes, own flag and delivery status. Persistence goes through one backend interface with two implementations, flash (C3) and SD (I4), so SD-primary (decision 2) needs no store change when the R8 arrives. | 4 |
| **C3 · Flash persistence:** two files on SPIFFS, a small thread/unread index written often and the ring written lazily. Writes never run on the drawing path. V4 limit 1,000 messages (§9). | 4 |
| **C4 · Own messages and echoes:** sent messages kept, and the "heard ×N" echo count for your own channel sends (HeardRepeats today only marks Delivered and is badge-only). | 3 |
| **C5 · Feed:** incoming DMs and channel messages go into the store alongside the unchanged phone queue (decision 3), so the phone download no longer means the device forgets. | 3 |
| **C6 · Epic verification:** tests green; survives a reset on the bench; memory telemetry recorded; a contacts save during store writes stalls neither. | 1 |

### Epic D: core screens, class A

| Task | Pts |
|---|---|
| **D1 · Failing tests:** view models for the status bar, home cells, the chat list sort and the compose byte counter. | 3 |
| **D2 · Status bar:** clock, unread, LoRa bars (long-press shows SNR, RSSI and noise), BLE, GPS states, battery with a charging bolt where the board reports charging. | 3 |
| **D3 · Home and Lock:** the six cells and Send advert; the lock screen with the two newest unread items. | 4 |
| **D4 · Chats and Conversation:** the thread list and the bubble view with the route meta line; the bell opens the notification sheet. | 4 |
| **D5 · Compose:** on-screen QWERTY with key preview and snap, canned chips, a /160 byte counter, send. | 4 |
| **D6 · Notification sheet:** the per-channel mode. | 3 |
| **D7 · Epic verification:** bench image; the owner runs a chat session standalone and with the phone connected. | 1 |

### Epic E: Nodes

| Task | Pts |
|---|---|
| **E1 · Failing tests:** filters, search matching (name prefix or key hex), the unread view. | 3 |
| **E2 · Nodes list:** type chips, Unread toggle, rows with tile, hops, last heard, bars and SNR. | 4 |
| **E3 · Search:** search with the on-screen keyboard, and the button-only letter wheel. | 4 |
| **E4 · Node detail:** repeater telemetry grid, path, and the actions Admin login, Trace and Ping (§9 defines Ping and Relayed); DM and Share contact for chat nodes. | 4 |
| **E5 · Epic verification:** bench image. | 1 |

### Epic F: Map (radar)

| Task | Pts |
|---|---|
| **F1 · Failing tests:** distance and bearing, ring scaling. | 3 |
| **F2 · Radar:** own position, pins by type, 1/2/3 km rings, selection sheet. This is the only map in this Feature. | 4 |
| **F3 · Epic verification:** bench image with GPS. | 1 |

### Epic G: Settings and boot

| Task | Pts |
|---|---|
| **G1 · Failing tests:** settings value formatting and validation (frequency range, TX power maximum). | 3 |
| **G2 · Settings list and Radio:** Save restarts the radio. | 4 |
| **G3 · Messages & alerts and Display.** | 4 |
| **G4 · Node, Radio stats, Privacy, Blocked, Contacts:** built to the drawn grammar for owner review. | 4 |
| **G5 · Connectivity (BLE only), Power, Actions, Debug, About device:** built to the drawn grammar for owner review. | 4 |
| **G6 · Boot screen:** the subsystem list from the setup phases, recorded as state the screen reads. Radio failure halts boot, so the screen never shows a failed radio row. | 3 |
| **G7 · Epic verification:** bench image; the owner reviews the nine built-to-grammar screens. | 1 |

### Epic H: class B landscape

| Task | Pts |
|---|---|
| **H1 · Failing tests:** layout selection by rotation. | 3 |
| **H2 · Class B layouts for every screen above** (48 px rail). | 4 |
| **H3 · Rotation setting:** portrait 0° and landscape 90°/270°. | 3 |
| **H4 · Epic verification:** bench image, rotated. | 1 |

### Epic I: Heltec V4 R8 TFT

| Task | Pts |
|---|---|
| **I0 · Failing tests:** SD bus arbitration (a flush and an SD write never overlap), SD backend round trip, fallback when the card is missing, hot-swap, the first-boot copy and the resync. | 4 |
| **I1 · Reset pins:** reconciled against the Expansion Board V2 schematic and checked on the bench, using a backup and restore of `Strycher-WR8`. | 3 |
| **I2 · GUI env:** `heltec_v4_r8_tft_companion_radio_ble_gui` plus `_diag`, added to CI. AUX is GPIO46. Coordinated with Epic #1322 (V4-R8 variant). | 3 |
| **I3 · SD driver on the shared display SPI bus:** SD access is arbitrated against panel flushes. | 4 |
| **I4 · SD store backend:** the store persists to SD when a card is present, 5,000 messages (§9). | 3 |
| **I6 · Design:** mockups for the owner's answer: the header disk icon (green healthy, red unavailable), the first-boot copy notice, and Settings → SD card. | 2 |
| **I7 · Hot-swap:** removal and reinsertion detected while running; the card remounts without a reboot. | 3 |
| **I8 · Copy and resync:** history is copied from flash to the card on first boot, with a notice if it's slow; flash keeps the newest 1,000 throughout (option B); while the card is out the store runs on flash, and what arrived meanwhile syncs to the card when it returns. | 4 |
| **I9 · SD in the GUI:** the header disk icon (green healthy, red unavailable), Settings → SD card (status, used/free/total, Format card), and the copy notice. | 3 |
| **I5 · Epic verification:** bench image on the R8, including a pull and reinsert, the first-boot copy and a format. | 1 |

SD card decisions (owner, 2026-10-21, #1433): the card can be swapped while running; history is copied to the card on first boot, and flash keeps the newest 1,000 as today while the card holds up to 5,000 (option B); a missing card falls back to flash and shows a red disk icon in the header (green while the card is present and healthy), and flash syncs back when it returns; Settings gets an SD card page.

### Epic J: LilyGo T-Deck

| Task | Pts |
|---|---|
| **J0 · Failing tests:** keyboard and trackball events to input-map actions, and Alt+1–9 to canned messages. | 3 |
| **J1 · GT911 touch through SensorLib (MIT).** | 3 |
| **J2 · Keyboard and trackball input:** the compose screen hides the on-screen keys; Alt+1–9 sends canned messages. | 4 |
| **J3 · SD store backend:** CS GPIO39, bus shared with the display and LoRa. | 3 |
| **J4 · Epic verification:** CI build; bench if a unit is available (§9). | 1 |

### Epic K: Integration testing

| Task | Pts |
|---|---|
| **K1 · End-to-end:** on the V4 TFT and R8, GUI and phone used together for a day of normal use, with memory telemetry logged. Results recorded. | 4 |
| **K2 · LVGL 9 evaluation:** a written comparison, no migration (decision 7). | 3 |
| **K3 · Sign-off:** the Feature's done-when list checked off. | 1 |

## 4. Order and dependencies

A → B → C → D → E → F → G → H → I → J → K, one at a time.

- A goes first because every screen needs the display, input and theme.
- B and C come before the screens because the screens read their APIs.
- D–H all edit the same UI tree, so they run serially to avoid conflicts.
- I and J reuse everything above.
- **Shared files:** `examples/companion_radio/MyMesh.{h,cpp}` (B, C), `variants/heltec_v4/platformio.ini` (A), and `variants/heltec_v4_r8/platformio.ini` (I). The last is coordinated with Epic #1322's owner through Agent Mail reservations.

## 5. Grants and authorization

| Term | Proposed | Why |
|---|---|---|
| **What merging this plan authorizes** | Building Epics A–K as written, in order | The plan is the argument for the grant |
| **Merges** | 11, one per Epic | Each Epic gets one PR |
| **Epic verification** | Owner: each Epic's PR plus his bench test of its image | The GUI is judged on the device |
| **Owner steps during the run** | A bench test at the end of each Epic; review of the nine built-to-grammar Settings screens (G7) | Bench each slice before the next |
| **Stops** | After each Epic's bench image, until the owner's bench result | As above |
| **Flash** | Not in the grant: a go per flash at each bench step | §9 choice 2 |

```grant-terms
{
  "epic_chain": [1425, 1426, 1427, 1428, 1429, 1430, 1431, 1432, 1433, 1434, 1435],
  "chain": true,
  "verification": { "mode": "owner" },
  "merge": { "budget": 11, "basis": "one merge per build Epic, A through K (#1425-#1435, section 3)" },
  "flash": null,
  "reset": null,
  "expires_after_hours": 168
}
```

`epic_chain` stays empty until the build Epics have issue numbers (§9 choice 1). An empty chain can't mint, so nothing is authorized by accident. Under choice 1A the Epics are filed and their numbers written here before the PR opens. Under 1B a one-line amendment fills them after the merge, before the mint.

Epic K has no failing-tests task: it tests the whole Feature on hardware rather than adding logic.

## 6. Can't-tell branches

| Where | Can't tell what | Does |
|---|---|---|
| Touch init (A4) | The CHSC6x doesn't answer at 0x2E | **stop** touch: the GUI runs buttons-only and the status line says touch is unavailable |
| Store load (C3, I4) | The file header has an unknown version or is corrupt | **stop**: the file is kept aside unread, the store starts empty, and a notice is shown. Nothing is overwritten |
| Store (C2) | The PSRAM allocation fails | **stop** the store: the GUI shows history as unavailable, and the phone queue is unaffected |
| SD mount (I3, J3) | A board with a slot can't mount the card | **fail-open — needs grant**: persist to flash at the flash limit and show "SD unavailable" |
| Remote action (E4) | A login, status or trace reply never arrives | **stop**: timeout, shown as "no reply" |
| Contact type (E2) | A contact's type is outside the four known | **stop**: shown with an "unknown" tile, never guessed as a type |
| Clock (D2, D3) | The clock isn't synced | **stop**: times shown as "--", never as a 1970 date |
| Compose (D5) | The text is over 160 bytes | **stop**: Send is disabled |
| Prefs load (B6) | A stored pref is out of range | **stop**: that pref reverts to its default and the event is logged |

## 7. Verification

- **Named command:** `pio test -e native` and `pio run -e heltec_v4_tft_companion_radio_ble_gui`, plus the CI matrix (`ci-green`) on every PR.
- **Acceptance bar:**
  - each Epic's native tests (A1, B1, C1, D1, E1, F1, G1, H1), written failing first;
  - the owner's bench test of each Epic image;
  - `[mem]` telemetry on each GUI image, with the largest internal block recorded in the PR.

## 8. Done when

- The V4 TFT and V4 R8 TFT GUI envs build in CI and run every class-A and class-B screen in scope, standalone and with the phone connected.
- Messages survive a reset and the phone download. The phone app works as before.
- A contacts save doesn't freeze the screen.
- The T-Deck GUI env builds and runs class B with keyboard, trackball and touch.
- K1's day of use is recorded with no lockups and the memory telemetry attached.

## 9. Your choices

| # | Question | Option A | Option B | Rec. | Your choice |
|---|---|---|---|---|---|
| 1 | When are the build Epics A–K filed? | Before this plan's PR, so `epic_chain` holds real numbers at merge, as #1385 did. This overrides #1313's "no build Epic before the plan merges." | After the merge, then a one-line amendment PR fills `epic_chain` before the mint | ⭐ A | A |
| 2 | Flash at bench steps | One go per flash, covering all its steps (backup, erase, write, restore), as #1385 did | A flash budget in the grant | ⭐ A | A |
| 3 | What is "Ping" on node detail? | A status request (round trip with telemetry) | A trace | ⭐ A | A |
| 4 | "Relayed" on repeater detail | Sent flood + direct counts from the status reply | Drop the cell | ⭐ A | A |
| 5 | "Heap" on repeater detail | Drop the cell (not in the repeater's status reply) | Add heap to the repeater status reply (a repeater protocol change) | ⭐ A | A |
| 6 | Satellites "seen" in the GPS state | Show satellites used only | Add GSV parsing for seen | ⭐ A | A |
| 7 | Message limits | V4 1,000 in flash; 5,000 with SD | V4 500; 2,000 with SD | ⭐ A | A |
| 8 | T-Deck without a unit on hand | Build and CI only; bench when one is available | Buy one before Epic J | ⭐ A | A |
| 9 | T-Deck Pro (e-ink) | A later Feature | Add to this Feature | ⭐ A | A |
| 10 | Map tiles | Out of this Feature (Radar only) | In, after you settle tile licensing | ⭐ A | A |
| 11 | SafeBoot menu UI | Its own design pass later | Build it to the drawn grammar now | ⭐ A | A |
| 12 | Accent theme's 8 preset colors | You name them | Derive 8 from the Offband palette for your review | ⭐ B | B |
| 13 | "flood · 14" in the conversation header | Echoes heard of your own last message | Hop count of the thread's last message | you define | A |
| 14 | App "Enable Message Tracing" | Maps to Debug › packet log | Drop it | ⭐ A | A |
| 15 | GUI env naming | `_gui` suffix, e.g. `heltec_v4_tft_companion_radio_ble_gui` | Replace the TFT companion envs | ⭐ A | A |

## 10. Carried-in work

| Item | Goes to |
|---|---|
| CrowPanel Advance 7 (ESP32-P4) bring-up and class C layouts | A new Feature under #1312, filed when this plan merges |
| `boards/heltec_v4.json` reports the PSRAM size as the RAM ceiling | A2 |
| WiFi+BLE coexistence RAM (decision 10, later) | A later task under #1312 |
| #788, the color theme for the existing non-GUI UI | Stays separate under #1312 |
| `tools/diag/lvgl-bench-1326` and `OFFBAND_MEM_TELEMETRY` (#1388) | Used by A3, C6 and K1 |

When this plan merges, each Epic and Task gets a GitHub issue and a Citadel task, per canon.
