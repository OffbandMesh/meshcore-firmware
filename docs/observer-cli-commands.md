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
| `mqtt status` | pool summary + per-slot live state (**configured slots only**) |
| `mqtt view <N>` | full stored config for slot N, configured or empty (secrets redacted) |
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

**`mqtt status` lists only *configured* slots** — a slot appears only once it
has a URL. An empty / unconfigured slot is **not** shown, so the highest number
you see is your last configured slot, not the slot ceiling (currently 10, slots
0–9). To inspect a specific slot regardless of whether it's configured, use
`mqtt view <N>`: an empty slot reads `url=(unset)`.

`mqtt view <N>` (#98) dumps every stored field for one slot in a few packed
lines, in the familiar order: `url, port, transport, auth_type, username,
jwt_audience, jwt_owner, jwt_email, jwt_refresh, ca_cert, iata`. Secrets are
never echoed — a basic-auth `password` shows `(set)` / `(unset)`, and the JWT
bearer token (auto-minted at connect) is omitted entirely. A JWT slot's
`username` and `jwt_owner` show their auto-derived defaults when unset
(`auto(v1_+pubkey)` and `auto(device-pubkey)` — the device's own pubkey, #95).
Live state (up / backoff / retries) stays in `mqtt status`; `view` is the
stored **config**.

## Broker auth — wss/jwt (the real recipe)

The public MeshCore brokers (LetsMesh, eastme.sh) use `wss` + JWT. The credential
is your node's **own Ed25519 public key** — the firmware mints the token; there's
no separate registration. Configure a wss/jwt slot like this:

```
set mqtt.broker.1.url           wss://mqtt-us-v1.letsmesh.net:443/mqtt
set mqtt.broker.1.transport     wss
set mqtt.broker.1.auth_type     jwt
set mqtt.broker.1.ca_cert       gts-r4
set mqtt.broker.1.jwt_audience  mqtt-us-v1.letsmesh.net
set mqtt.broker.1.username      <your node pubkey>
set mqtt.broker.1.jwt_owner     <your node pubkey>
set mqtt.broker.1.jwt_email     you@example.com
mqtt enable 1
mqtt status
```

- **`username` + `jwt_owner` = your node's pubkey.** The firmware mints the JWT
  (the password — you do **not** set it) and normalizes `jwt_owner` case
  internally, so the input case doesn't matter.
- **The exact `username` form is broker-dependent.** A **bare pubkey** works on
  eastme.sh; some brokers expect a `v1_<pubkey>` prefix. If a wss slot won't
  authenticate, try the other form. (Defaulting this per broker is Crosswire#95.)

### Known broker values
| Broker | url | ca_cert | jwt_audience |
|---|---|---|---|
| CoreScope | `mqtt://mqtt.w8oof.net:1883` | — (tcp / anon) | — |
| LetsMesh-US | `wss://mqtt-us-v1.letsmesh.net:443/mqtt` | `gts-r4` | `mqtt-us-v1.letsmesh.net` (bare) |
| eastme.sh | `wss://mqtt.eastme.sh:443/mqtt` | `letsencrypt` | `mqtt.eastme.sh` |

## Gotchas

- **A `set` on a *disabled* slot may not apply immediately, and `mqtt status`
  shows the broker's *cached* config, not NVS** — so a slot can look
  mis-configured right after a `set` (Crosswire#67). Do your `set`s, then
  `mqtt enable <N>` (which reloads), or reboot. If a field looks wrong, re-apply
  it while the slot is enabled.
- **wss/TLS brokers need a valid wall clock** (cert validity + JWT `exp`). Until
  the clock syncs (NTP, or a GPS fix), such a slot reads `state=held(no-clock)`
  in `mqtt status` — that's *deferred, not failing*; it connects on its own once
  the clock is sane. tcp brokers (CoreScope) never wait on the clock.

## First-time bring-up (minimal)

```
set wifi.ssid YourSSID
set wifi.pwd  YourPSK
wifi status                       # confirm StaConnected + IP
# ...configure a broker slot (see "Broker auth — wss/jwt" above)...
mqtt status                       # wss reads held(no-clock) until the clock syncs, then up
```
