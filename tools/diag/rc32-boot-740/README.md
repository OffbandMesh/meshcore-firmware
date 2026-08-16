# RC32 boot-path instrumentation (#740, for #702)

Makes the ESP32-S3 boot observable from ROM entry through to a live screen, on
one wire, in one file.

Built for RC32 first because it is the first of the four Beta 1 RadioCore boards
(RC32, RCC6, RC52/nRF52, HaLow). The beacon and the capture script carry to the
other ESP32 boards unchanged; the verbose bootloader needs one rebuild per
target; RC52 is nRF52 and has no ESP bootloader, so it needs a separate
equivalent.

## The blind spots this closes

| Stage | Before | After |
|---|---|---|
| ROM loader | visible on UART0 | unchanged |
| 2nd-stage bootloader | **silent** — `CONFIG_BOOTLOADER_LOG_LEVEL_NONE` | verbose bootloader, `LOG_LEVEL_DEBUG`, console pinned to UART0 |
| IDF startup (PSRAM, cache, heap) | ERROR only — `CONFIG_LOG_MAXIMUM_LEVEL=1` compiles INFO/DEBUG out of the prebuilt libs, unfixable at runtime | still ERROR only, but **bracketed**: `APP:CTOR` firing proves it completed |
| static ctors → `setup()` → screen | nothing until first `ESP_LOGE`; MeshLog rides USB-CDC, which dies with the chip | raw-UART0 beacon from the first executable instruction |

The load-bearing marker is `APP:CTOR`. If it appears after an RST, the
bootloader and IDF startup both completed and the fault is in our
ctors/`setup()`. If it does not, the fault is at or before the bootloader's jump
to the app, and **no application-level fix can ever address it** — which is
consistent with #702's four failed fixes.

## Contents

| Path | What |
|---|---|
| `bootloader-verbose/` | ESP-IDF 4.4.7 project that emits a `LOG_LEVEL_DEBUG` bootloader. Stub app, never flashed. |
| `rc32_uart_sniffer_v3/` | Feather sketch: UART0 sniffer + open-drain remote RST/BOOT. **Flash with Arduino IDE, not PlatformIO.** |
| `scripts/capture.py` | Long-running timestamped capture from the Feather to a file. |
| `scripts/make_merged.py` | Builds the merged image carrying the verbose bootloader. |
| `evidence/` | Captures. Append-only. |

The beacon itself lives in the main tree at `src/helpers/BootBeacon.h`, enabled
by `-D OFFBAND_BOOT_BEACON` (env `heltec_rc32_companion_radio_usb_diag`).

## Wiring

| RC32 header | Feather | Added |
|---|---|---|
| pin 12 (U0TXD/GPIO43) | RX | v2 |
| pin 20 (GND) | GND | v2 |
| pin 18 (RST) | A0, open-drain | v3 |
| pin 5 (GPIO0/BOOT) | A1, open-drain | v3 |

Feather TX stays unconnected.

**Open-drain is not a style choice.** Both RC32 lines have 10K pull-ups to 3V3.
The Feather only ever pulls them LOW and otherwise sits high-Z, so it never
fights the pull-up, and an unpowered or reflashed Feather cannot hold the RC32
in reset.

## Toolchain setup for the verbose bootloader (one-off, and it fights you)

Four separate obstacles, all hit on 2026-08-16. Recorded so RCC6 costs minutes,
not hours.

1. **Pinning the platform does not pin the IDF.** `espressif32@6.13.0` pairs
   `framework-arduinoespressif32` 3.20017 (arduino-esp32 2.0.17, built on ESP-IDF
   **4.4.7**) with `framework-espidf` 3.50503 (ESP-IDF **5.5.3**). Its two
   frameworks are on different IDF majors. Override the package explicitly —
   already done in `bootloader-verbose/platformio.ini`:
   `platform_packages = platformio/framework-espidf@3.40407.240606`
2. **PlatformIO espidf projects need `src/`, not `main/`** — otherwise
   "Missing the `src` folder with project sources".
3. **`IDF_COMPONENT_MANAGER=0`** — PlatformIO's `.espidf-4.4.7` venv has no
   `idf_component_manager` module and CMake configure dies. `project.cmake:43`
   reads the env var; the IDF default is already 0, PlatformIO turns it on.
4. **The 4.4.7 venv is seeded with IDF 5.x Python requirements** (`esp-idf-kconfig`,
   `pydantic`) and is missing 4.4.7's (`kconfiglib`). Install them, dropping the
   `esp-windows-curses` line — it carries an unexpanded `${IDF_PATH}` that pip
   mangles into an invalid UNC path:

   ```bash
   IDF="$HOME/.platformio/packages/framework-espidf@3.40407.240606"   # or C:/pio/packages/...
   grep -viE "esp-windows-curses|IDF_PATH" "$IDF/requirements.txt" > /tmp/idf447-req.txt
   "C:/pio/penv/.espidf-4.4.7/Scripts/python.exe" -m pip install -r /tmp/idf447-req.txt
   ```

⚠ Installing `framework-espidf@3.40407` **replaces the shared package** (was
5.5.3). Any other project on this host that needs 5.5.3 will silently
re-download it. See the `pio-package-store-thrash` failure mode: a build that
fails *earlier* on retry is that, not your diff.

## Build

```bash
# 1. instrumented application
pio run -e heltec_rc32_companion_radio_usb_diag

# 2. verbose bootloader (note the env var)
cd tools/diag/rc32-boot-740/bootloader-verbose && IDF_COMPONENT_MANAGER=0 pio run

# 3. merged image (bootloader @0x0 + partitions + otadata + app)
python tools/diag/rc32-boot-740/scripts/make_merged.py \
  --build-dir .pio/build/heltec_rc32_companion_radio_usb_diag \
  --bootloader tools/diag/rc32-boot-740/bootloader-verbose/.pio/build/rc32_bootloader_verbose/bootloader.bin \
  --boot-app0 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  --out tools/diag/rc32-boot-740/out/heltec_rc32_companion_radio_usb_diag-v1.5.0-<sha7>-merged.bin
```

The output filename must match `pio-flash`'s `ARTIFACT_RE`
(`-v<X.Y.Z>-<sha7>-merged.bin`) or the wrapper records the identity as
`artifact-filename-unparsed`.

## Flash

Through the wrapper, never around it. `--erase` is **required** — it is what
selects the full-image write at `0x0`, which is the only sanctioned path that
places a bootloader.

```bash
python scripts/pio-flash.py list
python scripts/pio-flash.py preview rc32-bench-1 --artifact <merged.bin> --erase   # exits non-zero by design
python scripts/pio-flash.py confirm rc32-bench-1 --token <token path>
python scripts/pio-flash.py list                                                   # re-enumerate; never carry a port over
```

⚠ **`--erase` wipes NVS**: node identity, contacts, and all persisted config.
On `rc32-bench-1` that means the bench setup (TX power 15, advert name
`Offband-RC32-R`, admin password, `path.hash.mode 1`, clock) must be re-applied
afterwards. The diag env seeds the US radio preset at build time so an erased
board cannot come up on the base-default 869.618 MHz.

## Capture

```bash
python tools/diag/rc32-boot-740/scripts/capture.py --list
python tools/diag/rc32-boot-740/scripts/capture.py --port COMxx \
       --out tools/diag/rc32-boot-740/evidence/<session>.log
```

Hold ONE connection open for the whole session. Do not attach anything to the
**RC32's** own USB console during a test — that power-cycles the board, which is
why every log captured that way came from a boot that succeeded (#702).

With SNIFFER-v3 wired, trigger resets without touching the board:

```bash
python tools/diag/rc32-boot-740/scripts/capture.py --port COMxx --send RST
python tools/diag/rc32-boot-740/scripts/capture.py --port COMxx --send BOOTRST
```

## Rollback

| Step | How |
|---|---|
| Stock bootloader | `framework-arduinoespressif32/tools/sdk/esp32s3/bin/bootloader_qio_80m.elf` → `.bin`, or simply flash a merged image built with it |
| Full factory restore | `flash-backups/rc32-bench-1-20260813-235410.bin` (16,777,216 B, sha256 `5c44a95b490d0e796ffd73b634934cfd173bbeb09ac4fe51b2186f5db958cd4f`) — the only copy, beta hardware |
| Unbootable board | ROM download mode is in silicon: BOOT+RST, or `--send BOOTRST` once v3 is wired |

## Reading a capture

A healthy boot, in order, on one wire:

```
ESP-ROM:esp32s3-20210327
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
load:... entry 0x403c98d0            <- 2nd-stage BOOTLOADER entry, not the app
I (nn) boot: ESP-IDF ... 2nd stage bootloader     <- only with the verbose bootloader
I (nn) boot: Loaded app from partition at offset 0x10000
[BEACON] APP:CTOR -- ...             <- bootloader + IDF startup COMPLETED
[BEACON] setup:ENTRY -- before Serial.begin
[BEACON] setup:before board.begin
...
[BEACON] post:display.begin(OK)
```

Where it stops is the finding.
