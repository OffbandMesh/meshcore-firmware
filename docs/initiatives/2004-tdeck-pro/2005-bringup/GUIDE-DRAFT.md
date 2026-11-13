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
| Keyboard | Not yet checked on a radio. The key layout and modifiers are tested in CI against the keycaps' layout; a press of every key on a T-Deck Pro V1.1 is the next check. |
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
- **Drag** a list to scroll it. The screen redraws once your finger lifts, where the drag
  ended, and does not keep gliding. A drag held longer than about a second and a half
  also redraws partway, so you can see where you are.
- **Newest** appears at the bottom of a conversation while you are scrolled up into its
  history. Tap it to go back to the latest message.
- **Hold** for about half a second where the GUI has a hold action. Holding a message that
  wasn't delivered sends it again, and holding the signal bars at the top shows the LoRa
  details.

The BOOT button works as well.

### 4. Use the keyboard

- **Type** wherever the screen shows a text field: a message, a node name, a channel
  name, a search. Letters are lowercase unless you add Shift.
- **Shift** gives a capital. Hold it while you press the letter, or tap it once and the
  next letter you type is a capital. Tap it a second time to change your mind.
- **Alt** or **Sym** gives the symbol printed on the key: Alt then Q types `#`, Alt then
  M types `.`. Like Shift, hold it or tap it once for the next key. The microphone key
  types `0` with Alt, and nothing on its own.
- **Backspace** deletes one character. Hold it to keep deleting: after half a second it
  repeats about ten times a second. It stops when the field is empty, and doesn't take
  you out of the screen you were typing in.
- **Enter** sends the message or saves the field.
- When no text field is open, **Enter** opens the highlighted item and **Backspace** goes
  back. Use touch to move between items.
- In an open conversation, the digits 1 to 4 (Alt then W, E, R or S) start one of the four
  quick replies.
- **Keys aren't lost while the screen is redrawing.** What you type during an update is
  kept and appears when the update finishes.
- A key that types wakes a dark screen; that key only wakes it, and types nothing. Shift,
  Alt and Sym on their own don't wake it. The keys light up while the screen is on, and
  go dark when it dims or turns off.

E-paper keeps its last image with no power, so the screen does not go blank when the
radio sleeps or turns off. The front-light follows the GUI's brightness setting.

## Not yet supported

GPS, the battery gauge and charging status, the motion sensor, the light sensor, the SD
card, 4G and audio are not used by this image. Each is planned as its own
feature. Touch works only on units with the CST3530 touch chip; V1.1 also shipped with a
CST328, which this image does not drive.

## If something goes wrong

- **The screen stays blank after flashing.** Report it with the diagnostic log from the
  USB port.
- **Touch does nothing.** The radio may have the other touch chip. The diagnostic log says
  `touch none (buttons only)` at start-up; report it with that log. The BOOT button still
  works.
- **The keyboard does nothing.** The diagnostic log says `keyboard none` at start-up when
  the keyboard chip didn't answer; report it with that log. Touch and the BOOT button
  still work.
- **You need the factory firmware back.** Restore the backup you took before flashing.
