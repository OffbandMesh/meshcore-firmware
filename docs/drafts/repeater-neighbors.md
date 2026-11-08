---
title: Report the regions of a repeater and its neighbors to CoreScope
feature: repeater-neighbors
---

# Report the regions of a repeater and its neighbors to CoreScope

A repeater with WiFi telemetry can publish the flood regions it serves to its MQTT
brokers as a `/neighbors` report. It also asks each repeater it hears directly (zero
hops) which regions that repeater serves, and lists the answers. A map service such as
CoreScope records each list on that repeater's node, read from the repeater itself
rather than inferred from traffic. The repeater's own list is exactly the one it gives a
nearby node that asks it for its regions.

## Applies to

The Heltec V4 WiFi-telemetry repeater image. Development builds only; no release
carries these commands yet. Checked against source and automated tests; no on-air,
hardware or live-broker trial yet.

## Before you start

Send these as an admin: on the USB serial console, or logged in to the repeater as
admin from the app. A guest login cannot run them.

To publish, the repeater needs at least one MQTT broker enabled and connected (`mqtt
status`), and its clock set: a report is never sent with an unset clock. The repeaters
you want listed must be in direct radio range. A repeater answers the regions request
only if its firmware has it.

## Steps

### 1. Query the neighbors and publish now

```text
discover.scopes
```

Reply: `OK - querying <N> neighbors, then publishing`. The repeater asks one neighbor at
a time and waits up to 20 seconds for each, so a long list takes a while (up to about
17 minutes for 50 neighbors that all stay silent); forwarding and admin logins keep
working meanwhile. When the last one is done, the report goes to every
connected broker, whether or not periodic publishing is on.

Send `discover.scopes` again to see progress: `OK - querying neighbors: <D> of <M> done`.
While a query is running the command reports progress and does not restart it.

If the repeater has heard no neighbors yet, it publishes its own regions straight away:
`OK - publishing own scopes`, or, with no clock or no connected broker yet,
`OK - own scopes queued; waiting for clock and a broker`; the report then goes out within
a minute of it having both.

### 2. Publish on a schedule (optional)

Periodic publishing is off until you turn it on:

```text
set mqtt.neighbors on
```

Reply: `mqtt.neighbors = on, every 24 h`. The first cycle starts ten minutes after the
repeater starts, or within a minute of turning this on if that is later; after that,
one per interval. Each cycle asks nearby repeaters to identify themselves, waits a
minute for their answers, queries every neighbor and publishes. To change the interval
(12 to 336 hours):

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
CoreScope, the repeater's node and each neighbor that answered show their list as a
confirmed configured scope.

## Troubleshooting

- `waiting for clock and a broker`, or `nbr: ... due (waiting for clock and a broker)`:
  set the repeater's time and check `mqtt status` for a connected broker. A report that
  still cannot go out after 30 minutes is dropped, and the next cycle tries again.
- A neighbor is listed as `timeout`: a repeater answers only a few such requests every
  few minutes, and one whose firmware lacks this request does not answer at all. Try
  again later. A timeout does not mean the neighbor serves no regions.
- A region is missing from this repeater's list: the report lists only the regions it
  floods. A region set to deny flooding is left out, exactly as it is from the answer a
  nearby node gets.
- `Err - no memory for the query`: the query could not start; report this with the
  board model.
- `Err - discover.scopes has no options`: send the command on its own.

## Limits and recovery

The neighbor list and query results are kept in memory only and start empty after a
restart. Only repeaters heard directly are queried, and signal strength (RSSI) is not
reported for them. `mqtt status` puts the `nbr:` line ahead of the broker lines, so a
long broker list shows fewer brokers there; `mqtt view <N>` shows each broker in full.
`mqtt.neighbors` and `mqtt.neighbors.interval` are saved; set them back with
`set mqtt.neighbors off` and `set mqtt.neighbors.interval 24`.
