# LVGL bench (#1326)

Times LVGL 8.3 drawing a message-list screen on the Heltec V4 TFT, at 80, 160 and 240 MHz in one run. Each frame is split into render time (CPU) and flush time (pushing pixels to the panel over SPI).

The panel path matches Offband's `ST7789LCDDisplay`: Adafruit_ST7789 on HSPI, 40 MHz SPI, rotation 3 (320×240). The numbers therefore describe the driver Offband ships today.

This is a standalone bench image, not firmware. It has no radio and no mesh, and it prints with `Serial.printf` because none of the firmware's logging is linked in.

## Build and flash

```
pio run -d tools/diag/lvgl-bench-1326
```

The image is `tools/diag/lvgl-bench-1326/.pio/build/lvgl_bench_v4/firmware.bin`. Flash it as an app-slot write through `scripts/pio-flash` with `--artifact` (copy it to a name ending in `.bin` first). The board needs a 16 MB layout with app0 at `0x10000`, which is Offband's `default_16MB.csv`.

## Read it

Hold the USB port with one continuous capture (`tools/diag/rc32-boot-740/scripts/capture.py --port COMx --no-dtr --reset --out <log>`). Every 20 s the bench prints one line per clock:

```
[lvgl] mhz=80 full=115.6ms (render 61.5 flush 54.1) scroll=107.8ms/frame (render 59.8 flush 48.0) max=113.3ms fps=9.3 buf_lines=24 spi=40MHz 320x240
```

- `full`: everything invalidated, one synchronous refresh (average of 10).
- `scroll`: the list scrolled 6 px per frame, 100 frames (average and worst case).
- `heap before-lvgl` / `after-ui` are printed once at boot. Start the capture before the board boots, or reset it with the capture running.

Change `BUF_LINES` or `TFT_SPI_HZ` in `platformio.ini` to test other buffer sizes or SPI clocks.

## Results

The V4-R2 (heltec-v4-tft-1) results from 2026-10-07 are on [#1326](https://github.com/OffbandMesh/meshcore-firmware/issues/1326): 9.3 / 14.1 / 16.9 fps scrolling at 80 / 160 / 240 MHz.

## Companion memory telemetry

The companion's own memory diagnostic is a build flag, not part of this bench. Build any companion env with

```
PLATFORMIO_BUILD_FLAGS="-D OFFBAND_MEM_TELEMETRY -D CAPLOG_ON_BOOT" pio run -e <env>
```

and it logs a `[mem]` line every 30 s (internal RAM and PSRAM free / minimum / largest block, SPIFFS used/total, link state). It also logs a `[mem] contacts_save` line after every contacts-file rewrite. `CAPLOG_ON_BOOT` turns capture on in fresh prefs, so the lines reach the USB console. On a device with saved prefs, enable caplog from the app instead.
