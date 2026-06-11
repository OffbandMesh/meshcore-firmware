# Observer GPS, Location, and Time Configuration

This doc is for operators deploying a Crosswire observer node — the WiFi+MQTT gateway that listens on
LoRa and forwards traffic to a broker. It covers what position and time the observer can report, how to
configure it, and which hardware it applies to.

---

## What an observer can report

A Crosswire observer reports position in two places simultaneously, using the same data and the same
policy:

- **LoRa advert** — broadcast to the mesh on the advertise interval.
- **MQTT `/status` payload** — published to your broker.

Both carry position when location sharing is on and a position is set; both suppress it when sharing is
off. There is no separate MQTT position knob — one setting drives both.

**Time** is reported implicitly via timestamps in `/status` and other payloads; it also gates TLS
connections to `wss://` or JWT-secured brokers. A node without a valid clock cannot open a TLS session.

---

## Board support

| Board | GPS hardware | Can get position | Time source |
|---|---|---|---|
| Heltec V3 (`Heltec_lora32_v3`) | Yes | GPS (live) or manual app entry | GPS or NTP |
| Heltec V4 (`heltec_v4`) | Yes | GPS (live) or manual app entry | GPS or NTP |
| Seeed XIAO S3 WIO (`Xiao_S3_WIO`) | **No** | Manual app entry only | NTP only |

---

## Time source

Time priority is: **GPS > NTP > BLE**.

- **GPS (when enabled and locked):** GPS sets the real-time clock from NMEA. This is authoritative —
  NTP will not overwrite a GPS-set clock. A GPS-enabled observer gets accurate time phone-free, which is
  what you want for unattended deployment.
- **NTP (SNTP over WiFi):** When GPS is disabled or has not yet acquired a fix, the observer syncs time
  over WiFi automatically. This is the fallback and the only option on XIAO. NTP is fast — it gives a
  valid clock within seconds of WiFi connect. GPS then catches up when it locks (typically 30–90 s cold,
  faster warm).
- **BLE (app):** Lowest priority. Only accepted before GPS or NTP have set the clock.

A valid clock is required for TLS connections (`wss://`, JWT brokers). If your broker rejects the
connection, check that the node has time (NTP needs WiFi; GPS needs to be enabled and have a fix).

---

## Position: how it works

Both GPS and manual app entry write to the **same single position store** (`sensors.node_lat` /
`sensors.node_lon`, decimal degrees). Whichever was set last is what gets advertised and published.

- **GPS** updates the store continuously when enabled and locked.
- **Manual** — set via the companion app (`CMD_SET_ADVERT_LATLON`); useful for fixed-location nodes,
  especially on boards with no GPS (XIAO).

The `advert_loc_policy` setting controls whether anything is shared:

| Policy | Effect |
|---|---|
| `none` | No position in advert or MQTT. Node is invisible on maps. |
| `share` | Position from the store (GPS or manual) appears in advert and MQTT `/status`. A 0,0 position (null-island) is treated as "unset" and is never published. |

There is no intermediate "share manual only" or "share live GPS only" policy on the companion firmware.
`share` publishes whatever is in the store at the time.

---

## Enabling GPS (V3 / V4 only)

GPS is off by default. Enable it with the CLI over BLE (from the companion app terminal) or over a serial
console:

```
set gps:1
```

Optional — set the GPS update interval (seconds):

```
set gps_interval:30
```

**These settings persist across reboots automatically.** The firmware re-applies `gps_enabled` at boot
(`applyGpsPrefs()`), so you do not need a phone attached for GPS to run after the initial configuration.

To disable GPS:

```
set gps:0
```

---

## Setting location sharing

Set `advert_loc_policy` via the companion app. The exact UX depends on the app version; look for a
"location" or "privacy" toggle on the node settings screen. Two states: sharing on (`share`) or off
(`none`).

For a **manual position** (fixed-location or no-GPS board): set the position in the app's node location
entry. This writes to the same store GPS would use; once set, `share` will publish it.

---

## Recommended setups

### GPS observer (V3 / V4) — unattended, position + time

1. Connect over BLE once to configure:
   ```
   set gps:1
   set gps_interval:30
   ```
2. In the app, set `advert_loc_policy` → `share`.
3. Deploy. After boot, GPS acquires a fix (30–90 s cold); until then NTP provides time. No phone needed
   after this point. Position and clock are fully autonomous.

### Fixed no-GPS observer (XIAO) — manual position, NTP time

1. In the app, enter the node's fixed location (lat/lon).
2. Set `advert_loc_policy` → `share`.
3. Deploy. NTP provides time over WiFi. Position stays fixed at whatever you entered.

### Privacy — no position published

- Set `advert_loc_policy` → `none`.
- The node is invisible on position maps. Drop counters and connection status still appear in `/status`.

---

## Notes

**In-app GPS/location UX is rough.** The MeshCore companion app does not make it obvious how to
configure GPS or set location policy. This doc is the workaround — configure it with the CLI commands
above, then verify in the app that the policy and position show as expected.

**No secrets in adverts or `/status`.** WiFi credentials, broker passwords, and OTA tokens never appear
in LoRa advertisements or MQTT payloads. Do not include them in any log or support report.
