# DRAFT — Find the repeaters an observer hears, and their regions

> Not published. Moves to `docs/user/guides/observer-neighbors.md` (front matter
> `feature: observer-neighbors`, catalog coverage `documented`) when a release carries
> the feature.

An observer can list the repeaters it hears directly (zero hops) and ask each one which
flood regions it serves. It publishes the result to its MQTT brokers as a `/neighbors`
report, which a map service such as CoreScope uses to show which region each repeater
belongs to, from the repeater itself rather than from guesswork.

## Applies to

WiFi companion-observer images: Heltec V3, Heltec V4 OLED/TFT, Heltec V4 R8, Heltec
RCC6 and XIAO S3 WIO. Development builds only; no release carries these commands yet.
Checked against source and automated tests; no on-air, hardware or live-broker trial
yet.

## Before you start

Connect to the observer the way you send other observer commands: the reserved
`_sys @ <8hex>` channel in the app, or the USB serial console. The repeaters you want
to see must be in direct radio range. A repeater answers `discover.neighbors` only with
forwarding on; it answers the regions request either way, if its firmware has it.

To publish, the observer needs at least one MQTT broker enabled and connected (`mqtt
status`), and its clock set: a report is never sent with an unset clock.

## Steps

### 1. Fill the neighbor list

The observer adds a repeater to its list whenever it hears that repeater's advert
directly. To ask nearby repeaters to answer now instead of waiting:

```text
discover.neighbors
```

Reply: `discover.neighbors: request queued; <N> neighbors in table, answers arrive over
the next minute`. Answers keep arriving for one minute. Sending the command again
inside that minute replies `still collecting answers to the last request (<S> s left)`
and sends nothing.

### 2. Ask each neighbor for its regions, and publish

```text
discover.scopes
```

Reply: `discover.scopes: querying <N> neighbors, one at a time (up to 20 s each), then
publishing /neighbors`. The observer asks one repeater at a time and waits up to 20
seconds for each, so a long list takes a while; normal traffic and the app connection
keep working meanwhile. When the last one is done, the report goes to every connected
broker, whether or not periodic publishing is on.

Send `discover.scopes` again to see progress:
`discover.scopes: running, <D> of <M> done (<R> responded, <T> timeout, <F> send_failed)`.
While a query is running the command reports progress and does not restart it.

### 3. Publish on a schedule (optional)

Periodic publishing is off until you turn it on:

```text
set mqtt.neighbors on
```

Reply: `mqtt.neighbors = on, every 24 h`. The first cycle starts ten minutes after the
observer starts, or within a minute of turning this on if that is later; after that,
one per interval. Each one refreshes the
neighbor list, queries every neighbor and publishes. To change the interval (12 to 336
hours):

```text
set mqtt.neighbors.interval 48
```

Both settings are saved and survive a restart. `get mqtt.neighbors` and
`get mqtt.neighbors.interval` show them; `set mqtt.neighbors off` stops it.

## Check it worked

`mqtt status` shows where publishing stands on its second line, for example
`nbr: on, every 24 h, next in 3h12m, last 20h48m ago`. `last never` means no report has
gone out yet.

When a query finishes, `discover.scopes` starts a new one and reports how the last one
ended on a second line: `last walk: <R> responded, <T> timeout, <F> send_failed of <M>`.
`responded` means the repeater answered with its regions (an empty answer is still an
answer).

On the broker, the report is retained on `<prefix>/<iata>/<device>/neighbors`.

## Troubleshooting

- `no neighbors yet`: the observer has not heard a repeater directly. Run
  `discover.neighbors`, wait a minute, and try again.
- Many `timeout`: a repeater answers only a few such requests every few minutes, and a
  repeater whose firmware lacks this request type does not answer at all. Try again
  later. A timeout does not mean the repeater serves no regions.
- `send_failed`: the observer was briefly out of packet buffers. Try again.
- `no memory for the query`: on boards with PSRAM the query needs it; report this with
  the board model.
- `nbr: ... due (waiting for clock and a broker)`: a report is due but the observer has
  no clock or no connected broker. It starts as soon as it has both. Check `mqtt status`
  and the observer's time.
- A report on a board without PSRAM is smaller and may list fewer neighbors; the report
  then says so (`truncated`).

## Limits and recovery

The neighbor list and query results are kept in memory only and start empty after a
restart. `mqtt.neighbors` and `mqtt.neighbors.interval` are saved; set them back with
`set mqtt.neighbors off` and `set mqtt.neighbors.interval 24`.
