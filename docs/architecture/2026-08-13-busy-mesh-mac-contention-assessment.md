# Busy-Mesh MAC Contention — Upstream Assessment & Offband Impact

**Date:** 2026-08-13
**Status:** Assessment / watch position — no firmware change proposed here.
**Tracking:** [#672](https://github.com/OffbandMesh/meshcore-firmware/issues/672)
**Upstream sources:**
- Issue [meshcore-dev/MeshCore#2820](https://github.com/meshcore-dev/MeshCore/issues/2820) — "Busy mesh collisions" (yg-ht, open)
- Discussion [meshcore-dev/MeshCore#2053](https://github.com/meshcore-dev/MeshCore/discussions/2053) — "Algorithm to Automatically Adjust Repeater Parameters…" (KPrivitt, Ideas)
- Referenced upstream PR [#2842](https://github.com/meshcore-dev/MeshCore/pull/2842) (RSSI fix + MAC/CAD instrumentation), PR [#2125](https://github.com/meshcore-dev/MeshCore/pull/2125) (autotune delays)

---

## TL;DR

Two upstream threads describe the **same underlying reliability problem**: in dense networks with a dominant repeater, MeshCore's transmit gating is not a real mesh-wide contention mechanism, so flood retransmits and fixed-delay ACKs collide — losing DMs, ACKs, and path-returns. **Offband inherits all of this verbatim** (MeshCore 1.16.0 base; the relevant code is unchanged in our tree). Because Offband does not track upstream (#197), any fix is watch-and-selectively-cherry-pick, not automatic.

**Recommendation:** watch, don't yet change. The highest-value near-term item is upstream's **instrumentation** (`stats-mac-cad` / `stats-mac-tx`, PR #2842) — it would let us *measure* the problem on our own repeaters before touching any timing. The density-autotune idea (#2053) is interesting but unproven and premature.

---

## The two threads

### #2820 — "Busy mesh collisions" (credible field report)

A methodical, non-accusatory report + six design questions — explicitly *not* a bug claim. Symptoms: sent packets often produce no visible heard-repeat or ACK; confirmation feels unreliable around dominant repeaters; a prominent repeater regularly reports `Debug 2`.

`Debug 2` = `ERR_EVENT_CAD_TIMEOUT`: the node considered the channel busy for ~4 s, then transmitted anyway.

Key points raised:
- Timing defaults may be too low for dense meshes (`txdelay`, `direct.txdelay`, `rxdelay`, fixed 200 ms ACK).
- RSSI-based channel detection is disabled by default; companion firmware hard-disables it pending a `currentRSSI()` fix.
- Normal TX is only locally gated by `isReceiving()` — **not** a cross-mesh contention mechanism. Multiple devices independently schedule into overlapping windows, especially after a flood packet is heard by several repeaters.

**Independently confirmed** in-thread (HenrikBo): near a dominant repeater, path/ACK are received by *his* repeater but never passed on; removing local repeaters and raising his antenna 1.2 m took him from **~50 to ~500 messages/day**. Called "the single most important fix" for network reliability.

Author shipped **PR #2842**: fixes RSSI measurement and adds MAC/CAD counters (`stats-mac-cad`: CAD deferrals/timeouts/drops; `stats-mac-tx`: normal TX / retransmit / queue counters) — instrumentation, not a behavioural change.

### #2053 — density-based auto-tune (earnest, inconclusive)

Proposal to auto-adjust repeater timing (`rxdelay`, `txdelay`, `direct.txdelay`, CR) from a **local density metric**, so the mesh self-optimizes and operators who leave defaults get good values automatically. Backed by seven white papers and a community LoRa simulator (fork of matthewdgreen/meshcore_sim; PR #2125).

Status: **inconclusive.** Simulator delivery rates are low and noisy (~18–26%), the author repeatedly doubts his own topology realism, and maintainers are lukewarm ("show test results"). A useful technical nit surfaced (KPrivitt): the `txdelay` slope should be 0.2, not 0.3, because `txdelay` is multiplied by 5 and truncated, so 0.3 skips backoff steps. The thread ends waiting for a core maintainer to weigh in on whether to optimize for *minimum collisions* or *maximum delivery*, and whether ACK delivery is protected.

---

## Root cause (shared by both)

Weak cross-mesh contention control. There is no mesh-wide MAC coordination — a node only checks *its own* radio via `isReceiving()` before transmitting. In a dense area, a single flood packet heard by N repeaters produces N scheduled retransmits into overlapping windows, and fixed-delay ACKs (200 ms) can fire while repeaters are still echoing the original flood. The CAD-timeout "transmit anyway after 4 s" recovery path then adds to the contention rather than backing off.

---

## Offband touchpoints (verified in `firmware-base`, this tree)

All of the code the upstream issue cites exists **unchanged** in our fork:

| Concern | File:line | Current state |
|---|---|---|
| TX gate / CAD loop | `src/Dispatcher.cpp:279` `checkSend()`, `:293` `isReceiving()` gate | local-only gate; no mesh coordination |
| CAD busy → timeout | `src/Dispatcher.cpp:294-309`; max duration `:62-63` `getCADFailMaxDuration()` = **4000 ms** | after 4 s busy → set flag + TX anyway (`:298-299`, `:305` retry delay) |
| `Debug 2` bit | `src/Dispatcher.h:109` `ERR_EVENT_CAD_TIMEOUT (1<<1)` | this is the `Debug 2` operators see |
| Channel-active / noise floor | `src/helpers/radiolib/RadioLibWrappers.cpp:181` `isChannelActive()`; `:90-98` noise-floor sampling (clamp −120 dBi) | noise floor sampled; interference threshold not applied by companion |
| Interference threshold (companion) | `examples/companion_radio/MyMesh.cpp:361-362` `getInterferenceThreshold()` | `return 0; // disabled for now, until currentRSSI() problem is resolved` |
| Repeater timing defaults | `examples/simple_repeater/MyMesh.cpp:900-917` | `rx_delay_base=0.0` (off, *was 10.0*), `tx_delay_factor=0.5` (*was 0.25*), `direct_tx_delay_factor=0.3` (*was 0.2*), `interference_threshold=0` (disabled) |
| RX/TX delay calc | `examples/simple_repeater/MyMesh.cpp:560-570` | `calcRxDelay` = `(pow(rx_delay_base, 0.85−score) − 1)·airtime`; tx/direct-tx = `airtime · factor` |
| Fixed ACK / response delay | `src/helpers/BaseChatMesh.cpp:6` `SERVER_RESPONSE_DELAY=300`, `:10` `TXT_ACK_DELAY=200`; ACKs sent `:45/:251/:278` | fixed, not airtime/route-aware |
| Flood/direct retransmit schedule | `src/Mesh.cpp:17` `getRetransmitDelay`, `:22` `getDirectRetransmitDelay`, `:62-63` `ACTION_RETRANSMIT_DELAYED(5, d)` | eligible repeaters schedule retransmit at priority 5 |

Note: our defaults already carry upstream's *first round* of tuning (the `was 0.25`/`was 0.2`/`was 10.0` comments). The #2820 argument is that even these are too low for dense meshes — i.e. this is not a place where Offband has diverged; we are exactly where upstream is.

---

## Assessment

- **#2820 is high-quality and worth taking seriously.** Evidence-based, cross-references 8 prior issues, second-operator confirmation with a concrete before/after (50→500 msgs/day), and the author is contributing instrumentation rather than a speculative fix. This is a real failure mode, not a one-off.
- **#2053 is good thinking but not yet actionable.** The simulator results are too noisy to trust, the maintainers aren't convinced, and auto-tuning timing before the basic contention/instrumentation story is settled risks optimizing a model that doesn't match reality. Hold.

## Relevance to Offband

- Our **repeater / fixed-node work** (duty-cycle epic, off-grid nodes) will hit this exact failure mode if deployed into a dense area. It is a fleet-reliability issue, not a corner case.
- **No-upstream-merge policy (#197):** we will not receive any upstream fix automatically. This is a deliberate watch-and-cherry-pick position.
- The instrumentation angle aligns with how we already diagnose hardware (serial-frame observation, as used for the block feature): measure first, tune second.

## Recommendation & next steps

1. **Watch upstream PR #2842.** If/when it merges, evaluate cherry-picking the **instrumentation only** (`stats-mac-cad` / `stats-mac-tx` + the RSSI-measurement fix) — low-risk, high-diagnostic-value, no behavioural change.
2. **Do not adopt density-autotune (#2053) yet.** Revisit only if upstream reaches consensus and shows field (not just simulated) results.
3. **If we later see the symptom on our own fleet**, use the instrumentation to characterize it before changing any timing default — and file a separate implementation issue at that point.
4. Re-evaluate the disabled `interference_threshold` / `currentRSSI()` path only in concert with upstream's resolution of the same question (`#2051`).

**No firmware change is recommended at this time.** This doc records the position so a future session doesn't re-derive it.
