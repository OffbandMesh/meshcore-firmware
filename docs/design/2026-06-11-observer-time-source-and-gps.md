# Observer time-source arbitration + GPS (time & position) — design

**Status:** draft for review (rev 2) · **Date:** 2026-06-11 · **Owner:** Strycher
**Tracks:** #69 (NTP/SNTP), #31 (position-to-map) · **Prototype HW:** HV4 / ST-P (GPS attached)

> **rev 2 corrects rev 1's wrong premise.** The sensor/GPS layer is **not** dropped from the
> observer — it's compiled and the GPS provider is instantiated. The real gaps are narrower.

---

## 1. What's actually there (verified)

The observer is GPS-capable **today**:

- `[Heltec_lora32_v3]` defines `PIN_GPS_RX/TX/EN` and adds `+<helpers/sensors>` to the source
  filter; `sensor_base` sets `-D ENV_INCLUDE_GPS=1`; `Heltec_v3_companion_radio_ble` and the observer
  env both inherit all of it
  ([variants/heltec_v3/platformio.ini:1-48,184,416](../../variants/heltec_v3/platformio.ini)).
- The board **instantiates** it: `MicroNMEALocationProvider(Serial1, &rtc_clock)` +
  `EnvironmentSensorManager(nmea)` under `#if ENV_INCLUDE_GPS`
  ([variants/heltec_v3/target.cpp:20](../../variants/heltec_v3/target.cpp)).
- So on a board with a GPS module (ST-P):
  - **GPS time sets the RTC directly** — `_clock->setCurrentTime(getTimestamp())`
    ([MicroNMEALocationProvider.h:154](../../src/helpers/sensors/MicroNMEALocationProvider.h)).
  - **GPS updates `node_lat/lon`**
    ([EnvironmentSensorManager.cpp:768](../../src/helpers/sensors/EnvironmentSensorManager.cpp)).

Only `wifi_observer`'s **own** code references no GPS — which is the source of the real gaps below,
not the firmware "dropping" GPS.

## 2. The actual gaps

1. **Observer payload carries no position.** `MqttPayload` has no lat/lon/alt fields, so the
   GPS-updated `node_lat/lon` is never published. *Root of "coords never update" — a payload gap, not
   a GPS-read gap.* → #31.
2. **The in-flight SNTP slice (#69) would clobber GPS time.** `configTime()` writes the same system
   clock MicroNMEA sets via `settimeofday`. With no arbitration, NTP overwrites a more-accurate GPS
   clock — on a GPS observer (ST-P) the slice would **break working GPS time**. Worse than incomplete.
3. **GPS time is coupled to positioning** via `isEnabled()` (the provider is enabled/disabled as a
   unit; time only updates while the GPS loop runs).

## 3. Operating context (sets the priorities)

The observer detects mesh messages → queues → publishes over MQTT (WiFi). An observer that is
detecting messages **essentially always has WiFi** (else the queue/publish premise collapses). So we
practically always have **either GPS or NTP, usually both**; GPS additionally gives **rich position
data** and **better time** than NTP.

## 4. Decisions

| # | Decision |
|---|---|
| D1 | Time-source priority **GPS > NTP > BLE**. GPS authoritative on any valid fix. |
| D2 | **Never block on GPS** (cold fix can exceed 30s). NTP gives the fast initial clock on cold boot / no-GPS boards; GPS corrects on fix ("heartbeat catch-up"). |
| D3 | **SNTP must defer to GPS, not overwrite it** — a source arbiter so NTP only sets the clock when GPS has no fix. This is the corrective fix to the in-flight slice. |
| D4 | **Add position (lat/lon[/alt]) to the observer MQTT payload**, fed from `node_lat/lon` → #31. |
| D5 | `isEnabled`: prefer decoupling time from positioning (GPS time even when not reporting position); keep the upstream gating if decoupling makes consuming MeshCore updates materially harder; if kept gated, GPS time is top priority. |
| D6 | Prototype on **HV4 / ST-P** (GPS attached). HV3 / XIAO have none. |

## 5. Design

### 5.1 Time-source arbiter
- GPS already owns the RTC whenever it has a fix. The arbiter's job is to keep **NTP from stomping
  it**: NTP/`configTime` is only applied when there is no GPS provider or no fix (and optionally
  after a long GPS-lost window). Simplest first cut: enable the NTP path only when the board has no
  GPS provider / no recent fix.
- BLE (`CMD_SET_DEVICE_TIME`) stays lowest priority — accepted only before GPS/NTP have set the clock.
- The existing `wallClockSane()` TLS gate (no wss/TLS connect until the clock passes ~2025-01-01)
  stays as the source-agnostic safety net.

### 5.2 Position into the observer payload
- Extend `MqttPayload` (+ the status snapshot) to carry lat/lon (alt optional), fed from the
  SensorManager's `node_lat/lon`. This is the missing leg of the #31 position-to-map pipeline.

### 5.3 Impact on the in-flight #69 SNTP slice
- **HOLD + rework — do not flash/PR.** As-is it clobbers GPS time on ST-P. It becomes the
  **no-GPS / fallback** arm under D3, never an unconditional clock write.

## 6. Open questions / verify on hardware (ST-P)
- Confirm GPS is actually **detected/active** on ST-P (`gps_detected`/`gps_active`) and setting the
  RTC — verify at runtime rather than by code-read.
- D5 decouple-vs-upstream cost (measure against the next MeshCore base-update).
- ST-P GPS specifics (module, UART = Serial1 per target.cpp, pins, power).
- Scope: expand #69, or split a new "observer GPS time+position" issue under #31?

## 7. Acceptance (target)
- ST-P with GPS: GPS holds the clock; NTP never overwrites it while a fix is held; serial shows the
  source. No-GPS observers fall back to NTP (the existing slice).
- Observer MQTT payload carries live GPS lat/lon (feeds #31).
