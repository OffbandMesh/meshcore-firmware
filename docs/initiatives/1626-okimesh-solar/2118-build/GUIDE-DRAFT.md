# Use your OKIMesh Solar node

> **Draft, not published.** No release carries these images yet, and no OKIMesh Solar
> board has run them. Every statement below comes from the source and the board layout,
> not from a device. This draft is published once a release carries the non-diagnostic
> images and a hardware test is recorded.

The OKI Mesh Alliance solar node is a ProMicro nRF52840 carrier with an Ai-Thinker
Ra-01SH (SX1262) radio, a GPS header and solar charging. Offband builds two images for it:

| Image | What it is |
|---|---|
| `OKIMesh_Solar_repeater` | A repeater |
| `OKIMesh_Solar_companion_radio_ble` | A Bluetooth companion for the Offband or MeshCore app |

Each also comes as a `_diag` build, which logs more and is meant for testing.

## Why not the stock ProMicro images

The stock `promicro` images don't fit this board:

- **GPS:** they expect the GPS on P0.06/P0.08. On this board it is on P0.20/P0.22, and its
  power is switched on P0.24, so the stock images never see a fix.
- **Battery reading:** the stock reading is about 8% off for this board's 680k/1M divider. On
  a solar node the battery reading decides when the node shuts down, so it has to be right.

## Installing

Flash the `.uf2` the same way as any ProMicro board: double-tap reset to get the USB drive,
then copy the file onto it.

The companion image has no screen, so its Bluetooth PIN is fixed at `123456`.

## Low battery

The node uses one line, **3100 mV**:

- **At start-up**, it won't start below 3100 mV. It goes to sleep instead (see "Waking up").
- **While running**, if the battery stays under 3100 mV for five minutes, the node restarts
  into that same sleep. Plugging in USB, or the cell reading above the line again, cancels
  the countdown.
- **Waking up:** the node comes back on its own once the solar charge brings the cell above
  about 3.78 V, then starts once it reads at least 3700 mV. Pressing the button also wakes
  it.

One case to know: a cell that stops charging between 3700 mV and about 3.78 V stays asleep
until it crosses the wake line, usually with the next day's sun, or until you press the
button.

> **Untested:** the running cutoff and the restart into sleep have not run on any hardware.
> Until a test is recorded, don't rely on them to protect a cell with no protection circuit
> of its own.

## Diagnostic builds

The `_diag` images send their log over the ProMicro's USB and also out of the `GPIO38` pad
at 115200 baud, so a capture can run while the node is on its battery.
