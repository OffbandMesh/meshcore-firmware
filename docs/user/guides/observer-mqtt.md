---
title: Connect an observer to an MQTT broker
feature: observer-mqtt
---

# Connect an observer to an MQTT broker

WiFi connectivity is the first prerequisite. This guide connects one broker
slot and separates a saved configuration, a live connection and received data.

## Applies to

WiFi companion-observer images for Heltec V3, Heltec V4 OLED/TFT and XIAO S3 WIO.
Commands and state labels are source-checked against `offband-v1.6.0-beta1` and
the current implementation. That release is a beta; older releases can use
different labels or defaults. No new broker/hardware trial is claimed.
Repeater telemetry has a different configuration surface and is outside this guide.

## Before you start

Complete [WiFi setup](observer-wifi.md). Have the broker operator's endpoint,
transport, authentication and certificate requirements. Decide what observations
and location you are willing to send to that operator. Broker connectivity is
not permission to publish private information.

Send commands only in the connected radio's reserved `_sys @ <8hex>` channel.
Never send credentials in a normal mesh channel. Use the examples one command
at a time, substituting your values without angle brackets. Credential entry may
remain in app history even when firmware reads are write-only.

## Steps

### 1. Inspect before choosing a slot

```text
mqtt status
```

Current firmware has slots 0 through 9. Status lists configured slots, so its
highest displayed number is not the limit. Inspect a specific slot with:

```text
mqtt view 6
```

Use slot 6 below only if it is empty or you intend to replace its configuration.
Existing devices can retain older default layouts. Do not assume a particular
broker occupies a particular slot, and do not enable every seeded broker.
If an existing configured slot already matches the broker you intend to use,
review its stored endpoint/authentication first, then enable that slot instead
of rewriting its fields. Substitute its number for `6` in the later commands.

### 2. Configure one intended connection

First disable the chosen slot while changing its settings:

```text
mqtt disable 6
```

Set the complete URI and transport supplied by your broker. For example, a broker
that explicitly supports MQTT over TLS with basic authentication needs fields
of this form:

```text
set mqtt.broker.6.url mqtts://broker.example:8883
set mqtt.broker.6.transport tls
set mqtt.broker.6.port 8883
set mqtt.broker.6.auth_type basic
set mqtt.broker.6.username <broker-username>
set mqtt.broker.6.password <broker-password>
set mqtt.broker.6.ca_cert <supported-ca-name>
```

`broker.example` is an example hostname: replace it with your broker's real host.
Use its actual port in both the URI and the stored port field. The checked
implementation passes the URI directly to the connection library; a bare
hostname is insufficient, and changing only the separate `port` field does not
change the URI's destination. Keep the URI scheme, port and transport consistent.

This is a field checklist, not a working public-broker recipe. `tls` uses a URI
such as `mqtts://broker.example:8883`; `tcp` uses `mqtt://` without TLS. Use `wss`
for a WebSocket endpoint only when the broker specifies it, with a full URI such
as `wss://broker.example:443/mqtt`, including the broker's path and port.
JWT brokers use `auth_type jwt` and broker-specific audience/identity fields;
do not substitute a basic password for a JWT recipe. Obtain the correct supported
CA name for your broker and firmware. Do not disable certificate checks to make
a connection work. Plain `tcp` provides no TLS confidentiality.

Check every reply. An `ERROR` means the setting was not accepted. Editing a live
slot disables it; completing the fields is not the same as enabling the connection.

### 3. Read back and enable

```text
mqtt view 6
mqtt enable 6
mqtt status
```

`view` shows stored settings and redacts the password. Normal enable acknowledgement
is `mqtt slot 6: enabled`. If a reply says a live reload failed and the change is
effective after reboot, the stored change is not yet a live success; restart
normally and check again. Settings and enable policy persist across normal restarts.

## Check it worked

Look for the chosen slot to show `state=up` in `mqtt status`. Then use an authorized
broker-side subscriber or the operator's dashboard to confirm fresh messages
from your node. A saved field, an enable acknowledgement or WiFi IP alone is not
proof of received observations. A connection also does not guarantee every packet
is delivered: radio reception, filtering, queueing and broker rules are separate.

## Troubleshooting

| State or symptom | Meaning and next step |
|---|---|
| `held(no-clock)` | Secure connection is waiting for valid time. Check WiFi and [time configuration](observer-location.md). |
| `waiting-turn` | Another TLS slot is using the pool's current connection budget. This is a normal rotation state. |
| `held(low-heap)` | Connection is deferred for memory pressure. Reduce unnecessary enabled slots and collect status if persistent. |
| `backoff` | An attempt failed and will retry. Check endpoint, port, transport, trust and authentication. |
| `failed(gave-up)` | The slot has stopped ordinary retries. Correct its configuration, then explicitly re-enable it. |
| `up`, no messages at the destination | Check the intended node/topics, subscription permissions and actual observed traffic. |

App-rendered status can use different machine tokens from the `_sys` text. Include
the exact surface and version when reporting a discrepancy.

## Limits and recovery

Use `mqtt disable 6` to stop using this slot without deleting its stored settings.
Confirm the reply and status. This does not disable other brokers or erase data
already received by a broker. Avoid `mqtt clear` as a routine connectivity fix:
it deletes that slot's configuration, and default slots may be reseeded at boot.
Keep a private record of your intended configuration and redact support evidence.

[Back to the manual](../index.md).
