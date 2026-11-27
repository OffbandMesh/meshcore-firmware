---
title: Collect useful support evidence
feature: diagnostic-logs
---

# Collect useful support evidence

Start with the exact image and symptom. A short reproducible report with the
right status is often more useful than a large unreviewed log.

## Applies to

Heltec V3/V4 observer and repeater reporting paths, source-checked against
`offband-v1.6.0-beta1` and current source. Command availability and USB logging
depend on image and build options. The `[WIFI]` lines were checked in a serial
capture from a Heltec V4 Wi-Fi telemetry repeater. The rest of this guide was
checked against the source only. Use controls present in your installed image;
other diagnostic surfaces may require a different build.

## Before you start

Record board/revision, complete image name, Offband version/channel, app version,
power source and approximate time of the problem. Describe what you expected,
what happened and the smallest steps that reproduce it. Record whether there was
a recent update or configuration change.

Logs can contain credentials, network names, coordinates, public keys and device
identifiers. Automated redaction catches known forms; it cannot guarantee that
arbitrary output is safe to publish. Plan to review the file yourself.

## Steps

### 1. Collect the right status surface

For a WiFi observer, send these one at a time in its reserved `_sys @ <8hex>`
channel, then redact identifying values from the replies:

```text
wifi status
mqtt status
```

Use `mqtt view <slot>` only for a slot relevant to the problem. Its password
display is redacted, but other fields can still identify you. On observer builds
with capture-log support, this additional status is available:

```text
caplog status
```

An unknown reply is evidence about the image's capabilities, not a reason to
try filesystem or reset commands. Repeater serial/admin commands are a different
surface. Do not paste them into an observer channel. Do not run commands that
return passwords or secrets while capturing.

### 2. Capture serial output only when that surface is available

Use the source archive matching your firmware, or a maintainer-supplied matching
copy of the capture tools. Keep both `scripts/_cap_serial.py` and
`scripts/log_redact.py` in the same directory; the capture script imports the
redaction helper. Downloading `_cap_serial.py` alone is not sufficient.

Install Python 3 and the capture tool's `pyserial` dependency in a local virtual
environment if needed. From the source directory, with only the intended radio
selected, run:

```text
python scripts/_cap_serial.py --port <serial-port>
```

Replace `<serial-port>` with the device's actual OS port. Close other programs
holding that port. The default rate is 115200; use a different rate only when
the image's instructions require it. Reproduce the problem once and press Ctrl-C.
The tool prints the resulting timestamped log filename. It reads output; it is
not the observer's WiFi/MQTT text-management console.

Opening serial can affect USB/reset behavior on some boards. If that would
disrupt a deployment, agree on the capture method with support first. Do not
enable `--no-redact`, add secret-returning commands, or wire a raw UART mirror
as a routine workaround. Some native-USB early boot output is unavailable after
the port opens.

#### Wi-Fi lines on a Wi-Fi telemetry repeater

A Heltec V4 repeater built with Wi-Fi telemetry prints one `[WIFI]` line for every
station Wi-Fi event. It does this on every such image, with nothing to turn on.
Include these lines when a report involves Wi-Fi, dropped telemetry or a restart.

| Line | Meaning |
|---|---|
| `[WIFI] begin` / `[WIFI] end` | The repeater started or ended a Wi-Fi session. `from=` is a code address a maintainer can match to the image. |
| `[WIFI] assoc rssi=-60` | Joined the access point, at that signal strength in dBm. |
| `[WIFI] got_ip` / `lost_ip` | The network address was assigned or released. |
| `[WIFI] disconnected reason=8(ASSOC_LEAVE)` | The station left the access point, with the standard reason code and its name. |

Each line also carries elapsed times (`since_begin=`, `since_end=`, `since_assoc=`)
and two counts: `joins=` sessions so far and `short=` sessions shorter than 2 s.

How to read them:

- **A normal burst-mode cycle** is one `begin`, one `assoc`, one `end` and one
  `disconnected` every few minutes, with the session lasting several seconds.
- **The repeater ended the session itself** when `disconnected` shows a small
  `since_end=`, a few hundred milliseconds or less. That is the expected way a
  cycle ends.
- **The access point or the radio link ended it** when `disconnected` arrives
  with `since_end=none` or a large `since_end=`. Reason codes such as
  `BEACON_TIMEOUT` or `4WAY_HANDSHAKE_TIMEOUT` are worth reporting.
- **A climbing `short=` count** means sessions keep ending within 2 s of joining.
  Report it with the capture.

### 3. Review and share the smallest useful report

Open the saved file before sending it. Remove any remaining credentials, network
details, location and identifying values you do not intend to disclose. Include
the symptom, versions, reproduction steps and relevant redacted status alongside
the log. Share it through the agreed support route rather than assuming a public
issue is an appropriate place for the full capture.

## Check it worked

Confirm the file is nonempty and covers the problem time, includes the relevant
state/error, and has been reviewed for private information. A file containing
only a restart banner may show a capture-induced reset rather than the original
fault. Tell support if the capture changed the symptom.

## Troubleshooting

| Symptom | Next check |
|---|---|
| `log_redact` import fails | Restore the helper beside the capture script from the same source revision. |
| `serial` import fails | Install `pyserial` into the Python environment running the tool. |
| Port busy or wrong device | Close other serial users and identify the correct OS port explicitly. |
| Empty capture | Check cable, interface, build logging and whether opening the port restarted the radio. |
| Problem only occurs unattended | Provide time/power/network context; do not repeatedly reconfigure the node to manufacture a log. |

## Limits and recovery

Collecting evidence does not repair a fault or prove a hardware diagnosis.
Stop capture normally and reconnect the app if needed. Do not erase identity,
change boot flags or perform a factory reset just to obtain logs. If recovery
is needed, preserve this evidence and follow [power and recovery](power-and-recovery.md).

[Back to the manual](../index.md).
