# Plan: true nap-independent green-LED heartbeat on nRF52 (#275, P0)

- **Issue:** OffbandMesh/meshcore-firmware#275 (P0) · Citadel `Crosswire-am2`
- **Feature/Epic:** #253 (nRF52 field reliability & crash observability); ties to #255 (watchdog)
- **Origin of the defect:** #7 (RAK4631 companion heartbeat), #9 (repeater heartbeat)
- **Suppressing mechanism:** #208 (nRF52 power-save nap)
- **Authoritative technical grounding:** `docs/llm-consultations/2026-07-03-275-nrf52-wake-heartbeat-gemini-gemini-2.5-pro.log` (Gemini 2.5 Pro design consult)
- **Author:** BrightBeaver, 2026-07-03

## Requirement (from the maintainer, non-negotiable)
The green (user) LED heartbeat blink **must be an absolute, true indicator that the heartbeat / main loop is running.** It **must NOT** be gated behind any other pre-condition — not `UITask`/any UI, not display state/presence, not connection, not the power-save nap, not traffic. It **MAY also** indicate traffic, **but it can never FAIL to indicate the heartbeat.** Applies to every headless nRF52 role (companion + repeater), where the LED is the only liveness signal.

## Root cause (verified)
- Companion heartbeat is driven by `UITask::userLedHandler()` (per #7), which only runs on a full main-loop pass, and is compiled under `DISPLAY_CLASS`/UI plumbing.
- The nRF52 loop naps via `NRF52Board::sleep()` → `sd_app_evt_wait()` (WFE) when `!the_mesh.hasPendingWork()` (`examples/companion_radio/main.cpp:516-518`). Under FreeRTOS tickless-idle it wakes only on SoftDevice/interrupt events (radio DIO1, BLE advert, some USB) — not on a fixed cadence.
- Net: on a quiet/idle node (USB-serial companion with no client; repeater in RF silence) the loop sleeps for seconds, so the heartbeat only blips on traffic and goes dark when idle → a **running device looks dead.** The repeater's in-loop blip (#9) has the same nap suppression ("blips on each wake").

## Design decision + rationale (Gemini-grounded; see consult log)
Guarantee the loop runs at a fixed ~1–2 Hz so it can toggle the LED (and feed the WDT) regardless of traffic — **the toggle stays loop-driven so a genuine hang freezes the LED and trips the watchdog.**

**Mechanism: a bluefruit `SoftwareTimer` (FreeRTOS `xTimer`), ~500 ms, repeating.**
- A running periodic `SoftwareTimer` causes FreeRTOS tickless-idle to program **RTC1** to fire at the timer's expiry; that RTC interrupt **wakes the CPU out of `sd_app_evt_wait()`** → the loop runs a pass. (Gemini consult, Q1.)
- **Adaptation to our loop:** our loop naps in `sd_app_evt_wait()` (wakes on any interrupt), NOT a `ulTaskNotifyTake` blocking-wait. So we do **not** rewrite `loop()` or add task-notifications (Gemini's sample assumed a notify-wait loop). We simply run the timer; its RTC wake makes the existing loop cycle. Callback is **empty** (the wake is the point) — it must NOT toggle the LED or feed the WDT, or it would mask a hang.
- **Rejected: raw `NRF_RTC`/`NRF_TIMER`.** SoftDevice reserves `TIMER0`; FreeRTOS owns `RTC1`. Manual peripheral use risks hard-fault/hang. (Gemini consult, Q2 — "strongly disadvised.")

## Plan
1. **Loop-owned heartbeat.** Move the green-LED toggle into the main loop, **ungated** by `UITask`/`DISPLAY_CLASS` — a fixed ~1 Hz `millis()` toggle, the single heartbeat driver. Companion (`examples/companion_radio/main.cpp`) + repeater (`examples/simple_repeater/main.cpp`, replacing its nap-suppressed in-loop blip with the same reliable path). `#if defined(NRF52_PLATFORM)`-guarded (ESP32 unaffected).
2. **Neutralize `UITask::userLedHandler()`** LED driving so it doesn't contend for `PIN_STATUS_LED` (UI keeps its screen logic; the heartbeat leaves the UI layer). An activity/traffic blip may layer on top later, but must never be the *only* driver.
3. **Add the ~500 ms repeating `SoftwareTimer`** (nRF52-only), empty callback, started after boot init — guarantees the loop wakes ≥2 Hz even when fully idle.
4. Keep the existing `board.sleep()` nap for power-save between wakes.

## Falsifiable acceptance test (this is how it's verified — independent of understanding the internals)
- **Idle liveness:** a fully idle nRF52 device (no traffic, no client connected) blinks the green LED at the fixed ~1 Hz cadence, indefinitely.
- **Hang detection:** an induced loop hang (temp `while(1){}`) → the green LED **freezes** AND the device auto-reboots via the watchdog, next boot banner `[boot] last reset: Watchdog`.
- **Both roles, headless:** verified on a headless nRF52 companion AND repeater (no display attached).

## Verification note
The maintainer cannot independently verify the SoftDevice/FreeRTOS wake internals. Verification therefore rests on two auditable things, both defined here: (a) the cited Gemini consult log (the technical claim, re-checkable by any expert or a fresh Gemini pass), and (b) the falsifiable behavioral test above (checkable on hardware regardless of the internals).

## Gates ahead
Edits (Tier-1, task claimed) → build nRF52 companion + repeater envs + one ESP32 guard env (Tier-2) → Gemini review of the diff (standards#145) → the hardware acceptance test above → commit. Part of the Epic #253/#255 reliability work / branch.
