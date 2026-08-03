# HA / Queue-Triggered OTA Flash (STA-mode, over home WiFi)

How to flash a deployed Crosswire node (e.g. the patio repeater) over WiFi, by
queuing an `ota_enable` command and then pushing the binary — no USB cable, no
serial console. This is the **STA-mode** OTA path (device stays on your home
WiFi). It is **not** the CLI `start ota` SoftAP path — see
[Two OTA mechanisms](#two-ota-mechanisms).

> **Draft.** Written 2026-08-03 from the firmware + wrapper source. File:line
> references are given so anything here can be re-verified against the code.

---

## TL;DR cheat sheet

```
# 0. Build the firmware
cd C:\Dev\meshcore-firmware
pio run -e heltec_v4_repeater_telemetry           # -> .pio/build/<env>/firmware.bin

# 1. Open the device's OTA window via the command queue (cmdrelay, on Pi5:8765)
#    Device pulls this on its next ~5-min poll and brings up http://<ip>/update.
curl -s -X POST http://<pi5>:8765/devices/<node_id>/cmds \
  -H "Authorization: Bearer $ADMIN_BEARER_TOKEN" -H "Content-Type: application/json" \
  -d '{"action":"ota_enable","params":{"window_sec":900},"expires_in_sec":900}'

# 2. Push the binary while the window is open (mechanical-MD5 wrapper, verifies)
cd C:\Dev\LoRa
python scripts/ota-push.py <device-name> --firmware C:\Dev\meshcore-firmware\.pio\build\<env>\firmware.bin
```

- `<node_id>` is the registry `ota.node_id` (patio = `wsmj898-ltb`, ST-P = `stp-lab`).
- `<device-name>` is the registry short-name (`ota-push` looks up lan_ip / password / verify_channel from `hardware-devices.yaml`).
- The device only acts on the command **while its WiFi is up**; the reboot/poll and the OTA window are the two timing constraints (see [Timing & gotchas](#timing--gotchas)).

---

## Two OTA mechanisms

There are **two different OTA modes** in this firmware. Don't confuse them
(`docs/cli-and-mqtt-commands.md` covers the CLI side).

| | **STA-mode (this doc)** | **SoftAP-mode (CLI)** |
|---|---|---|
| Trigger | `ota_enable` command (queue/MQTT) | `start ota` over LoRa admin CLI |
| Firmware call | `board.startOTAUpdateOverSTA()` | `board.startOTAUpdate()` |
| Network | Your **home WiFi**, device keeps its STA IP | Device raises its own AP `MeshCore-OTA` at `192.168.4.1` |
| Endpoint | `http://<lan_ip>/update` | `http://192.168.4.1/update` |
| Auth | HTTP Basic Auth (`admin` + node password) | **None** (fallback path) |
| Use when | Device is on home WiFi (normal deployed case) | Device has no home-WiFi creds |
| Source | `ESP32Board.cpp:65 startOTAUpdateOverSTA` | `ESP32Board.cpp:23 startOTAUpdate` |

> `start ota` (SoftAP) has a documented **panic-loop interaction** if WiFi is
> already active, and it resets WiFi state afterward. For a deployed node on
> home WiFi, always use the STA-mode `ota_enable` path below.

---

## End-to-end flow

```
[build]  pio run -> .pio/build/<env>/firmware.bin
   |
[queue]  enqueue ota_enable ---> cmdrelay (Pi5:8765)  OR  MQTT meshcore/<node>/cmd
   |                                   |
   |     device polls / bursts WiFi ---+
   v
[device] RemoteCommand OTA_ENABLE dispatch:
           wifiOn(window_sec)  -> WiFi persistent (clamped 60..1800s)
           otaStart()          -> startOTAUpdateOverSTA(node_name, node_password)
                                  -> AsyncElegantOTA on http://<lan_ip>/update  (Basic Auth)
           responds with ota_url
   |
[push]   ota-push.py -> POST http://<lan_ip>/update  (auth=admin:password, MD5 + bytes)
   |
[device] Update.begin -> write inactive OTA partition -> Update.end (MD5 check)
           -> reboot ~1s later into the new partition
   |
[boot]   arduino-esp32 initArduino auto-validates PENDING_VERIFY -> VALID before setup()
           (bootloader rolls back if it crashes in the pending window)
   |
[verify] ota-push verifies via serial | mqtt | telemetry:
           running partition SWITCHED  AND  state in {pending_verify, valid}
```

---

## Step 1 — Build & where the binary goes

Build in the firmware repo:

```
cd C:\Dev\meshcore-firmware
pio run -e <env>            # e.g. heltec_v4_repeater_telemetry
```

Output lands at `.pio/build/<env>/firmware.bin`. **There is no "drop it in a
folder on a server" step** — the binary is read off disk by `ota-push.py` and
uploaded straight to the device over HTTP. You just need the path; pass it to
`--firmware`. (`ota-push.py` also reads the sibling `firmware.elf` for the
embedded identity marker used in the audit log — keep them together.)

---

## Step 2 — The queue command (`ota_enable`)

`ota_enable` tells the device to keep WiFi up and open its `/update` server.
Two transports reach the same firmware handler:

### 2a. cmdrelay HTTP queue (recommended — same queue used for `reboot`)

cmdrelay runs on Pi5 port **8765**; the device polls `GET /devices/<node_id>/cmds`
on each WiFi burst and executes what it finds.

```
POST http://<pi5>:8765/devices/<node_id>/cmds
Authorization: Bearer <ADMIN_BEARER_TOKEN>
Content-Type: application/json

{"action":"ota_enable","params":{"window_sec":900},"expires_in_sec":900}
```

- `window_sec` goes **inside `params`** (HTTP cmd shape) — `RemoteCommand.cpp:229-235`.
- No in-payload secret is needed on this path; the HTTP transport is authed by
  the bearer header (`RemoteCommand.cpp:183,208`). cmdrelay uses
  `ADMIN_BEARER_TOKEN` for enqueue and `DEVICE_BEARER_TOKEN` (= the
  `ota_secrets.trigger_secret`) for the device's poll.
- Returns `201 {"cmd_id":N,...}`. The device posts its result (including the
  `ota_url`) back to `POST /devices/<node_id>/responses`.

### 2b. MQTT cmd channel (direct pub/sub)

```
publish  meshcore/<node_id>/cmd
  {"action":"ota_enable","auth":"<trigger_secret>","window_sec":900}

# device replies on:
subscribe meshcore/<node_id>/cmd/response      # payload includes ota_url
```

- `auth` is the shared secret (`ota_secrets.trigger_secret`), checked
  constant-time (`RemoteCommand.cpp:156-173`). `window_sec` is top-level here
  (`RemoteCommand.cpp:294`).
- This is the path `scripts/test_mqtt_ota_enable_burst.py` exercises.

### Window & rate limits (both transports)

- `window_sec` is **clamped to 60..1800s** (1-30 min) — `RemoteCommand.cpp:262-263`.
- `OTA_ENABLE` is **rate-limited to one attempt per 5 min** — `RemoteCommand.cpp:36`.
- The window keeps WiFi *persistent* (via `wifi_telemetry_set_persistent`);
  the underlying persistent cap is 60 min (`main.cpp:649-653`,
  `WIFI_PERSISTENT_MAX_MS`).

---

## Step 3 — How it triggers on the device

On receiving `OTA_ENABLE` (`RemoteCommand.cpp:68`), the repeater's
`PatioRemoteCallbacks` (`main.cpp:647`) runs:

1. `wifiOn(window_sec)` -> `wifi_telemetry_set_persistent(window_sec*1000)` —
   WiFi stays connected instead of dropping after the telemetry burst
   (`main.cpp:649-653`).
2. `otaStart()` -> `board.startOTAUpdateOverSTA(node_name, node_password, reply)`
   (`main.cpp:660-682`), which:
   - refuses cleanly if STA WiFi isn't up (`ESP32Board.cpp:67-69`),
   - starts an `AsyncWebServer` + `AsyncElegantOTA` bound to the STA IP,
   - sets HTTP Basic Auth to **user `admin` / password = the node's LoRa admin
     password** (`MeshCore.h:104`; `AsyncElegantOTA.cpp:22-25,46-49`).
3. Device replies with the OTA URL: `http://<lan_ip>/update`.

The `/update` server stays up for the `window_sec` you asked for.

---

## Step 4 — Push the binary (`ota-push.py`)

`scripts/ota-push.py` (in `C:\Dev\LoRa`) is the authorized push wrapper. It
exists because the 2026-05-17 silent-failure incident (Strycher/LoRa#88) showed
raw `curl` can't be trusted for OTA. It makes three mistakes structurally
impossible (`ota-push.py:9-24`):

- **Wrong MD5** — firmware is read into memory **once**; MD5 is hashed from the
  *same* bytes object that is uploaded (`ota-push.py:300-330`).
- **Wrong file** — device resolved from `hardware-devices.yaml`; SHA-256 of the
  uploaded bytes recorded in `flash-history.jsonl`.
- **Silent success** — curl's exit code lies for OTA (AsyncWebServer +
  `ESP.restart()` race drops the response). The wrapper **ignores upload-side
  signals** and confirms via a separate channel.

Invocation:

```
python scripts/ota-push.py <device-name> --firmware <path-to-firmware.bin>
       [--verify-channel serial|mqtt|telemetry]   # override registry
       [--upload-timeout-sec 180] [--verify-timeout-sec N]
       [--settle-sec 15] [--dry-run]
```

What it does (`ota-push.py:667-810`): resolve device -> capture pre-state ->
`POST http://<lan_ip>/update` with `auth=(admin, password)`, form field
`firmware` = the bytes, form field `MD5` = md5 -> settle 15s -> verify -> append
audit row. `--dry-run` prints the plan and touches nothing.

Exit codes: `0` success (verified) / `1` refused (config/args) / `2` upload HTTP
failure / `3` uploaded but verify said FAILURE / `4` verify channel unavailable.

---

## Step 5 — Flash, reboot, partition validation

- AsyncElegantOTA `Update.begin()` writes to the **inactive** OTA partition,
  `Update.end()` checks the MD5, then it reboots ~1s later
  (`main.cpp:420`, `AsyncElegantOTA.cpp:81-84`).
- On boot, arduino-esp32's `initArduino()` auto-validates the new image
  `PENDING_VERIFY -> VALID` **before** `setup()` runs. If the new image crashes
  during the pending window, the bootloader rolls back to the previous
  partition. (See `findings_2026-05-14_ota_rollback_actual_behavior.md`.)

---

## Step 6 — Verify (do not trust the upload)

`ota-push` picks the channel from the registry `ota.verify_channel` (override
with `--verify-channel`). All strong checks assert **two** things: the running
partition **switched** AND its state is in `{pending_verify, valid}`.

| Channel | Mechanism | Strength | Default timeout |
|---|---|---|---|
| `serial` | `pio-flash send <dev> "safety partitions"`, parse `run=` + `appN:state` | strong (needs USB cable) | 60s |
| `mqtt` | `safety_log_dump` via #86 cmd channel, parse `boot_inc` seq + `st` | strong (needs #86 firmware) | 60s |
| `telemetry` | wait for a fresh (non-retained) `<prefix>/<node>/state` publish | **weak** — proves alive, not which partition | 1200s |

`ota-push.py:87-97` — telemetry gets 1200s because it may wait for the device's
next natural 15-min publish; a 60s default false-negatived real OTAs.

---

## Per-device config — `hardware-devices.yaml` `ota:` block

`ota-push` refuses if any required field is missing (`ota-push.py:147-192`).

```yaml
    ota:
      lan_ip: "192.168.50.197"                 # device's home-WiFi IP (DHCP; re-check if push fails)
      node_id: "wsmj898-ltb"                   # MQTT/cmdrelay node id
      mqtt_topic_prefix: "meshcore"            # topic root (default "meshcore")
      verify_channel: "mqtt"                   # serial | mqtt | telemetry
      admin_password_secret_key: "<section.key>"  # POINTER into platformio.local.ini (never the value)
```

Real examples in the registry:
- **ST-P (`stp-lab`)** — bench, USB cabled, `verify_channel: serial`, lan_ip `192.168.50.177`.
- **patio (`wsmj898-ltb`)** — deployed, `verify_channel: mqtt`, lan_ip `192.168.50.197`.

> The registry stores a **pointer** (`section.key`) to the password, never the
> password itself, so an accidental commit of `hardware-devices.yaml` leaks
> nothing (`ota-push.py:196-231`).

---

## Secrets — `meshcore-firmware/platformio.local.ini` (gitignored)

`ota-push.py` reads (`ota-push.py:80,206-260`):

- `[<section>]` `<key>` — the node's admin password (dereferenced from
  `admin_password_secret_key`). Used for `/update` Basic Auth.
- `[mqtt_secrets]` `host` / `port` / `user` / `password` — for mqtt/telemetry verify.
- `[ota_secrets]` `trigger_secret` — the shared secret for the MQTT `ota_enable`
  auth field (and cmdrelay's `DEVICE_BEARER_TOKEN`).

---

## Timing & gotchas

- **Burst window.** In default burst mode the device's WiFi is only up ~30s
  every ~15 min (`WIFI_TELEMETRY_INTERVAL_MS`). A queued command is only acted
  on while WiFi is up. cmdrelay handles this naturally (the device pulls on its
  poll); the MQTT path must land during a burst — send `wifi_keepalive` first,
  or watch for a fresh state publish and fire immediately (what
  `test_mqtt_ota_enable_burst.py` does).
- **Two clocks.** cmdrelay delivery is on the device's poll cadence (~5 min for
  patio); the `ota_enable` window then keeps WiFi up long enough for the push.
  Size `window_sec` to comfortably cover the push (a ~1.2 MB image over home
  WiFi is well under a minute, but leave margin) — 900s is a safe default.
- **DHCP.** `lan_ip` can rotate. If the push connection-refuses, re-check the
  device's IP (router DHCP list, or its telemetry once it reports its own IP)
  and update the registry.
- **Rate limit.** One `ota_enable` per 5 min — don't spam it.
- **Never claim success from the upload.** Timeout/200/connection-drop all look
  the same. Only the verify channel confirms it; that's the whole reason
  `ota-push.py` exists.

---

## Source references

| What | File |
|---|---|
| Push wrapper (MD5, verify, audit) | `C:\Dev\LoRa\scripts\ota-push.py` |
| MQTT `ota_enable` burst test | `C:\Dev\LoRa\scripts\test_mqtt_ota_enable_burst.py` |
| Command parse/auth/window/rate-limit | `src/helpers/wifi_telemetry/RemoteCommand.cpp` |
| OTA_ENABLE dispatch (wifiOn + otaStart) | `examples/simple_repeater/main.cpp:647-699` |
| STA-mode OTA server + Basic Auth | `src/helpers/ESP32Board.cpp:65` |
| AsyncElegantOTA `/update` + auth | `arch/esp32/AsyncElegantOTA/src/AsyncElegantOTA.cpp` |
| CLI SoftAP OTA (the *other* mechanism) | `docs/cli-and-mqtt-commands.md` §OTA |
| Per-device OTA config | `C:\Dev\LoRa\hardware-devices.yaml` (`ota:` blocks) |
| OTA discipline / incident background | `C:\Dev\LoRa\CLAUDE.md` §OTA Discipline (Strycher/LoRa#88) |
| Rollback behavior | `findings_2026-05-14_ota_rollback_actual_behavior.md` (memory) |
