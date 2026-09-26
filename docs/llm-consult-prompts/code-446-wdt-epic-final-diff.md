# Adversarial review — ESP32 task watchdog epic (#446), FINAL diff as it will be pushed

You are a hostile reviewer. Do not praise. Do not summarise what the code does. Find what is
wrong, what will misfire in the field, and what the tests fail to prove. If you cannot find a
defect in an area, say "no finding" for that area in one line and move on.

## What this is

Offband is a MeshCore (LoRa mesh) firmware fork: C++, Arduino framework, PlatformIO. Targets
are ESP32 (S3, C6) and nRF52840. Roles: repeater, companion radio, room server, sensor.

Before this change, only nRF52 builds armed a watchdog: `board.startWatchdog()` and
`board.feedWatchdog()` sat inside `#if defined(NRF52_PLATFORM)` in the repeater and companion
`main.cpp`, and the room server never called them at all. A hung ESP32 node stayed hung until
someone power-cycled it. This epic:

1. Arms and feeds the watchdog on every platform in all four roles (`WDT_TIMEOUT_SECS`,
   default 30, overridable with `-D`).
2. Adds console-only `wdt` / `wdt status` (reports `isWatchdogArmed()`), and a `wdt hang` verb
   compiled only under `WDT_TEST_HANG` that spins forever so a bench can prove the reset.
3. Adds `WDT_TEST_HANG_AFTER_MS`: a build flag that makes ANY role spin once uptime passes N ms
   (`mesh::wdtTestHangTick()`, called on the line after the feed). Companions need this because
   their console is the framed app protocol and never reaches `CommonCLI`.
4. Adds source-level guard scripts and wires them into CI as blocking steps.

## Why this review exists — read this part carefully

Every piece was reviewed once in isolation. Since then the branch was rebased across 40
commits of other people's work. Three files AUTO-MERGED with changes nobody has reviewed in
combination: `CommonCLI.cpp` (another author rewrote the `caplog` arms on both sides of the
`wdt` arms), `examples/simple_repeater/main.cpp` and `examples/companion_radio/main.cpp`
(caplog-forward, boot summary and status-line additions). `ci.yml` had a hand-resolved
conflict. You are reviewing the files AS THEY NOW STAND. Attack the combination.

## Attack these specifically

**A. False trips — the dangerous failure.** A watchdog that reboots a healthy node is worse
than none. In each role's `loop()` and everything it calls, find any path that can block the
main task for longer than 30 s without reaching `board.feedWatchdog()`: WiFi association or
reconnect, MQTT/TLS connect or teardown, HTTP calls (there is an `http.setTimeout(5000)` — are
there retries that stack?), OTA, the caplog forward / syslog flush, flash or filesystem
writes, GPS, display init, `delay()` loops, light sleep / nap / deep-sleep entry and wake.
For each candidate give the file, the function, and the worst-case duration you can justify.
Say explicitly whether `setup()` runs long work AFTER `startWatchdog()` and before the first
feed.

**B. Starvation of the feed itself.** Any early `return`, `continue`, or conditional block in
`loop()` that skips `feedWatchdog()` on some iterations. Any role where the feed is not at
the top of the loop.

**C. The CLI chain.** In `CommonCLI::handleCommand`: can the `wdt` arms be shadowed by an
earlier arm, or shadow a later one (prefix matches such as `memcmp(command, "wdt", 3)`
swallowing `wdt hang` or an unrelated future `wdtX`)? Is `wdt hang` truly unreachable from
the mesh (only when `sender_timestamp == 0`)? Is the reply buffer written on every path?

**D. Release hygiene.** Prove or disprove: with neither `WDT_TEST_HANG` nor
`WDT_TEST_HANG_AFTER_MS` defined, no hang-capable code is compiled into any role. Look for a
`for(;;)` or spin that escapes its `#if`.

**E. The self-hang predicate.** `wdtTestHangDue(now_ms)` uses a plain `>=` against
`millis()`. State the behaviour at boot, at N, and at the 49.7-day wrap, and whether any of
it matters for a bench-only flag.

**F. nRF52 parity.** The same calls are now unconditional on nRF52 too, including the room
server, which previously had no watchdog. Any nRF52 sleep / SoftDevice / DFU path that can
now false-trip?

**G. The guard scripts and CI.** `scripts/check_wdt_guard.py` claims to fail if a watchdog
call sits inside a platform `#if`. Find an input that defeats it: `#ifdef` vs `#if defined`,
`#ifndef` with `#else`, macros that wrap the platform test, a call hidden in a helper
function, nested guards, line continuations. `scripts/test_wdt_self_hang.py` claims the tick
is on the line immediately after the feed in every role — find a way that passes while the
intent is violated. In `ci.yml`: are all watchdog steps blocking (no `continue-on-error`),
and did the conflict resolution drop, duplicate or reorder anyone else's step?

## Output format

Rank findings Critical / High / Medium / Low. For each: file, function or line, the concrete
failure scenario (inputs or state -> wrong behaviour), and the smallest fix. No preamble, no
closing summary.
