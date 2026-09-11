# Review: #1171 — `WDT_TEST_HANG_AFTER_MS` build-flag self-hang

Fourth task in the #1083 chain (epic #446, OffbandMesh/meshcore-firmware). You reviewed
#1083, #1159 and #1160 today. This adds a bench-only trigger that works on every role,
because `wdt hang` (#1159) lives in `CommonCLI::handleCommand` and the companion role never
calls it — a companion's console is the framed app protocol. Review for correctness and for
any way this could reach a release image or fire when it should not. Rank Critical / High /
Medium / Low; say when a level is empty.

## The change (diff attached)

- `src/MeshCore.h`: the `WDT_TIMEOUT_SECS` macro block moved from inside `class MainBoard`
  to namespace scope just above it (unchanged semantics; it had to precede the new
  functions). New, in `namespace mesh`:
  - `inline bool wdtTestHangDue(uint32_t now_ms)` — with `WDT_TEST_HANG_AFTER_MS` defined,
    `now_ms >= WDT_TEST_HANG_AFTER_MS`; otherwise a constant `false`.
  - `inline void wdtTestHangTick()` — under `#if defined(WDT_TEST_HANG_AFTER_MS) && ARDUINO`:
    if due, print one line to `Serial`, flush, `for (;;) { }`. Otherwise an empty function.
- Each of the four role `loop()`s calls `mesh::wdtTestHangTick();` on the line directly
  after `board.feedWatchdog();`.
- Tests: native `test/test_wdt_self_hang/` (flag defined before the include: not due at
  n-1, due at n) and `test/test_wdt_self_hang_noflag/` (own suite binary — the two flag
  states cannot share a binary because the inline body differs); source-level
  `scripts/test_wdt_self_hang.py` (both helpers exist; hang-capable code only under the
  flag; every role calls the tick exactly once, immediately after the feed; the tick really
  spins; documented copy printed and flushed first); `ci.yml` blocking step;
  `test_ci_wdt_steps.py` list updated.

## Questions

1. `millis()` wraps at ~49.7 days. With the flag set to 90000, is there any path where the
   predicate is true at boot, or never true, on a real board? (Bench use only, but say if
   the comparison should be wrap-safe anyway.)
2. The tick is placed after the feed so the hang starts from a fed state and the trip lands
   at the full timeout. Is there a reason to prefer before the feed, or elsewhere in loop()?
3. On nRF52 (which also has `ARDUINO` defined), the same tick would spin and the #257
   hardware WDT would fire. Any nRF52-specific hazard — e.g. the #275 loop-wake heartbeat
   interacting with a spinning loop — that makes this unsafe to compile there even for a
   bench build?
4. The `inline` + `#if` pattern: any ODR or link hazard beyond the test-binary case already
   handled?
5. Anything in the tests that passes without proving what it claims.

__FILES__
