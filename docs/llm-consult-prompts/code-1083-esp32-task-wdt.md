# Review: #1083 — arm the ESP32 task watchdog in every role

You are reviewing a small firmware change plus its unit tests before it is committed.
Repository: OffbandMesh/meshcore-firmware (a MeshCore fork; ESP32 via arduino-esp32 3.x /
ESP-IDF 5.x, nRF52 via Adafruit core). Review for correctness and for anything that would
make the change unsafe on a deployed, unattended node. Be specific: file, line, why, what
to change. Rank findings Critical / High / Medium / Low. Say explicitly if you find nothing
at a level.

## Context

For fifteen months the repeater and companion roles wrapped `board.startWatchdog(30)` and
`board.feedWatchdog()` in `#if defined(NRF52_PLATFORM)`. The guard's comment said ESP32
bridge builds "can legitimately block >30 s" on network calls. That is not true of the
current code: MQTT runs in its own FreeRTOS task (`mqtt_worker`), and the only loop-context
network calls are an HTTP poll/POST with a 5 s timeout. Meanwhile `simple_sensor` has armed
the same watchdog unconditionally on every platform since #519 and it is bench-verified.
The room server had no runtime watchdog on any platform.

`ESP32Board::startWatchdog()` (already in tree, #518) reconfigures the ESP-IDF task WDT
with the timeout and `trigger_panic`, subscribes the calling (loop) task, and degrades to
"not armed" on error. `ESP32Board::sleep()` feeds on entry. `NRF52Board` has its own
implementation. `mesh::MainBoard` defaults both to no-ops.

## The change

- `src/MeshCore.h`: `WDT_TIMEOUT_SECS` macro, default 30, `#ifndef`-guarded so an env can
  override with `-D WDT_TIMEOUT_SECS=n`.
- `examples/simple_repeater/main.cpp`, `examples/companion_radio/main.cpp`: `startWatchdog`
  and `feedWatchdog` moved OUT of the nRF52 guard; the nRF52 LED heartbeat
  (`startHeartbeat`/`heartbeatTick`) stays inside it.
- `examples/simple_room_server/main.cpp`: `startWatchdog` after `onBootComplete()`,
  `feedWatchdog` at the top of `loop()` — new on every platform, including nRF52 room
  servers. That loop has no power-save nap.
- `examples/simple_sensor/main.cpp`: literal `30` → the macro.
- Tests: `test/test_wdt_timeout/` (googletest, native): default is 30; a pre-include
  `#define` overrides it; a MainBoard subclass with no watchdog accepts the calls.
  `scripts/check_wdt_guard.py` + `scripts/test_wdt_guard.py`: a source-level guard that
  fails if any role's `main.cpp` places either call inside a platform `#if`; feature guards
  are not platform guards; escape comment `wdt-guard: allow-platform <reason>`.

## Questions I want answered

1. Is there any path in these four `loop()`s where a legitimate operation can exceed 30 s
   without returning to the top of the loop? Consider display init, flash/LittleFS writes,
   prefs migration, OTA, BLE pairing, and the companion's power-save nap on ESP32.
2. nRF52 room server: the nRF52 WDT keeps counting during sleep and #275 pairs it with a
   loop-wake heartbeat on the other roles. The room server loop does not nap. Is arming it
   there without the heartbeat safe, or is there a sleep path I have not seen?
3. `esp_task_wdt_add(NULL)` subscribes the loop task. On ESP32 companion builds with BLE
   (NimBLE) or WiFi, is there any other task that the Arduino core auto-subscribes whose
   starvation could now panic the board?
4. The check script: false positives / false negatives in its preprocessor tracking that
   would matter for these four files.
5. Anything in the tests that passes without proving what it claims.

__FILES__
