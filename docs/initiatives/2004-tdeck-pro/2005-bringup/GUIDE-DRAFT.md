# DRAFT — Use your LilyGo T-Deck Pro

> Not published. Moves to `docs/user/guides/tdeck-pro.md` (front matter
> `title: Use your LilyGo T-Deck Pro`, `feature: tdeck-pro`, catalog coverage `documented`)
> when a release carries a non-diagnostic T-Deck Pro image, and after the hardware checks
> listed under "Applies to" are recorded.

The LilyGo T-Deck Pro is a handheld MeshCore radio with a 3.1" e-paper screen, a keyboard,
touch, GPS and a LoRa radio. Offband runs on it as a companion radio: you pair it with the
Offband client on a phone or computer, and the e-paper screen shows the Offband home screen.

## Applies to

The T-Deck Pro **V1.1** only, and two development images, both Bluetooth companions with
the diagnostic log turned on. Neither is in any release yet.

- `LilyGo_TDeck_Pro_companion_radio_ble_gui_diag`: the Offband GUI, the same screens as
  the color radios, in black and white.
- `LilyGo_TDeck_Pro_companion_radio_ble_diag`: the simpler text screens, kept as a
  fallback.

To tell V1.1 from V1.0 without opening the case: V1.1 has a haptic motor driver on the
I2C bus at 0x5A (LilyGo's own detection rule), and its factory firmware reports hardware
version `v2.0-250915`.

What has been checked, and how:

| Behavior | Checked |
|---|---|
| Pins (radio, screen, SD, power enables) | Against LilyGo's V1.1 schematic and pin header. |
| The image builds | In CI, with source tests of its pins, shared SPI bus and diag flags. |
| Text screens on the e-paper panel | On a T-Deck Pro V1.1: the home screen drew; a full refresh took 1.9 s and a partial one 0.8 s. |
| Offband GUI on the e-paper panel | On a T-Deck Pro V1.1: the screens drew in black and white; a partial refresh took 0.7–0.8 s and a full one 1.2 s. |
| Touch | On a T-Deck Pro V1.1 with the CST3530 touch chip: taps landed where touched, in every corner; taps made while the screen was still changing were kept; a drag stopped when the finger lifted; a hold opened the LoRa details. |
| Radio | On a T-Deck Pro V1.1, it powered up and read the noise floor. Sending and receiving not yet tested. |

## Before you start

- Charge the radio, and [download the Offband client](https://offband.app/).
- Keep a backup of the factory firmware before flashing anything else onto the radio.

## Steps

### 1. Pair it with the client

Both images are Bluetooth companions. Pair from the Offband client as you would any other
companion radio, with the PIN the screen shows. It is a new random PIN each time the radio
starts, unless you have set a fixed one from the client.

### 2. Read the home screen

The GUI image shows the Offband home screen in black and white, with no grey, so text and
lines stay sharp. It always uses the high-contrast theme: picking another theme on this
radio changes nothing on screen. Each e-paper update takes
most of a second, and the screen waits at least a second after one before the next, so
changes show up a moment late. The whole screen flashes once when the radio starts and
when the screen wakes, to set a clean image; after that it only redraws what changed.

### 3. Use touch

- **Tap** a tab or a row to open it. The new screen takes about a second to appear, because
  the e-paper has to redraw first.
- **Taps are not lost while the screen is redrawing.** A tap made during an update counts,
  and acts as soon as the update finishes.
- **Drag** a list to scroll it. It stops where your finger lifts: it does not keep gliding,
  so each drag costs one or two screen updates.
- **Hold** for about half a second where the GUI has a hold action. Holding a message that
  wasn't delivered sends it again, and holding the signal bars at the top shows the LoRa
  details.

The BOOT button works as well. The keyboard is not used yet.

E-paper keeps its last image with no power, so the screen does not go blank when the
radio sleeps or turns off. The front-light follows the GUI's brightness setting.

## Not yet supported

The keyboard, GPS, battery gauge and charging status, the motion sensor, the light
sensor, the SD card, 4G and audio are not used by this image. Each is planned as its own
feature. Touch works only on units with the CST3530 touch chip; V1.1 also shipped with a
CST328, which this image does not drive.

## If something goes wrong

- **The screen stays blank after flashing.** Report it with the diagnostic log from the
  USB port.
- **Touch does nothing.** The radio may have the other touch chip. The diagnostic log says
  `touch none (buttons only)` at start-up; report it with that log. The BOOT button still
  works.
- **You need the factory firmware back.** Restore the backup you took before flashing.
