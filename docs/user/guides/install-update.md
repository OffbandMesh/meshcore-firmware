---
title: Choose, install and update firmware
feature: firmware-install
---

# Choose, install and update firmware

Choose an image for your radio and intended role before choosing a flashing tool.
An image for a different board can have the wrong display, pins or flash layout.

## Applies to

This guide covers the ESP32-S3 release-file conventions used by Heltec V3,
Heltec V4 and XIAO S3 WIO images. The conventions and example image families were
checked against the published `offband-v1.6.0-beta1` prerelease and current source.
This is not a hardware flashing trial or a recommendation to install a beta.
Use the instructions accompanying your selected release if its format differs.
Other chips and bootloaders need their own flashing procedure.

## Before you start

Record the exact board model/revision, current firmware version, role, node
identity and settings you need to keep. Use your app's supported backup/export
facility where available and keep the result private. A saved node name alone
is not a backup of its identity. Have a reliable USB data cable and power source.

Use [Offband releases](https://github.com/OffbandMesh/meshcore-firmware/releases).
The [firmware website](https://offband.org/firmware/) is another entry to downloads;
the selected release's asset names and notes determine what you install.
A version tag does not by itself mean that a tested downloadable release exists.
Read the release's known issues and the applicability section of the feature
guide you intend to use.

## Steps

### 1. Choose a channel and role

- **Stable:** a release intended for normal use; check its release notes and
  hardware coverage. The GitHub Latest release normally selects this channel.
- **Beta or release candidate:** a release marked Pre-release for testing.
  A guide checked against a beta does not establish availability in stable.
- **Development:** a CI artifact, with no implied hardware validation.

A **companion** connects a user app to the radio. A **companion-observer with
WiFi** adds the observer uplink described in this manual. A **repeater** is a
different role; its configuration and command surfaces are not interchangeable
with the observer's. Do not choose a role merely because its filename contains
your board name.

### 2. Match the complete image name

Examples of observer environments are `Heltec_v3_companion_observer_wifi`,
`heltec_v4_companion_observer_wifi`, `heltec_v4_tft_companion_observer_wifi`,
and `Xiao_S3_WIO_companion_observer_wifi`. The V4 TFT and OLED images differ.
Match case, hardware and role to an asset actually present in the chosen release.
If there is no matching asset, stop rather than choose a similar-looking board.

### 3. Distinguish first installation from an update

| ESP32 asset | Purpose |
|---|---|
| `...-merged.bin` | Bootloader, partition table and application together. Used for a blank device or an explicitly approved full recovery. |
| `....bin` without `-merged` | Application-only update for an existing compatible installation. |

The checked release convention writes a merged image at `0x0` and an app-only
image at `0x10000`. Those are not universal offsets for arbitrary firmware.
Never put an app-only file at `0x0`. Do not use a merged/erase procedure as a
routine update: a full erase removes identity and configuration.

### 4. Flash using the release's supported procedure

For these ESP32 images the repository documents the
[MeshCore web flasher](https://flasher.meshcore.io/), **Custom Firmware**, with
the downloaded file. Use a compatible Chromium browser with Web Serial, choose
the exact board and review the selected file and erase setting before starting.
For an ordinary update, do not enable full erase. For first installation, follow
the release's merged-image instructions and understand that erase is destructive.

Keep the device connected until the tool reports completion.

## Check it worked

After the radio restarts, read the Offband version from its supported display,
app device information or boot banner. Compare it with the downloaded release,
not just an upstream MeshCore version. Confirm the expected role and display,
then reconnect the app and check the identity and configuration you intended to
preserve. For an observer, continue with [WiFi setup](observer-wifi.md).

## Troubleshooting

| Problem | Next check |
|---|---|
| No port appears | Check the data cable, OS permissions and the board's documented bootloader procedure. |
| Flash succeeds but the radio will not boot | Recheck board, image type and flashing method. Do not repeatedly erase it. |
| App cannot reconnect | Confirm the image's role, reconnect Bluetooth and check its device information. |
| Version or display differs | Recheck the complete downloaded asset name, including OLED/TFT and role. |

## Limits and recovery

An update can still change defaults or require a migration; preserving flash
partitions is not a guarantee of configuration compatibility. Read the notes
before changing roles or channels. Automatic OTA rollback is not a general undo
for a USB erase. If recovery is needed, preserve the image/version details and
use [power and recovery guidance](power-and-recovery.md) before a destructive step.

[Back to the manual](../index.md).
