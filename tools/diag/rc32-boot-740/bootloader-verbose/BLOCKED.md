# Verbose bootloader — BLOCKED (2026-08-16, #740)

The project is complete and correct. It is blocked on a PlatformIO packaging
issue, not on anything about the bootloader itself. Recorded precisely so the
next attempt starts at obstacle 5, not obstacle 1.

## Goal

Replace the stock second-stage bootloader (built `CONFIG_BOOTLOADER_LOG_LEVEL_NONE`,
so it prints nothing on success *or* failure) with an identical one built at
`LOG_LEVEL_DEBUG` and its console pinned to UART0 — making the one boot stage
nobody has ever observed visible. See `../README.md`.

## Obstacles, in the order hit

| # | Obstacle | Resolved? |
|---|---|---|
| 1 | Pinning `platform = espressif32@6.13.0` does not pin the IDF — its arduino framework is IDF 4.4.7 but its espidf framework is **5.5.3** | ✅ `platform_packages = platformio/framework-espidf@3.40407.240606` |
| 2 | PlatformIO espidf needs sources in `src/`, not `main/` | ✅ moved |
| 3 | `ModuleNotFoundError: idf_component_manager` at CMake configure | ✅ `IDF_COMPONENT_MANAGER=0` (`project.cmake:43` reads the env var) |
| 4 | `ModuleNotFoundError: kconfiglib` — the `.espidf-4.4.7` venv was seeded with IDF **5.x** Python requirements | ✅ installed 4.4.7's `requirements.txt` into that venv, minus the `esp-windows-curses` line (unexpanded `${IDF_PATH}` → invalid UNC path) |
| 5 | **`crosstool_version_check` fatal**: IDF 4.4.7 requires crosstool-NG `esp-2021r2-patch5` / GCC **8.4.0**; espressif32@6.13.0's espidf builder selects `toolchain-xtensa-esp-elf` **14.2.0** | ❌ **BLOCKED** |

## Why obstacle 5 is not a one-line fix

The correct compiler is already on the host — `toolchain-xtensa-esp32s3`
8.4.0+2021r2-patch5, used by the arduino builds:

```
xtensa-esp32s3-elf-gcc.exe (crosstool-NG esp-2021r2-patch5) 8.4.0
```

Adding it to `platform_packages` **installs it but does not select it**. The
builder still resolves the compiler to
`C:/pio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc.exe`, so
the version check still fails. The selection is hardcoded in the espressif32
platform's builder scripts.

Note the registry no longer serves `platformio/toolchain-xtensa-esp32s3@8.4.0+2021r2-patch5`
(`UnknownPackageError`); it resolves only from a local `file://` path, which must
not be committed.

## Options for the next attempt, least invasive first

1. **Use an older `espressif32` platform** whose espidf builder already pairs
   IDF 4.4.x with GCC 8.4.0 (e.g. `espressif32@5.x`). Most likely to just work,
   and changes nothing shared.
2. **Build the bootloader outside PlatformIO** with a real ESP-IDF 4.4.7
   install and `idf.py build`, taking only `build/bootloader/bootloader.bin`.
   Bypasses PlatformIO's package selection entirely.
3. **Patch the platform builder** to honour a toolchain override. Rejected:
   modifies shared PlatformIO internals and would affect every other project on
   this host.

## Explicitly rejected

**Do not set `IDF_MAINTAINER=1`.** It silences the version check and builds
IDF 4.4.7 sources with GCC 14.2. The check exists because that combination is
not supported, and the artifact would go to flash offset `0x0` on beta hardware
with a single irreplaceable backup. A bootloader that *might* be miscompiled is
worse than no verbose bootloader — it would produce a boot failure
indistinguishable from the #702 bug it is meant to diagnose.

## What shipped instead

The raw-UART0 beacon (`src/helpers/BootBeacon.h`, env
`heltec_rc32_companion_radio_usb_diag`). It answers the load-bearing question on
its own: if `APP:CTOR` appears after an RST, the bootloader and IDF startup both
completed; if it never appears, the fault is at or below them and the verbose
bootloader becomes worth this fight.

Take the beacon measurement first. It may make this unnecessary.
