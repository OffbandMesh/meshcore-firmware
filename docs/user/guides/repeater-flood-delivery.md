---
title: Get more messages past a busy repeater
feature: repeater-flood-delivery
---

# Get more messages past a busy repeater

A repeater passes on a flood message, such as a channel message, by transmitting it
once after a short random wait. On a busy mesh, that one transmission can go out
while the next repeaters are busy relaying someone else's packet. All of them miss
it, and the message never gets further than this repeater.

Offband repeaters do two things about that, and both are on by default:

- **Wait for quiet air.** If the repeater has just heard another packet, it holds the
  message it is about to pass on until the air has been quiet for about the length of
  that packet. It never holds a message for more than 3 extra seconds.
- **Retry once.** After passing a message on, the repeater listens for a repeater
  further out to pass it on in turn. If nobody does within a few seconds, it sends the
  same message once more. It never sends a message a third time.

Nothing changes on the air. Other radios, including stock MeshCore radios and the
stock app, see at most one extra copy of a message. They already drop copies they
have seen.

## Applies to

Offband repeater images. Built and checked for the Heltec V4 and RAK4631 repeater
images. Needs firmware newer than `offband-v1.7.1`. This is development behavior: it
has been checked against the code and automated tests, and has not yet been tested
on the air.

## Before you start

Send these commands as an admin: on the USB serial console, or logged in to the
repeater as admin from the app. A guest login cannot run them.

## Steps

### 1. See the settings and what the repeater has done

```text
get flood
```

Reply, on one line:

```text
quiet=on retry=on | fwd=120 echo=98 rtx=14 rtx_echo=9 no_nbr=0 evict=0 hold=37 cap=2
```

| Key | Meaning |
|---|---|
| `quiet` | The wait for quiet air, `on` or `off` |
| `retry` | The single retry, `on` or `off` |
| `fwd` | Messages passed on that the repeater was listening for |
| `echo` | ...of those, ones a repeater further out passed on |
| `rtx` | Retries sent |
| `rtx_echo` | ...of those, ones a repeater further out then passed on |
| `no_nbr` | Retries skipped because no repeater further out has been heard recently |
| `evict` | Messages it stopped listening for to make room for newer ones |
| `hold` | Messages held for quiet air |
| `cap` | ...of those, ones sent anyway after the 3 second limit |

The counts start from zero at every restart.

### 2. Turn either part off or back on

```text
set flood.quiet off
set flood.retry off
set flood.quiet on
set flood.retry on
```

Each replies `OK`. The change applies at once and is kept across restarts.

## Check it worked

Run `get flood` again after the repeater has been passing on traffic for a while.
`fwd` and `echo` rising show it is listening for its messages being passed on. `rtx`
rising shows it is retrying the ones that weren't. `rtx_echo` shows how many of
those retries then got through.

If the repeater has its log forwarding on, the same line also appears in its log
every 15 minutes, beginning `[flood]`.

## Troubleshooting

- **`Err - expected on or off`:** the value after `set flood.quiet` or
  `set flood.retry` must be exactly `on` or `off`.
- **An unknown-setting reply:** the setting name was mistyped. The two names are
  `flood.quiet` and `flood.retry`.
- **`rtx` stays at 0 and `no_nbr` rises:** the repeater has not heard a repeater
  further out pass anything on in the last 30 minutes, so it does not retry. On a
  repeater with nobody beyond it, that is correct: a retry would only cost airtime.
- **`cap` is close to `hold`:** the channel is busy almost continuously, so most held
  messages go out at the 3 second limit anyway.

## Limits and recovery

- Adverts are never retried. They are sent regularly anyway.
- The repeater's own messages, and messages sent along a known route rather than
  flooded, are never held or retried.
- A retry costs one extra transmission, and only for a message that no repeater
  further out passed on. It counts against the repeater's airtime budget like any
  other transmission.
- To undo, turn the part off with the commands in step 2. No restart or reset is
  needed.
