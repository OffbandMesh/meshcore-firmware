# ESP32 runtime watchdog — design of record (epic #446, #1083 chain)

**Status:** code complete on `epic/1083-esp32-task-wdt` (2026-09-11); hardware proof in
#1137. **Owner:** epic #446.

## The defect

From #266 (2026-06) until #1083 (2026-09-11), `simple_repeater` and `companion_radio` wrapped
`board.startWatchdog(30)` and `board.feedWatchdog()` in `#if defined(NRF52_PLATFORM)`. The
guard's comment said ESP32 WiFi/MQTT-bridge repeaters "can legitimately block >30 s" on
network calls. `ESP32Board::startWatchdog()` (#518) was complete and `simple_sensor` had
been arming it on every platform since #519 — but no ESP32 repeater or companion ever did,
and `simple_room_server` had no runtime watchdog on any platform.

The result: a hung ESP32 node stayed hung until a host reset. The Photon-1W did exactly
that after an I²C hang (#1055) and was diagnosed for a week under the false belief that "the
30 s watchdog did not fire, so the loop must be alive."

The guard's premise was stale by the time it was checked: MQTT runs in its own FreeRTOS task
(`MqttBrokerPool.cpp`, `mqtt_worker`); the only loop-context network calls are the
telemetry HTTP poll/POST with a 5 s timeout; OTA (AsyncElegantOTA) does its flash writes in
the AsyncTCP task. Nothing in `loop()` blocks anywhere near 30 s.

## The mechanism

- **Per role, unconditional:** `board.startWatchdog(WDT_TIMEOUT_SECS)` once after
  `onBootComplete()`; `board.feedWatchdog()` at the top of `loop()`. `MainBoard` defaults
  both to no-ops; `ESP32Board` (ESP-IDF task WDT, `trigger_panic`, loop task subscribed) and
  `NRF52Board` (hardware WDT, #257) override.
- **`WDT_TIMEOUT_SECS`** (`src/MeshCore.h`), default 30, `#ifndef`-guarded so an env with a
  proven long blocking path raises it with `-D WDT_TIMEOUT_SECS=n`. Nothing is excluded by
  platform any more.
- **Sleep:** `ESP32Board::sleep()` feeds on entry (#446). On nRF52 the WDT counts during
  sleep, so roles that nap pair it with the #275 loop-wake heartbeat; the room server never
  naps and needs no pairing.
- **Reset reason:** a trip decodes as `TASK_WDT` (ESP32, `ESP_RST_TASK_WDT` = 6; the ROM
  line reads `rst:0xc (SW_CPU)` because the panic handler reboots by software reset) or
  `Watchdog` (nRF52 `RESETREAS=DOG`) on the next boot's banner and in the crash ring.
  First observed on `photon-c6`, 2026-09-11 (#1161): `wdt hang` → `task_wdt: Task watchdog
  got triggered … loopTask` at ~30 s → reboot → `reset_reason=6 (TASK_WDT)`.

## Proving it without breaking something else

- **`wdt` / `wdt status`** (every build, console): `wdt: armed, timeout 30 s, fed from
  loop()` or `wdt: NOT armed on this board` — answered by `MainBoard::isWatchdogArmed()`,
  i.e. by what the board actually did, not by a build flag. #1083's lesson was that a
  watchdog that is configured but never armed looks exactly like one that never had to fire.
- **`wdt hang`** (only with `-D WDT_TEST_HANG`, console-only via `sender_timestamp == 0`):
  prints `wdt: hanging the main loop -- expect a TASK_WDT reset in 30 s`, flushes, then
  `for (;;) { }`. It writes straight to `Serial` because `handleCommand`'s caller prints
  `reply` after return, and this never returns. Absent from every release image — verified
  by string check on the built binaries.
- **Bench build recipe** (no env defines the flag):
  `PLATFORMIO_BUILD_FLAGS="-D WDT_TEST_HANG=1" scripts/pio-flash preview <device> --env <env>`.

## Keeping it fixed

Four blocking steps in `ci.yml` `config-lint`, all gating `ci-green`:

| Step | What it pins |
|---|---|
| `scripts/check_wdt_guard.py` | no role `main.cpp` places either call inside a platform `#if` (feature guards are fine; escape `// wdt-guard: allow-platform <reason>`) |
| `scripts/test_wdt_guard.py` | the checker itself, incl. the exact #1083 regression shape |
| `scripts/test_wdt_hang_verb.py` | `wdt hang` is unique, flag-guarded, console-only, really spins; `wdt status` is unguarded and asks the board |
| `scripts/test_ci_wdt_steps.py` | the three above are present, blocking, and `ci-green` needs `config-lint` + `native-tests` |

`test/test_wdt_timeout/` (googletest) covers the macro default and override and is picked
up by `native-tests` discovery.

## What this does not cover

- **Logical liveness.** A loop that runs but whose radio has stopped receiving feeds the
  watchdog happily. That is #560 (RX-liveness), now under the patio-repeater epic #1145.
- **Bridge-build false trips under real outages.** Argued from source, proven only on the
  bench: #1162 runs `heltec_v4_repeater_telemetry` through WiFi and broker outages before
  inducing a hang.
- **Future synchronous OTA.** If #303's OTA-pull-from-GitHub is ever written as a blocking
  download + `Update.write` loop in loop context, it must run in its own task or bracket the
  section with `esp_task_wdt_delete(NULL)` / `esp_task_wdt_add(NULL)` and feed inside the
  write loop. Recorded on #303.

## Record

Commits `9648b33b` (#1083), `27ac7d24` (#1159), `4ea470f3` (#1160). Gemini 2.5 Pro reviews
in `docs/llm-consultations/2026-09-11-code-1083-*`, `-1159-*`, `-1160-*` (gitignored logs;
prompts committed under `docs/llm-consult-prompts/`).
