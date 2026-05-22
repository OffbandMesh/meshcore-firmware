# Crosswire CLI + MQTT Command Reference

Catalog of commands exposed by Crosswire firmware over (a) serial CLI and
(b) MQTT command channel. Source-of-truth references in parentheses point
at the actual handlers; this doc summarizes behavior.

If you find a command that's used in code but missing from this doc, fix
the doc — that's the recurring failure mode this file exists to prevent.

---

## Two transports, two surfaces

| Transport | Reaches device via | Auth | Typical use |
|---|---|---|---|
| **Serial CLI** | USB cable -> CDC serial @ 115200 | none (physical access implies trust) | Bench / dev / recovery / lab triage |
| **MQTT cmd channel** | WiFi+MQTT -> `<prefix>/<node>/cmd` topic | `trigger_secret` (from `platformio.local.ini`) | Remote / production / unattended |

Both transports use the **same underlying handlers** for some commands
(see "Equivalent transport pairs" below). Others are transport-specific
(e.g., `start ota` is CLI-only and uses a different OTA mode than the
MQTT `ota_enable` action — see "OTA: two mechanisms" below).

---

## Serial CLI commands

Sent as plain text terminated by `\r` (or `\n`). Reply is plain text.
Handler: `src/helpers/CommonCLI.cpp::handleCommand()` at line 232.

Send via:

```
python scripts/pio-flash.py send <device-name> "<command>"
```

(The `pio-flash send` wrapper opens the serial port, writes the command,
reads the response for ~5s, prints it.)

### System

| Command | Behavior | Reply | Source |
|---|---|---|---|
| `version` | Print upstream + Crosswire identity | `Upstream MeshCore: vX.Y.Z (date)\nCrosswire fork: crosswire-vA.B.C (sha, branch, date)` | CommonCLI.cpp:237 (FF3 / #180) |
| `reboot` | Reboot device | does not return | CommonCLI.cpp:235 |
| `poweroff` / `shutdown` | Power off (deep sleep) | does not return | CommonCLI.cpp:233 |
| `clkreboot` | Reset RTC to 2024-05-15 + reboot | does not return | CommonCLI.cpp:237 |
| `clock` | Print current UTC time | `HH:MM - D/M/Y UTC` | CommonCLI.cpp:263 |
| `clock sync` | Set RTC to sender_timestamp+1 (must be future) | `OK - clock set: ...` | CommonCLI.cpp:249 |
| `time <epoch>` | Set RTC to epoch seconds | `OK - clock set: ...` | CommonCLI.cpp:267 |

### Mesh / radio

| Command | Behavior | Reply | Source |
|---|---|---|---|
| `advert` | Send a flood advert (1500ms delay) | `OK - Advert sent` | CommonCLI.cpp:245 |
| `advert.zerohop` | Send a zerohop advert | `OK - zerohop advert sent` | CommonCLI.cpp:241 |
| `neighbors` | List recent neighbors | text table | CommonCLI.cpp:278 |
| `neighbor.remove <hex-pubkey>` | Forget a neighbor | `OK` or `ERR: bad pubkey` | CommonCLI.cpp:280 |
| `tempradio <freq> <bw> <sf> <cr> <timeout_min>` | Temporary radio params | varies | CommonCLI.cpp:291+ |

### WiFi / telemetry (D2/D3 / issue #57)

| Command | Behavior | Reply | Source |
|---|---|---|---|
| `wifi` (no args) | Query WiFi+MQTT state | `wifi a=N f=N \| mqtt a=N f=N state=N \| last_ok=Ns ago \| <ok\|warn> \| mode=burst\|persistent(Ns left)` | CommonCLI.cpp:~530 |
| `wifi on [N]` | Enter PERSISTENT_STA mode for N minutes (default 15) | `ok - persistent for N min` | CommonCLI.cpp:506 |
| `wifi off` | Exit PERSISTENT mode (back to burst publishing) | `ok - burst mode` | CommonCLI.cpp:502 |
| `wifi reset` | Reset WiFi connection state (reconnect on next publish) | `ok (will reconnect on next publish)` | CommonCLI.cpp:499 |
| `telemetry` | Print on/off status | `telemetry: <on\|off>` | CommonCLI.cpp:497 |

**Persistent vs burst mode** (per #57 / D2/D3):

- **Burst (default)**: WiFi only connects briefly to publish telemetry, then disconnects. Saves power on battery. Typical publish window ~30s every 15 min.
- **Persistent**: WiFi stays connected continuously for the configured duration. Required for OTA over the home WiFi (the MQTT `ota_enable` mechanism uses the existing persistent WiFi connection).

After persistent expires the device returns to burst mode automatically. WiFi state does NOT survive a reboot — `wifi on N` must be re-issued after each restart.

### OTA (CLI-only, SoftAP mode)

| Command | Behavior | Reply | Source |
|---|---|---|---|
| `start ota` | Bring up **SoftAP at 192.168.4.1** with `/update` endpoint | `Started: http://192.168.4.1/update` | CommonCLI.cpp:259, board.startOTAUpdate() |

**CAUTION**: `start ota` puts the device in **SoftAP mode**, disconnecting
from home WiFi (and MQTT). To OTA via this path, you must connect your
computer's WiFi to the SoftAP that the device just brought up, then POST
firmware to `http://192.168.4.1/update`. Inconvenient. Use the MQTT
`ota_enable` mechanism instead unless the device has no MQTT path
available.

After `start ota` the device typically also resets WiFi state — `mode=burst`
on next query, persistent window lost.

### Safety log (#74)

| Command | Behavior | Source |
|---|---|---|
| `safety log [N]` | Dump last N safety events (default all) | CommonCLI.cpp via E4/E8/E9/E10 work |
| `safety log tail [N]` | Tail-style newest-first dump | CommonCLI.cpp (#74, E10) |
| `safety log clear` | Erase safety log | CommonCLI.cpp |
| `safety partitions` | Show OTA partition state (`run=<slot>`, `app0/app1` states) | CommonCLI.cpp (#72, E8) |
| `safety state` | Show pending-verify / valid / aborted / etc. | CommonCLI.cpp (#68) |

The `safety partitions` output is what `scripts/ota-push.py`'s `verify_channel=serial` parses for pre/post OTA verification.

---

## MQTT command channel

Topics:
- **Publish**: `<mqtt_topic_prefix>/<node_id>/cmd` (default prefix `meshcore`)
- **Subscribe for responses**: `<mqtt_topic_prefix>/<node_id>/cmd/response`

For ST-P Lab: `meshcore/stp-lab/cmd` + `meshcore/stp-lab/cmd/response`.
For patio: `meshcore/wsmj898-ltb/cmd` + `meshcore/wsmj898-ltb/cmd/response`.

Payload format (JSON):

```json
{
  "action": "<action_name>",
  "auth": "<trigger_secret>",
  // ... action-specific fields
}
```

`auth` value is the `trigger_secret` from `meshcore-firmware/platformio.local.ini` under `[ota_secrets]`. Must match what the device was built with.

Handler: `src/helpers/wifi_telemetry/RemoteCommand.cpp::handle()`. Strict action whitelist at line 68.

### Available actions (#86 + #88 + later)

| Action | Behavior | Required fields | Rate limit | Source |
|---|---|---|---|---|
| `ota_enable` | Bring up OTA endpoint on home WiFi IP (NOT SoftAP). Calls `wifiOn(window_sec)` + `otaStart()`. Response data includes `ota_url`. | `auth`, `window_sec` (optional) | 5 min between attempts | RemoteCommand.cpp:295 |
| `ota_disable` | Stop OTA server, turn WiFi off | `auth` | (default) | RemoteCommand.cpp:318 |
| `ota_status` | Query OTA server status | `auth` | (default) | RemoteCommand.cpp:324 |
| `safety_log_dump` | Dump safety log to MQTT response. Used by `ota-push.py` verify_channel=mqtt. | `auth` | (default) | RemoteCommand.cpp |

The OTA enable action's response includes:

```json
{
  "ota_url": "http://192.168.50.177/update",
  "window_sec": 600
}
```

That's the URL `scripts/ota-push.py` should POST firmware to.

### Equivalent transport pairs

| CLI command | MQTT action | Notes |
|---|---|---|
| `wifi on N` | `ota_enable` calls `wifiOn(N)` internally | OTA-enable does both wifi + otaStart in one shot |
| `safety log` | `safety_log_dump` | Same data, different transport |
| `start ota` | (no MQTT equivalent — different OTA mode) | CLI is SoftAP; MQTT is home WiFi |

### Authentication failures

A bad `auth` value results in `publishReject(AUTH_FAIL, ...)` on the response topic. Silent failure ONLY if response publish itself fails (e.g., MQTT disconnected mid-cmd).

---

## OTA: two mechanisms (don't confuse them)

### Mechanism A: SoftAP (CLI-driven)

```
1. Serial: "start ota"
2. Device brings up SoftAP "MeshCore-<node>" at 192.168.4.1
3. Computer joins that SoftAP (manual WiFi switch)
4. POST firmware to http://192.168.4.1/update
5. Device reboots, runs new firmware
```

Use when: device has no home WiFi (e.g., field deploy outside coverage), or first-time setup.

### Mechanism B: Home WiFi (MQTT-driven, #86)

```
1. Ensure device WiFi is up (e.g., CLI "wifi on 30" if it isn't already)
2. MQTT publish to <prefix>/<node>/cmd: {"action":"ota_enable", "auth":"...", "window_sec":600}
3. Device responds with ota_url (e.g., http://192.168.50.177/update)
4. POST firmware to that ota_url
5. Device reboots, runs new firmware
```

Use when: device is on home WiFi (or can be brought up to it). Convenient
because no WiFi-network switching required. This is what
`scripts/ota-push.py` expects.

**The two mechanisms are not interchangeable.** If you `start ota` first,
the device leaves home WiFi and the MQTT path is unavailable. Pick one
mechanism per OTA session.

---

## Wrappers (mechanical discipline layer)

| Wrapper | Purpose | Source |
|---|---|---|
| `scripts/pio-flash.py send <device> "<cmd>"` | Serial CLI sender; identity-verified port-open | scripts/pio-flash.py:484 (cmd_send) |
| `scripts/pio-flash.py monitor <device>` | Serial monitor; identity-verified port-open | scripts/pio-flash.py:548 (cmd_monitor) |
| `scripts/pio-flash.py preview <device> --env <env>` | Tier A stage 1: write token, no flash | scripts/pio-flash.py:355 (cmd_preview) |
| `scripts/pio-flash.py confirm <device> --token <path>` | Tier A stage 2: flash via pio (token-bound) | scripts/pio-flash.py:407 (cmd_confirm) |
| `scripts/ota-push.py <device> --firmware <path>` | OTA push to home WiFi IP, verify, log to flash-history.jsonl | scripts/ota-push.py |

The wrappers do NOT auto-call `wifi on` or `ota_enable` before flashing —
those are caller responsibilities, documented above. The wrappers
enforce the safety + audit layer; the actual enable sequence is on the
operator (or a higher-level script).

---

## Common workflows

### Bring ST-P Lab to OTA-ready state (USB tethered, home WiFi)

```bash
# 1. Verify USB enumeration matches registry (Tier 0, free)
python scripts/pio-flash.py list

# 2. Bring WiFi up to home network (Tier B serial send)
python scripts/pio-flash.py send ST-P "wifi on 30"

# 3. Verify WiFi+MQTT actually up
python scripts/pio-flash.py send ST-P "wifi"
# Expect: "mode=persistent(<seconds> left)", "last_ok=<small> ago"

# 4. Trigger OTA endpoint on home WiFi via MQTT
python -c "
import configparser, json, paho.mqtt.client as mqtt, time
secret = configparser.ConfigParser()
secret.read('meshcore-firmware/platformio.local.ini')
c = mqtt.Client()
c.connect('192.168.50.24', 1883, 30)
c.publish('meshcore/stp-lab/cmd', json.dumps({
    'action':'ota_enable',
    'auth': secret['ota_secrets']['trigger_secret'],
    'window_sec':600
}), qos=0)
c.disconnect()
"

# 5. Run the OTA push
python scripts/ota-push.py ST-P --firmware \
    C:/Dev/LoRa/meshcore-firmware/.pio/build/heltec_v4_repeater_telemetry_stp/firmware.bin
```

### Verify device identity post-flash

```bash
# Serial
python scripts/pio-flash.py send ST-P "version"

# MQTT (read HA discovery payload's sw_version)
ssh pi5 "mosquitto_sub -h localhost -t 'homeassistant/sensor/stp-lab/+/config' -C 1 -v"
```

### Tail safety log over MQTT

```bash
# Use ota-push.py's internal helper, or:
python -c "
# ... mosquitto publish safety_log_dump cmd, subscribe response, print
"
```

---

## Why this file exists

Recurring failure mode (multiple sessions): a CLI command or MQTT action
is used during a session, but it's only documented in source code. Next
session, the agent has to grep for handlers, sometimes incorrectly
guesses behavior, sometimes burns 30 minutes rediscovering what
"wifi on N" does versus "start ota".

**Rule**: if you use a CLI command or MQTT action in a session and find
it's NOT documented here, your job is to add it before closing the
session. The cost of adding one row to a table is far less than the
cost of the next agent rediscovering it.

Cross-reference: this file is the per-firmware companion to
`C:\Dev\LoRa\HARDWARE.md` (hardware inventory + RF chain reference) and
`meshcore-firmware/VERSIONING.md` (fork versioning scheme).

---

**Last updated**: 2026-05-22 (initial creation; covers commands used in
the FF1-FF5 + ST-P Lab OTA-flash diagnostic session per Epic #176)
