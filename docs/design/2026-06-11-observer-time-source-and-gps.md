# Observer time-source arbitration + GPS (time & position) — design

**Status:** draft for review (rev 4) · **Date:** 2026-06-11 · **Owner:** Strycher
**Tracks:** #69 (NTP/SNTP), #31 (position-to-map) · **Prototype HW:** HV4 / ST-P (GPS attached)

> **rev 3** (owner direction): MQTT position **follows `advert_loc_policy`**; GPS stays **gated behind
> `isEnabled`** (keep upstream-compatible); GPS must **auto-enable at boot** for unattended use; ship a
> **user-facing config doc**. Rewriting the MeshCore app's GPS/location UX is explicitly out of scope.

> **rev 4** (correction): §1 and D4/D7 previously described the *repeater's* `CommonCLI` 3-policy model
> (`none`/`share`/`prefs`, `_prefs->node_lat/lon`, `gps setloc`). The companion firmware is 2-policy only
> (`ADVERT_LOC_NONE` / `ADVERT_LOC_SHARE`) with a **single** `sensors.node_lat/lon` store written by GPS
> or by app command `CMD_SET_ADVERT_LATLON`; there is no separate `_prefs->node_lat/lon` and no `gps setloc`
> on the companion. MQTT position mirrors the advert (owner decision): `loc_valid = policy!=NONE && nonzero`.

---

## 1. What's actually there (verified, with cites)

The observer is GPS-capable and the layer is compiled — but it's gated, and a single position store is written by GPS or by the app:

- **Compiled + instantiated:** `[Heltec_lora32_v3]` defines `PIN_GPS_RX/TX/EN`, adds `+<helpers/sensors>`,
  and `sensor_base` sets `ENV_INCLUDE_GPS=1`; companion + observer inherit it. `target.cpp:20`
  instantiates `MicroNMEALocationProvider(Serial1, &rtc_clock)`.
- **`gps_active` is the master gate.** `EnvironmentSensorManager::loop()` only runs `_location->loop()`
  `if (gps_active)` ([:761](../../src/helpers/sensors/EnvironmentSensorManager.cpp)), and that loop sets
  **both** the RTC time ([MicroNMEALocationProvider.h:152-158](../../src/helpers/sensors/MicroNMEALocationProvider.h))
  and parses position. `initBasicGPS` detects the module then forces `gps_active=false` at boot
  ([:636-637](../../src/helpers/sensors/EnvironmentSensorManager.cpp)); the app's `gps` setting toggles
  it via `start_gps()`/`stop_gps()` ([:575-583](../../src/helpers/sensors/EnvironmentSensorManager.cpp)).
- **Single position store.** Both GPS and the app write to `sensors.node_lat` / `sensors.node_lon`
  (`double`, decimal degrees — `SensorManager` base member, `src/helpers/SensorManager.h:14`). GPS divides
  NMEA micro-degrees by 1e6 when updating it (`EnvironmentSensorManager`). A manual app command
  `CMD_SET_ADVERT_LATLON` also divides by 1e6 and writes the same store (`MyMesh.cpp:1345-1354`). There is
  no separate `_prefs->node_lat/lon` on the companion and no `gps setloc` — those belong to the
  *repeater's* `CommonCLI.cpp`.
- **Adverts choose via `advert_loc_policy`** (`MyMesh::advert` / `CMD_SEND_SELF_ADVERT` / `CMD_EXPORT_CONTACT`,
  `MyMesh.cpp:1378/1462/2441`): the companion is **2-policy only** — `ADVERT_LOC_NONE` (0) omits location;
  `ADVERT_LOC_SHARE` (1) passes `sensors.node_lat/lon` to `createSelfAdvert`. There is no
  `ADVERT_LOC_PREFS` on the companion. The policy is set by the app via a CMD frame (`MyMesh.cpp:1577`).
- **MQTT carries no position at all** — zero `node_lat`/location references in `wifi_observer` (as of this
  writing; #31 adds it).

## 2. Gaps

1. **MQTT publishes no position** (`MqttPayload` has no lat/lon). → #31.
2. **The in-flight SNTP slice would clobber GPS-set time** (both write the same RTC, no arbitration).
3. ~~GPS off after reboot~~ **RESOLVED (verified in code):** `sensors.begin()` forces GPS off, then
   `applyGpsPrefs()` re-applies the saved `gps_enabled` ([main.cpp:288-292](../../examples/companion_radio/main.cpp)),
   so GPS *does* persist across reboots. No code needed — runtime-verify on ST-P only.
4. **No user-facing documentation.** The companion knobs are poorly surfaced: `set gps:1` (+ optional
   `set gps_interval:<sec>`) to enable GPS; `advert_loc_policy` `none`/`share` to publish or suppress
   position; and setting a manual position via `CMD_SET_ADVERT_LATLON` in the app. Without a doc,
   operators can't tell what to configure — the GPS "might as well not be attached."

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
| D4 | **MQTT position follows `advert_loc_policy`** (2-policy on companion): `none`→omit; `share`→`sensors.node_lat/lon` (GPS-set or manually-set, same store), suppressing 0,0 (null-island). **No `prefs` / `ADVERT_LOC_PREFS` on the companion.** MQTT mirrors the advert exactly (owner decision: no separate live-fix gate). **Reuse the existing policy; do NOT invent a separate MQTT knob.** |
| D5 | **Keep GPS gated behind `isEnabled`/`gps_active`** (upstream-compatible — eases consuming MeshCore base-updates). When enabled, GPS time outranks NTP/BLE. GPS already auto-enables at boot from the saved `gps_enabled` pref (`applyGpsPrefs()` after `sensors.begin()`, main.cpp:288-292) — verified, no new code. |
| D6 | Prototype on **HV4 / ST-P** (GPS attached). HV3 / XIAO have none. |
| D7 | **Ship a user-facing GPS/location config doc** — enable GPS (`set gps:1`, auto-persists across reboots), `advert_loc_policy` (`none`/`share`), manual position via app (`CMD_SET_ADVERT_LATLON`), and the recommended observer setup. |

> **Out of scope (future):** rewriting the MeshCore app's GPS/location UX. Noted; not this work.

## 5. Design

### 5.1 Time-source arbiter
- GPS owns the clock whenever `gps_active && isValid()` (it already writes the RTC). The arbiter keeps
  **NTP from overwriting it**: NTP/`configTime` is applied only when GPS is disabled or has no fix
  (optionally after a GPS-lost window). The `wallClockSane()` TLS gate stays as the source-agnostic net.
- BLE `CMD_SET_DEVICE_TIME` lowest priority; accepted only before GPS/NTP have set the clock.

### 5.2 Position into MQTT — mirror `MyMesh::advert`
- Add lat/lon to the observer `/status` payload, mirroring `MyMesh::advert` exactly (owner decision).
  Logic: `loc_valid = (advert_loc_policy != ADVERT_LOC_NONE) && (sensors.node_lat != 0 || sensors.node_lon != 0)`.
  When `loc_valid`, emit `"lat"`/`"lon"` (`%.6f`, decimal degrees); otherwise omit. No separate live-GPS-fix
  gate — policy `share` publishes whichever position is in `sensors.node_lat/lon` (GPS-set or manually-set),
  suppressing only the 0,0 null-island. One policy drives both advert and MQTT.

### 5.3 GPS enable persistence — already handled (verified)
- `main.cpp` calls `applyGpsPrefs()` right after `sensors.begin()` ([:288-292](../../examples/companion_radio/main.cpp)),
  re-applying the saved `gps_enabled` so GPS auto-enables at boot. **No new code** — runtime-verify on ST-P only.

### 5.4 Impact on the in-flight #69 SNTP slice
- **HOLD + rework — do not flash/PR.** As-is it clobbers GPS time on a GPS-enabled board. It becomes the
  GPS-disabled / no-fix arm under D3.

## 6. Open / verify on hardware (ST-P)
- Boot-restore of `gps_enabled` — **verified in code** (`applyGpsPrefs()`); confirm at runtime on ST-P.
- Confirm GPS detect/fix on ST-P at runtime (`gps_detected`/`gps_active`/`isValid`).
- ST-P GPS specifics (UART = `Serial1`, pins, power).
- Scope: expand #69 vs. split a new "observer GPS time+position" issue under #31.

## 7. Acceptance
- ST-P, GPS enabled: clock from GPS when locked, NTP fallback otherwise; MQTT **and** advert position both
  follow `advert_loc_policy`; GPS auto-enables after a reboot with no phone.
- No-GPS observers (HV3/XIAO): NTP-only time, unchanged.
- User-facing GPS/location config doc published.
