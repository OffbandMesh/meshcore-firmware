# Observer Time-Source + Position Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the WiFi observer an authoritative clock (GPS > NTP > BLE) and let it publish its own position over MQTT, both governed by existing settings, with no NVS wipe and no upstream-incompatible changes.

**Architecture:** main.cpp is the only translation unit that holds `sensors` (EnvironmentSensorManager), `_prefs`, and `advert_loc_policy`. The observer (`src/helpers/wifi_observer/*`) stays decoupled and receives state *pushed in* from main.cpp via small setters — it never reaches into the sensor manager. Time arbitration runs inside `WifiObserver.cpp` (gate `configTime` on GPS state). Position rides the `/status` publish, which must first be **wired** — the architecture review verified `wifiObserverSetStatusSnapshot` is never called today (`docs/architecture/2026-06-01-observer-architecture-review.md:30`).

**Tech Stack:** C++ (Arduino-ESP32 / PlatformIO), MeshCore companion base, ESP-IDF SNTP (`configTime`), MicroNMEA GPS. Host-buildable payload logic under `#ifdef ARDUINO` guards.

**Design of record:** `docs/design/2026-06-11-observer-time-source-and-gps.md` (rev 3 + boot-restore verified). Decisions D1–D7 are binding.

**Test strategy (firmware-adapted):** There is no fast unit-test loop for the GPS/SNTP paths (they need hardware). "Verify" steps are therefore (a) **host/target compile** via `pio run -e <env>` (Tier-2 — needs per-action approval) and (b) **runtime serial verification on ST-P** (collected into Task F). Where a pure-logic host test is cheap (payload JSON), add one.

**SAFELANE:** `pio run` (build), `git push`, flash, and merge are **all Tier-2** — explicit per-action approval for each, every time. "commit"/"land"/"finalize" do **not** authorize merge; only the word "merge" does. Frequent local commits are fine.

---

## File Structure

| File | Responsibility | Tasks |
|---|---|---|
| `src/helpers/sensors/EnvironmentSensorManager.h/.cpp` | Add public getters: `bool gpsIsActive()`, `bool gpsHasFix()` (wrap `gps_active` + `_location->isValid()`). | A |
| `src/helpers/wifi_observer/WifiObserver.h/.cpp` | New `wifiObserverSetGpsTimeState(bool enabled, bool locked)` cache; arbiter gate around `configTime`. | A, B |
| `src/helpers/wifi_observer/MqttBroker.cpp` | `wallClockSane()` TLS gate — keep source-agnostic; ensure a GPS-set clock satisfies it. | B |
| `examples/companion_radio/main.cpp` | Push GPS time-state each loop; populate + call `wifiObserverSetStatusSnapshot` each publish window (currently absent). | A, C |
| `src/helpers/wifi_observer/MqttPayload.h/.cpp` | Add `node_lat`/`node_lon` (+optional `node_alt`, `loc_valid`) to `MqttStatusSnapshot`; emit in `buildStatusJson`. | D |
| `src/helpers/wifi_observer/MqttBrokerPool.cpp` | Confirm `/status` is actually published from `last_status_snap_`; wire if not. | C |
| `docs/observer-gps-location-config.md` (new) | User-facing config doc. | E |

---

## Task A — Expose GPS time-state to the observer  ·  Citadel `Crosswire-o9t` (#69)

**Files:**
- Modify: `src/helpers/sensors/EnvironmentSensorManager.h` / `.cpp`
- Modify: `src/helpers/wifi_observer/WifiObserver.h` / `.cpp`
- Modify: `examples/companion_radio/main.cpp`

- [ ] **Step 1 — Confirm signatures.** Read `EnvironmentSensorManager` around `gps_active` and the `_location` member, and `MicroNMEALocationProvider::isValid()`. Confirm `_location` is non-null on the non-RAK path before calling `isValid()`.

- [ ] **Step 2 — Add sensor getters.** In `EnvironmentSensorManager.h` (public):
```cpp
bool gpsIsActive() const { return gps_active; }
bool gpsHasFix()   const { return gps_active && _location != nullptr && _location->isValid(); }
```
(If `_location` type/visibility forbids inline, add out-of-line in `.cpp`.)

- [ ] **Step 3 — Add observer setter.** In `WifiObserver.h` declare, in `.cpp` define + cache:
```cpp
// file-scope statics
static bool s_gps_time_enabled = false;
static bool s_gps_time_locked  = false;
void wifiObserverSetGpsTimeState(bool enabled, bool locked) {
    s_gps_time_enabled = enabled;
    s_gps_time_locked  = locked;
}
```

- [ ] **Step 4 — Push state from main.cpp.** In `examples/companion_radio/main.cpp` loop (guarded `#if ENV_INCLUDE_GPS == 1`), throttled to ~1 Hz:
```cpp
wifiObserverSetGpsTimeState(the_mesh._prefs.gps_enabled, sensors.gpsHasFix());
```
Confirm the exact prefs accessor name in main.cpp's scope first.

- [ ] **Step 5 — Compile (Tier-2, request approval).** `pio run -e Heltec_v3_companion_observer_wifi`. Expected: builds clean.

- [ ] **Step 6 — Commit.** `git add` the four files; `git commit -m "feat(#69): expose GPS time-state to wifi_observer (Crosswire-o9t)"`.

---

## Task B — Time-source arbiter: SNTP defers to GPS  ·  Citadel `Crosswire-i3h` (#69, ⟸ A)

**Files:**
- Modify: `src/helpers/wifi_observer/WifiObserver.cpp` (the parked SNTP slice, lines ~105–153)
- Modify: `src/helpers/wifi_observer/MqttBroker.cpp` (`wallClockSane` gate)

- [ ] **Step 1 — Gate `configTime` on GPS.** Rework Phase-1 SNTP start: only call `configTime(...)` when GPS will **not** provide time (`!s_gps_time_enabled`), OR when GPS is enabled but hasn't locked within a grace window (`s_gps_time_enabled && !s_gps_time_locked && elapsed > kGpsGraceMs`). Never call it while `s_gps_time_locked` (GPS owns the clock; it sets the RTC directly via MicroNMEA).
```cpp
constexpr uint32_t kGpsGraceMs = 60000; // GPS cold-fix budget before NTP fallback
bool gps_will_serve = s_gps_time_enabled &&
                      (s_gps_time_locked || (now - s_sta_up_ms) < kGpsGraceMs);
if (!s_sntp_started && s_context_set && wifiBootstrap().isStaConnected() && !gps_will_serve) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
    s_sntp_started = true; s_sntp_started_ms = now;
}
```
Record `s_sta_up_ms` on the STA-up transition.

- [ ] **Step 2 — Keep clock-readiness source-agnostic.** Phase-2 pool bring-up already waits on `wallClockSane()` — leave it; a GPS-set clock satisfies it identically (both write system time). No NTP-specific assumption.

- [ ] **Step 3 — Reconcile the TLS gate.** In `MqttBroker.cpp`, confirm the `wallClockSane()` TLS skip is purely "is the wall clock past 2025?" with no SNTP-only coupling. Adjust only if it assumes SNTP.

- [ ] **Step 4 — Compile (Tier-2, request approval).** `pio run -e Heltec_v3_companion_observer_wifi`. Expected: clean.

- [ ] **Step 5 — Commit.** `git commit -m "feat(#69): SNTP defers to GPS via time-source arbiter (Crosswire-i3h)"`. This supersedes the parked SNTP slice; do not flash/PR the old version.

---

## Task C — Wire the dead `/status` publish vertical  ·  Citadel `Crosswire-8t5` (#31)

**Files:**
- Modify: `src/helpers/wifi_observer/MqttBrokerPool.cpp` (verify/enable `/status` publish)
- Modify: `examples/companion_radio/main.cpp` (populate + call `wifiObserverSetStatusSnapshot`)

- [ ] **Step 1 — Establish ground truth.** Read `MqttBrokerPool::loop()` / publish scheduler and find whether `last_status_snap_` is ever published to a `/status` topic. Read `WifiObserverConfig` for a status-interval. Document the finding in the task before changing anything (the arch review says dead — confirm against current code).

- [ ] **Step 2 — Populate the snapshot in main.cpp.** Where the radio/battery stats are available, build an `MqttStatusSnapshot` and call `wifiObserverSetStatusSnapshot(snap)` on the status cadence. Match existing field sources documented in `MqttPayload.cpp:135-142`.

- [ ] **Step 3 — Ensure the pool publishes it.** If Step 1 shows `/status` is not emitted, enable a periodic status publish in the pool from `last_status_snap_` using `buildStatusJson` + `formatTopic("status", ...)`.

- [ ] **Step 4 — Compile (Tier-2, request approval).** `pio run -e Heltec_v3_companion_observer_wifi`.

- [ ] **Step 5 — Commit.** `git commit -m "feat(#31): wire observer /status publish vertical (Crosswire-8t5)"`.

---

## Task D — Publish position in `/status` via `advert_loc_policy`  ·  Citadel `Crosswire-jn2` (#31, ⟸ C)

**Files:**
- Modify: `src/helpers/wifi_observer/MqttPayload.h` (`MqttStatusSnapshot` +fields)
- Modify: `src/helpers/wifi_observer/MqttPayload.cpp` (`buildStatusJson`)
- Modify: `examples/companion_radio/main.cpp` (fill the fields per policy)
- Test: restore/refresh a host golden test if one is reintroduced

- [ ] **Step 1 — Extend the snapshot.** In `MqttPayload.h` `struct MqttStatusSnapshot` add:
```cpp
double  node_lat;   // 0 when loc_valid == false
double  node_lon;
bool    loc_valid;  // false => omit position from JSON
```

- [ ] **Step 2 — Fill per policy in main.cpp**, mirroring `buildAdvertData` exactly (`CommonCLI.cpp:281-292`):
```cpp
switch (the_mesh._prefs.advert_loc_policy) {
  case ADVERT_LOC_SHARE: snap.node_lat = sensors.node_lat; snap.node_lon = sensors.node_lon;
                         snap.loc_valid = sensors.gpsHasFix(); break;
  case ADVERT_LOC_PREFS: snap.node_lat = the_mesh._prefs.node_lat; snap.node_lon = the_mesh._prefs.node_lon;
                         snap.loc_valid = (snap.node_lat != 0 || snap.node_lon != 0); break;
  default: /* ADVERT_LOC_NONE */ snap.loc_valid = false; break;
}
```
Confirm `sensors.node_lat` units (the design notes GPS stores `/1e6`; advert path uses the already-scaled `_sensors->node_lat`). Match whatever `buildAdvertData` feeds the advert so MQTT and advert agree.

- [ ] **Step 3 — Emit in buildStatusJson.** When `snapshot.loc_valid`, append `,"lat":%.6f,"lon":%.6f` to the JSON body (before the closing brace); when false, append nothing. Keep all existing fields byte-identical so non-position consumers are unaffected.

- [ ] **Step 4 — Host logic check.** If a golden-JSON harness is reintroduced, add a case for policy=none (no lat/lon) and policy=share (lat/lon present). Otherwise note that coverage moves to Task F runtime.

- [ ] **Step 5 — Compile (Tier-2, request approval).** `pio run -e Heltec_v3_companion_observer_wifi`.

- [ ] **Step 6 — Commit.** `git commit -m "feat(#31): observer publishes position in /status per advert_loc_policy (Crosswire-jn2)"`.

---

## Task E — User-facing GPS/location config doc  ·  Citadel `Crosswire-6t9` (docs)

**Files:** Create `docs/observer-gps-location-config.md`

- [ ] **Step 1 — Write the doc.** Cover: enabling GPS (`gps on`, and that it **auto-persists across reboot** via `applyGpsPrefs`); `advert_loc_policy` values `none`/`share`/`prefs` and what each does; `gps setloc` (copy live fix into manual prefs); the live `_sensors` vs manual `_prefs` position split; recommended observer setup (GPS-attached vs NTP-only boards); how position appears in both adverts and MQTT `/status`. No secrets/PSKs.

- [ ] **Step 2 — Commit.** `git commit -m "docs(#31): observer GPS/location config guide (Crosswire-6t9)"`.

---

## Task F — ST-P runtime verification  ·  Citadel `Crosswire-zjh` (⟸ B, D)

**Files:** none (hardware) — uses `scripts/pio-flash.py` `--env` path (NVS-preserving)

- [ ] **Step 1 — Build all three observer envs (Tier-2, approval each).** `pio run -e Heltec_v3_companion_observer_wifi` and the XIAO/ST-P observer envs.

- [ ] **Step 2 — Flash ST-P NVS-preserving (Tier-2, approval).** `python scripts/pio-flash.py --env <st-p observer env>` (preview→confirm token). Never `--erase`/merged/factory-reset.

- [ ] **Step 3 — Serial-verify** on ST-P: GPS detect + fix; clock sourced from GPS when locked and from NTP when GPS off/no-fix; MQTT `/status` carries lat/lon matching `advert_loc_policy`; advert position matches MQTT; GPS still enabled after a power-cycle (boot-restore).

- [ ] **Step 4 — Record results** in `Crosswire-zjh` and the design doc §6; close the epics when acceptance (§7) is met.

---

## Self-Review

- **Spec coverage:** D1–D3 → Task B; D4 → Task D; D5 (gated GPS + boot-restore) → verified, Task F runtime-checks it; D6 (ST-P) → Task F; D7 (doc) → Task E. Gap #1 (MQTT no position) → C+D; gap #2 (SNTP clobbers GPS) → B; gap #4 (no docs) → E. Covered.
- **Discovered dependency:** position (#31) is gated by wiring the dead `/status` vertical (Task C) — this is new scope beyond "add two fields," and may justify keeping #31 as its own epic rather than folding into #69. Owner call.
- **Type consistency:** `gpsHasFix()`/`gpsIsActive()` used identically in Tasks A and D; `wifiObserverSetGpsTimeState(enabled, locked)` signature consistent A↔B; `MqttStatusSnapshot.loc_valid` defined in D before use.
- **Open verifications (not placeholders — first steps of their tasks):** A.1 (sensor signatures), C.1 (`/status` liveness), D.2 (lat/lon units/scale). Each is an explicit read-first step.
