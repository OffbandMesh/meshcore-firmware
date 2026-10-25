---
title: Offband firmware manual
---

# Offband firmware manual

Offband adds observer, safety and diagnostic capabilities to MeshCore firmware.
Use this manual to configure your radio and check whether a feature is working.

## Start here

- [Connect an observer to WiFi](guides/observer-wifi.md).
- [Download firmware](https://github.com/OffbandMesh/meshcore-firmware/releases).
- [Report a problem](https://github.com/OffbandMesh/meshcore-firmware/issues/new/choose).

Check each guide's **Applies to** section before following it. A feature available
in a beta or development build is not automatically available in a stable image.
Your board name alone does not identify its role: an observer image and a plain
companion or repeater image can expose different settings.

## Manual coverage

The WiFi guide is the first published-ready journey. Installation and updates,
MQTT setup, GPS/privacy, safety and troubleshooting guides are being prepared.
Their absence here does not mean the firmware lacks those features.

These pages are maintained with the firmware source. Read the release named in
each guide and its verification limits; the manual can receive corrections
between firmware releases. Source review and automated documentation checks do
not replace testing instructions with a real radio.

When asking for help, include the board, image role, firmware version, command
and error text. Remove passwords and identifying details before sharing logs
or screenshots.
