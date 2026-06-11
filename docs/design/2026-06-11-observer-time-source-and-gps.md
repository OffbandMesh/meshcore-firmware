# Observer time-source arbitration + GPS (time & position) — design

**Status:** draft for review · **Date:** 2026-06-11 · **Owner:** Strycher
**Tracks:** #69 (NTP/SNTP), #31 (position-to-map) · **Prototype HW:** HV4 / ST-P (GPS attached)

---

## 1. Problem

Building the observer on the **companion** base threw out the sensor/GPS layer:

- Observer envs extend `*_companion_radio_*`, whose `build_src_filter` does **not** include
  `helpers/sensors/`. The observer only adds `+<helpers/wifi_observer/*.cpp>`
  ([variants/heltec_v3/platformio.ini:438](../../variants/heltec_v3/platformio.ini)). So
  `LocationProvider` / `SensorManager` / `MicroNMEALocationProvider` are **not compiled** into
  the observer at all.
- `wifi_observer` references **none** of the GPS/location code.

Upstream MeshCore already has the pieces — they just don't reach the observer:

- **GPS → time:** `MicroNMEALocationProvider` sets the RTC from the fix:
  `_clock->setCurrentTime(getTimestamp())`
  ([MicroNMEALocationProvider.h:154](../../src/helpers/sensors/MicroNMEALocationProvider.h)).
- **GPS → position:** `EnvironmentSensorManager` sets `node_lat/lon` from `_location->getLatitude()`
  when the fix is valid ([EnvironmentSensorManager.cpp:768](../../src/helpers/sensors/EnvironmentSensorManager.cpp)).
- `LocationProvider` exposes both: `getTimestamp()`, `getLatitude/Longitude/Altitude()`,
  `isValid()`, `satellitesCount()`, `isEnabled()`, plus a `syncTime()`/`waitingTimeSync()` latch
  ([LocationProvider.h](../../src/helpers/sensors/LocationProvider.h)).

Consequences on an observer with GPS attached (ST-P):
1. GPS coordinates never update — the observer reads no live `LocationProvider`; position can only
   come from static `node_lat/lon` prefs.
2. GPS time never reaches the clock — not wired.
3. The in-flight **SNTP slice (#69) ignores GPS** and would *clobber* it: a blind, periodic
   `configTime()` overwrites whatever the clock holds, including a more-accurate GPS time. The SNTP
   work is really just the **NTP arm** of a missing multi-source arbiter.
4. Even where GPS time exists upstream, it's coupled to positioning via `isEnabled()` (the provider
   is enabled/disabled as a unit; time is only set inside the GPS loop).

## 2. Operating context (sets the priorities)

The observer's job: detect mesh messages → queue → publish over MQTT (WiFi). An observer that is
detecting messages **essentially always has WiFi** — otherwise the queue/publish premise collapses.
So:

- WiFi (→ NTP) is ~always available. A "GPS-only, no-WiFi" observer is a near-nonexistent case.
- In practice we have **either GPS or NTP, usually both**.
- GPS additionally yields **rich data** (position) and **better time** than NTP.

## 3. Decisions

| # | Decision |
|---|---|
| D1 | **Time-source priority: GPS > NTP > BLE.** GPS authoritative on any valid fix (network-independent, more accurate). |
| D2 | **Never block on GPS.** Cold fix can exceed 30s (sometimes minutes). NTP provides the *fast initial* clock (WiFi ~always present) so MQTT/TLS come up promptly; GPS **corrects/overrides** the clock when it acquires — a "heartbeat catch-up", not a gate. |
| D3 | **Bring the sensor/GPS layer back into the (HV4) observer build** and wire `LocationProvider` for both time and position. |
| D4 | **Position → observer payload** (the #31 pipeline): refresh lat/lon/alt from live GPS, not static prefs. |
| D5 | **`isEnabled` gating — prefer to decouple** time acquisition from positioning (run GPS for time even when not reporting position). **But** keep the upstream `isEnabled` gating if decoupling makes consuming upstream MeshCore changes materially harder. If we keep it gated: when GPS is enabled, GPS time is top priority over any other source. (Decide at implementation; flag the upstream-divergence cost.) |
| D6 | **Prototype on HV4 / ST-P** (GPS attached). HV3 and XIAO have no GPS. |

## 4. Design

### 4.1 Time-source arbiter
A single arbiter owns "who set the clock and how recently", so sources don't fight:

- Track `current_source ∈ {none, ble, ntp, gps}` and a freshness timestamp.
- **GPS:** on a valid fix, set the RTC and mark source=GPS. Once GPS is the source, NTP must **not**
  overwrite it (only GPS resyncs from then on, or after a long GPS-lost timeout fall back to NTP).
- **NTP:** allowed to set the clock when source is `none`/`ble`, or when GPS has not produced a fix.
  This is the fast path that unblocks TLS/MQTT (replaces the current blind `configTime` write).
- **BLE (`CMD_SET_DEVICE_TIME`):** lowest priority; accepted only before NTP/GPS have set the clock.
- The existing `wallClockSane()` gate (TLS/wss won't connect until the clock passes ~2025-01-01)
  stays as the safety net — it's source-agnostic.

### 4.2 GPS wiring on the observer (HV4)
- Add `+<helpers/sensors/...>` to the HV4 observer env's `build_src_filter` (and a `heltec_v4`
  observer env if one doesn't exist yet) + the GPS lib (`MicroNMEA`).
- Instantiate a `MicroNMEALocationProvider` on ST-P's GPS UART (pins TBD), handing it the RTC.
- Run the provider's `loop()` from the observer loop; feed `getTimestamp()` to the arbiter and
  `getLatitude/Longitude/Altitude()` to the MQTT payload.
- Per D5, ideally run the read for **time** independent of the positioning-enable flag.

### 4.3 Impact on the in-flight #69 SNTP slice
- The SNTP commits (`89e99b90`, `99d9303c`, `2042d9e5` on `feat/69-ntp-sync`) become the **NTP arm**
  of §4.1 — they must defer to GPS rather than write blindly.
- **HOLD: do not flash or PR the slice as-is.** It compiles, but shipping it standalone bakes in the
  "NTP clobbers GPS" bug.

## 5. Open questions
- D5: decouple vs. upstream-compat — measure the divergence cost against the next MeshCore base-update.
- ST-P GPS specifics: module, UART, pins, power.
- Scope: extend #69, or split a new "observer GPS time+position" issue linked to #31?

## 6. Acceptance (target)
- On ST-P with GPS: cold boot → NTP gives a clock within seconds (TLS/wss connect) → GPS acquires →
  clock switches to GPS authority; serial shows the source transitions.
- Observer MQTT payload carries live GPS lat/lon (feeds #31).
- No source fights another; GPS time is never overwritten by NTP while a fix is held.
