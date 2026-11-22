---
title: QCC badge parts list
---

# QCC badge parts list

The circuit boards of the Queen City Con 0x4 badge, and every part on the badge
schematic (EasyEDA, "QCC 0x4 Badges" V1.0). Use it to repair a badge, or to
fit a part that yours came without. For what each part does in use, see
[Use your QCC badge](qcc-badge.md).

## Applies to

Supporter and pyramid badges. Both follow this schematic, but they differ in shape and in
which parts are fitted, so not every part below is on every badge. A pyramid badge's
front corner LEDs are not on the schematic, so they are not listed here.

## Circuit boards

The badges are built on custom circuit boards designed for the con: purple, with white
artwork. The schematic gives the boards themselves no reference, so they are listed by
name.

| Board | Notes |
|---|---|
| Supporter badge board | The main board of a supporter badge, a rectangle with rounded corners and a lanyard slot. |
| Keyboard board | The supporter badge's keyboard, a second custom board that mounts on standoffs over the battery and the ProMicro. It has its own microcontroller and a tactile switch for each key, and it works as a CardKB-compatible I2C keyboard through the main board's KEYBOARD connector. Its indicator LEDs are marked **FN_ON**, **SYM_ON**, **SHIFT_ON** and **POWER**, and it has its own headers marked **SAOV2**, **ICSP** and **TTL_SERIAL**. Its parts are not on the badge schematic, so they are not listed here. |
| Pyramid badge board | A triangle about 156 mm along the base and 101 mm tall, with a lanyard slot near the top point. It follows the same schematic as the supporter badge board, with fewer parts fitted. |

## Main modules

| Ref | Part | Notes |
|---|---|---|
| U1 | ProMicro nRF52840 board, sold as nice!nano compatible | The main controller, with the USB-C port. |
| U2 | Ai-Thinker Ra-01SH LoRa module (SX1262) | |
| RF1 | Antenna connector | The LoRa antenna attaches here. On a supporter badge it is a right-angle SMA jack at the board's edge. |
| OLED | 128×64 OLED, I2C | Pins GND, VCC, SCL, SDA. On supporter badges; added to some pyramid badges. |
| KEYBOARD | Custom keyboard board, CardKB-compatible over I2C | The keyboard board, under [Circuit boards](#circuit-boards). On supporter badges. |
| GPS | GPS module with patch antenna | Not on every badge. The supporter badge carries a NEO-7M. |

## Transistors, diodes and LED

| Ref | Part | Notes |
|---|---|---|
| Q1 | 2N7000 MOSFET | Switches the GPS on and off. |
| Q2 | 2N7000 MOSFET | Drives the message LED. |
| Q3 | 2N7000 MOSFET | Drives the buzzer. |
| D1, D2 | 1N914 diode (1N914BTR) | Let the battery feed the 3.3 V supply as a backup. The schematic marks them as a workaround for faulty ProMicro boards that won't start on battery power. |
| MSG_LED | XL-502UGD LED | The message LED. The schematic calls for a green LED, but a red one has been seen fitted here. |

## Resistors and capacitors

| Ref | Value | Notes |
|---|---|---|
| R1, R2, R3 | 10 kΩ | Hold Q1, Q2 and Q3 off. |
| R4 | 680 kΩ | Battery voltage divider, upper. |
| R5 | 1 MΩ | Battery voltage divider, lower. |
| R6 | 10 kΩ | Pull-up for SW1. |
| R7 | 10 kΩ | Pull-up for RESET. |
| R8 | 330 Ω | Message LED. |
| C1 | 100 nF | Filters the battery supply after the POWER switch. |
| C2 | 100 nF | Debounces SW1. |
| C3 | 100 nF | Debounces RESET. |

## Switches and buzzer

| Ref | Part | Notes |
|---|---|---|
| POWER | SK12D07VG4 slide switch | Disconnects the battery. |
| MUTE | SK12D07VG4 slide switch | Cuts the buzzer. Not on every badge. |
| SW1 | Tactile switch | The user button, marked **USR** on a pyramid badge. |
| RESET | 6×6 mm tactile switch (SH-6X6X5.5H-C5) | |
| BZ1 | 4 kHz passive buzzer, 5×5 mm, surface mount | Not on every badge. It must be a **passive** buzzer: the badge plays tunes on it, and an active buzzer can only make one fixed tone. |

## Connectors

| Ref | Part | Notes |
|---|---|---|
| BATT | Molex 53048-0210, 1.25 mm pitch, 2 pins | The battery connector. |
| BATT2 | PH2.0 2-pin footprint (WAFER-PH2.0-2PWB) | An alternative position for a 2.0 mm battery connector. |
| KEYBOARD | HY2.0 4-pin (BX-HY2.0-4PWZ) | GND, VCC, SDA, SCL. |
| GPS | 4-pin 2.54 mm header (PZ254V-11-04P) | VCC, GPS RX, GPS TX, and a ground that Q1 switches. |
| SAO | 2×3 2.54 mm add-on (SAO v2) header (PM2.54-2*3) | 3.3 V, GND, SDA, SCL. Pins 5 and 6 are not connected. |
| GPIO33, GPIO34, GPIO38, GPIO39 | Single-pin 2.54 mm header positions | Spare ProMicro pins. |
| 3v3, GND | Pads | The 3.3 V supply and ground. |

The 3v3 pad, the SAO header and the KEYBOARD, GPS and OLED connectors all sit on the
badge's 3.3 V supply. Never connect 5 V or a battery to them. Anything you power from
them must run on 3.3 V.

## Battery

A single-cell 3.7 V LiPo on BATT. Supporter badges came with a 3000 mAh cell, and other
badges with a 2000 mAh cell.

Before you plug in a cell from another source, check the polarity at its plug with a
meter. Makers do not all wire two-pin plugs of this size the same way, and a reversed
cell can damage the badge.

Treat the cell as you would any LiPo: never puncture, crush or short it, stop using it
if it swells, and charge it through the badge's USB-C port.

[Back to Use your QCC badge](qcc-badge.md).
