---
title: Understand low-power boot and recovery
feature: safeboot
---

# Understand low-power boot and recovery

A quiet radio is not always a failed flash. SafeBoot can deliberately delay
startup when power is too low. Update rollback is a different mechanism and
must not be treated as an undo button for every flashing problem.

## Applies to

This bounded guide covers SafeBoot and ESP32 boot-safety source paths used by
Heltec V3/V4 companion-observer and repeater images. Source was checked in
`offband-v1.6.0-beta1` and current firmware. SafeBoot is conditional on platform,
battery sensing and build flags; it is not a universal board-support declaration.
No low-voltage, OTA failure or recovery trial was performed for this guide.

## Before you start

Identify the image, version, hardware revision, power source and recent action
(normal restart, USB update, OTA update or erase). Preserve the node identity
and settings if accessible. Do not intentionally flatten a battery or repeatedly
force startup to test this page. Use the board and battery manufacturer's supported
power/charging arrangements; firmware does not make an unsuitable supply safe.

## Steps

### 1. Check the power situation first

If a battery-powered node repeatedly tries to start, becomes quiet after a power
loss, or fails after poor solar conditions, restore a suitable power source and
allow it time to recover. Repeated manual resets can obscure the cause.

SafeBoot runs early, before normal high-current initialization. With a supported
battery measurement it can return to sleep instead of completing startup. It uses
thresholds, hysteresis, retry backoff and a cooldown after unclean resets. Build
defaults are not a measurement or guarantee for your particular battery/board.

### 2. Look for evidence rather than assuming corruption

If your build exposes the relevant boot log, look for `[SafeBoot]` entries
describing a stable voltage and continuation, or a below-threshold reading and
sleep interval. See [support logs](support-logs.md). This early output may not
reach USB on a native-USB board; absence of a visible line does not prove SafeBoot
was disabled or that the board is dead.

A zero/unavailable battery reading can cause the implementation to continue
boot rather than sleep. Do not use that behavior to infer battery health.
SafeBoot behavior on USB power can differ from battery-only operation.

### 3. Separate power guard from update rollback

ESP32 boot-safety code can validate a running OTA image and restore a previous
partition after certain unvalidated boots, where a compatible bootloader and
partition layout support it. It is not a backup of all identity/configuration,
does not guarantee recovery from an erased device, and does not prove radio,
broker or every application function is healthy.

If the radio seems to return to an older version after an OTA update, record the
before/after versions and `[SAFETY]` evidence before trying again. Do not assume
repeated resets or a full erase are the correct next step. This guide does not
provide a universal recovery command or button for all image/bootloader combinations.

## Check it worked

After power is restored, verify normal boot and the expected version, then check
the intended role's actual operation: app connectivity for a companion, WiFi and
broker state for an observer, or expected repeater behavior. A lit display or a
boot-validation log alone does not establish a complete functional recovery.

## Troubleshooting

| Observation | What to preserve/check |
|---|---|
| Low-power sleep repeats | Supply, battery condition and build-specific sensing; include the reported thresholds/readings if available. |
| No USB boot output | Correct USB interface and logging availability; very early logs may be inaccessible there. |
| Version reverted after OTA | Both image versions and safety/boot evidence, including the exact update method. |
| No boot after USB flash | Correct board, merged/app-only file and release-specific procedure. |

## Limits and recovery

SafeBoot policy is built into the image; this guide does not prescribe changing
voltage thresholds to force a weak node to run. Wake behavior and sensing differ
by board, so nRF52 recovery is outside this ESP32-focused guide.

Before a destructive recovery, request support with the collected evidence and
confirm the exact image and procedure. A full erase can destroy identity and
configuration and cannot be undone by OTA rollback. If a reflash is advised,
follow [installation versus update](install-update.md), not an assumed universal
offset or erase command.

[Back to the manual](../index.md).
