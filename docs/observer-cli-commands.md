# Observer `_sys` CLI command reference

The Crosswire observer is configured over the **`_sys` system channel** in the
MeshCore companion app (BLE) — type these as channel messages. The observer
build has **no USB-serial text console**; the `_sys` channel is the management
surface.

## Grammar (Strycher/Crosswire#45)

Two shapes, consistently:

- **status / actions** — namespace-subcommand `<ns> <verb>`:
  `wifi status`, `mqtt status`, `mqtt enable 0`, `wifi disable`
- **single fields** — verb-first dotted `get|set <ns>.<field>`:
  `set wifi.ssid Home`, `get mqtt.broker.0.url`

The verb + field are case-insensitive (phone auto-capitalize is tolerated, so
`Wifi status` works). Secrets (`wifi.pwd`, `mqtt.broker.N.password`) are
**write-only** — never echoed back.

Commands reach the dispatcher through a `_sys` allowlist that permits the
`get`, `set`, `mqtt`, and `wifi` verbs (and rejects shell metacharacters and
device-state verbs like `reboot`/`format`/`erase`/`factory`/`ota.`).

## WiFi

| Command | Effect |
|---|---|
| `wifi status` | STA state + IP when connected |
| `set wifi.ssid <ssid>` | set STA SSID |
| `set wifi.pwd <psk>` | set STA password (reboot / STA-retry to apply) |
| `get wifi.ssid` | read configured SSID |
| `wifi enable` / `wifi disable` | STA on/off policy flag — **reboot to apply** |

> `get wifi.status` still works as a backward-compat alias for `wifi status`.
> `wifi disable` is deliberately reboot-to-apply: it does **not** drop a live
> STA, because the `_sys` channel and the MQTT uplink ride that link.

## MQTT broker pool

| Command | Effect |
|---|---|
| `mqtt status` | pool summary + per-slot state |
| `mqtt enable <N>` / `mqtt disable <N>` | enable/disable broker slot N |
| `set mqtt.broker.<N>.<key> <value>` | set a broker field |
| `get mqtt.broker.<N>.<key>` | read a broker field (secrets redacted) |
| `set mqtt.iata <code>` | global IATA / location code |
| `set mqtt.status_interval <10..3600>` | status publish cadence (seconds) |

Broker fields (`<key>`): `url`, `port`, `transport` (tcp\|tls\|wss),
`auth_type` (none\|basic\|jwt), `username`, `password` (write-only),
`topic_prefix`, `iata_override`, `ca_cert`, `jwt_audience`, `jwt_refresh`,
`jwt_owner`, `jwt_email`, `enabled`.

Setting a field on a **live** (enabled) broker slot auto-disables that slot;
re-enable explicitly with `mqtt enable <N>` once the config is complete.

## First-time bring-up

```
set wifi.ssid YourSSID
set wifi.pwd  YourPSK
wifi status                       # confirm StaConnected + IP
set mqtt.broker.0.url  mqtts://broker.example:8883
set mqtt.broker.0.transport tls
set mqtt.broker.0.auth_type basic
set mqtt.broker.0.username obs-node
set mqtt.broker.0.password ******
mqtt enable 0
mqtt status                       # confirm the slot is up
```
