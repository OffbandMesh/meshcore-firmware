# Observer time-source arbitration + GPS (time & position) — design

**Status:** draft for review (rev 3) · **Date:** 2026-06-11 · **Owner:** Strycher
**Tracks:** #69 (NTP/SNTP), #31 (position-to-map) · **Prototype HW:** HV4 / ST-P (GPS attached)

> **rev 3** (owner direction): MQTT position **follows `advert_loc_policy`**; GPS stays **gated behind
> `isEnabled`** (keep upstream-compatible); GPS must **auto-enable at boot** for unattended use; ship a
> **user-facing config doc**. Rewriting the MeshCore app's GPS/location UX is explicitly out of scope.

---

## 1. What's actually there (verified, with cites)

The observer is GPS-capable and the layer is compiled — but it's gated, and position lives in two places:

- **Compiled + instantiated:** `[Heltec_lora32_v3]` defines `PIN_GPS_RX/TX/EN`, adds `+<helpers/sensors>`,
  and `sensor_base` sets `ENV_INCLUDE_GPS=1`; companion + observer inherit it. `target.cpp:20`
  instantiates `MicroNMEALocationProvider(Serial1, &rtc_clock)`.
- **`gps_active` is the master gate.** `EnvironmentSensorManager::loop()` only runs `_location->loop()`
  `if (gps_active)` ([:761](../../src/helpers/sensors/EnvironmentSensorManager.cpp)), and that loop sets
  **both** the RTC time ([MicroNMEALocationProvider.h:152-158](../../src/helpers/sensors/MicroNMEALocationProvider.h))
  and parses position. `initBasicGPS` detects the module then forces `gps_active=false` at boot
  ([:636-637](../../src/helpers/sensors/EnvironmentSensorManager.cpp)); the app's `gps` setting toggles
  it via `start_gps()`/`stop_gps()` ([:575-583](../../src/helpers/sensors/EnvironmentSensorManager.cpp)).
- **Two positions.** GPS updates **`_sensors->node_lat/lon`** (live). The app shows/edits
  **`_prefs->node_lat/lon`** (manual). They only sync via `gps setloc`
  ([CommonCLI.cpp:471-473](../../src/helpers/CommonCLI.cpp)).
- **Adverts already choose via `advert_loc_policy`** (`buildAdvertData`,
  [CommonCLI.cpp:281-292](../../src/helpers/CommonCLI.cpp)): `none`→omit, `share`→`_sensors` (live),
  `prefs`→`_prefs` (manual).
- **MQTT carries no position at all** — zero `node_lat`/`_sensors`/location references in `wifi_observer`.

## 2. Gaps

1. **MQTT publishes no position** (`MqttPayload` has no lat/lon). → #31.
2. **The in-flight SNTP slice would clobber GPS-set time** (both write the same RTC, no arbitration).
3. **GPS is OFF by default at boot** (`initBasicGPS` forces `gps_active=false`), and the saved
   `gps_enabled` pref does not appear to be re-applied at startup — so GPS is lost on every power cycle.
   Fatal for an unattended observer. *(Must verify + fix — see §5.3.)*
4. **No user-facing documentation.** The upstream app exposes enable / `advert_loc_policy` / `gps setloc`
   poorly, so operators can't tell what to configure — to the point a GPS "might as well not be attached."

## 3. Operating context

Observer detects mesh messages → queues → publishes over MQTT (WiFi). It **essentially always has WiFi**
(else the queue/publish premise fails), so we practically always have **either GPS or NTP, usually both**.
GPS adds rich position + better time.

## 4. Decisions

| # | Decision |
|---|---|
| D1 | Time priority **GPS > NTP > BLE**. GPS authoritative **only while enabled (`gps_active`) and locked (`isValid`)**. |
| D2 | **Never block on GPS** (cold fix can exceed 30s). NTP gives the fast initial clock; GPS corrects on fix ("catch-up"). |
| D3 | **SNTP defers to GPS** via a source arbiter — NTP only sets the clock when GPS is disabled or has no fix. (Corrective fix to the in-flight slice.) |
| D4 | **MQTT position follows `advert_loc_policy`** — `share`→live `_sensors->node_lat/lon`, `prefs`→`_prefs->node_lat/lon`, `none`→omit. **Reuse the existing policy; do NOT invent a separate MQTT knob.** |
| D5 | **Keep GPS gated behind `isEnabled`/`gps_active`** (upstream-compatible — eases consuming MeshCore base-updates). When enabled, GPS time outranks NTP/BLE. **GPS must auto-enable at boot from the saved `gps_enabled` pref** so an unattended observer keeps GPS across reboots. |
| D6 | Prototype on **HV4 / ST-P** (GPS attached). HV3 / XIAO have none. |
| D7 | **Ship a user-facing GPS/location config doc** — enable GPS, `advert_loc_policy` (`none`/`share`/`prefs`), `gps setloc`, the `_sensors` (live) vs `_prefs` (manual) split, and the recommended observer setup. |

> **Out of scope (future):** rewriting the MeshCore app's GPS/location UX. Noted; not this work.

## 5. Design

### 5.1 Time-source arbiter
- GPS owns the clock whenever `gps_active && isValid()` (it already writes the RTC). The arbiter keeps
  **NTP from overwriting it**: NTP/`configTime` is applied only when GPS is disabled or has no fix
  (optionally after a GPS-lost window). The `wallClockSane()` TLS gate stays as the source-agnostic net.
- BLE `CMD_SET_DEVICE_TIME` lowest priority; accepted only before GPS/NTP have set the clock.

### 5.2 Position into MQTT — follow `advert_loc_policy`
- Add lat/lon (alt optional) to the observer payload, **selected exactly as `buildAdvertData` does**:
  `share`→`_sensors`, `prefs`→`_prefs`, `none`→omit. One policy drives both advert and MQTT.

### 5.3 GPS enable persistence
- Re-apply `_prefs->gps_enabled` at boot (call `start_gps()` when set) so GPS survives power cycles.
  Verify the current behavior first; fix if absent. This is what makes the gated model viable unattended.

### 5.4 Impact on the in-flight #69 SNTP slice
- **HOLD + rework — do not flash/PR.** As-is it clobbers GPS time on a GPS-enabled board. It becomes the
  GPS-disabled / no-fix arm under D3.

## 6. Open / verify on hardware (ST-P)
- **Boot-restore of `gps_enabled`** — verify, and fix per D5/§5.3 (now a requirement, not a nicety).
- Confirm GPS detect/fix on ST-P at runtime (`gps_detected`/`gps_active`/`isValid`).
- ST-P GPS specifics (UART = `Serial1`, pins, power).
- Scope: expand #69 vs. split a new "observer GPS time+position" issue under #31.

## 7. Acceptance
- ST-P, GPS enabled: clock from GPS when locked, NTP fallback otherwise; MQTT **and** advert position both
  follow `advert_loc_policy`; GPS auto-enables after a reboot with no phone.
- No-GPS observers (HV3/XIAO): NTP-only time, unchanged.
- User-facing GPS/location config doc published.
