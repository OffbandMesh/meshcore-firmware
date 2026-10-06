# QCC badge radio parameters — picking the spreading factor at 500 kHz

**Date:** 2026-10-05
**Issues:** [#1363](https://github.com/OffbandMesh/meshcore-firmware/issues/1363) (badge first-boot radio defaults), [#1172](https://github.com/OffbandMesh/meshcore-firmware/issues/1172) (QCC badge), [#1064](https://github.com/OffbandMesh/meshcore-firmware/issues/1064) / [#1065](https://github.com/OffbandMesh/meshcore-firmware/issues/1065) (verified 500 kHz posture)
**Status:** recommendation, pending owner decision and a bench measurement

## The constraint

FCC Part 15.247 digital-transmission-system operation in the 902–928 MHz band wants a
6 dB bandwidth of **at least 500 kHz**. That fixes bandwidth at 500 kHz; it says nothing
about spreading factor. So the only free parameter for recovering link margin is SF.

## The arithmetic

LoRa symbol time is exact:

```
Ts = 2^SF / BW
```

Both sensitivity and airtime track symbol time. Receiver sensitivity is
`-174 + 10·log10(BW) + NF + SNR_required(SF)`: widening bandwidth 8× costs
`10·log10(8) ≈ 9 dB`, and each SF step buys it back through a lower required SNR.

The badge runs **SF7 at 62.5 kHz** today: `Ts = 128 / 62 500 = 2.048 ms`.

| At 500 kHz | Symbol time | vs SF7 @ 62.5 kHz (today) | Relative airtime |
|---|---|---|---|
| **SF7** | 0.256 ms | **~9 dB worse** | 0.125× |
| SF8 | 0.512 ms | ~6 dB worse | 0.25× |
| SF9 | 1.024 ms | ~3 dB worse | 0.5× |
| **SF10** | **2.048 ms** | **≈ equal** | **1.0×** |
| **SF11** | 4.096 ms | ~3 dB better | 2.0× |
| SF12 | 8.192 ms | ~6 dB better | 4.0× |

**SF10 at 500 kHz has exactly the same symbol time as SF7 at 62.5 kHz** — 2048 µs either
way. The symbol-time and airtime equalities are exact. The dB column is derived from the
~3 dB-per-SF-step idealisation; real SX1262 datasheet SNR steps are closer to 2.5 dB, so
SF10 @ 500 kHz may sit roughly 1–1.5 dB *below* SF7 @ 62.5 kHz rather than dead level.
SF11 is unambiguously better than today.

## Why SF7 at 500 kHz tested poorly

A field test of `919.5 / 500 / SF7 / CR 4:5` worked but was reported as "not great."
That is the expected result, and it is not the bandwidth's fault: moving 62.5 → 500 kHz
raised the noise floor 9 dB and SF7 bought none of it back. The 500 kHz requirement is
not what costs range — pairing it with SF7 is.

## Recommendation

**`919.5 / 500 / SF10 / CR 4:5`.**

It satisfies the 500 kHz requirement at the badge's present range and airtime, so nothing
is given up relative to what the badge does today. Start here.

**`919.5 / 500 / SF11 / CR 4:5`** if range still disappoints in the hall. It buys ~3 dB
over today and matches the published **MeshCore 500** preset's spreading factor
(902.250 / 500 kHz / SF11 / CR 4:5), so anything else running that preset interoperates.
The cost is 2× airtime, which on a floor with a hundred badges pushes toward collisions —
so it is the fallback, not the opening move.

Avoid SF12: 4× airtime for 6 dB is a poor trade in a dense deployment.

### What would settle it properly

These are derived numbers, not measurements. The real answer is a bench comparison of
SF7 / SF10 / SF11 at 500 kHz against the current SF7 @ 62.5 kHz, at a fixed geometry,
with the **same TX level set by the same method on every board** — RSSI and SNR recorded
per configuration. A method change invalidates the earlier runs and means retesting all of
them. Epic #1065 / task #1066 is the fleet-wide version of this question and currently has
no findings recorded.

## How to actually apply these values, per role

The two roles differ, and this bit is easy to get wrong.

### Companion (the badge) — client app only

**There is no CLI on a companion.** `CommonCLI::handleCommand`, which implements the text
commands, is wired up only by `simple_repeater` and `simple_room_server`. The companion
links `CommonCLI` solely for `loadPrefs` / `savePrefs`. So `tempradio` is **not available
on a badge**.

A companion's radio is set over the companion API, command `CMD_SET_RADIO_PARAMS` (11),
which in the client is:

> **Settings → Radio Settings** — frequency, bandwidth, spreading factor, coding rate and
> TX power, plus the preset selector.

The firmware handler calls `savePrefs()`, so **the change persists and there is no
auto-revert**. Returning to regional values is a second manual edit in the same screen.
That same screen is also the only way to try the new values on a badge that already has
prefs — see below.

### Repeater / room server — `tempradio`, which self-reverts

```
tempradio <freq>,<bw>,<sf>,<cr>,<minutes>
```

Comma-separated. It applies the parameters and reverts automatically after the given number
of minutes, without writing prefs — useful for a reversible experiment. Example:

```
tempradio 919.5,500,10,5,20
```

Ranges enforced by the handler: freq 150–2500 MHz, SF 5–12, CR 5–8, BW 7–500 kHz,
timeout > 0.

## Interaction with the first-flash-only default

The compiled `LORA_*` values in `variants/qcc_badge/platformio.ini` are **first-boot
seeds**: `MyMesh` seeds `_prefs` from them, `loadPrefs()` then overwrites from
`/prefs.json`, and DFU preserves that file. Consequences worth stating plainly:

- A **fresh** badge takes the compiled default. Conference badges handed out unconfigured
  get it automatically.
- A badge that **already has prefs** keeps its own frequency across a flash. Flashing a new
  image to such a badge therefore proves nothing about the new default.
- To observe the default on an existing badge you must clear its prefs — a factory reset
  formats the filesystem, so the next boot takes the compiled values — or simply set the
  values by hand in Radio Settings, which tests the radio without touching the default.
- Fresh badges and already-configured badges sit on different frequencies and **cannot hear
  each other**. That is the same fact as the first-flash-only guarantee, seen from the other
  side.

## Decisions recorded

- **Mixed fleet (2026-10-05, owner):** acceptable for now — there are no other QCC badges to
  message with yet, so existing badges stay on the local mesh. To be revisited before the
  conference.
- **Spreading factor:** open. SF7 is what is committed in #1363 as originally specified;
  this document recommends SF10, pending the owner's and Jeremy's input.
