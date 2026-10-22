# Changelog

All notable changes to Offband are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). This
project uses **Pattern B fork versioning** -- an independent
`offband-vMAJOR.MINOR.PATCH` line (historically `crosswire-v*`) with the upstream MeshCore baseline tracked
separately. See [VERSIONING.md](VERSIONING.md) for the cadence (commit per task /
compile, PR per epic, tag per landing), the `+N` dev-build suffix, and the
dev / `-rc` / stable release channels.

This changelog begins at the unified `firmware-base` line (**0.13.0**). Versions
prior to 0.13.0 predate it; see `git tag -l 'crosswire-v*'` and the tag
annotations for that history (highlights: v0.12.0 NimBLE migration, v0.11.x
Plan-3 web UI, v0.10.x observer multi-broker pipeline, v0.5.0 initial backfill).

## [Unreleased]

## [1.6.0-beta1] - 2026-10-22

221 commits since 1.5.0-beta7, on the MeshCore 1.17.0 base.

### Added

**On-device companion GUI** — color touch panels, on the Heltec V4 TFT and the V4-R8 TFT (feature #1313)

- The GUI shell with Home and Lock screens (#1497), a status bar with a LoRa long-press popover
  (#1496), and Chats and Conversation screens (#1498).
- LVGL 8.3.11 on a DMA ST7789 driver (#1441), an in-house CHSC6x touch driver wired into LVGL
  (#1442), and the V4 TFT GUI companion env with a USER button and an honest RAM ceiling (#1440).
- The R8 TFT GUI env, pins taken from the V2.03 schematic (#1676).
- GUI themes, Look, and the design's fonts (#1444). Characters the fonts lack fall back to the
  nearest they carry (#1761).
- Input map, wake/sleep, and a 240/80 MHz clock policy (#1443). USR works like stock MeshCore;
  AUX scrolls up and goes back (#1451).
- Layout selection by rotation, class B landscape layouts for every screen, and the panel, LVGL
  and touch all following Display > Rotation (#1642, #1643, #1644).
- **Chats and channels** — Compose with the on-screen keyboard (#1499), per-channel notifications
  with the bell's sheet and a channel menu (#1500), adding a channel on the device as Public,
  hashtag or private (#1747, #1749), a "+" in Chats that asks what to start and an empty-box
  button (#1748), and a private channel's key from its menu plus Leave channel (#1750).
- **Nodes** — the Nodes list (#1531), search by keyboard or letter wheel matching anywhere in the
  name (#1532, #1534), and node detail, actions and the login sheet (#1533).
- **Map** — a radar view on the Map tab (#1430, #1565).
- **Settings and boot** — Settings and the boot screen (#1431, #1604), settings persisted in
  `/prefs.json` (#1457), a time-zone setting that can suggest the zone from the kept position and
  name its source (#1610), the radio's own sensors with touch-scrolling Settings pages (#1661),
  and GPS 2D/3D fix state with time since the fix was lost (#1456).
- **Data layer** — login, status, trace and path replies reach the GUI (#1454); radio and contact
  metrics (#1455); the phone's command actions become MyMesh methods (#1453); and the contacts
  save runs a slice per tick (#1458).
- **Message store** — the store core (#1462), persisted to flash without stalling the GUI (#1463),
  own messages kept with delivery state and a "heard xN" count (#1464), and incoming messages kept
  on the device (#1465).
- **SD card** — the card on the panel's SPI bus (#1677), an archive that counts what it holds
  (#1678), the archive and card monitor (#1674), catching up with the store including hot-swap
  (#1704), the card in the GUI (#1705), card memory with a cap on what polling an empty slot costs
  (#1703), and a crash's program counter and backtrace reaching the SD log (#1730).
- Trace gets its round trip from its own button, as Ping does, and a trace's SNR shows on the
  detail (#1534).
- Opt-in memory telemetry and an LVGL bench for GUI budgeting (#1389), plus frame-time telemetry
  for the bench check (#1441).

**LilyGo T-Deck** (epic #1434)

- GT911 touch (#1726), read from the controller's first point register, without an axis swap
  (#1730).
- The hardware keyboard and trackball (#1727), the keyboard backlight following the screen, the
  on-screen keys hidden when a keyboard is present, and digits 1 through 4 starting a canned reply.
- The radio on the shared SPI2 bus through RadioLib (#1725), with the SD card's chip-select held
  high on that shared bus (#1728).
- The T-Deck GUI env and its `_diag` variant in CI (#1729), with an env lint asserting the
  schematic pins and one SPI bus (#1723).
- **The T-Deck GUI image is not published in this release.** The code is in and the env builds in
  CI.

**Heltec V4-R8, with the SAW filter** (epic #1322)

- FEM/LNA runtime control (#1339), TX LED control to #542 parity (#1340), and a power-off path
  that cuts FEM power and takes the base teardown (#1341).
- A WiFi+MQTT observer env (#1343) carrying the full channel list plus `_sys` and 50 contacts
  (#1487), diagnostic builds (#1342), and the board in the CI matrix and the release list (#1344).
- Host tests for the R8 board controls (#1338).

**Other boards and images**

- Diagnostic builds for the Heltec V4 (R2) (#1375).
- V4 TFT and R8 GUI images are published in releases (#1772).
- **The color Offband lockup on every color display** — the ST7735 panels (#867) and the remaining
  color panels (#868), with each panel's splash art sized to the space it actually has (#1124).

**Queen City Con badge**

- **Sound** — Settings > Tones with a picker that auditions a tone before you keep it (#1421),
  tunes as a table with a typed tune checked before it plays (#1418), every sound event given a
  slot with a send confirmed (#1419), the tune for an event coming from prefs rather than the build
  (#1420), and the shipped chime list of six tones (#1447).
- **Custom tunes** — the slot format and slot store, one file per slot (#1474), a slot id resolving
  to its tune and joining the picker (#1475), Settings > Tones > Custom with the eight slots listed
  (#1476), an RTTTL editor typed on the badge and validated before it saves (#1477), the app able
  to push a tune into a slot in chunks (#1478), and a wrapped view over LineEdit so a long string
  can be proofread (#1473). A refused chunk says where the device is, and an edit cancels a push
  (#1478).
- **Per-channel tunes** — a channel can hold its own tune, with an encoding and a fallback (#1522),
  persisted in `/channels2` (#1523), resolved on an arriving channel message (#1524), a
  conversation showing and setting its own tune (#1525), and events able to play custom tunes
  (#1549).
- GPS power modes — Off, On and Power-save, measured and driven (#1415).
- The LED shows traffic over unread rather than sitting solid-on (#1367), with the heartbeat moved
  to the module LED to free the badge's own (#1366).
- A fresh badge ships holding the conference channels (#1364), takes the conference radio defaults
  on first boot (#1363), and names itself from the last five of its BLE MAC (#1598).
- Navigation and messages get their own text size (#1362), and the title bar is one size on every
  screen (#1370).

**Observer**

- Broker lifecycle events are queued and emitted on `loopTask` (#1133), MQTT protocol events
  recorded in the capture ring (#1134), the pool's slot decisions and non-MQTT failures recorded
  (#1135), and rotation plus every slot's standing (#1136).

**Build, CI and tooling**

- A docs-only change no longer pays for the full build matrix (#1708).
- Build-time guards: per-peripheral pin collision across every variant (#1601), a built-artifact
  check that the Offband capabilities are linked (#1602), a display-virtual guard that scans
  variant drivers in CI (#1123), and a source-level guard that frame sizing goes through
  `maxFramePayload` (#1619).
- A rig boot smoke test that flashes, captures and asserts the badge actually runs (#1641).
- An on-demand diagnostic-build how-to (#969).
- The sniffer rig samples the INA fast enough to see a transmit burst (#1693).

### Fixed

- **A USB companion no longer streams fewer contacts than it declares** (epic #1298) — the known
  issue carried in beta6 and beta7. `writeFrame` refusals were unchecked when the TX FIFO filled,
  so whole frames were dropped silently. The stream's cursor now advances only when the frame was
  sent (#1283), `isWriteBusy()` reports the transport rather than a constant false (#1302), a
  committed frame is finished instead of retried behind itself (#1315), and an abandoned stream is
  reported on a stock build rather than only under debug (#1316).
- **MACThenDecrypt rejects non-block-aligned ciphertext** (#1586) — a malformed packet could
  overflow a stack buffer.
- `ver`, `version` and the companion device-info field now report one consistent Offband-first
  `<offband>-<stock>` string (#1391). Previously these three could disagree, and a build with no
  reachable tag could present itself as a plausible stock version.
- Companion frames are sized by what the BLE link actually delivers rather than by the buffer
  (#1621).
- The low-battery cutoff debounces over 300 s instead of firing on a single raw ADC sample, in all
  three UI families (#1667).
- The BLE PIN is shown in full, whether set or random (#1768).
- Settings > Actions no longer reboots a GUI in landscape (#1766).
- The GUI panel moved off the radio's SPI bus (#1450).
- The status bar stays filled through a screen change (#1730).
- Every channel the radio holds appears in Chats, instead of a channel being hidden until its
  first message (#1741).
- Channel names no longer display two `#` symbols (#1624).
- A channel message clears its sender tag only on the row that has one (#1685).
- Chat rows can't overlap, emoji render, one-line labels stay on one line, "Mentions only" says
  what it does, GPS seeking pulses instead of vanishing, and there is a way out of Compose by
  touch (#1501).
- The Nodes list builds 12 rows at a time, search keeps up with a full contact list, and with an
  AUX button every USR click acts at once (#1534).
- The GUI contacts save writes every contact (#1551) and walks every contact, newest included
  (#1550).
- A torn store entry is dropped at load rather than displayed as garbage (#1482).
- The message store writes only what changed, in place, and flash writes wait until nobody is
  using the device (#1472).
- A badge with no keyboard fitted can leave Settings (#1590).
- A tone preview can no longer silence a real notification (#1417). The tone picker auditions what
  it opens on (#1467), its highlight lands on a row that is actually drawn (#1475), the slot list
  re-reads on entry with its row buffer sized honestly (#1476), and the badge can reach the gate
  that decides whether it makes a sound (#1468).
- A committed tune slot stores its text rather than two NUL bytes, and the slot-push surface is
  guarded where its state lives (#1479, #1480).
- `#test` and `#echo` ship quiet on a fresh badge (#1625).
- The badge thread screen shows 10 characters of the sender name, with a 4-character hanging
  indent on wrapped channel messages (#1669).
- The released V4 repeater image no longer carries a personal node identity (#1378).
- A `platformio.ini` section lookup no longer finds its own name in a comment (#1646).
- The boot smoke test no longer skips its own stale-image check (#1659).
- The bench capture survives a line it cannot print (#1514).
- `pool_start_recorded_` is read by another task, so it is marked volatile (#1134).

### Changed

- **The root default radio preset is now the US standard — 910.525 MHz, 62.5 kHz, SF7** (#1488).
  Previously the root default was the European preset. `heltec_v4` inherits the root preset rather
  than restating it (#1506).
- The status-LED heartbeat cadence is overridable per board, and the badge slows it to about 5 s
  (#1614).
- Device-identity build envs move to a per-host file outside every checkout (#1410).
- The hardware reference files live in this repository rather than depending on a sibling path
  (#735).
- The RC32 display-session capture is preserved in the repository before its worktree goes (#1732).

### Documentation

- Plan of record for the on-device companion GUI (#1329) and the companion-observer `/neighbors`
  contract (#1385).
- Plan of record for the Offband broker on `offband-prod` over wss/443 (#1520).
- The Queen City Con badge sound plan of record, at Feature level (#1408).
- MeshCore 1.18.0 base-update assessment and merge plan (#1575). **The base update itself has not
  landed; this release remains on 1.17.0.** Two hardware-ownership claims in that assessment were
  corrected (#1616).
- SF7 stands for the conference channel, with the reasoning kept, and why 500 kHz would need SF10
  (#1363).
- Epic I SD card mockups and the SD decisions behind them (#1702).
- The navigation-face inventory says what it does not cover (#1502).

## [1.5.0-beta7] - 2026-09-28

### Added
- **Queen City Con 0x4 badge — a new board, and Offband messaging on it** (feature [#1172](https://github.com/OffbandMesh/meshcore-firmware/issues/1172); bring-up [#1173](https://github.com/OffbandMesh/meshcore-firmware/issues/1173), interface [#1188](https://github.com/OffbandMesh/meshcore-firmware/issues/1188)). Board class, pin map with `Serial1` kept off the buzzer and LED pins, BLE companion env ([#1177](https://github.com/OffbandMesh/meshcore-firmware/issues/1177), [#1180](https://github.com/OffbandMesh/meshcore-firmware/issues/1180)); a ThingPulse-font renderer usable by any `DisplayDriver` ([#1179](https://github.com/OffbandMesh/meshcore-firmware/issues/1179)); the conference splash and its generator ([#1181](https://github.com/OffbandMesh/meshcore-firmware/issues/1181), [#1182](https://github.com/OffbandMesh/meshcore-firmware/issues/1182)); battery math with a SafeBoot ADC sample-time hook ([#1178](https://github.com/OffbandMesh/meshcore-firmware/issues/1178)).
  - **The keyboard works and drives the UI** — CardKB decode and I2C driver, arrows/Enter/Esc ([#1204](https://github.com/OffbandMesh/meshcore-firmware/issues/1204), [#1205](https://github.com/OffbandMesh/meshcore-firmware/issues/1205)).
  - **Messaging**: channel and DM sends with retries and an ACK tick when a repeater is heard passing a channel send on, a message store with conversations and unread counts, Messages-as-Home with inline compose, and Contacts / Nearby / Status / GPS / Settings on the SW1 cycle ([#1227](https://github.com/OffbandMesh/meshcore-firmware/issues/1227)–[#1235](https://github.com/OffbandMesh/meshcore-firmware/issues/1235)).
  - Text in Org_01 with TomThumb for status lines, a Text size setting, a Screen off setting defaulting to one minute, and first boot with the GPS on ([#1236](https://github.com/OffbandMesh/meshcore-firmware/issues/1236)–[#1238](https://github.com/OffbandMesh/meshcore-firmware/issues/1238), [#1244](https://github.com/OffbandMesh/meshcore-firmware/issues/1244), [#1245](https://github.com/OffbandMesh/meshcore-firmware/issues/1245)).
  - A Battery screen that learns the badge's own 100% from the charger settling, with a manual pin and a way back to auto ([#1254](https://github.com/OffbandMesh/meshcore-firmware/issues/1254)).
  - A diag env with a bring-up self-test legend, a spare-pad ID beacon, and SafeBoot's battery reading on screen ([#1183](https://github.com/OffbandMesh/meshcore-firmware/issues/1183), [#1210](https://github.com/OffbandMesh/meshcore-firmware/issues/1210), [#1185](https://github.com/OffbandMesh/meshcore-firmware/issues/1185), [#1211](https://github.com/OffbandMesh/meshcore-firmware/issues/1211)).
- **`wdt` / `wdt status` console verbs** ([#1159](https://github.com/OffbandMesh/meshcore-firmware/issues/1159)) — the board answers whether its watchdog actually armed, rather than the build flag answering for it. `wdt hang` and `WDT_TEST_HANG_AFTER_MS` are diagnostic-only and prove the watchdog on hardware, in every role ([#1171](https://github.com/OffbandMesh/meshcore-firmware/issues/1171)).
- **Observer log forwarding to a syslog sink** ([#1059](https://github.com/OffbandMesh/meshcore-firmware/issues/1059)–[#1061](https://github.com/OffbandMesh/meshcore-firmware/issues/1061), [#1193](https://github.com/OffbandMesh/meshcore-firmware/issues/1193), [#1194](https://github.com/OffbandMesh/meshcore-firmware/issues/1194), [#1240](https://github.com/OffbandMesh/meshcore-firmware/issues/1240)). Wired on all six observer envs; the sink is stored in NVS so it survives a reboot; a window can run until switched off; the capture ring is read through a non-destructive cursor so forwarding never consumes what it forwards; an armed forward names a capture stop to the sink.
- **More on the diagnostic log channel**: the UART0 wire is capped at `OFFBAND_LOG_MIRROR_LEVEL` ([#1069](https://github.com/OffbandMesh/meshcore-firmware/issues/1069)), selective BLE diagnostics ([#1070](https://github.com/OffbandMesh/meshcore-firmware/issues/1070)), a periodic `[radio]` line on the telemetry tick ([#1071](https://github.com/OffbandMesh/meshcore-firmware/issues/1071)), USB companion lifecycle lines ([#1072](https://github.com/OffbandMesh/meshcore-firmware/issues/1072)), and a `[boot]` summary of what the board is actually running ([#1073](https://github.com/OffbandMesh/meshcore-firmware/issues/1073)).
- **RC32 diagnostic builds carry the fleet diag surface** ([#935](https://github.com/OffbandMesh/meshcore-firmware/issues/935)) — capture forced on in all three, and the two companion builds emit a battery and uptime line every 30 s.
- **Badge binaries publish with every release** ([#1241](https://github.com/OffbandMesh/meshcore-firmware/issues/1241), [#1243](https://github.com/OffbandMesh/meshcore-firmware/issues/1243)), diag build included, so a tester on unproven hardware can produce a log.

### Fixed
- **ESP32 repeaters and companions now arm the runtime watchdog** ([#1280](https://github.com/OffbandMesh/meshcore-firmware/issues/1280), epic [#446](https://github.com/OffbandMesh/meshcore-firmware/issues/446)). The `startWatchdog`/`feedWatchdog` calls had sat inside `#if defined(NRF52_PLATFORM)`, so **no ESP32 repeater or companion had ever armed one** — a hung node stayed hung until someone reset it by hand. A hung main loop on any ESP32 board now reboots within 30 s with `reset_reason=TASK_WDT`. nRF52 behaviour is unchanged. `WDT_TIMEOUT_SECS` is the one build flag for an env that proves it needs longer.
  - CI blocks the regression, and the guard now sees through line continuations and same-file aliases ([#1160](https://github.com/OffbandMesh/meshcore-firmware/issues/1160), [#1281](https://github.com/OffbandMesh/meshcore-firmware/issues/1281)).
- **The WiFi config setters report a failed NVS write instead of acknowledging it** ([#697](https://github.com/OffbandMesh/meshcore-firmware/issues/697)): `set wifi.ssid`, `set wifi.pwd` and `wifi enable/disable` discarded the write result, so a failed save still returned success and the value was silently gone after a reboot. `wifi clear all` now names both keys when both fail, instead of only `pwd`.
- **Boot and shutdown reporting says what it means** ([#1074](https://github.com/OffbandMesh/meshcore-firmware/issues/1074), [#1270](https://github.com/OffbandMesh/meshcore-firmware/issues/1270), [#1075](https://github.com/OffbandMesh/meshcore-firmware/issues/1075), [#1272](https://github.com/OffbandMesh/meshcore-firmware/issues/1272)): previous-boot uptime and `rtc_count` report what they claim, the runtime is saved at shutdown, every reset is named with why the board went down, and a previous-boot runtime recovered from NVS prints as a floor rather than a measurement.
- **The noise-floor line is rate-limited** so a capture stays readable ([#1275](https://github.com/OffbandMesh/meshcore-firmware/issues/1275)), and the diag-build log-mirror flag uses its current name ([#1041](https://github.com/OffbandMesh/meshcore-firmware/issues/1041)).
- **The caplog logrotate owner is resolved at install and the policy checked first** ([#1265](https://github.com/OffbandMesh/meshcore-firmware/issues/1265)).
- **Badge corrections**: DMs keep the recipient's whole key ([#1227](https://github.com/OffbandMesh/meshcore-firmware/issues/1227)); message times, contacts and adverts survive the clock being set ([#1233](https://github.com/OffbandMesh/meshcore-firmware/issues/1233)); stale contacts stay readable ([#1231](https://github.com/OffbandMesh/meshcore-firmware/issues/1231)).

- **A stock MeshCore client no longer chokes on our own debug output** ([#1114](https://github.com/OffbandMesh/meshcore-firmware/issues/1114)): on a `_usb` companion, `Serial` *is* the framed companion protocol, and three paths wrote plain text into it. The client parsed one "unexpected frame type" per byte, settings rendered empty and the channel list never arrived.
- **Companion USB frames no longer stall at 64-byte multiples** ([#1093](https://github.com/OffbandMesh/meshcore-firmware/issues/1093)): a frame whose on-wire length is a multiple of the USB max packet size was held host-side until the next write. USB-CDC console replies gained the same zero-length terminator ([#1039](https://github.com/OffbandMesh/meshcore-firmware/issues/1039)).
- **`caplog forward` no longer owns the WiFi link** ([#1048](https://github.com/OffbandMesh/meshcore-firmware/issues/1048)): arming overwrote whatever the operator had set with `wifi on`, and disarming tore the link down.
- **The badge's diag build no longer puts the wearer's position on an exposed pad** ([#1247](https://github.com/OffbandMesh/meshcore-firmware/issues/1247)): the periodic GPS line carries `pos=valid` / `pos=invalid` and no coordinates.
- **The badge battery bar tells the truth** ([#1246](https://github.com/OffbandMesh/meshcore-firmware/issues/1246), [#1252](https://github.com/OffbandMesh/meshcore-firmware/issues/1252), [#1254](https://github.com/OffbandMesh/meshcore-firmware/issues/1254)): ends a real cell reaches, the lithium curve measured from the RC52 [#1004](https://github.com/OffbandMesh/meshcore-firmware/issues/1004) burn, an average so a transmit stops moving the number, and a 100% point learned from the charger settling.
- **Any key wakes the badge's display** ([#1239](https://github.com/OffbandMesh/meshcore-firmware/issues/1239)); **first boot starts with the GPS on and the US radio preset** ([#1236](https://github.com/OffbandMesh/meshcore-firmware/issues/1236)); the diag log mirror moved to the one pad that is actually connected ([#1185](https://github.com/OffbandMesh/meshcore-firmware/issues/1185)).

### Changed
- **RC32 diagnostic build filenames change: `_testdiag` → `_diag`** ([#935](https://github.com/OffbandMesh/meshcore-firmware/issues/935)). Every board's diagnostic assets now use one suffix. **If you download RC32 diag builds, the filenames differ from beta4–beta6** — they are now `heltec_rc32_companion_radio_ble_diag`, `..._usb_diag` and `heltec_rc32_repeater_diag`.
  - The `_diag` names were previously held by three RC32 bring-up envs (`..._usb_diag`, `..._ble_diag`, `..._usb_diag_nocrashlog`) that diverged from the shipped image with their own GPS/LoRa overrides — the divergence the tester-build policy exists to prevent ([#704](https://github.com/OffbandMesh/meshcore-firmware/issues/704)). Those envs served the [#702](https://github.com/OffbandMesh/meshcore-firmware/issues/702)/[#740](https://github.com/OffbandMesh/meshcore-firmware/issues/740)/[#741](https://github.com/OffbandMesh/meshcore-firmware/issues/741) investigation and were **retired**, not renamed.
  - **RC32 diagnostic builds now capture more** ([#935](https://github.com/OffbandMesh/meshcore-firmware/issues/935)): log capture is forced on in all three, and the two companion builds emit a battery and uptime line every 30 s.
- **CI builds every shipped RC32 and RCC6 env** ([#877](https://github.com/OffbandMesh/meshcore-firmware/issues/877)): `release-envs.txt` ships 15 envs from those two boards and the build matrix compiled none of them, so a change that broke either board merged green and surfaced only when a release tag was cut. Since [#1016](https://github.com/OffbandMesh/meshcore-firmware/issues/1016) one failed env blocks the whole release.
- **A beta's release notes come from that beta's own CHANGELOG section** ([#1294](https://github.com/OffbandMesh/meshcore-firmware/issues/1294)): the release body was matched after stripping the pre-release suffix, so every beta published the base version's section — beta6's page opened with "### Fixed (beta2)". The tagged version's own section now wins, with the base version kept as the fallback for `-rc` tags.
- **Standards pointers moved to a local file and private data redacted** ([#1098](https://github.com/OffbandMesh/meshcore-firmware/issues/1098)): this repository is public, and tracked files cited a private engineering-standards document by name and rule number while carrying private-subnet addresses and a personal email.
- **A release page's body is prose when there is any** ([#1306](https://github.com/OffbandMesh/meshcore-firmware/issues/1306)): `release-notes/<version as tagged>.md` outranks the CHANGELOG section, so a release page carries written notes rather than a changelog. An empty notes file fails the release instead of silently publishing the changelog. With no notes file present the previous behaviour is unchanged.
- Release asset envs: **108 → 110**. Added `QCC_Badge_companion_radio_ble` and `QCC_Badge_companion_radio_ble_diag`; the three RC32 `_testdiag` names became `_diag`.

### Known issues shipping in this beta
- **A USB companion streams fewer contacts than it declares** ([#1279](https://github.com/OffbandMesh/meshcore-firmware/issues/1279), [#1283](https://github.com/OffbandMesh/meshcore-firmware/issues/1283), epic [#1298](https://github.com/OffbandMesh/meshcore-firmware/issues/1298)): a RAK 4631 declared 350 contacts and streamed 344–345. `writeFrame` results go unchecked when the TX FIFO is full, so whole frames are dropped silently. Present in beta6 as well; the fix is in progress.
- **The badge holds its GPS powered the whole time it is enabled** ([#1288](https://github.com/OffbandMesh/meshcore-firmware/issues/1288)): with a GPS module fitted, 2.5–3 days of battery against over a week without one. Duty cycling is filed and is not in this build.
- **The badge's Epic 2 verification is still open** ([#1208](https://github.com/OffbandMesh/meshcore-firmware/issues/1208)): the keyboard and SW1 interface ships in this beta without that sign-off. Treat the badge build as under test.

## [1.5.0-beta6] - 2026-08-29

### Added
- **Heltec RadioCore RC52 (nRF52840) ships** ([#625](https://github.com/OffbandMesh/meshcore-firmware/issues/625) family, [#1016](https://github.com/OffbandMesh/meshcore-firmware/issues/1016)): first release binaries for the RC52 — companion (BLE/USB), repeater, and display variants, each with a `_diag` twin. Brought up on real hardware: TFT pins corrected and rotation confirmed on the bench ([#950](https://github.com/OffbandMesh/meshcore-firmware/issues/950)/[#951](https://github.com/OffbandMesh/meshcore-firmware/issues/951)), flicker-free indexed-palette display buffer ([#856](https://github.com/OffbandMesh/meshcore-firmware/issues/856)), FEM powered before enable with runtime FEM/LNA control ([#931](https://github.com/OffbandMesh/meshcore-firmware/issues/931)/[#983](https://github.com/OffbandMesh/meshcore-firmware/issues/983)), low-battery boot protection with USB-attach wake ([#857](https://github.com/OffbandMesh/meshcore-firmware/issues/857)), reset-cause capture ([#953](https://github.com/OffbandMesh/meshcore-firmware/issues/953)). Room server excluded from the release list for now.
- **nRF52 boards get the diagnostic log channel** ([#887](https://github.com/OffbandMesh/meshcore-firmware/issues/887)): the UART0 caplog mirror and boot beacon — previously ESP32-only — now run on nRF52, with bench diag envs for the case-accessible boards ([#979](https://github.com/OffbandMesh/meshcore-firmware/issues/979)) and a bounded per-byte wait so the mirror can't stall a boot ([#888](https://github.com/OffbandMesh/meshcore-firmware/issues/888)).
- **INA228 power telemetry is actually read** ([#991](https://github.com/OffbandMesh/meshcore-firmware/issues/991)): the sensor was identified but never decoded; every I²C read is now checked instead of silently becoming 0xFF.

### Fixed
- **RAK3401 configuration un-forked** ([#984](https://github.com/OffbandMesh/meshcore-firmware/issues/984)): removed the `RAK_BOARD` define that was the board's whole divergence from upstream, and disabled the GPS probe (which kills TX) on every RAK3401 env. **The board's TX investigation itself remains open on #984** — these changes clean the ground under it.

### Known issues shipping in this beta
- Serial-framing fix ([#718](https://github.com/OffbandMesh/meshcore-firmware/issues/718)) still unverified on hardware; RC32 companion RST dark-boot ([#702](https://github.com/OffbandMesh/meshcore-firmware/issues/702)) still open — power-cycle instead.

## [1.5.0-beta5] - 2026-08-22

### Fixed
- **BLE transfers actually stop losing 3 bytes per frame now — the beta2–beta4 fix never
  took effect** ([#711](https://github.com/OffbandMesh/meshcore-firmware/issues/711),
  [PR #939](https://github.com/OffbandMesh/meshcore-firmware/pull/939)): a tester on beta4
  still saw the exact clipping signature (222 bytes short = 3 × 74 full frames), which
  exposed the real defect. The 1.17.0 base update wrapped the companion's serial path in
  `MultiSerialInterface`, which overrides nine interface methods but not `maxFrameSize()` —
  so the MTU-aware BLE frame sizing was **never consulted on the companion path at all**,
  on any board, and every earlier fix changed code that wasn't reached. `maxFrameSize()`
  now delegates to the enabled interfaces, using the same predicate `writeFrame()` uses.
  **Bench-verified on a Heltec V4.3:** a near-full 16 KB caplog downloads complete in 96
  chunks, which mathematically proves 171-byte payloads (the broken sizing gives 95).
  Covers all three field reports. Note the wider blast radius: every chunked BLE path on
  ESP32 — config stream, observer views, block list, GPS — shared the same sizing and was
  silently clipping; all are corrected by this, though only the caplog path has been
  re-tested on hardware. nRF52 boards were never affected (their frames fit the link).
- A `setMTU()` refusal is now checked and logged instead of silently discarded
  (same PR; previously inferable only from a truncated download).

### Known issues shipping in this beta
- **The serial-framing fix ([#718](https://github.com/OffbandMesh/meshcore-firmware/issues/718))
  is still unverified on hardware** — merged in beta2, test-pinned, awaiting a bench capture.
  If you see corrupted serial captures, a report either way helps.
- **RC32 companion boots dark after RST** ([#702](https://github.com/OffbandMesh/meshcore-firmware/issues/702)) —
  power-cycle instead; repeater role unaffected.

## [1.5.0-beta4] - 2026-08-20

### Changed
- **WiFi telemetry lifecycle reworked — fresh plumbing, watch it** ([#910](https://github.com/OffbandMesh/meshcore-firmware/issues/910)–[#915](https://github.com/OffbandMesh/meshcore-firmware/issues/915)): the
  repeater's WiFi+MQTT connect/teardown moved from blocking spins in the loop task to a
  host-testable state machine — multi-pass triggers, a cycle deadline for a flapping AP,
  MQTT throttling, and an unstickable `Failed` state. Host-tested, **not yet soaked on
  hardware** — this beta is its first field exposure. Repeater-telemetry testers: if your
  node's publishing or OTA window behaves differently than beta3, that report is exactly
  what we need.

### Added
- **Offband branding on every role** ([#842](https://github.com/OffbandMesh/meshcore-firmware/issues/842)/[#822](https://github.com/OffbandMesh/meshcore-firmware/issues/822)): six independent splash implementations
  became one shared component — repeaters, room servers, and sensors no longer boot to
  MeshCore's logo, and colour panels get native-resolution Offband art. Also: ST7735 boot
  rotation un-shadowed after being silently discarded on 27 envs since June ([#840](https://github.com/OffbandMesh/meshcore-firmware/issues/840)/[#841](https://github.com/OffbandMesh/meshcore-firmware/pull/841)),
  an Offband dark palette for ST7735 ([#840](https://github.com/OffbandMesh/meshcore-firmware/issues/840)), and the repeater power-off screen now stays
  lit for its full countdown ([#845](https://github.com/OffbandMesh/meshcore-firmware/issues/845)).
- **RadioCore tester fleet** ([#824](https://github.com/OffbandMesh/meshcore-firmware/issues/824)/[#834](https://github.com/OffbandMesh/meshcore-firmware/pull/834), [#804](https://github.com/OffbandMesh/meshcore-firmware/issues/804), [#830](https://github.com/OffbandMesh/meshcore-firmware/issues/830)): RC32 `testdiag` diagnostic builds
  publish on every release, and the RCC6 (ESP32-C6) env family ships — companion
  BLE/USB, repeater-display, and the **first ESP32-C6 observer** (plus diag variants).
  RCC6 heap headroom is roughly half an S3's; single-broker-with-rotation is the
  supported observer shape there.

### Fixed
- **Serial output is usable again** ([#899](https://github.com/OffbandMesh/meshcore-firmware/issues/899)): unconfigured broker slots were spamming NVS
  ERROR lines at ~29/sec — 97% of all serial output — and splicing CLI replies mid-word.
  Optional NVS reads now go through a type-checked guarded helper; the spam is gone.
  (The underlying two-writers-one-UART splice is [#871](https://github.com/OffbandMesh/meshcore-firmware/issues/871), mitigated by this but not closed.)
- **CLI `get` surface verified clean on hardware** ([#764](https://github.com/OffbandMesh/meshcore-firmware/issues/764)/[#765](https://github.com/OffbandMesh/meshcore-firmware/issues/765) via [#778](https://github.com/OffbandMesh/meshcore-firmware/issues/778)/[#779](https://github.com/OffbandMesh/meshcore-firmware/issues/779)): three `get`
  keys returned empty replies for ten months, and four call sites transmitted
  uninitialized stack when a handler wrote nothing — the original "mojibake in the app"
  report. Fixed, then swept on-device: 47/47 keys answer, zero empties, zero
  prefix-match leaks. An advisory CI guard ([#775](https://github.com/OffbandMesh/meshcore-firmware/issues/775)) now watches the dispatch chains.
- **Observer brokers rotate honestly** ([#720](https://github.com/OffbandMesh/meshcore-firmware/issues/720)/[#848](https://github.com/OffbandMesh/meshcore-firmware/issues/848)): a broker flapping faster than the
  60s dwell could monopolize its slot forever (eviction now ages on a reconnect-proof
  clock), and a terminal-`Failed` broker is reaped once instead of panicking the pool.
- **RC32/RCC6 board corrections** ([#700](https://github.com/OffbandMesh/meshcore-firmware/issues/700), [#835](https://github.com/OffbandMesh/meshcore-firmware/issues/835)): `monitor_rts/dtr=0` board-wide on RC32 so
  serial tools stop resetting it, and the RCC6 battery ADC multiplier derived from the
  actual divider.

### Known issues shipping in this beta
- **RC32 companion boots dark after RST** ([#702](https://github.com/OffbandMesh/meshcore-firmware/issues/702)) — power-cycle instead; repeater role
  unaffected. Under active diagnosis.
- **The beta2 BLE and serial-framing fixes ([#711](https://github.com/OffbandMesh/meshcore-firmware/issues/711)/[#718](https://github.com/OffbandMesh/meshcore-firmware/issues/718)) are STILL unverified on
  hardware, three betas later.** If you had truncated caplog downloads on Android or
  corrupted serial captures: flash this, retry, report either way — two minutes of your
  time closes two P1s.

## [1.5.0-beta3] - 2026-08-17

### Added
- **Heltec RadioCore RC32 binaries are published for the first time** (#771): the release
  matrix carried no RC32 env at all, so the pipeline produced nothing for the board and a
  tester could only be handed an ad-hoc file. `heltec_rc32_companion_radio_ble`,
  `heltec_rc32_companion_radio_usb`, `heltec_rc32_repeater` and `heltec_rc32_room_server`
  now build as release assets. Applies to the **RC32-L62** (HT-RA62A LoRa) carrier; the
  RC32-68 Wi-Fi HaLow variant has a different pinout and is not supported.
- **RC32 display, brought up properly** (#749, #757, #758): a colour splash rendered at the
  panel's native 220x128 rather than upscaled from the 128x64 logical surface, a dark theme
  in Offband brand colours, and antialiased JetBrains Mono replacing the 1x2-stretched 5x7
  font.
- **`OFFBAND_MESHLOG_UART0`** (#769): mirrors the capture log to raw UART0 in parallel with
  the ring. On the 15 native-USB-CDC boards the live log is unreachable exactly when it
  matters -- USB-CDC dies with the chip and power-cycles the board on attach, so every log
  captured that way comes from a boot that already succeeded. UART0 survives the chip
  failing and does not reset it, so a board on battery with no host can still be observed.
  Off by default; enabled per-env.
- **`OFFBAND_SHOW_HEAP`** (#761): home-page heap readout, opt-in and off by default.

### Fixed
- **A reset could leave a USB companion dead until it was unplugged** (#702, #756):
  `Serial.setTxTimeoutMs(0)` set the retry budget to zero in the USB-CDC write path, and the
  decrement underflowed to 4,294,967,295 -- making the escape from a blocked write
  unreachable for roughly 50 days. A board plugged into a computer with nothing draining the
  port stalled in early boot with no display, no radio and no BLE. Attaching a serial
  monitor drained the buffer and released it, which is why every previous investigation
  found a healthy board. **Affects every native-USB-CDC board, not only the RC32.** Power
  banks were never affected: with no host there is no enumeration and nothing to block on.
- **CrashLog could block the boot it was diagnosing** (#756): its serial writes are now
  non-blocking and bounded, and the flush in the shutdown handler is removed. Losing the
  last bytes of a crash dump is preferable to a device that will not reset.
- **RC32: GPS is disabled** (#704): `PIN_GPS_EN` is GPIO45, the ESP32-S3 **VDD_SPI strap**.
  Driving a strapping pin as a peripheral enable is a boot hazard, and the stock env shipped
  with GPS on.
- **RC32: display orientation corrected to landscape**, peripherals the board does not have
  removed, and the variant aligned to Heltec's own board definition (#704).
- **RC32 display performance** (#745, #747): bulk SPI writes replace one-byte-per-call
  transfers, and a back buffer means a frame blits once instead of the panel being visibly
  erased and redrawn live.

### Known issues
- **RC32 battery indicator reads high** (#780): percentage is mapped linearly from voltage,
  which overstates through the middle of a LiPo's discharge -- measured at 68% displayed
  where the cell held roughly 40-45%. It also cannot reach 100%, since that requires exactly
  4200 mV and a real pack never presents it. The device additionally powers off at
  `AUTO_SHUTDOWN_MILLIVOLTS` while the indicator still shows 33%.
- **A SafeBoot low-battery hold is silent** (#782): the board looks dead rather than
  protected, and connecting power does not end the hold -- it wakes on its own timer, which
  backs off to as much as 10 minutes.

## [1.5.0] - 2026-08-15

### Fixed (beta2)
- **Rotated-out brokers no longer lose the traffic they missed** (#723): a regression in
  `beta1`. Re-attaching a broker resynced its ring cursor to the head, which was meant to
  stop a *newly enabled* broker replaying stale boot backlog — but the same path runs on
  every rotation re-entry, so a broker returning from its dwell discarded exactly the
  backlog the ring exists to hold. On an observer with rotating TLS brokers this silently
  dropped most of the feed, and the drop counter read zero throughout because a resync is
  a deliberate skip, not an overrun. Resync now happens on first attach only.
- **Observer packets over ~98 bytes were silently discarded** (#726): the `/packets`
  JSON builder renders up to 1024 bytes but the publish ring only accepted 512, and a
  refused append counted nothing anywhere. Hex-encoding doubles the packet, so anything
  over ~98 bytes of air time simply vanished — present since the ring was introduced
  (#175), found in the field on beta1 by a tester whose observer published one small
  message and silently dropped the two larger ones. The ring now accepts what the
  builder produces, and a compile-time assert makes the size mismatch a build error
  rather than a silent refusal.
- **BLE transfers no longer lose 3 bytes per full frame** (#711): a regression of #450,
  reintroduced by #454 — when the cached peer MTU was ≥ 179, every full BLE frame was
  sized 3 bytes over what the link delivers and the stack clipped the tail. Caplog
  downloads noticed (they announce a byte total); every other chunked path clipped
  silently. Frame sizing is now pinned by a regression test that fails without the fix.
  Not yet device-verified — that is what this beta is for.
- **Framed serial writes are all-or-nothing** (#718): a short non-blocking write could
  emit a frame header with no payload behind it, desyncing the stream — corruption, not
  just loss. A refused frame is now retried whole; nothing partial reaches the wire.
  Also awaiting device verification on this beta.
- **Repeater OTA recovers from a stale session** (#676): starting OTA over WiFi tears
  down a stale server instead of refusing, and the repeater holds off idle light-sleep
  while a persistent-WiFi window is open so the OTA window stays reachable.

### Changed (beta2)
- **Observer publish ring reworked; `/packets` payload slimmed** (#727): the ring now
  stores compact packet records rendered per-broker at publish time instead of
  pre-rendered JSON — roughly 10 KB less RAM and a 40-deep ring, with receive
  timestamps captured at receive so a backlogged broker's messages aren't mis-stamped.
  The `/packets` body now carries the raw packet plus link metadata only: CoreScope and
  its forks (CoreComms/EastMesh, confirmed from their public ingestor source) decode
  the raw packet themselves and never read the on-device parsed fields. **If you run a
  custom `/packets` consumer that read parsed fields, decode from `raw` now.**

### Added (beta2)
- **Heltec Vision Master E290 builds** (#733): companion BLE + USB for the first e-ink
  board in the fleet, from the upstream 1.17 variant. The radio/mesh side is stock
  upstream support; the e-ink display path under the Offband stack is exactly what beta
  testing needs to prove. Repeater role deferred.

### Added
- **`wifi clear` and a visible reason for WiFi disconnects** (#696): an observer on an
  open, passwordless network could sit at `StaConnecting` indefinitely with no reason
  available anywhere, and a stored PSK could not be cleared without an NVS wipe that
  destroys the device identity. `wifi clear [pwd|all]` now removes stored credentials
  properly, and STA disconnect reasons appear in the `wifi status` reply as well as the
  retry log — the status reply being the load-bearing half, since a remote reporter
  cannot read a serial console. Association behaviour is unchanged.

### Fixed
- **Observer multi-broker rotation actually rotates** (#708): with more than two TLS
  brokers enabled, promotion was a first-eligible-by-slot-index scan whose only
  fairness mechanism only works at exactly two candidates. Every broker above the two
  lowest slots was starved permanently — measured on a Heltec V3 as 36/35/**0**
  promotions over 80 minutes, with the third broker never publishing once. Promotion is
  now oldest-served-first, verified on hardware as an even three-way cycle. The
  selection logic moved into a dependency-free header with unit tests, because the
  defect is invisible at two brokers and two brokers is what it was validated at.
- **Broker slot 0 is an always-on primary again** (#707): a tcp broker holds no mbedTLS
  context and is therefore exempt from TLS rotation — that exemption is the only thing
  that ever made slot 0 always-on. Moving slots 0–1 to `wss` in 1.4.0 silently demoted
  slot 0 into the rotating pool, where it was torn down and rebuilt every ~2 minutes
  (178 evictions in one 6.9 h soak). The seed puts slot 0 back on
  `mqtt://mqtt1.okimesh.org:1883`, with the trade recorded at the seed row so it is not
  "upgraded" again by accident. Existing devices need the matching config profile —
  firmware seeds only apply to fresh NVS.
- **JWT brokers could not be configured from a config profile** (#709): the config wire
  key was `jwt_audience` while the schema, `SCHEMA.md`, and every published profile used
  `jwt_aud`, so setting the audience by its documented name failed with "unknown broker
  field". Every JWT broker slot was unconfigurable. Both spellings are now accepted, on
  the setter and the getter, keyed off the schema constant so they cannot drift apart
  again.
- **Publish-ring overrun is counted instead of discarded silently** (#710): when a
  rotated-out broker fell behind, the ring clamped its cursor, counted nothing, logged
  nothing, and the only indicator reset itself as soon as the broker caught up — a fleet
  could discard most of its feed with every diagnostic reading clean. There is now a
  monotonic per-broker drop counter, reported in `mqtt status` as `ring_dropped` and on a
  60-second `[ring]` line so a passive serial capture measures it. A newly-attached
  broker resyncs to the ring head instead of replaying stale backlog.

### Changed
- **"Held, low heap" no longer reports normal rotation as a memory error** (#715): one
  broker state covered three different conditions and was named after the rarest. With
  multiple TLS brokers the common case is "waiting its turn", which displayed as a
  memory fault — on a 3-broker pool, 2 of 3 permanently showed a problem that did not
  exist. Serial and CLI now distinguish `waiting-turn` from `held(low-heap)`. **The app
  still shows the old wording**: the wire value is deliberately unchanged until the
  client can accept the new one.
- **Observer builds trimmed and the publish ring deepened** (#701/#710): observer envs
  drop to 5 contacts, 4 user channels and 6 broker slots, freeing 14,660 B of static
  RAM; 8,192 B of that funds a 32-slot publish ring (was 16), sized from measured
  fleet traffic rather than estimated. **On these builds the BLE-companion half is
  capped at 5 contacts and 4 channels** — devices holding more truncate on load
  (favourites and most-recent survive). Non-observer builds are byte-for-byte unchanged.


## [1.4.0] - 2026-08-13

### Added
- **Settings survive the 1.16 → 1.17 upgrade** (#627/#659/#665): a legacy-prefs layout
  detector with Offband-aware offsets migrates existing device preferences across the
  upstream prefs-layout change; cross-family records are refused rather than mis-read;
  caplog settings now apply on the 1.17 `/prefs.json` path; and every migration outcome
  leaves a persistent event record that survives reboot. Hardware-verified across 7
  device/architecture cells.
- **On-air packet hash returned on channel-send confirmation** (#611, capability-gated):
  lets the client correlate an outgoing channel message with CoreScope observer counts
  by packet hash.

### Fixed
- **Future-poisoned clocks are now recoverable** (#607, PRs #612): owner-authenticated
  time sets (client sync, CLI `time`/`clock sync`) are accepted in **both directions** —
  "clock cannot go backwards" refusals are gone. Automated clock sources (GPS decode,
  boot-time contacts bootstrap) are bounded by a plausibility window (firmware build
  date → +20 y), so a GPS week-rollover mis-decode can no longer poison the clock, and
  contact `lastmod` values written under a poisoned clock are clamped on load — stores
  self-heal at first boot. Every accepted clock set (and boot-capped rejections) emits a
  `[clock]` audit line on serial/caplog. **Note for recovered nodes:** peers that heard a
  node's poisoned (future) timestamps may ignore its messages until their per-contact
  recency view catches up; re-adding the contact on the peer resolves it immediately.
- **nRF52 BLE companions crash-boot loop on the 1.17.0 base** (#668): RAK4631 and
  T1000-E BLE companions double-initialized BLE and crash-looped ~4 s after boot;
  never in a tagged release, listed for bench-build testers.
- **Caplog retrieval uses one shared redaction pass on both paths** (#667): closes a
  gap where one retrieval path could return lines the other would have redacted.
- **Wio Tracker L1: stale SafeBoot battery-divider gate removed** (#620).

### Changed
- **MeshCore base updated 1.16.0 → 1.17.0** (#628/#643): full upstream refresh of the
  fork base, bringing upstream 1.17.0 fixes and board support forward under the
  Offband feature set.
- **Eastme.sh broker seed renamed to CoreComms.net** (#677) — the seeded slot-3 broker
  follows the service's rebrand: `wss://mqtt.corecomms.net:443/mqtt`, JWT audience
  `mqtt.corecomms.net`, CA anchor `gts-r4` (endpoint + full TLS chain verified live
  2026-08-13). Slot 4 (Eastmesh.au) also repinned `letsencrypt` → `gts-r4`: its live
  chain moved to GTS Root R4, so the old anchor no longer validates at all. Fresh-NVS
  only (#262 semantics); existing devices keep their stored config — operators with an
  old seeded slot can `mqtt clear <N>` + reboot to reseed. Re-implements external
  PR #282; thanks to **EldoonNemar** for the original contribution and the rebrand
  heads-up.

- **OKIMesh broker slots 0-1 moved to wss/TLS** — the two seeded OKIMesh brokers now use
  `wss://mqtt{1,2}.okimesh.org:9002/mqtt` over TLS (`letsencrypt` CA) instead of plaintext
  `mqtt://…:1883`. Auth stays anonymous (TLS secures the transport; no MQTT credential).
  Both remain disabled by default (#262). Note: as wss/TLS, these slots now wait for a
  valid wall clock — `mqtt status` shows `held(no-clock)` until NTP/GPS, then connects.
  Fresh-NVS only; existing devices keep their stored config. (#592)

## [1.3.0] - 2026-08-06

The **field-diagnostics** release. Adds an end-to-end crash/observability stack —
persistent CrashLog on every role, a live **caplog** capture-and-forward pipeline
(RAM ring → client download, or live UDP syslog to a remote sink), a runtime
watchdog on both ESP32 and nRF52, and an operator **command surface** to drive it
all without a serial cable. Plus user-controllable indicators (kill the LED/OLED
light show), headless-device notification scope + button-action matrix, and the
shared multi-broker MQTT engine the repeater-telemetry genericization builds on.

Most changes are additive — existing nodes keep working. One caveat: the repeater
multi-broker engine is **foundation only** this release (not yet operator-configurable
end-to-end); the field-provisionable binary is a future release (#295).

### Added

**Field diagnostics — caplog (capture + forward)**
- **caplog capture** — a serial-capture tee into a static RAM ring, with CLI verbs
  (`caplog start/stop/erase/status/dump`) and level filtering (boot/error/debug/packet). (#401, #403, #432)
- **Client download (Companion)** — framed caplog download to the companion app over
  the companion API (`0xC4`), BLE-MTU-aware chunking. (#406, #418, #451, #454)
- **Live syslog forward (Repeater, WiFi-telemetry builds)** — stream captured lines
  off-device as UDP syslog to a remote rsyslog sink during an operator-armed window,
  so a panic's lead-up survives the reboot the RAM ring can't hold. Sink host/port
  are a runtime pref (`set syslog.host/port`). (#561, #566)
- **Common across roles** — caplog and its persisted enabled-state run on
  repeater / observer / room-server / sensor, with early boot-log capture and a
  non-blocking serial mirror. (#562, #435, #448)
- **Command surface (`offband-cmd`)** — queue commands, poll status, fetch results,
  arm/tail caplog over the cmdrelay HTTP API — drive a headless node with no SSH —
  plus a receiver setup script and operator guide (`docs/remote-diagnostics.md`). (#567, #569)
- **Test harness + redaction** — companion protocol test harness (frame core +
  caplog round-trips, hardware-verified) and a redacting serial-capture tool that
  never leaks an SSID. (#414, #421, #380, #382)

**Crash observability — CrashLog**
- **Persistent CrashLog on every role** — relocated to a portable `diagnostics/`
  core; compiles and runs on nRF52 (`.noinit` retained RAM); deferred re-dump for a
  late serial connect; uniform boot-survival across all roles. (#367, #376, #377, #361, #463, #475)

**Runtime watchdog**
- **nRF52 hardware watchdog** — auto-recovers a hung main loop with a visible reset
  reason; true loop-driven green-LED heartbeat. (#257, #266, #275)
- **ESP32 runtime watchdog** — board-layer `esp_task_wdt` plumbing, activated
  sensor-first. (#518, #519)
- Gated debug CLI (`hangtest`, `resetreason`) + internal-DRAM heap-headroom in stats. (#357, #358)

**User-controllable indicators (#542)**
- Kill the light show: `led on|off|status` and `display auto|always-on|always-off`
  CLI, persisted and applied at boot across all roles; companion `0xC5` client
  control + capability bit so the app can render toggles. `FIRMWARE_VER_CODE` → 21.

**Headless-device UI**
- **Notification scope** ALL/SELF/NONE + whitespace-tolerant mention matching, via
  `0xC5`. (#510, #524, #525)
- **Button-action matrix** — dispatch button presses through a stored action matrix;
  edge-capture (not sampling) with a debounced, race-free queue on the T1000-E. (#550, #551)

**Repeater multi-broker engine** *(foundation for the telemetry-genericization feature — not yet operator-configurable end-to-end)*
- Shared `MqttBrokerPool` + round-robin TLS scheduler made reusable from the
  repeater: dwell-timer rotation, per-broker ring-log drain, heap-gated concurrency
  (measured ceiling `OFFBAND_MAX_LIVE_TLS=4`), repeater broker-config CLI over serial
  + client. (#175, #536, #537, #538, #539, #506, #512, #554)
- Per-message RSSI populated in the v3 message / data-recv frames. (#460, #465)

### Changed
- **Default broker slots reseated** — the OKI Mesh's own two brokers now hold slots 0
  and 1: slot 0 `mqtt1.okimesh.org` (relabelled from "CoreScope Dayton", same host) and
  slot 1 `mqtt2.okimesh.org` (new, same plaintext/anonymous/1883 config). MeshMapper and
  eastme.sh swap to slots 2 and 3, Eastmesh.au moves 5 → 4, and slot 5 frees up for
  operator use. **LetsMesh-US and LetsMesh-EU are no longer seeded** — both remain fully
  supported and can be added by hand in any free slot (`gts-r4` still resolves). Every
  seeded slot still ships disabled per #262. Because seeding is skip-if-present, this
  only affects fresh-NVS devices; existing devices keep their current layout. (#317)
- **Role-agnostic config dispatch** — `wifi.*` / `display.*` / broker config
  extracted into shared providers with a registration-time key-space overlap
  detector. (#364, #366, #486, #512)

### Fixed
- caplog command code `0xC3`→`0xC4` (FEM/LNA collision); cap bit `0x08`→`0x20`. (#409, #434)
- MQTT keepalive capped at 60 s + `[mqtt-err]` connect errors surfaced; keepalive
  re-asserted on JWT-refresh rebuilds; esp_mqtt client allocated on promotion, not at
  configure time. (#506, #532, #534)
- **Flash tooling** — one-approval-one-flash token TTL; serial-first device matching
  (VID:PID demoted to a gated class fallback); bootstrap records `usb_serial`;
  bridge-board (CP2102/CH340) identity + flash path. (#500, #501, #503, #355, #273, #468)
- GPS routed through the log sink with runtime client-controllable debug. (#424)
- **NVS round-trip test asserted a stale default** — the test still required slot 0 to
  seed as *enabled*, which #262 changed to disabled on 2026-07-02 without updating the
  assertion. Corrected alongside the #317 layout change. (#317)

## [1.2.0] - 2026-07-21

Adds **MeshSmith Photon‑1W support** (both MCU flavors) on the **MeshCore 1.16.0** base, and **receive-sensitivity recovery on Heltec V4** (external FEM LNA control, which stock MeshCore leaves bypassed),
plus the ESP32‑C6 I2C‑scan boot‑hang fix found bench‑validating it, a NimBLE build fix,
and flashing‑docs corrections.

### Added
- **MeshSmith Photon‑1W support — ESP32‑C6 + nRF52 (#193, #194)** — vendors MeshSmith's MIT Photon
  variants (`meshsmith_photon_esp32c6`, `meshsmith_photon_nrf52`; Seeed XIAO ESP32‑C6 / XIAO nRF52840 +
  Ebyte E22‑900M30S 1W radio) and wires all roles into CI + the release pipeline. ESP32‑C6 needed
  minimal MeshCore‑consistent base edits (antenna‑switch virtuals, protected `_gps_serial`, NimBLE dep);
  nRF52 needed none. The **ESP32‑C6 companion + repeater are bench‑verified** on hardware; the
  **nRF52 variant is not yet bench‑verified** — review findings tracked as bench checkpoints on #193/#194.
- **External FEM LNA control on Heltec V4 companions (#298).** MeshCore leaves the Heltec V4
  front-end module's LNA bypassed at boot, so V4 companions ran with degraded receive
  sensitivity. Offband now enables it at companion boot from a persisted `radio_fem_rxgain`
  preference (default on), on the boards whose FEM exposes an independent LNA line
  (`heltec_v4`, `heltec_tracker_v2`, `heltec_t096`). A new companion-API command (`0xC3`,
  SET/GET) plus a capability bit lets the client show a user toggle, gated on the
  runtime-detected FEM chip. `FIRMWARE_VER_CODE` 15 -> 16. Verified end-to-end on a
  KCT8103L (V4.3) board.
- **Model string names the detected FEM part (#327)** — reads `Heltec V4 OLED (KCT8103L)`
  or `(GC1109)`, so a V4.3 (independent LNA control) is distinguishable from a V4.2 in the app.

### Fixed
- **Photon‑1W ESP32‑C6 hung at boot, dead on the mesh (#294).** The C6 variant declares its I2C bus,
  but `EnvironmentSensorManager::begin()` ran the blind I2C scan regardless; on the C6 the scan wedges
  the I2C peripheral (hangs at addr `0x0d`) and never returns, so `setup()` never reached `loop()`.
  The scan is now skipped on boards that set `ENV_SKIP_I2C_SENSOR_SCAN` (guard vendored verbatim from
  MeshSmith's fork); every other board is unchanged.
- **CLI accepted garbage after a valid key (#299).** `get radio foobar` returned the radio
  settings, and `set radio 910.525,62.5,7,5,junk` silently applied the first four values and
  dropped the rest. Keys now require a whole-token match; extra `set radio` parameters are rejected.
- **64 ESP32 BLE companion environments could not build (#199, #90).** The NimBLE dependency was
  declared per-variant with no shared source, and the greedy source filter pulled the BLE
  interface into non-BLE (`usb`/`wifi`) builds too. Factored into a shared config; restores
  boards silently absent from prior releases, including `Heltec_v2_companion_radio_usb` and
  `Xiao_C3_companion_radio_usb`. A CI invariant now guards every ESP32 env. (The #89 fix below
  is the first, single-env instance of this class.)
- **FEM auto-detect source comment corrected (#318, #321).** The Heltec V4 FEM type is set by a
  board strap, not chip-internal pulls as the code claimed; documented against schematics + the
  GC1109 datasheet. A boot detection probe is available behind `-D FEM_DEBUG_PROBE` (off by default).
- **`pio-flash` identified devices by USB port-path, not identity (#336, #323).** Boards were
  misidentified after any USB port or device swap, and a no-discriminator registry entry
  wildcard-matched its whole chip family. Now keys on the device-unique USB serial and refuses
  ambiguous matches; hardware-verified across colliding same-VID:PID boards. Bench tooling, not
  shipped firmware.
- **`pio-flash` bootstrap parses the ESP32-C6/H2 base MAC and anchors its MAC regexes (#290, #292).**
  Bench tooling.
- **`Xiao_S3_WIO_companion_radio_usb` build (#89)** — exclude `SerialBLEInterface.cpp` from the USB
  companion env (it has no BLE), resolving the `NimBLEDevice.h` regression from the #288 NimBLE migration.
- **Flashing docs pointed at the wrong page and omitted a release file** (#326). The
  README's web-flasher link went to `meshcore.co.uk/flasher.html` — a wrapper page that
  forwards to a configurator, not the flasher. It now points at
  [`flasher.meshcore.io`](https://flasher.meshcore.io/). The per-release download table
  (README + `.github/release-footer.md`) also never listed the nRF52 `*.zip` DFU package —
  40 of the 130 assets in a release — so it now has a row, pointing at serial DFU tooling
  while steering most users to the simpler `*.uf2`. Both tables described the merged image as a web-flasher **"Full Firmware"**
  option; that is the device-catalog flow, and an Offband user takes the **Custom Firmware**
  flow instead, where the flasher auto-detects the `-merged.bin` suffix and prompts before
  erasing. Corrected, and both surfaces gained a short "how do I flash it" section covering
  the nRF52 UF2 drag-drop path, the ESP32 web-flasher path, and building from source.
  Documentation only — no firmware change.

### Changed
- **CLAUDE.md: no‑upstream‑merge policy (#197)** — Offband does not merge from upstream MeshCore; the
  `upstream` remote stays fetch‑only for reference. Keep MeshCore nomenclature/coding‑standard consistency
  for clean rebasing.

### Documentation
- Heltec V4 FEM/LNA ground-truth from schematics, the GC1109 datasheet, and on-device
  measurement, including V4.2-vs-V4.3 RX-path differences (#320, #321).
- Repeater WiFi-telemetry genericization design-of-record (#296, design only).
- Block-user companion-API contract reconciled to as-built (#313, #315).

## [1.1.2] - 2026-07-02

Companion **GPS auto-baud + on-demand GPS status**, plus the wedge/time fixes
found while validating it on real hardware, and a **safer first-flash default** — a
fresh Observer no longer auto-publishes to a preset MQTT broker. Still on the
**MeshCore 1.16.0** base.

### Added
- **GPS auto-baud detection** — the companion now detects the GPS modem's baud rate at
  runtime (probing 115200 then 9600, validating by a checksum-good NMEA sentence), so one
  image reads either a standard 9600 module or a 115200 one with no rebuild. It re-probes
  on a GPS enable and keeps trying if a modem is slow to start at boot. (#216, #233)
- **On-demand GPS status to the app (`0xC1`)** — the client can query live GPS state
  (enabled / detected / fix / baud / lat / lon / alt / sats / time) instead of only seeing
  position at connect. (#216)
- **On-device build identity** — an optional build tag shows on the app device-info
  field and the OLED splash, so a specific build is identifiable without a serial
  console. (#222)

### Changed
- **Fresh flashes no longer auto-publish to OKIMesh CoreScope.** A newly-flashed
  Observer previously seeded the CoreScope (Dayton) broker **enabled**, so a device
  flashed anywhere in the world immediately fed MQTT to OKIMesh CoreScope tagged as a
  Dayton node. On a fresh flash every broker now ships **disabled** — the operator
  opts in per slot. (#262)
- **Default region is now `XYZ`** (a non-geographic placeholder) instead of `HAO`
  (Dayton), so an out-of-region device stops mislabeling itself until the operator sets
  its region via `mqtt iata`. Fresh / NVS-erased devices only; existing devices keep
  their stored config. (#262)

### Fixed
- **GPS at high baud could wedge BLE.** An unbounded GPS read loop let a fast modem
  (e.g. 115200, multi-constellation) monopolize the main loop and starve BLE — the app
  would slog or stall. GPS ingestion is now bounded per loop. (#231)
- **GPS time showed garbage before the date was acquired.** A position fix can arrive
  before the calendar date; the device now reports `time=0` ("acquiring") until a real
  date is parsed, instead of a wrapped-garbage timestamp. (#232)

### Internal
- Build/governance tooling + GPS design & diagnostic records — no firmware-behavior
  change. (#214, #217–#219)

## [1.1.1] - 2026-06-26

Urgent patch: the **RAK3401 1W companion** radio was dead on v1.1.0. Still on the
**MeshCore 1.16.0** base.

### Fixed
- **RAK3401 1W companion radio dead on Offband (no TX/RX).** Offband's GPS support
  (#104) probed a pin that doubles as the radio's power-enable on the RAK3401; with no
  GPS attached it left the radio unpowered, so it couldn't transmit or receive. GPS is
  now disabled on the RAK3401 companion build until the probe is fixed. (#211)

## [1.1.0] - 2026-06-24

The headline is **Epic F: the companion-API config command** — a WiFi observer can now be
configured entirely from the app (WiFi, the full MQTT broker pool, display) over BLE —
plus the observer config/NVS-layer hardening that came out of bench-validating it. Still
on the **MeshCore 1.16.0** base.

### Added
- **Companion-API config command (Epic F, #159)** — configure a WiFi observer from the
  MeshCore/Offband app over BLE: WiFi credentials, the full MQTT broker pool (per-slot
  url / port / transport / auth / JWT claims / CA-cert / topic-prefix / IATA),
  enable / disable / clear a slot, and display settings. Wholly observer-gated and surfaced
  behind a new `WIFI_OBSERVER_SUPPORT` capability bit + `FIRMWARE_VER_CODE 14`, so stock
  companions are unaffected. (#160–#169)
- **Per-broker live state in the broker read** (#172) — each slot now reports its live
  connection state (`connecting` / `up` / `backoff` / `held` …) + last-error class, so the
  app shows whether a broker actually connected, not just that it's enabled.
- **Resolved-default hints** (#173, #186) — when `jwt_owner` / `iata_override` are unset,
  the read shows the value the device will actually use at connect (its own pubkey / the
  global IATA) as a placeholder instead of a confusing blank — in both the pool dump and
  the per-field read.

### Changed
- **Honest observer config writes (#181)** — the config / NVS write path used
  to silently swallow failures (a broker disable could ACK success while nothing changed).
  Every writer, the live-reload path, and the crash logger itself now surface + log
  failures with context. This is what root-caused #179.
- **Compact broker config storage + seamless migration** (#182) — broker config is stored
  as small per-key entries (no blank fields) rather than one ~1 KB blob, so writes fit a
  near-full NVS; a one-time **invisible boot migration** reclaims space on upgrade, so the
  operator never sees a transient write error.
- **CoreScope (okimesh) default broker** → `mqtt://mqtt1.okimesh.org:1883` (#170).
- **Offband tell in MQTT** (#174) — observer nodes append a 📡 to their display name in
  MQTT payloads, so feeds / maps can flag Offband observers.

### Fixed
- **#179** — a broker disable ACK'd success while the dump still reported `enabled=1`.
  Root-caused as a near-full-NVS write failure the old code lied about; fixed by #181
  (honest write) + #182 (storage that fits). Verified on hardware.
- **Observer capped at 1 concurrent TLS broker** (#171) — a heap guard stops the OOM reboot
  when a 2nd TLS/wss broker's ~60 KB mbedTLS context exhausts the Heltec V3 heap. Measured
  on hardware (1 wss ≈ 63 KB free, 2 wss ≈ crash).
- **BLE connect survives the client's reconnect-thrash** (#178) — a deeper ESP32 BLE frame
  queue (4 → 12) + stream terminators on reconnect, so a mid-stream reconnect can't leave
  the app's contact / settings sync hung. Affects companion **and** observer. (The root —
  the client's uncapped reconnect retry — is paced client-side in meshcore-client.)
- **Crash log reliable for the whole boot** (#183) — the 1 Hz heartbeat was flooding the
  4 KB RTC crash-ring (wrapped in ~50 s) and evicting real crash diagnostics; the heartbeat
  now writes the ring every 30 s / on a sharp heap drop, so a post-reboot dump keeps the
  evidence that explains a crash past boot+50 s.

### CI / build
- **Heltec T114** (nRF52) companion added to the CI matrix (#185) — first nRF52-companion
  build coverage (complements the existing RAK4631 repeater).

## [1.0.0] - 2026-06-17

First production-stable Offband release — companion, observer, and repeater roles
all working and hardware-verified. Built on the **MeshCore 1.16.0** base.

### Base
- **MeshCore 1.16.0 base-update** (#126) — the fork is rebased onto upstream MeshCore
  1.16.0, smoke-verified across all three active roles (Companion, Observer, Repeater)
  on Heltec V3/V4 + RAK3401.

### Added
- **RAK3401 (WisMesh 1W) GPS** (#104) — the RAK12500 (u-blox ZOE-M8Q) I²C GPS now works
  in Slot D. Companion and repeater acquire a position fix. (Position only; the I²C
  path does not sync the clock.)
- **Display always-on toggle** (#141) — `display always on` keeps a USB/mains-powered
  observer's screen lit; `display normal` restores the 15 s timeout. Persists across
  reboots, applies immediately. Heltec V3, V4 OLED, V4 TFT observers.
- **Display rotation (0/180)** (#148) — `display rotate 0/180` / `display flip` over the
  `_sys` channel; persists, applies immediately. **Verified on the OLED observers
  (Heltec V3, V4 OLED).** Displays without a verified rotation driver (the V4 TFT)
  report `rotation not supported on this display` rather than silently no-op'ing; TFT
  rotation is tracked separately.

### Known issues
- **Heltec V4 observer GPS position unverified** (#149) — an attached UART GPS doesn't
  yet surface a position on the V4 observer (reads 0,0). Observer time (NTP/SNTP) and all
  other function are unaffected; GPS only adds the device's own map-position dot.

## [0.18.1] - 2026-06-14

### Fixed
- **WiFi password confirmation wording** — `set wifi.pwd` now replies
  `wifi.pwd set (N chars entered)` instead of `wifi.pwd set (length=N)`, which was
  being misread as a 17-character maximum. The reply reports the length of what was
  *entered* (never the secret PSK); it is not a cap. WiFi passwords accept the full
  WPA2 range (8–63 chars).

## [0.18.0] - 2026-06-14

### Added
- **Heltec V4 TFT observer build** (`heltec_v4_tft_companion_observer_wifi`) — the
  observer role on the TFT (ST7789) display variant, switched to the NimBLE stack
  the observer requires. Added to the CI build matrix and the release env set so it
  **always builds and ships**.

### Changed
- **Firmware download clarity** — a "Which file?" table in the README and a static
  footer appended to every GitHub Release, explaining `-merged.bin` (first install)
  vs `.bin` (update) vs `.uf2` (nRF52).

### Fixed
- **`pio-flash` device matching** — `find_in_registry` prefers an exact
  DeviceID-instance match over a class-only label, so a registered chip is no longer
  shadowed by a same-class device with null discriminators (fixes nRF52/ESP32
  mislabels where two boards share a VID:PID).

### Internal
- Wire a gitignored `HARDWARE.local.md` (symlink to the LoRa hardware inventory) plus
  a CLAUDE.md "read before any hardware work" pointer.

## [0.17.0] - 2026-06-14

First release under **OffbandMesh/meshcore-firmware**. Bundles the 0.16.0 observer
work below (which landed on `firmware-base` but was never separately tagged) with
the Crosswire→Offband rebrand and the OffbandMesh org cutover. *(Version pending
owner confirmation; the tag is hardware-gated per VERSIONING.md.)*

### Changed
- **Rebranded the fork from Crosswire to Offband** (GitHub org `OffbandMesh`):
  the C++ namespace, build macros, embedded identity blob, version prefix
  (`offband-v*`), MQTT / flash-audit identity fields (`offband_*`), brand
  strings (serial banner, `version` command, OLED splash, Home Assistant
  manufacturer), and the WiFi setup-AP SSID (`Offband-Observer-`). Historical
  `crosswire-v*` release tags are preserved; the `_sys` PSK domain separator
  and the MeshCore interop topic namespace are intentionally unchanged. (#100)
- **Repo / board / working-dir cutover to OffbandMesh** — repo
  `OffbandMesh/meshcore-firmware`, OffbandMesh org Projects board, and the
  preflight / CLAUDE.md / label-sync workflow re-pointed; removed the stale
  upstream `CNAME`. (#107, #111)

### Docs
- Finished the rebrand reference cleanup across docs + code comments. (#113, #114)
- Release-readiness pass: README getting-started + multi-role positioning, the
  docs index surfaces the observer guides, and observer `_sys` CLI reference
  corrections. (#117)

## [0.16.0] - 2026-06-12

Observer time/clock + `_sys` CLI + reporting hardening. Hardware-validated on
ST-P (Heltec V4): three brokers connected, a valid phone-free wall clock, and
correct radio + position reporting on CoreScope.

### Added
- Observer `_sys` CLI grammar standardization: `wifi status` / `wifi enable` /
  `wifi disable` (namespace-subcommand, aligned with `mqtt status/enable/disable`)
  and symmetric `get mqtt.broker.<N>.<key>` (read mirror of `set`, secrets
  redacted). `get wifi.status` kept as a backward-compat alias. (#45)
- Observer time-source arbiter, GPS > NTP > BLE: SNTP provides a fast clock and
  GPS takes over on lock (SNTP stopped once GPS owns the clock) -- a JWT-grade
  wall clock with no phone dependency. (#69)
- Position in the observer MQTT `/status` payload, selected by `advert_loc_policy`
  exactly as the LoRa advert (0,0 null-island suppressed). (#31)
- `mqtt status` broker state `held(no-clock)`: a wss/TLS slot deferred pending a
  sane wall clock now reads as *held* (no retry burn) instead of looking like a
  failure, and releases automatically once the clock syncs. (#87)

### Changed
- Observer MQTT pool no longer blocks on GPS acquisition: SNTP starts immediately
  on STA-connect and the pool comes up regardless, so tcp brokers publish at once
  while wss/TLS brokers self-defer to `held(no-clock)`. (#87)
- `/status` radio fields (`freq/bw/sf/cr`) now report the **runtime** radio config
  (`NodePrefs`, set via the companion API) instead of the compile-time `LORA_*`
  build macros. (#88)

### Docs
- `docs/observer-cli-commands.md` -- `_sys` CLI command reference. (#45)
- `docs/observer-gps-location-config.md` -- GPS / location operator guide. (#31/#69)

## [0.15.0] - 2026-06-10

Observer MQTT connectivity -- hardware-validated against three brokers
(CoreScope / W8OOF tcp-anon, eastme.sh wss/jwt, LetsMesh-US wss/jwt).

### Added
- Owner broker registry: seed the default 6-slot broker set with an `iata=HAO`
  default; per-broker GTS Root R4 + ISRG Root X2 CA certificates added and
  mapped, registry cert-names corrected. (#48)
- Per-broker JWT identity claims `jwt_owner` / `jwt_email`
  (`set mqtt.broker.<N>.jwt_owner|jwt_email`), surfaced in `mqtt status`. (#63)
- Multi-frame `mqtt status` over the BLE `_sys` channel: the per-slot broker
  table spans multiple frames instead of truncating. (#48)

### Fixed
- BLE `_sys` command channel no longer hangs when `set mqtt.broker.*` runs. The
  blocking `esp_mqtt` lifecycle ops (connect / destroy) moved off `loopTask` to a
  dedicated `mqtt_worker` task with a per-broker lock and a per-slot reconcile
  flag. (#53)
- wss/JWT broker authentication: send the MQTT CONNECT username
  `v1_<UPPERCASE pubkey>` (was a null username) so eastme.sh / LetsMesh accept
  the connection -- the broker verifies the token's `publicKey` claim against it
  and rejects a null username (CONNACK rc=5) even with an otherwise-valid token.
  (#68)

## [0.14.0] - 2026-06-07

### Added
- CI release pipeline (epic #14): dev-channel firmware artifacts on every
  `firmware-base` push / PR (`ci.yml`), and a `release.yml` workflow that builds
  the curated community board set from `crosswire-v*` tags and publishes a
  GitHub Release (pre-release for `-rc*`, "Latest" for stable). Curated env set
  in `.github/release-envs.txt` (72 envs; `heltec_v4_repeater_telemetry` gated on
  #20). Design of record: `docs/architecture/2026-06-06-ci-release-pipeline.md`.
  (#15, #16, #17)
- `pio-flash` artifact-flash mode: flash a prebuilt `.bin` through the same
  device-identity gate (ESP32 + nRF52). (#29)

### Changed
- Reconciled inherited CI workflows: removed 7 dead/superseded
  (`pr-build-check`, `auto-promote`, `github-pages`, the three upstream-tag
  `build-*-firmwares`, `branch-cleanup`); kept `build-safeboot-firmwares` +
  `sync-labels-to-board`. (#18)
- Observer firmware stripped to its minimum footprint; removed the dead RX ring
  buffer. (#42)

### Fixed
- `pio-flash`: default `firmware_dir` to the repo root with a `--firmware-dir`
  override; bootloader-port discovery + output-parsed flash verification.
  (#27, #34)
- OLED splash version carries the pre-release identifier (e.g. `-rc1`). (#33)
- Guard `_serial` in `onContactOverwrite` (was NULL during boot-load). (#42)

### Internal
- MeshCore 1.16.0 base-update impact assessment, position-to-map pipeline
  architecture draft, project-identity pinning, and the `/work` + `session-state`
  compaction-recovery tooling port. (#54, #32, #55, #59, #61)

## [0.13.2] - 2026-06-05

### Added
- Versioning + release discipline: this `CHANGELOG.md`, the cadence and
  release-channel sections in `VERSIONING.md`, and a Releases & versioning
  pointer in `README.md`. (#11)

## [0.13.1] - 2026-06-05

### Added
- Green status-LED heartbeat on the RAK4631 BLE companion
  (`-D PIN_STATUS_LED=LED_BUILTIN`, green LED1 / P1.03). (#7)
- Repeater heartbeat status LED in `simple_repeater`, enabled on the RAK3401
  (WisMesh 1W) repeater env. (#9)

### Fixed
- nRF52 companion builds: guard `ESP.getFreeHeap()` in the HomeScreen so
  non-ESP32 targets compile. (#8)
- Added `scripts/firmware_identity.py` to the repo -- closes the P5.2 pio-flash
  wrapper gap (the wrapper imported a module that wasn't present). (#6)

## [0.13.0] - 2026-06-05

Baseline of the unified `firmware-base` line -- the first version tag after the
firmware migrated out of the `Strycher/MeshCore` fork into this repository. It
carries the full migrated feature set (observer Plans 1-2, the NimBLE stack, the
SafeBoot port, and repeater-telemetry).

### Added
- Repo governance folded into the firmware tree: flash / OTA / agent-mail
  PreToolUse discipline, the Projects-v2 board sync workflow + field IDs, and
  build-verification CI. (#334)

### Fixed
- OLED splash build identity: untagged builds self-identify by abbreviated SHA
  instead of collapsing to a bare tag, and the version line is clamped to the
  panel width. (#319)
- Observer: publish heard packets to `/packets` (CoreScope schema) via the
  `logRx` hook. (#335)
- Observer: pair `esp_mqtt` stop with start on broker retry to stop a heap
  leak. (#327)
- Observer: reach the observer config CLI over USB serial
  (transport-agnostic). (#325)
