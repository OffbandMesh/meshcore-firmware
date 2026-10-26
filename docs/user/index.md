---
title: Offband firmware manual
---

# Offband firmware manual

Offband adds observer, safety and diagnostic capabilities to MeshCore firmware.
Use this manual to configure your radio and check whether a feature is working.

## Start here

- [Choose an image, install or update](guides/install-update.md).
- [Connect an observer to WiFi](guides/observer-wifi.md).
- [Connect an observer to MQTT](guides/observer-mqtt.md).
- [Choose time and location sharing](guides/observer-location.md).
- [Understand low-power boot and recovery](guides/power-and-recovery.md).
- [Collect useful support evidence](guides/support-logs.md).
- [Download firmware](https://github.com/OffbandMesh/meshcore-firmware/releases).
- [Report a problem](https://github.com/OffbandMesh/meshcore-firmware/issues/new/choose).

Check each guide's **Applies to** section before following it. A feature available
in a beta or development build is not automatically available in a stable image.
Your board name alone does not identify its role: an observer image and a plain
companion or repeater image can expose different settings.

## Manual coverage

This is a bounded first manual, not a complete command reference or board-support
matrix. The WiFi guide includes stable `offband-v1.4.0` source evidence. The new
journeys name the verified `offband-v1.6.0-beta1` source, explicitly a beta, and
their hardware and app-UI limits. Do not upgrade channels merely to follow a page;
check your installed release before using a feature or command.

These pages are maintained with the firmware source. Read the release named in
each guide and its verification limits; the manual can receive corrections
between firmware releases. Source review and automated documentation checks do
not replace testing instructions with a real radio.

When asking for help, include the board, image role, firmware version, command
and error text. Remove passwords and identifying details before sharing logs
or screenshots.
