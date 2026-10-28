# DRAFT — Find the repeaters an observer hears, and their regions

> Not published. Moves to `docs/user/guides/observer-neighbors.md` (front matter
> `feature: observer-neighbors`, catalog coverage `documented`) when a release carries
> the feature. Epic 1399 adds the `/neighbors` report and its settings to this guide.

An observer can list the repeaters it hears directly (zero hops) and ask each one which
flood regions it serves. A map service such as CoreScope uses that to show which region
each repeater belongs to, from the repeater itself rather than from guesswork.

## Applies to

WiFi companion-observer images: Heltec V3, Heltec V4 OLED/TFT, Heltec V4 R8, Heltec
RCC6 and XIAO S3 WIO. Development builds only; no release carries these commands yet.
Checked against source and automated tests; no on-air or hardware trial yet.

## Before you start

Connect to the observer the way you send other observer commands: the reserved
`_sys @ <8hex>` channel in the app, or the USB serial console. The repeaters you want
to see must be in direct radio range. A repeater answers `discover.neighbors` only with
forwarding on; it answers the regions request either way, if its firmware has it.

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

### 2. Ask each neighbor for its regions

```text
discover.scopes
```

Reply: `discover.scopes: querying <N> neighbors, one at a time (up to 20 s each)`. The
observer asks one repeater at a time and waits up to 20 seconds for each, so a long
list takes a while; normal traffic and the app connection keep working meanwhile.

Send `discover.scopes` again to see progress:
`discover.scopes: running, <D> of <M> done (<R> responded, <T> timeout, <F> send_failed)`.
While a query is running the command reports progress and does not restart it.

## Check it worked

When the query finishes, `discover.scopes` starts a new one and reports how the last one
ended: `last walk: <R> responded, <T> timeout, <F> send_failed of <M>`. `responded`
means the repeater answered with its regions (an empty answer is still an answer).

## Troubleshooting

- `no neighbors yet`: the observer has not heard a repeater directly. Run
  `discover.neighbors`, wait a minute, and try again.
- Many `timeout`: a repeater answers only a few such requests every few minutes, and a
  repeater whose firmware lacks this request type does not answer at all. Try again
  later. A timeout does not mean the repeater serves no regions.
- `send_failed`: the observer was briefly out of packet buffers. Try again.
- `no memory for the query`: on boards with PSRAM the query needs it; report this with
  the board model.

## Limits and recovery

The neighbor list and query results are kept in memory only and start empty after a
restart. The commands change no saved setting; nothing needs undoing.
