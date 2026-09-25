# Review: #1159 — `wdt hang` diag-only console verb + `wdt status`

Second task in the #1083 chain (epic #446, OffbandMesh/meshcore-firmware). You reviewed
#1083 earlier today (the ESP32 task watchdog armed in every role). This adds the way to
prove it on hardware. Review for correctness and for anything that could reach a release
image or a deployed node by accident. Rank Critical / High / Medium / Low; say explicitly
when a level is empty. Be specific: file, line, why, what to change.

## The change (diff attached; the four files are otherwise as in the tree)

- `src/MeshCore.h`: `virtual bool isWatchdogArmed() const { return false; }` on `MainBoard`.
- `src/helpers/ESP32Board.h`, `src/helpers/NRF52Board.h`: override returning `_wdt_started`,
  the flag each board sets only after its own arming succeeded.
- `src/helpers/CommonCLI.cpp`, in the console-only diagnostics block next to `caplog dump`:
  - `wdt` / `wdt status` (every build): reports `armed, timeout N s` or `NOT armed`, from
    the board, not from the build flag.
  - `wdt hang` (only when `WDT_TEST_HANG` is defined; console-only via
    `sender_timestamp == 0`): prints one line straight to `Serial`, flushes, then
    `for (;;) { }`. Straight to Serial because `handleCommand`'s caller prints `reply`
    only after return, and this never returns.
- `scripts/test_wdt_hang_verb.py`: source-level tests (CommonCLI.cpp is not built
  natively): the hang arm exists once, is console-only, is inside the flag, contains
  `for (;;)` with no feed/delay/yield in its code, prints the documented copy and flushes
  before spinning; the status arm exists outside the flag and calls `isWatchdogArmed()`.
- `WDT_TEST_HANG` is supplied at build time (`PLATFORMIO_BUILD_FLAGS=-D WDT_TEST_HANG=1`)
  for bench images; no env defines it in `platformio.ini`.

Verified by building: release image contains the status strings and not the hang copy;
the flagged image contains both.

## Questions

1. Is there any way `wdt hang` is reachable over the mesh or the companion protocol on a
   build where the flag is defined? (`sender_timestamp == 0` is the console-only gate the
   other diagnostics use.)
2. On USB companions the console IS the protocol stream (#1087). The line printed before
   the hang is plain text on that stream. Is that acceptable for a diag-only verb whose
   next act is a reboot, or should it be suppressed there?
3. `for (;;) { }` on ESP32: will the compiler or the RTOS do anything that lets the loop
   task feed anyway (e.g. optimise the loop, or preempt into a feed)? Is there a reason to
   prefer a different construct?
4. `isWatchdogArmed()`: any board type in the tree where `_wdt_started` would be true
   without the loop task actually subscribed?
5. Anything in the tests that passes without proving what it claims.

__FILES__
