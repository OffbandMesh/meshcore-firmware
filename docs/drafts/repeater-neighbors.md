---
title: Report a repeater's own regions to CoreScope
feature: repeater-neighbors
---

# Report a repeater's own regions to CoreScope

A repeater with WiFi telemetry can publish the flood regions it serves to its MQTT
brokers as a `/neighbors` report. A map service such as CoreScope records them on the
repeater's own node, read from the repeater itself rather than inferred from traffic.
The list is exactly the one the repeater gives a nearby node that asks it for its
regions.

## Applies to

The Heltec V4 WiFi-telemetry repeater image. Development builds only; no release
carries these commands yet. Checked against source and automated tests; no on-air,
hardware or live-broker trial yet. The report lists this repeater's own regions only;
the regions of the repeaters it hears are not in it yet.

## Before you start

Send these as an admin: on the USB serial console, or logged in to the repeater as
admin from the app. A guest login cannot run them.

To publish, the repeater needs at least one MQTT broker enabled and connected (`mqtt
status`), and its clock set: a report is never sent with an unset clock.

## Steps

### 1. Publish now

```text
discover.scopes
```

Reply: `OK - publishing own scopes`. The report goes to every connected broker, whether
or not periodic publishing is on. If the repeater has no clock or no connected broker
yet, the reply is `OK - own scopes queued; waiting for clock and a broker`, and the
report goes out within a minute of it having both.

### 2. Publish on a schedule (optional)

Periodic publishing is off until you turn it on:

```text
set mqtt.neighbors on
```

Reply: `mqtt.neighbors = on, every 24 h`. The first cycle starts ten minutes after the
repeater starts, or within a minute of turning this on if that is later; after that,
one per interval. Each cycle asks nearby repeaters to identify themselves, waits a
minute for their answers, then publishes. To change the interval (12 to 336 hours):

```text
set mqtt.neighbors.interval 48
```

Both settings are saved and survive a restart. `get mqtt.neighbors` and
`get mqtt.neighbors.interval` show them; `set mqtt.neighbors off` stops it.

## Check it worked

`mqtt status` shows where publishing stands on its second line, for example
`nbr: on, every 24 h, next in 3h12m, last 20h48m ago`. `last never` means no report has
gone out yet.

On the broker, the report is retained on `<prefix>/<iata>/<device>/neighbors`. In
CoreScope, the repeater's node shows the list as its confirmed configured scope.

## Troubleshooting

- `waiting for clock and a broker`, or `nbr: ... due (waiting for clock and a broker)`:
  set the repeater's time and check `mqtt status` for a connected broker. A report that
  still cannot go out after 30 minutes is dropped, and the next cycle tries again.
- A region is missing from the report: the report lists only the regions this repeater
  floods. A region set to deny flooding is left out, exactly as it is from the answer a
  nearby node gets.
- `Err - discover.scopes has no options`: send the command on its own.

## Limits and recovery

The report carries this repeater's own regions and how many repeaters it hears, not
those repeaters' regions. `mqtt status` puts the `nbr:` line ahead of the broker lines,
so a long broker list shows fewer brokers there; `mqtt view <N>` shows each broker in
full. `mqtt.neighbors` and `mqtt.neighbors.interval` are saved; set them back with
`set mqtt.neighbors off` and `set mqtt.neighbors.interval 24`.
