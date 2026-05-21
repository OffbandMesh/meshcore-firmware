# SafeBoot — Pre-Init Power Guard for Solar/Battery Nodes

SafeBoot is an opt-in pre-init power guard that prevents boot-loop drain on
solar/battery-powered MeshCore nodes. It runs very early in `setup()` —
before any high-current peripheral comes up — reads battery voltage with a
lightweight ADC sample, and either continues boot (voltage stable above
threshold) or puts the MCU back to sleep with hysteresis and exponential
backoff.

Ported from [Meshtastic PR #10391](https://github.com/meshtastic/firmware/pull/10391)
by Mickyleitor. Integration tracking lives in
[Strycher/LoRa#96](https://github.com/Strycher/LoRa/issues/96).

For maintainer-side workflows (upstream rebase, deploy-merge, rebase
runbook), see [safeboot-maintenance.md](safeboot-maintenance.md).

## When SafeBoot is for you

- **Solar-deployed Repeaters or sensor nodes.** Low-sun stretches deplete
  the battery; without SafeBoot, the firmware keeps trying to boot at
  marginal voltage, browns out mid-init, and burns ~100-300 mWs per failed
  cycle. SafeBoot gates this entirely.
- **Battery-only nodes** that may run down to near-empty. Same problem as
  solar at a different timescale — booting at 3.3V can corrupt the
  filesystem if a brownout hits during `LittleFS.begin()`.
- **Boards with marginal LDO regulation.** AMS1117-style 1.1V-dropout LDOs
  on a LiIon cell brown out around 3.4V; SafeBoot's default `SLEEP_MV=3400`
  threshold catches this with a small safety margin.

## When SafeBoot is NOT for you

- **USB-powered desk devices.** SafeBoot reads `analogReadMilliVolts(PIN_VBAT_READ)`;
  if it reads 0 (no battery rail, USB power only), it bails out and continues
  boot. No-op by design. You can also explicitly opt out with
  `-D SAFE_BOOT_DISABLED=1`.
- **Boards without a battery-voltage ADC path.** SafeBoot needs `PIN_VBAT_READ`
  (or `SAFEBOOT_PIN_VBAT_READ` aliased to the variant's equivalent). If
  neither is defined, SafeBoot compiles to a no-op.

## How it works

Decision tree on each boot, in order:

1. **Read reset reason.** If it was unclean (brownout, watchdog, lockup) AND
   we've been here before, force a brownout cooldown (300s default) before
   even checking Vbat. Anti-bootloop safeguard.
2. **Sample Vbat 8 times at 10 ms intervals.** Reject coupling-noise spikes
   by requiring N consecutive samples above the wake threshold.
3. **Decide:**
    - All samples above `WAKE_MV` (default 3700 mV) AND no forced cooldown
      → continue boot.
    - Any sample below `SLEEP_MV` (default 3400 mV) → sleep.
    - In hysteresis band (3400-3700 mV): if this is a fresh cold boot and
      the reset was clean, give it a chance; otherwise sleep.
4. **If sleeping:** record state to retention RAM (ESP32: `RTC_NOINIT_ATTR`;
   nRF52: `GPREGRET2` with a bit-7 ownership marker), then enter the
   platform's deep-sleep mode.
5. **On wake:** boot restarts from the top. The recorded attempt counter
   drives an exponential backoff (120s → 240s → 480s → 600s cap by
   default), so the device gradually slows its retry rate.

The whole sequence (steps 1-3) takes about 80 ms with default settings.

### Wake sources

| Platform | Wake source | Auto-recheck? |
|---|---|---|
| ESP32 (S3, original, C3, C6, etc.) | Hardware timer + optional ext1 button-wake | Yes (timer fires on backoff schedule) |
| nRF52 with `BATTERY_LPCOMP_INPUT` defined | SYSTEM_OFF + LPCOMP (Vbat rise) + optional button-wake | Yes (solar recharge wakes the device) |
| nRF52 without `BATTERY_LPCOMP_INPUT` but with `PIN_USER_BTN` | SYSTEM_OFF + GPIO SENSE on button | **No** (manual press only) |
| nRF52 with neither | System ON + WFE loop + WDT feed | Yes but ~1.5-3 mA (higher current) |

Solar deployments should ensure `BATTERY_LPCOMP_INPUT` is defined for
their variant.

## Hardware support matrix

| Variant | env | Wake | Notes |
|---|---|---|---|
| Heltec V4 OLED | `heltec_v4_repeater` | Timer (ESP32) + ext1 button | Primary user-fleet platform; bench-validated baseline |
| Heltec V3 | `Heltec_v3_repeater` | Timer (ESP32) + ext1 button | Auto-detects `PIN_ADC_CTRL` polarity for V3 hardware revisions ≤3.2 vs >3.2 (see [F14](https://github.com/Strycher/LoRa/issues/147)) |
| RAK4631 | `RAK_4631_repeater` | SYSTEM_OFF + LPCOMP (AIN3) + button | `SAFEBOOT_ADC_MULTIPLIER=2.054` algebraically derived from variant.h composite formula; awaits bench validation ([F6c](https://github.com/Strycher/LoRa/issues/116)) |
| Seeed XIAO nRF52840 (incl. Plus) | `Xiao_nrf52_repeater` | SYSTEM_OFF + LPCOMP (AIN7) + button | Drives `VBAT_ENABLE` (GPIO 14) LOW before Vbat read |
| Seeed T1000-E | `t1000e_repeater` | SYSTEM_OFF + button only | LPCOMP DEFERRED ([F15 / #148](https://github.com/Strycher/LoRa/issues/148)); manual wake only until AIN channel is identified |

Other MeshCore variants are not yet configured for SafeBoot but the
adapter overrides below let any variant be added without modifying core
SafeBoot code. See [Strycher/LoRa#145](https://github.com/Strycher/LoRa/issues/145)
for the broader hardware-expansion roadmap.

## Flashing

Download per-variant firmware from the GitHub Releases page (artifacts
tagged `safeboot-vX.Y.Z` are built automatically by
`build-safeboot-firmwares.yml`). Each release includes a `.sha256`
sidecar for every artifact:

```
sha256sum -c <filename>.sha256
```

### ESP32-S3 variants (Heltec V3, Heltec V4)

```
esptool.py --chip esp32s3 --port <PORT> write_flash 0x0 <filename>.bin
```

Or use the `-merged.bin` variant to flash bootloader + partitions + app
in one step at offset `0x0`. Replace `<PORT>` with `/dev/ttyUSB0` /
`COM5` / etc. per your OS.

### nRF52 variants (RAK4631, XIAO nRF52840, T1000-E)

Two paths:

**Adafruit DFU (USB CDC bootloader):** double-tap the reset button to
enter bootloader mode, then:
```
adafruit-nrfutil dfu serial -pkg <filename>.zip -p <PORT> -b 115200
```

**UF2 drag-and-drop:** double-tap reset, drag `<filename>.uf2` to the
USB mass-storage device that appears.

### Verifying SafeBoot at runtime

Open a serial terminal (115200 baud) and reset the device. On battery
power you should see one of two boot-log signatures:

**Normal boot** (Vbat above wake threshold):
```
[SafeBoot] Vbat=4015 mV stable -- continuing boot (attempts=0, unclean=0)
```

**Low-voltage sleep:**
```
[SafeBoot] Vbat=3380 mV below safe threshold (wake=3700 sleep=3400).
Sleep 120s, attempt #1, unclean=0.
```

If you see neither line, SafeBoot is compiled out (no `PIN_VBAT_READ` /
`SAFEBOOT_PIN_VBAT_READ` defined for your variant; SafeBoot is a no-op).
Confirm with `pio run -e <env> -t menu` or check the variant's
`platformio.ini`.

## Configuration

### Threshold tuning (per-variant)

In your variant's `platformio.ini` `build_flags`:

```ini
-D DEFAULT_SAFE_BOOT_WAKE_MV=3700          ; continue boot above this (mV)
-D DEFAULT_SAFE_BOOT_SLEEP_MV=3400         ; sleep below this (mV)
-D DEFAULT_SAFE_BOOT_RECHECK_SECS=120      ; initial sleep interval
-D DEFAULT_SAFE_BOOT_MAX_RECHECK_SECS=600  ; backoff cap
```

The defaults are tuned for single-cell LiIon + AMS1117-class LDO. If
your hardware uses a different chemistry (LiFePO4, LiPo with different
discharge curve) or a low-dropout LDO, adjust the thresholds. Bench
validation against your specific hardware is the only way to verify.

### Adapter overrides (non-standard hardware)

For variants whose battery-read plumbing doesn't match the MeshCore
default convention (`PIN_VBAT_READ` + simple-ratio `ADC_MULTIPLIER`),
opt-in overrides decouple SafeBoot from the variant's existing battery
code:

```ini
-D SAFEBOOT_PIN_VBAT_READ=PIN_VBAT      ; alias if variant uses a different pin name
-D SAFEBOOT_ADC_MULTIPLIER=2.054        ; override for variants with composite multipliers
-D SAFEBOOT_AREF_VOLTAGE=3.0            ; override for non-default ADC reference
-D SAFEBOOT_VBAT_ENABLE_PIN=VBAT_ENABLE ; toggle this GPIO around the ADC read
-D SAFEBOOT_VBAT_ENABLE_ACTIVE=LOW      ; polarity for the VBAT_ENABLE assert
-D SAFEBOOT_AUTODETECT_PIN_ADC_CTRL=1   ; runtime-detect PIN_ADC_CTRL polarity (V3-style)
```

See [F13](https://github.com/Strycher/LoRa/issues/146) for the override
mechanism design rationale.

### nRF52 LPCOMP wake (for solar deployments)

```ini
-D BATTERY_LPCOMP_INPUT=NRF_LPCOMP_INPUT_N   ; N = AIN channel wired to Vbat divider
-D BATTERY_LPCOMP_THRESHOLD=NRF_LPCOMP_REF_SUPPLY_5_8  ; or 3_8, 1_2, etc.
```

Without `BATTERY_LPCOMP_INPUT`, SafeBoot's nRF52 path falls back to
button-wake-only. Solar recharge will not wake the device. See
[F15](https://github.com/Strycher/LoRa/issues/148).

### Disabling SafeBoot

If you flash a SafeBoot build but want SafeBoot inactive (e.g., for
debugging the rest of the boot path):

```ini
-D SAFE_BOOT_DISABLED=1
```

`checkAndMaybeSleep()` becomes a no-op (sets `g_settled = true` and
returns immediately).

## Troubleshooting

### "My device boot-loops despite SafeBoot enabled"

Either:
- SafeBoot is compiled out on your variant (no `PIN_VBAT_READ` /
  `SAFEBOOT_PIN_VBAT_READ` defined). Check `[<variant>]` build_flags.
- SafeBoot reads Vbat as 0 mV (sensor not ready) → bails out → continues
  boot. Verify your variant's `ADC_MULTIPLIER` or `SAFEBOOT_ADC_MULTIPLIER`
  matches the divider ratio in hardware.
- The brownout is happening before SafeBoot's `setup()` body runs (e.g.,
  during `Serial.begin()`'s power surge). SafeBoot can't help with this;
  your LDO is sized too marginally for the boot peak.

### "My nRF52 device sleeps and never wakes when solar comes back"

You probably have `PIN_USER_BTN` defined but NOT `BATTERY_LPCOMP_INPUT`.
SafeBoot's nRF52 path takes the button-wake-only fallback. Add
`BATTERY_LPCOMP_INPUT` to your variant's `platformio.ini` pointing at
the AIN channel wired to the Vbat divider. See the F15-shipping variants
(rak4631, xiao_nrf52) for examples.

For t1000-e specifically, this is currently unresolved (LPCOMP wiring
not documented in the variant). Use button-wake until the AIN mapping is
identified.

### "Vbat readings in the log look wildly wrong"

Your `ADC_MULTIPLIER` (or `SAFEBOOT_ADC_MULTIPLIER`) doesn't match your
hardware's divider ratio. The correct value is the Vbat/Vadc ratio. For
a typical 1:1 divider (R1=R2, Vadc=Vbat/2), the multiplier is 2.0. For
the Heltec V4 (5.42 cal factor), it's 5.42. Verify against the variant's
existing `getBattMilliVolts()` formula if one exists.

### "SafeBoot triggers when the battery looks fine"

Likely a calibration issue (above) — SafeBoot is reading the divided
voltage incorrectly and thinks Vbat is lower than it actually is. Verify
on a bench supply: set 4.0V, read SafeBoot's log, compare to 4000 mV
expectation. If they disagree, the multiplier is wrong.

If the multiplier is right and SafeBoot still triggers spuriously, your
`WAKE_MV` threshold may be too aggressive for your specific chemistry.
Try lowering to 3650 or 3600 mV.

## Related

- [safeboot-maintenance.md](safeboot-maintenance.md) — maintainer
  workflows: upstream-rebase, deploy-merge, rebase runbook checklist.
- [Meshtastic PR #10391](https://github.com/meshtastic/firmware/pull/10391)
  — concept origin (by Mickyleitor; commit `4c7e1ee8`).
- [Strycher/LoRa#96](https://github.com/Strycher/LoRa/issues/96) —
  port tracking epic + sub-task list.
- [Strycher/LoRa#145](https://github.com/Strycher/LoRa/issues/145) —
  hardware-expansion roadmap (variants beyond the initial 5).
