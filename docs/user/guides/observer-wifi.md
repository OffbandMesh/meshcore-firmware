---
title: Connect an observer to WiFi
feature: observer-wifi
---

# Connect an observer to WiFi

Connect your observer radio to a wireless network so its network features can
operate. This page establishes WiFi connectivity; it does not configure a broker
or prove that observations are reaching one.

## Applies to

- **Role:** companion-observer with WiFi and BLE, not a plain companion image.
- **Boards checked here:** Heltec V3, Heltec V4 (OLED or TFT), and XIAO S3 WIO.
- **Images:** `Heltec_v3_companion_observer_wifi`,
  `heltec_v4_companion_observer_wifi`, `heltec_v4_tft_companion_observer_wifi`,
  and `Xiao_S3_WIO_companion_observer_wifi`.
- **Release evidence:** the basic setup commands and these images are present in
  the stable `offband-v1.4.0` source. This guide is also checked against the
  current firmware implementation. It makes no compatibility claim for earlier
  releases, other boards or other roles.
- **Verification:** implementation checked; a new hands-on hardware validation of this
  guide is still pending. Actual connection depends on your network and radio.

## Before you start

Have the matching observer firmware installed, power for the radio, your network
name and password, and a BLE-connected MeshCore-compatible app showing channels.
This observer setup path is not a USB-serial text console or a captive portal.

Open the reserved channel named `_sys @ <8hex>` for your connected radio. The
firmware creates this channel; the eight hexadecimal characters identify its
device-derived suffix. Do not create a public channel with that name, and never
send your network credentials in an ordinary mesh channel. If the reserved
channel is missing, check the image role and reconnect/synchronize the app's
channels before continuing.

Each command below is sent as a message in that reserved channel. Replace
angle-bracket placeholders with your values, without the brackets. Network names
and passwords are case-sensitive. Avoid sharing the app history or screenshots
of credential entry: a write-only firmware password read does not guarantee that
your app never stores what you typed.

## Steps

1. Read the current state:

   ```text
   wifi status
   ```

   You should receive a `wifi.status` reply. `AwaitingSetup` means no active
   connection is available, which can include missing credentials or disabled
   WiFi. If there is no reply, resolve the channel/image problem before sending
   a password.

2. Save the network name:

   ```text
   set wifi.ssid <your-network-name>
   ```

   The reply starts with `wifi.ssid =`. Confirm the name is correct. An `ERROR`
   reply means the value was not successfully saved; stop and correct that
   problem before continuing.

3. Save the password for a password-protected network:

   ```text
   set wifi.pwd <your-password>
   ```

   The normal reply reports `wifi.pwd set` and the number of entered characters,
   without printing the password. A save error is not a successful update.
   This guide covers password-protected networks; open-network setup is outside
   its scope.

4. Enable WiFi if it may have been disabled:

   ```text
   wifi enable
   ```

   The reply is `wifi.enabled = 1 (reboot to apply)`. This stores the policy;
   it does not establish that the radio has connected.

5. Restart the radio normally, using its power control or cycling its supply.
   Reconnect the app over BLE and reopen the reserved channel. Do not erase the
   radio, reset its identity or reflash it for this step. The reserved channel
   does not accept a `reboot` command.

6. Ask for the state again:

   ```text
   wifi status
   ```

   While joining, the reply can show `StaConnecting`. Wait for the network
   attempt to settle and check again.

## Check it worked

Success is a reply of the form:

```text
wifi.status = StaConnected ip=<assigned-address>
```

`StaConnected` with an assigned address confirms WiFi connectivity. A saved SSID,
a password-character count or `wifi.enabled = 1` by itself does not. WiFi
connectivity also does not prove internet access, broker authentication or MQTT
delivery.

The saved SSID, password and enabled policy are held in nonvolatile storage and
survive a normal restart. You can read the network name with `get wifi.ssid`.
Reading `get wifi.pwd` returns a write-only error, not the stored password.

## Troubleshooting

| What you see | What to check |
|---|---|
| No reserved channel or no command reply | Verify the observer image and the BLE connection. Reconnect and synchronize channels. Do not put credentials into another channel. |
| `AwaitingSetup` after restart | Check `get wifi.ssid`, resend `wifi enable`, and restart normally. If needed, enter the SSID/password again and check each acknowledgement. |
| `StaConnecting` or `StaFailed` | Check network spelling, password, access-point availability and signal. Newer firmware may include retry/disconnect details; preserve that error text for support. |
| An NVS/save error | The firmware did not confirm persistence. Record the error and seek support; do not assume another restart saved the value. |
| `StaConnected` but no observer data appears | WiFi is only one prerequisite. Broker configuration, authentication and internet reachability need separate checks. |

When requesting help, provide the board, image name and installed version, plus
the state/error text. Redact passwords, network names and assigned addresses
from public reports.

## Limits and recovery

To change networks, repeat the SSID and password steps and restart. Changing
credentials may apply during a later station retry; restarting gives a clear
application point rather than relying on retry timing.

To stop WiFi on future boots, send `wifi disable` and restart. The command
preserves saved credentials and deliberately does not drop an existing live
connection immediately. To re-enable it, use `wifi enable` and restart.

Do not use a factory reset or full flash erase as a routine network fix: those
can destroy unrelated configuration or device identity. If the radio cannot
reach the reserved management channel, collect the image/version details and
ask for recovery help rather than guessing at destructive commands.

[Back to the manual](../index.md)
