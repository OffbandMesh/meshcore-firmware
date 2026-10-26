---
title: Choose observer time and location sharing
feature: observer-location
---

# Choose observer time and location sharing

The observer needs valid time for secure broker connections. Sharing your
position is a separate choice; getting the clock working does not require
publishing your location.

## Applies to

WiFi companion-observer images for Heltec V3, Heltec V4 and XIAO S3 WIO.
Behavior is source-checked against `offband-v1.6.0-beta1` and current firmware;
this is not a new GPS, app-UI or privacy hardware trial. Earlier releases need
their own applicability check.

GPS support in a firmware build does not prove a receiver is physically fitted.
Heltec V3/V4 images have GPS integration for suitable hardware; a bare board must
not be assumed to contain a working GNSS receiver. The checked XIAO S3 WIO image
disables the GPS sensor integration. It can still use network time and a manually
set position through a compatible app.

## Before you start

Complete [WiFi setup](observer-wifi.md). Use an app that exposes your firmware's
device location/privacy and, where supported, GPS/custom-variable controls.
Exact screen names depend on the app version; this manual does not claim a
particular client UI. If those controls are absent, record the app and firmware
versions and ask for support rather than guess at commands.

Decide whether you want your node's position in mesh advertisements and broker
status. Both can reach other people. Turning sharing off does not delete
previously transmitted information or make radio activity anonymous.

## Steps

### 1. Let the clock synchronize

After WiFi connects, the observer starts network time synchronization. It does
not wait for a GPS fix before starting that process. A fitted, enabled GPS can
later take priority; firmware then stops NTP from overwriting GPS time. App-set
time is a fallback before a better source owns the clock.

For a secure MQTT slot, inspect:

```text
mqtt status
```

`held(no-clock)` is waiting for valid time, not an authentication failure. Allow
network time access and check again. There is no fixed promised synchronization
time, especially without internet access or with a receiver that cannot see satellites.

### 2. Choose privacy before setting a position

In the connected app's supported device location/privacy control, choose no
location sharing, corresponding to firmware policy `none`, if you do not want
the observer to include coordinates in its advertisements and MQTT status.
Read the setting back in the app. Do not assume disabling GPS also disables sharing.

### 3. Set a position only if you intend to share it

For a fixed node, use the app's supported node-position entry, then read it back.
For a receiver-equipped node, use the app's supported GPS control and wait for
an actual position fix. GPS settings are persisted by the firmware; their mere
presence in the app does not establish that a receiver is connected or has a fix.

GPS and manual entry write the same position store. Active GPS can replace a
manual position. Disabling GPS does not necessarily clear an old position.
If you want sharing, explicitly select the sharing policy `share` and verify
what position is currently stored before sending an advertisement.

The older `set gps:1` examples are not commands supported by this guide's
reserved `_sys` channel. GPS/custom-variable configuration and observer WiFi/MQTT
text commands use different firmware paths. Do not paste a serial/repeater recipe
into `_sys` as a workaround.

## Check it worked

Read the selected policy and position back using your app. When sharing is off,
verify a fresh advertisement and, if you have broker access, a fresh observer
status payload omit coordinates. When sharing is on, verify the fresh position
matches what you intended. Old cached map entries or retained broker messages
are not a fresh privacy test. The MQTT payload suppresses the unset position
`0,0`; that is not a substitute for selecting privacy explicitly.

A secure broker progressing beyond `held(no-clock)` shows the clock gate cleared;
it is not proof of a GPS fix or successful location sharing. Check the receiver's
reported fix separately when GPS is part of your deployment.

## Troubleshooting

| Symptom | Next check |
|---|---|
| GPS control absent | Confirm image, physical receiver support and app capabilities. |
| GPS enabled but no position | Check supported receiver installation and sky view; do not infer a fix from the toggle. |
| Position changes after manual entry | Active GPS may be replacing the shared store. |
| Old position remains after GPS is disabled | Sharing policy controls publication independently of GPS power. |
| Secure broker still held for time | Confirm WiFi and network-time reachability; preserve exact state/version for support. |

## Limits and recovery

Set sharing to `none` to stop future coordinate inclusion, read it back, and
check fresh outputs. This does not revoke historical data. After a normal restart,
read policy and position again before trusting an unattended deployment. Use a
normal restart if needed; a factory reset is not a routine time/location fix.
No instructions here guarantee anonymity or remove the need to test your actual app.

[Back to the manual](../index.md).
