# DRAFT — Use your LilyGo T-Deck Pro

> Not published. Moves to `docs/user/guides/tdeck-pro.md` (front matter
> `title: Use your LilyGo T-Deck Pro`, `feature: tdeck-pro`, catalog coverage `documented`)
> when a release carries a non-diagnostic T-Deck Pro image, and after the hardware checks
> listed under "Applies to" are recorded.

The LilyGo T-Deck Pro is a handheld MeshCore radio with a 3.1" e-paper screen, a keyboard,
touch, GPS and a LoRa radio. Offband runs on it as a companion radio: you pair it with the
Offband client on a phone or computer, and the e-paper screen shows the radio's home screen.

## Applies to

The T-Deck Pro **V1.1** only, and one development image:
`LilyGo_TDeck_Pro_companion_radio_ble_diag`, a Bluetooth companion with the diagnostic
log turned on. It is not in any release yet.

To tell V1.1 from V1.0 without opening the case: V1.1 has a haptic motor driver on the
I2C bus at 0x5A (LilyGo's own detection rule), and its factory firmware reports hardware
version `v2.0-250915`.

What has been checked, and how:

| Behavior | Checked |
|---|---|
| Pins (radio, screen, SD, power enables) | Against LilyGo's V1.1 schematic and pin header. |
| The image builds | In CI, with source tests of its pins, shared SPI bus and diag flags. |
| Home screen on the e-paper panel | Not yet tested on a T-Deck Pro. |
| Radio | Not yet tested on a T-Deck Pro. |

## Before you start

- Charge the radio, and [download the Offband client](https://offband.app/).
- Keep a backup of the factory firmware before flashing anything else onto the radio.

## Steps

### 1. Pair it with the client

The image is a Bluetooth companion. Pair it from the Offband client as you would any other
companion radio. The PIN is `123456`.

### 2. Read the home screen

The e-paper screen shows the standard companion home screen. E-paper keeps its last image
with no power, so the screen does not go dark when the radio sleeps or turns off.

## Not yet supported

The keyboard, touch, GPS, battery gauge and charging status, the motion sensor, the light
sensor, the SD card, 4G and audio are not used by this image. Each is planned as its own
feature.

## If something goes wrong

- **The screen stays blank after flashing.** Report it with the diagnostic log from the
  USB port.
- **You need the factory firmware back.** Restore the backup you took before flashing.
